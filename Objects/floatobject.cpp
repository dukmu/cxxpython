#include "cxxpython/floatobject.hpp"
#include "cxxpython/complexobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <charconv>
#include <cmath>
#include <limits>

namespace cxxpython::core {
bool detail::parse_float(std::u32string_view input, double &value)
{
    while (!input.empty() && detail::numeric_whitespace(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && detail::numeric_whitespace(input.back()))
        input.remove_suffix(1);
    if (input.empty())
        return false;
    std::string text;
    text.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        auto c = input[i];
        if (c == U'_') {
            if (i == 0 || i + 1 == input.size() || detail::decimal_digit(input[i - 1]) < 0 ||
                detail::decimal_digit(input[i + 1]) < 0)
                return false;
            continue;
        }
        if (const int digit = detail::decimal_digit(c); digit >= 0) {
            text.push_back(static_cast<char>('0' + digit));
            continue;
        }
        if (c > 127)
            return false;
        text.push_back(static_cast<char>(c));
    }
    const char *first = text.data(), *last = first + text.size();
    if (text.find_first_of("()") != std::string::npos)
        return false;
    if (*first == '+') {
        ++first;
        if (first != last && *first == '-')
            return false;
    }
    if (first == last)
        return false;
    auto parsed = std::from_chars(first, last, value, std::chars_format::general);
    if (parsed.ec == std::errc::result_out_of_range && parsed.ptr == last) {
        // from_chars leaves the destination untouched for both overflow and
        // underflow. Its validated decimal exponent distinguishes the two.
        std::string_view number(first, static_cast<std::size_t>(last - first));
        const bool negative = number.front() == '-';
        if (negative)
            number.remove_prefix(1);
        const auto epos = number.find_first_of("eE");
        const auto mantissa = number.substr(0, epos);
        int exponent = 0;
        if (epos != std::string_view::npos) {
            auto exp = number.substr(epos + 1);
            const bool minus = !exp.empty() && exp.front() == '-';
            if (!exp.empty() && (exp.front() == '+' || exp.front() == '-'))
                exp.remove_prefix(1);
            for (char digit : exp)
                exponent = std::min(1000000, exponent * 10 + digit - '0');
            if (minus)
                exponent = -exponent;
        }
        auto point = mantissa.find('.');
        if (point == std::string_view::npos)
            point = mantissa.size();
        auto first_digit = mantissa.find_first_of("123456789");
        if (first_digit == std::string_view::npos) {
            value = negative ? -0.0 : 0.0;
            return true;
        }
        const auto order = static_cast<long long>(point) - static_cast<long long>(first_digit) -
                           (first_digit < point ? 1 : 0) + exponent;
        value = order < 0 ? 0.0 : std::numeric_limits<double>::infinity();
        if (negative)
            value = -value;
        return true;
    }
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

namespace {
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}

const FloatObject *require_float(Attachment &context, Value value, ReturnSlot &out)
{
    if (!is_float(context, value)) {
        type_error(context, U"float descriptor requires a float", out);
        return nullptr;
    }
    return &context.payload<FloatObject>(value);
}

bool integer_to_double(Attachment &context, Value value, double &number, ReturnSlot &out)
{
    auto integer = detail::integer_value(context.payload<IntObject>(value));
    const bool negative = integer < 0;
    if (negative)
        integer = -integer;
    if (integer == 0) {
        number = 0.0;
        return true;
    }
    const auto bits = boost::multiprecision::msb(integer) + 1;
    if (bits > 1024) {
        raise_error(context, CoreObject::OverflowErrorType, U"int too large to convert to float",
                    out);
        return false;
    }
    unsigned shift = bits > 53 ? bits - 53 : 0;
    auto significand = (integer >> shift).convert_to<std::uint64_t>();
    if (shift) {
        const detail::Integer remainder = integer - (detail::Integer(significand) << shift);
        const detail::Integer halfway = detail::Integer(1) << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (significand & 1)))
            ++significand;
        if (significand == (std::uint64_t{1} << 53)) {
            significand >>= 1;
            ++shift;
        }
    }
    number = std::ldexp(static_cast<double>(significand), static_cast<int>(shift));
    if (negative)
        number = -number;
    if (std::isinf(number)) {
        raise_error(context, CoreObject::OverflowErrorType, U"int too large to convert to float",
                    out);
        return false;
    }
    return true;
}

void float_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::FloatType))) {
        type_error(context, U"float.__new__ requires a float type", out);
        return;
    }
    const auto &values = context.payload<TupleObject>(args[1]).items;
    if (values.size() > 1) {
        type_error(context, U"float expected at most one argument", out);
        return;
    }
    RootScope roots(context);
    auto cls = roots.root(args[0]);
    double number = 0.0;
    if (!values.empty()) {
        auto input = roots.root(values[0]);
        const auto value = input.get(context);
        if (context.type_of(value) == context.builtin(CoreObject::FloatType) &&
            cls.get(context) == context.builtin(CoreObject::FloatType)) {
            out.ok(value);
            return;
        }
        if (context.type_of(value) == context.builtin(CoreObject::FloatType))
            number = context.payload<FloatObject>(value).value;
        else if (context.type_of(value) == context.builtin(CoreObject::IntType)) {
            if (!integer_to_double(context, value, number, out))
                return;
        } else if (context.type_of(value) == context.builtin(CoreObject::StrType)) {
            if (!detail::parse_float(context.payload<StrObject>(value).value, number)) {
                raise_error(context, CoreObject::ValueErrorType, U"invalid float literal", out);
                return;
            }
        } else {
            auto method = detail::type_attribute(context, context.type_of(value), U"__float__");
            ReturnSlot converted(context);
            if (method)
                detail::invoke_special(context, input, roots.root(*method), {{}, {}}, converted);
            else {
                auto slot = resolve_protocol(context, roots.root(context.type_of(input)),
                                             Protocol::Index, roots);
                if (slot.state == SlotState::Missing && context.has_layout<StrObject>(value) &&
                    is_subtype(context, context.type_of(value),
                               context.builtin(CoreObject::StrType))) {
                    if (!detail::parse_float(context.payload<StrObject>(value).value, number)) {
                        raise_error(context, CoreObject::ValueErrorType, U"invalid float literal",
                                    out);
                        return;
                    }
                    context.allocate_instance<FloatObject>(cls, out, number);
                    return;
                }
                index(context, input, converted);
            }
            if (converted.tag() == ResultTag::Raised) {
                out.raised(converted.exception());
                return;
            }
            if (method) {
                if (!is_float(context, converted.value())) {
                    type_error(context, U"__float__ returned a non-float", out);
                    return;
                }
                number = context.payload<FloatObject>(converted.value()).value;
            } else if (!integer_to_double(context, converted.value(), number, out))
                return;
        }
    }
    context.allocate_instance<FloatObject>(cls, out, number);
}

void float_bool(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (value)
        out.ok(context.builtin(value->value != 0.0 ? CoreObject::True : CoreObject::False));
}

void float_identity(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (!value)
        return;
    if (context.type_of(args[0]) == context.builtin(CoreObject::FloatType))
        out.ok(args[0]);
    else
        make_float(context, value->value, out);
}
void float_getnewargs(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (!value)
        return;
    const double number = value->value;
    RootScope roots(context);
    roots.root(args[0]);
    ReturnSlot copied(context);
    make_float(context, number, copied);
    const std::array arguments{copied.value()};
    cxxpython::core::make_tuple(context, arguments, out);
}

void float_negative(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (value)
        make_float(context, -value->value, out);
}
void float_absolute(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (value)
        make_float(context, std::fabs(value->value), out);
}

bool operand_double(Attachment &context, Value value, double &number, ReturnSlot &out)
{
    if (is_float(context, value)) {
        number = context.payload<FloatObject>(value).value;
        return true;
    }
    if (is_int(context, value))
        return integer_to_double(context, value, number, out);
    out.ok(context.builtin(CoreObject::NotImplemented));
    return false;
}

template <BinaryOp Op, bool Reflected>
void float_arithmetic(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *self = require_float(context, args[0], out);
    if (!self)
        return;
    double a = self->value, b;
    if (!operand_double(context, args[1], b, out))
        return;
    if constexpr (Reflected)
        std::swap(a, b);
    if constexpr (Op == BinaryOp::TrueDivide || Op == BinaryOp::FloorDivide ||
                  Op == BinaryOp::Modulo)
        if (b == 0.0) {
            raise_error(context, CoreObject::ZeroDivisionErrorType,
                        U"float division or modulo by zero", out);
            return;
        }
    double result;
    if constexpr (Op == BinaryOp::Add)
        result = a + b;
    if constexpr (Op == BinaryOp::Subtract)
        result = a - b;
    if constexpr (Op == BinaryOp::Multiply)
        result = a * b;
    if constexpr (Op == BinaryOp::Power) {
        if (b == 0.0)
            result = 1.0;
        else if (std::isnan(a))
            result = a;
        else if (std::isnan(b))
            result = a == 1.0 ? 1.0 : b;
        else if (std::isinf(b)) {
            double magnitude = std::fabs(a);
            result = magnitude == 1.0 ? 1.0 : ((b > 0) == (magnitude > 1.0) ? std::fabs(b) : 0.0);
        } else if (std::isinf(a)) {
            bool odd = std::fmod(std::fabs(b), 2.0) == 1.0;
            result = b > 0 ? (odd ? a : std::fabs(a)) : (odd ? std::copysign(0.0, a) : 0.0);
        } else if (a == 0.0) {
            if (b < 0) {
                raise_error(context, CoreObject::ZeroDivisionErrorType,
                            U"0.0 cannot be raised to a negative power", out);
                return;
            }
            result = std::fmod(std::fabs(b), 2.0) == 1.0 ? a : 0.0;
        } else if (a < 0 && b != std::floor(b)) {
            RootScope roots(context);
            ReturnSlot base(context), exponent(context);
            make_complex(context, a, 0.0, base);
            make_float(context, b, exponent);
            binary(context, roots.root(base.value()), roots.root(exponent.value()), BinaryOp::Power,
                   out);
            return;
        } else {
            result = std::pow(a, b);
            if (std::isinf(result)) {
                raise_error(context, CoreObject::OverflowErrorType,
                            U"numerical result out of range", out);
                return;
            }
        }
    }
    if constexpr (Op == BinaryOp::TrueDivide)
        result = a / b;
    if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo) {
        double remainder = std::fmod(a, b);
        double quotient = (a - remainder) / b;
        if (remainder != 0.0) {
            if ((remainder < 0) != (b < 0)) {
                remainder += b;
                quotient -= 1.0;
            }
        } else
            remainder = std::copysign(0.0, b);
        if constexpr (Op == BinaryOp::Modulo)
            result = remainder;
        else {
            if (quotient != 0.0) {
                result = std::floor(quotient);
                // Recover an integral quotient rounded just below its exact value.
                if (quotient - result > 0.5)
                    result += 1.0;
            } else
                result = std::copysign(0.0, a / b);
        }
    }
    make_float(context, result, out);
}

template <bool Reflected>
void float_divmod(Attachment &context, Handle function, std::span<const Value> args,
                  ReturnSlot &out)
{
    ReturnSlot quotient(context), remainder(context);
    float_arithmetic<BinaryOp::FloorDivide, Reflected>(context, function, args, quotient);
    if (quotient.tag() == ResultTag::Raised) {
        out.raised(quotient.exception());
        return;
    }
    if (quotient.value() == context.builtin(CoreObject::NotImplemented)) {
        out.ok(quotient.value());
        return;
    }
    float_arithmetic<BinaryOp::Modulo, Reflected>(context, function, args, remainder);
    if (remainder.tag() == ResultTag::Raised) {
        out.raised(remainder.exception());
        return;
    }
    const std::array values{quotient.value(), remainder.value()};
    cxxpython::core::make_tuple(context, values, out);
}

void float_int(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (!value)
        return;
    if (std::isnan(value->value)) {
        raise_error(context, CoreObject::ValueErrorType, U"cannot convert float NaN to integer",
                    out);
        return;
    }
    if (std::isinf(value->value)) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"cannot convert float infinity to integer", out);
        return;
    }
    detail::publish_integer(context, detail::Integer(value->value), out);
}

void float_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (!value)
        return;
    if (std::isnan(value->value)) {
        make_str(context, U"nan", out);
        return;
    }
    if (std::isinf(value->value)) {
        make_str(context, value->value < 0 ? U"-inf" : U"inf", out);
        return;
    }
    char buffer[128];
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), value->value, std::chars_format::scientific);
    if (converted.ec != std::errc{}) {
        raise_error(context, CoreObject::OverflowErrorType, U"float representation failed", out);
        return;
    }
    std::string scientific(buffer, converted.ptr);
    const auto epos = scientific.find('e');
    const int exponent = std::stoi(scientific.substr(epos + 1));
    std::string text;
    if (exponent >= -4 && exponent < 16) {
        auto mantissa = scientific.substr(0, epos);
        if (mantissa.front() == '-') {
            text.push_back('-');
            mantissa.erase(0, 1);
        }
        const auto point = mantissa.find('.');
        if (point != std::string::npos)
            mantissa.erase(point, 1);
        const int decimal = exponent + 1;
        if (decimal <= 0)
            text += "0." + std::string(static_cast<std::size_t>(-decimal), '0') + mantissa;
        else if (static_cast<std::size_t>(decimal) >= mantissa.size())
            text += mantissa +
                    std::string(static_cast<std::size_t>(decimal) - mantissa.size(), '0') + ".0";
        else
            text += mantissa.substr(0, decimal) + "." + mantissa.substr(decimal);
    } else
        text = std::move(scientific);
    make_str(context, {text.begin(), text.end()}, out);
}

void float_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = require_float(context, args[0], out);
    if (!value)
        return;
    const double number = value->value;
    if (std::isnan(number)) {
        auto address = reinterpret_cast<std::uintptr_t>(args[0].object());
        auto hash =
            static_cast<std::int64_t>((address >> 4) | (address << (sizeof(address) * 8 - 4)));
        make_int(context, hash == -1 ? -2 : hash, out);
        return;
    }
    if (std::isinf(number)) {
        make_int(context, number > 0 ? 314159 : -314159, out);
        return;
    }
    constexpr int bits = sizeof(std::intptr_t) == 8 ? 61 : 31;
    constexpr std::uint64_t modulus = (std::uint64_t{1} << bits) - 1;
    int exponent = 0;
    double fraction = std::frexp(number, &exponent);
    const int sign = fraction < 0 ? -1 : 1;
    fraction = std::abs(fraction);
    std::uint64_t hash = 0;
    while (fraction) {
        hash = ((hash << 28) & modulus) | (hash >> (bits - 28));
        fraction *= 268435456.0;
        exponent -= 28;
        const auto part = static_cast<std::uint64_t>(fraction);
        fraction -= static_cast<double>(part);
        hash += part;
        if (hash >= modulus)
            hash -= modulus;
    }
    exponent = exponent >= 0 ? exponent % bits : bits - 1 - ((-1 - exponent) % bits);
    hash = ((hash << exponent) & modulus) | (hash >> (bits - exponent));
    auto result = sign * static_cast<std::int64_t>(hash);
    make_int(context, result == -1 ? -2 : result, out);
}

template <CompareOp Op>
void float_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *left = require_float(context, args[0], out);
    if (!left)
        return;
    const double number = left->value;
    bool unordered = std::isnan(number);
    int order = 0;
    if (is_float(context, args[1])) {
        const double right = context.payload<FloatObject>(args[1]).value;
        unordered |= std::isnan(right);
        order = (number > right) - (number < right);
    } else if (is_int(context, args[1])) {
        if (std::isinf(number))
            order = number > 0 ? 1 : -1;
        else if (!unordered) {
            const detail::Integer truncated(number);
            const auto right = detail::integer_value(context.payload<IntObject>(args[1]));
            order = (truncated > right) - (truncated < right);
            if (order == 0 && std::trunc(number) != number)
                order = number > 0 ? 1 : -1;
        }
    } else {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    bool result;
    if constexpr (Op == CompareOp::Eq)
        result = !unordered && order == 0;
    if constexpr (Op == CompareOp::Ne)
        result = unordered || order != 0;
    if constexpr (Op == CompareOp::Lt)
        result = !unordered && order < 0;
    if constexpr (Op == CompareOp::Le)
        result = !unordered && order <= 0;
    if constexpr (Op == CompareOp::Gt)
        result = !unordered && order > 0;
    if constexpr (Op == CompareOp::Ge)
        result = !unordered && order >= 0;
    out.ok(context.builtin(result ? CoreObject::True : CoreObject::False));
}
} // namespace

bool is_float(Attachment &context, Value value)
{
    return context.has_layout<FloatObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::FloatType));
}

void make_float(Attachment &context, double value, ReturnSlot &out)
{
    RootScope roots(context);
    context.allocate_instance<FloatObject>(roots.root(context.builtin(CoreObject::FloatType)), out,
                                           value);
}

void detail::install_floats(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    auto install = [&](std::u32string name, NativeEntry entry, bool binary = false) {
        install_native_attribute(context, core, at(CoreObject::FloatType), std::move(name),
                                 binary ? pair : unary, entry);
    };
    install(U"__bool__", float_bool);
    install(U"__float__", float_identity);
    install(U"__getnewargs__", float_getnewargs);
    install(U"__pos__", float_identity);
    install(U"__neg__", float_negative);
    install(U"__abs__", float_absolute);
    install(U"__int__", float_int);
    install(U"__repr__", float_repr);
    install(U"__str__", float_repr);
    install(U"__hash__", float_hash);
    install(U"__eq__", float_compare<CompareOp::Eq>, true);
    install(U"__ne__", float_compare<CompareOp::Ne>, true);
    install(U"__lt__", float_compare<CompareOp::Lt>, true);
    install(U"__le__", float_compare<CompareOp::Le>, true);
    install(U"__gt__", float_compare<CompareOp::Gt>, true);
    install(U"__ge__", float_compare<CompareOp::Ge>, true);
    install(U"__add__", float_arithmetic<BinaryOp::Add, false>, true);
    install(U"__radd__", float_arithmetic<BinaryOp::Add, true>, true);
    install(U"__sub__", float_arithmetic<BinaryOp::Subtract, false>, true);
    install(U"__rsub__", float_arithmetic<BinaryOp::Subtract, true>, true);
    install(U"__mul__", float_arithmetic<BinaryOp::Multiply, false>, true);
    install(U"__rmul__", float_arithmetic<BinaryOp::Multiply, true>, true);
    install(U"__truediv__", float_arithmetic<BinaryOp::TrueDivide, false>, true);
    install(U"__rtruediv__", float_arithmetic<BinaryOp::TrueDivide, true>, true);
    install(U"__pow__", float_arithmetic<BinaryOp::Power, false>, true);
    install(U"__rpow__", float_arithmetic<BinaryOp::Power, true>, true);
    install(U"__floordiv__", float_arithmetic<BinaryOp::FloorDivide, false>, true);
    install(U"__rfloordiv__", float_arithmetic<BinaryOp::FloorDivide, true>, true);
    install(U"__mod__", float_arithmetic<BinaryOp::Modulo, false>, true);
    install(U"__rmod__", float_arithmetic<BinaryOp::Modulo, true>, true);
    install(U"__divmod__", float_divmod<false>, true);
    install(U"__rdivmod__", float_divmod<true>, true);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    install_native_attribute(context, core, at(CoreObject::FloatType), U"__new__", constructor,
                             float_new);
    RootScope roots(context);
    ReturnSlot out(context);
    const auto dict = *TypeView(context, at(CoreObject::FloatType)).dictionary();
    auto function = *name_lookup(context, dict, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out, function);
    auto wrapper = roots.root(out.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), out, U"__new__");
    name_store(context, dict, out.value(), wrapper.get(context));
}
} // namespace cxxpython::core
