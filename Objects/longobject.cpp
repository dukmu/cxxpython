#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/version.hpp>
#include <cmath>
#include <iterator>

#if BOOST_VERSION < 107400
#error Boost 1.74 or newer is required
#endif

namespace cxxpython::core {
namespace {
using Integer = boost::multiprecision::cpp_int;
void type_error(Attachment &context, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType,
                U"integer operation received an incompatible value", out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
const IntObject &integer(Attachment &context, Value value)
{
    if (!is_int(context, value))
        throw std::invalid_argument("expected an integer value");
    return context.payload<IntObject>(value);
}
Integer expanded(const IntObject &value)
{
    return value.large ? value.large->value : Integer(value.small);
}
bool fits(const Integer &value)
{
    return value >= std::numeric_limits<std::int64_t>::min() &&
           value <= std::numeric_limits<std::int64_t>::max();
}
void publish(Attachment &context, Integer value, ReturnSlot &out)
{
    if (fits(value)) {
        make_int(context, value.convert_to<std::int64_t>(), out);
        return;
    }
    auto storage = std::make_unique<detail::BigInt>();
    storage->value = std::move(value);
    RootScope roots(context);
    context.allocate_instance<IntObject>(roots.root(context.builtin(CoreObject::IntType)), out,
                                         std::move(storage));
}
void boolean(Attachment &context, bool value, ReturnSlot &out)
{
    out.ok(context.builtin(value ? CoreObject::True : CoreObject::False));
}
void integer_round(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const auto &arguments = context.payload<TupleObject>(args[1]).items;
    if (arguments.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"__round__ takes at most one argument",
                    out);
        return;
    }
    if (arguments.empty()) {
        copy_int(context, self.get(context), out);
        return;
    }
    auto digits_arg = roots.root(arguments[0]);
    ReturnSlot indexed(context);
    index(context, digits_arg, indexed);
    if (raised(indexed)) {
        out.raised(indexed.exception());
        return;
    }
    Integer digits = expanded(integer(context, indexed.value()));
    if (digits >= 0) {
        copy_int(context, self.get(context), out);
        return;
    }
    Integer value = expanded(integer(context, self.get(context)));
    if (value == 0) {
        copy_int(context, self.get(context), out);
        return;
    }
    const bool negative = value < 0;
    if (negative)
        value = -value;
    digits = -digits;
    // 10**n > 2**n, so this bound guarantees a rounded result of zero.
    if (digits > boost::multiprecision::msb(value) + 1) {
        make_int(context, 0, out);
        return;
    }
    auto count = digits.convert_to<std::size_t>();
    Integer scale = 1, base = 10;
    while (count) {
        context.poll();
        if (count & 1)
            scale *= base;
        count >>= 1;
        if (count)
            base *= base;
    }
    Integer quotient = value / scale;
    Integer remainder = value % scale;
    if (remainder * 2 > scale || (remainder * 2 == scale && (quotient & 1) != 0))
        ++quotient;
    value = quotient * scale;
    publish(context, negative ? -value : value, out);
}
template <bool Count>
void int_bits(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    Integer value = expanded(integer(context, args[0]));
    if (value < 0)
        value = -value;
    Integer result = 0;
    if constexpr (Count) {
        const auto &backend = value.backend();
        for (std::size_t i = 0; i < backend.size(); ++i)
            result += std::popcount(backend.limbs()[i]);
    } else if (value != 0) {
        result = boost::multiprecision::msb(value);
        ++result;
    }
    publish(context, std::move(result), out);
}
template <bool Reflected>
void true_divide(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!is_int(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    Integer numerator = expanded(integer(context, args[Reflected ? 1 : 0]));
    Integer denominator = expanded(integer(context, args[Reflected ? 0 : 1]));
    if (denominator == 0) {
        raise_error(context, CoreObject::ZeroDivisionErrorType, U"division by zero", out);
        return;
    }
    const bool negative = (numerator < 0) != (denominator < 0);
    if (numerator < 0)
        numerator = -numerator;
    if (denominator < 0)
        denominator = -denominator;
    if (numerator == 0) {
        make_float(context, negative ? -0.0 : 0.0, out);
        return;
    }
    const auto nbits = boost::multiprecision::msb(numerator);
    const auto dbits = boost::multiprecision::msb(denominator);
    if (nbits > dbits && nbits - dbits > 1024) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"integer division result too large for a float", out);
        return;
    }
    if (dbits > nbits && dbits - nbits > 1075) {
        make_float(context, negative ? -0.0 : 0.0, out);
        return;
    }
    int exponent =
        nbits >= dbits ? static_cast<int>(nbits - dbits) : -static_cast<int>(dbits - nbits);
    if (exponent >= 0 ? numerator < (denominator << exponent)
                      : (numerator << -exponent) < denominator)
        --exponent;
    const int shift = exponent < -1022 ? 1074 : 52 - exponent;
    Integer scaled_numerator = numerator;
    Integer scaled_denominator = denominator;
    if (shift >= 0)
        scaled_numerator <<= shift;
    else
        scaled_denominator <<= -shift;
    Integer quotient = scaled_numerator / scaled_denominator;
    Integer remainder = scaled_numerator % scaled_denominator;
    const Integer twice = remainder << 1;
    if (twice > scaled_denominator || (twice == scaled_denominator && (quotient & 1) != 0))
        ++quotient;
    double result = std::ldexp(quotient.convert_to<double>(), -shift);
    if (std::isinf(result)) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"integer division result too large for a float", out);
        return;
    }
    make_float(context, negative ? -result : result, out);
}
template <BinaryOp Op, bool Reflected>
void arithmetic(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!is_int(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    auto &a = integer(context, args[Reflected ? 1 : 0]);
    auto &b = integer(context, args[Reflected ? 0 : 1]);
    if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo)
        if (!b.large && b.small == 0) {
            raise_error(context, CoreObject::ZeroDivisionErrorType,
                        U"integer division or modulo by zero", out);
            return;
        }
    if (!a.large && !b.large) {
        std::int64_t result = 0;
        bool overflow = false;
        if constexpr (Op == BinaryOp::Add)
            overflow = __builtin_add_overflow(a.small, b.small, &result);
        if constexpr (Op == BinaryOp::Subtract)
            overflow = __builtin_sub_overflow(a.small, b.small, &result);
        if constexpr (Op == BinaryOp::Multiply)
            overflow = __builtin_mul_overflow(a.small, b.small, &result);
        if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo) {
            overflow = a.small == std::numeric_limits<std::int64_t>::min() && b.small == -1;
            if (!overflow) {
                const auto quotient = a.small / b.small;
                const auto remainder = a.small % b.small;
                const bool adjust = remainder && ((remainder < 0) != (b.small < 0));
                if constexpr (Op == BinaryOp::FloorDivide)
                    result = quotient - (adjust ? 1 : 0);
                else
                    result = adjust ? remainder + b.small : remainder;
            } else if constexpr (Op == BinaryOp::Modulo) {
                result = 0;
                overflow = false;
            }
        }
        if (!overflow) {
            make_int(context, result, out);
            return;
        }
    }
    auto left = expanded(a);
    auto right = expanded(b);
    Integer result;
    if constexpr (Op == BinaryOp::Add)
        result = left + right;
    if constexpr (Op == BinaryOp::Subtract)
        result = left - right;
    if constexpr (Op == BinaryOp::Multiply)
        result = left * right;
    if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo) {
        Integer remainder = left % right;
        const bool adjust = remainder != 0 && ((remainder < 0) != (right < 0));
        if constexpr (Op == BinaryOp::FloorDivide) {
            result = left / right;
            if (adjust)
                --result;
        } else {
            result = std::move(remainder);
            if (adjust)
                result += right;
        }
    }
    publish(context, std::move(result), out);
}
template <BinaryOp Op, bool Reflected>
void integer_extended(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!is_int(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    Integer a = expanded(integer(context, args[Reflected ? 1 : 0]));
    Integer b = expanded(integer(context, args[Reflected ? 0 : 1]));
    Integer result;
    if constexpr (Op == BinaryOp::BitAnd)
        result = a & b;
    if constexpr (Op == BinaryOp::BitXor)
        result = a ^ b;
    if constexpr (Op == BinaryOp::BitOr)
        result = a | b;
    if constexpr (Op == BinaryOp::LeftShift || Op == BinaryOp::RightShift) {
        if (b < 0) {
            raise_error(context, CoreObject::ValueErrorType, U"negative shift count", out);
            return;
        }
        if (b > std::numeric_limits<std::size_t>::max()) {
            if constexpr (Op == BinaryOp::RightShift) {
                make_int(context, a < 0 ? -1 : 0, out);
                return;
            }
            if (a == 0) {
                make_int(context, 0, out);
                return;
            }
            raise_error(context, CoreObject::OverflowErrorType, U"too many digits in integer", out);
            return;
        }
        auto shift = b.convert_to<std::size_t>();
        if constexpr (Op == BinaryOp::RightShift) {
            Integer magnitude = a < 0 ? -a : a;
            if (magnitude == 0 || shift > boost::multiprecision::msb(magnitude)) {
                make_int(context, a < 0 ? -1 : 0, out);
                return;
            }
        }
        if constexpr (Op == BinaryOp::LeftShift)
            result = a << shift;
        else
            result = a >> shift;
    }
    if constexpr (Op == BinaryOp::Power) {
        if (b < 0) {
            double x = a.convert_to<double>(), y = b.convert_to<double>();
            if (std::isinf(x) || std::isinf(y)) {
                raise_error(context, CoreObject::OverflowErrorType,
                            U"int too large to convert to float", out);
                return;
            }
            RootScope roots(context);
            ReturnSlot tmp(context);
            make_float(context, x, tmp);
            auto left = roots.root(tmp.value());
            make_float(context, y, tmp);
            auto right = roots.root(tmp.value());
            binary(context, left, right, BinaryOp::Power, out);
            return;
        }
        result = 1;
        while (b != 0) {
            context.poll();
            if ((b & 1) != 0)
                result *= a;
            b >>= 1;
            if (b != 0)
                a *= a;
        }
    }
    publish(context, std::move(result), out);
}
template <BinaryOp Op>
void bool_bitwise(Attachment &context, Handle function, std::span<const Value> args,
                  ReturnSlot &out)
{
    const auto yes = context.builtin(CoreObject::True), no = context.builtin(CoreObject::False);
    if ((args[0] != yes && args[0] != no) || (args[1] != yes && args[1] != no)) {
        integer_extended<Op, false>(context, function, args, out);
        return;
    }
    const bool a = args[0] == yes, b = args[1] == yes;
    if constexpr (Op == BinaryOp::BitAnd)
        boolean(context, a && b, out);
    if constexpr (Op == BinaryOp::BitXor)
        boolean(context, a != b, out);
    if constexpr (Op == BinaryOp::BitOr)
        boolean(context, a || b, out);
}
template <bool Reflected>
void integer_power(Attachment &context, Handle function, std::span<const Value> args,
                   ReturnSlot &out)
{
    if (args[2] == context.builtin(CoreObject::None)) {
        integer_extended<BinaryOp::Power, Reflected>(context, function, args.first(2), out);
        return;
    }
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!is_int(context, args[1]) || !is_int(context, args[2])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    Integer base = expanded(integer(context, args[Reflected ? 1 : 0]));
    Integer exponent = expanded(integer(context, args[Reflected ? 0 : 1]));
    Integer modulus = expanded(integer(context, args[2]));
    if (modulus == 0) {
        raise_error(context, CoreObject::ValueErrorType, U"pow() 3rd argument cannot be 0", out);
        return;
    }
    const bool negative = modulus < 0;
    if (negative)
        modulus = -modulus;
    base %= modulus;
    if (base < 0)
        base += modulus;
    if (exponent < 0) {
        // Extended Euclid supplies the inverse without a second integer implementation.
        Integer a = base, b = modulus, x = 1, y = 0;
        while (b != 0) {
            context.poll();
            Integer quotient = a / b;
            Integer remainder = a % b;
            Integer next = x - quotient * y;
            a = std::move(b);
            b = std::move(remainder);
            x = std::move(y);
            y = std::move(next);
        }
        if (a != 1) {
            raise_error(context, CoreObject::ValueErrorType,
                        U"base is not invertible for the given modulus", out);
            return;
        }
        base = x % modulus;
        if (base < 0)
            base += modulus;
        exponent = -exponent;
    }
    Integer result = Integer(1) % modulus;
    while (exponent != 0) {
        context.poll();
        if ((exponent & 1) != 0)
            result = (result * base) % modulus;
        exponent >>= 1;
        if (exponent != 0)
            base = (base * base) % modulus;
    }
    if (negative && result != 0)
        result -= modulus;
    publish(context, std::move(result), out);
}
template <CompareOp Op>
void compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!is_int(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    const auto &a = integer(context, args[0]);
    const auto &b = integer(context, args[1]);
    int order;
    if (!a.large && !b.large)
        order = (a.small > b.small) - (a.small < b.small);
    else {
        auto left = expanded(a);
        auto right = expanded(b);
        order = (left > right) - (left < right);
    }
    if constexpr (Op == CompareOp::Eq)
        boolean(context, order == 0, out);
    if constexpr (Op == CompareOp::Ne)
        boolean(context, order != 0, out);
    if constexpr (Op == CompareOp::Lt)
        boolean(context, order < 0, out);
    if constexpr (Op == CompareOp::Le)
        boolean(context, order <= 0, out);
    if constexpr (Op == CompareOp::Gt)
        boolean(context, order > 0, out);
    if constexpr (Op == CompareOp::Ge)
        boolean(context, order >= 0, out);
}
void int_bool(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    boolean(context, int_sign(context, args[0]) != 0, out);
}
void int_identity(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    copy_int(context, args[0], out);
}
void int_getnewargs(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    ReturnSlot value(context);
    publish(context, expanded(integer(context, self.get(context))), value);
    const std::array arguments{value.value()};
    cxxpython::core::make_tuple(context, arguments, out);
}
std::u32string bytes_type_name(Attachment &context, Value value)
{
    TypeView view(context, context.type_of(value));
    const auto name = view.name();
    if (const auto *text = std::get_if<std::u32string>(&name))
        return *text;
    return context.payload<StrObject>(std::get<Value>(name)).value;
}
bool byte_order(Attachment &context, Handle value, bool from, bool &little, ReturnSlot &out)
{
    if (!context.has_layout<StrObject>(value.get(context)) ||
        !is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType))) {
        const auto type = value.get(context) == context.builtin(CoreObject::None)
                              ? U"None"
                              : bytes_type_name(context, value.get(context));
        raise_error(context, CoreObject::TypeErrorType,
                    std::u32string(from ? U"from_bytes()" : U"to_bytes()") +
                        U" argument 'byteorder' must be str, not " + type,
                    out);
        return false;
    }
    const auto &text = context.payload<StrObject>(value).value;
    little = text == U"little";
    if (!little && text != U"big") {
        raise_error(context, CoreObject::ValueErrorType,
                    U"byteorder must be either 'little' or 'big'", out);
        return false;
    }
    return true;
}
void int_from_bytes(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto cls = roots.root(args[0]);
    auto source = roots.root(args[1]);
    auto order = roots.root(args[2]);
    auto sign = roots.root(args[3]);
    ReturnSlot temp(context);
    // Argument Clinic checks the string type before evaluating signed.
    if (!context.has_layout<StrObject>(order.get(context)) ||
        !is_subtype(context, context.type_of(order), context.builtin(CoreObject::StrType))) {
        bool ignored;
        byte_order(context, order, true, ignored, out);
        return;
    }
    truth(context, sign, temp);
    if (raised(temp)) {
        out.raised(temp.exception());
        return;
    }
    const bool is_signed = temp.value() == context.builtin(CoreObject::True);
    bool little;
    if (!byte_order(context, order, true, little, out))
        return;
    std::string data;
    auto method = detail::type_attribute(context, context.type_of(source), U"__bytes__");
    if (method) {
        detail::invoke_special(context, source, roots.root(*method), {}, temp);
        if (raised(temp)) {
            out.raised(temp.exception());
            return;
        }
        if (!is_bytes(context, temp.value())) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"__bytes__ returned non-bytes (type " +
                            bytes_type_name(context, temp.value()) + U")",
                        out);
            return;
        }
        data = context.payload<BytesObject>(temp.value()).value;
    } else if (is_bytearray(context, source.get(context)) ||
               context.has_layout<MemoryViewObject>(source.get(context)) ||
               is_bytes(context, source.get(context))) {
        if (!detail::buffer_bytes(context, source, data, out, false))
            return;
    } else {
        if (!context.has_layout<StrObject>(source.get(context)))
            get_iter(context, source, temp);
        if (context.has_layout<StrObject>(source.get(context)) || raised(temp)) {
            if (!context.has_layout<StrObject>(source.get(context)) &&
                !is_subtype(context, context.type_of(temp.exception()),
                            context.builtin(CoreObject::TypeErrorType))) {
                out.raised(temp.exception());
                return;
            }
            raise_error(context, CoreObject::TypeErrorType,
                        U"cannot convert '" + bytes_type_name(context, source.get(context)) +
                            U"' object to bytes",
                        out);
            return;
        }
        auto iterator = roots.root(temp.value());
        ReturnSlot item(context), number(context);
        for (;;) {
            const auto status = iter_next(context, iterator, item);
            if (status == NextStatus::Raised) {
                out.raised(item.exception());
                return;
            }
            if (status == NextStatus::Done)
                break;
            RootScope iteration(context);
            index(context, iteration.root(item.value()), number);
            if (raised(number)) {
                out.raised(number.exception());
                return;
            }
            std::int64_t byte;
            if (!int_fits_signed(context, number.value(), byte) || byte < 0 || byte > 255) {
                raise_error(context, CoreObject::ValueErrorType, U"bytes must be in range(0, 256)",
                            out);
                return;
            }
            data.push_back(static_cast<char>(byte));
        }
    }
    const bool negative = is_signed && !data.empty() &&
                          (static_cast<unsigned char>(little ? data.back() : data.front()) & 0x80);
    if (negative)
        for (char &byte : data)
            byte = static_cast<char>(~static_cast<unsigned char>(byte));
    const std::vector<unsigned char> bytes(data.begin(), data.end());
    Integer value = 0;
    // Boost 1.74 cannot normalize the zero-limb backend created by an empty range.
    if (!bytes.empty())
        boost::multiprecision::import_bits(value, bytes.begin(), bytes.end(), 8, !little);
    if (negative)
        value = -value - 1;
    publish(context, std::move(value), temp);
    if (raised(temp)) {
        out.raised(temp.exception());
        return;
    }
    if (cls.get(context) == context.builtin(CoreObject::IntType)) {
        out.ok(temp.value());
        return;
    }
    const std::array converted{temp.value()};
    call(context, cls, {converted, {}}, out);
}
void int_to_bytes(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto length_arg = roots.root(args[1]);
    auto order = roots.root(args[2]);
    auto sign = roots.root(args[3]);
    if (!is_int(context, self.get(context))) {
        type_error(context, out);
        return;
    }
    ReturnSlot temp(context);
    index(context, length_arg, temp);
    if (raised(temp)) {
        out.raised(temp.exception());
        return;
    }
    std::int64_t length;
    if (!int_fits_signed(context, temp.value(), length) ||
        length > std::numeric_limits<std::ptrdiff_t>::max() ||
        length < std::numeric_limits<std::ptrdiff_t>::min()) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return;
    }
    if (!context.has_layout<StrObject>(order.get(context)) ||
        !is_subtype(context, context.type_of(order), context.builtin(CoreObject::StrType))) {
        bool ignored;
        byte_order(context, order, false, ignored, out);
        return;
    }
    truth(context, sign, temp);
    if (raised(temp)) {
        out.raised(temp.exception());
        return;
    }
    const bool is_signed = temp.value() == context.builtin(CoreObject::True);
    bool little;
    if (!byte_order(context, order, false, little, out))
        return;
    if (length < 0) {
        raise_error(context, CoreObject::ValueErrorType, U"length argument must be non-negative",
                    out);
        return;
    }
    Integer value = expanded(integer(context, self.get(context)));
    const bool negative = value < 0;
    if (negative && !is_signed) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"can't convert negative int to unsigned", out);
        return;
    }
    if (negative)
        value = -value - 1;
    const auto size = static_cast<std::size_t>(length);
    if (value != 0) {
        const auto bits = static_cast<std::size_t>(boost::multiprecision::msb(value)) + 1;
        const auto needed = bits / 8 + ((bits % 8 != 0 || is_signed) ? 1 : 0);
        if (needed > size) {
            raise_error(context, CoreObject::OverflowErrorType, U"int too big to convert", out);
            return;
        }
    }
    if (size > std::string{}.max_size()) {
        out.raised(context.builtin(CoreObject::MemoryError));
        return;
    }
    std::vector<unsigned char> bytes;
    boost::multiprecision::export_bits(value, std::back_inserter(bytes), 8, false);
    std::string data(size, negative ? '\xff' : '\0');
    for (std::size_t i = 0; i < bytes.size() && i < size; ++i)
        data[little ? i : size - i - 1] =
            static_cast<char>(negative ? static_cast<unsigned char>(~bytes[i]) : bytes[i]);
    make_bytes(context, std::move(data), out);
}
void int_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    make_int(context, detail::int_hash_value(context, args[0]), out);
}
void int_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    auto text = int_decimal(context, args[0]);
    make_str(context, {text.begin(), text.end()}, out);
}
void bool_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.builtin(CoreObject::BoolType)) {
        type_error(context, out);
        return;
    }
    make_str(context, args[0] == context.builtin(CoreObject::True) ? U"True" : U"False", out);
}
bool parse_integer(std::u32string_view text, int base, Integer &result)
{
    while (!text.empty() && detail::numeric_whitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && detail::numeric_whitespace(text.back()))
        text.remove_suffix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == U'+' || text.front() == U'-')) {
        negative = text.front() == U'-';
        text.remove_prefix(1);
    }
    if (text.empty())
        return false;
    const bool automatic = base == 0;
    bool prefixed = false;
    if (text.size() >= 2 && detail::decimal_digit(text[0]) == 0) {
        const auto marker = text[1];
        const int prefix = marker == U'x' || marker == U'X'   ? 16
                           : marker == U'o' || marker == U'O' ? 8
                           : marker == U'b' || marker == U'B' ? 2
                                                              : 0;
        if (prefix && (automatic || base == prefix)) {
            base = prefix;
            text.remove_prefix(2);
            prefixed = true;
            if (!text.empty() && text.front() == U'_')
                text.remove_prefix(1);
        }
    }
    if (automatic && !prefixed)
        base = 10;
    bool digit_seen = false, previous_digit = false;
    result = 0;
    for (auto c : text) {
        if (c == U'_') {
            if (!previous_digit)
                return false;
            previous_digit = false;
            continue;
        }
        int digit = detail::decimal_digit(c);
        if (digit < 0 && c >= U'a' && c <= U'z')
            digit = static_cast<int>(c - U'a') + 10;
        if (digit < 0 && c >= U'A' && c <= U'Z')
            digit = static_cast<int>(c - U'A') + 10;
        if (digit < 0 || digit >= base)
            return false;
        digit_seen = previous_digit = true;
        result *= base;
        result += digit;
    }
    if (!digit_seen || !previous_digit)
        return false;
    if (automatic && !prefixed && detail::decimal_digit(text.front()) == 0 && result != 0)
        return false;
    if (negative)
        result = -result;
    return true;
}
void invalid_literal(Attachment &context, Handle value, int base, ReturnSlot &out,
                     const std::string *bytes = nullptr)
{
    // CPython 3.12.14 PyLong_FromUnicodeObject/_PyLong_FromBytes use %.200R.
    RootScope roots(context);
    ReturnSlot displayed(context);
    if (bytes) {
        make_bytes(context, bytes->substr(0, 200), displayed);
        value = roots.root(displayed.value());
    }
    repr(context, value, displayed);
    if (raised(displayed)) {
        out.raised(displayed.exception());
        return;
    }
    auto text = context.payload<StrObject>(displayed.value()).value;
    if (text.size() > 200)
        text.resize(200);
    const auto number = std::to_string(base);
    raise_error(context, CoreObject::ValueErrorType,
                U"invalid literal for int() with base " +
                    std::u32string(number.begin(), number.end()) + U": " + text,
                out);
}
void parse_object(Attachment &context, Handle value, int base, bool explicit_base, ReturnSlot &out)
{
    std::u32string text;
    std::string data;
    const bool unicode =
        context.has_layout<StrObject>(value.get(context)) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType));
    if (unicode)
        text = context.payload<StrObject>(value).value;
    else {
        if (explicit_base && !is_bytes(context, value.get(context)) &&
            !is_bytearray(context, value.get(context))) {
            type_error(context, out);
            return;
        }
        if (!detail::buffer_bytes(context, value, data, out)) {
            if (!raised(out))
                type_error(context, out);
            return;
        }
        for (unsigned char ch : data) {
            if (ch >= 128) {
                invalid_literal(context, value, base, out, &data);
                return;
            }
            text += static_cast<char32_t>(ch);
        }
    }
    Integer parsed;
    if (!parse_integer(text, base, parsed)) {
        invalid_literal(context, value, base, out, unicode ? nullptr : &data);
        return;
    }
    publish(context, std::move(parsed), out);
}
void int_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::IntType)) ||
        args[0] == context.builtin(CoreObject::BoolType)) {
        type_error(context, out);
        return;
    }
    const auto &values = context.payload<TupleObject>(args[1]).items;
    DictView keywords(context, args[2]);
    const auto base_keyword = keywords.find(U"base");
    if (values.size() > 2 || keywords.entries().size() != (base_keyword ? 1 : 0) ||
        (values.size() == 2 && base_keyword)) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    auto cls = roots.root(args[0]);
    ReturnSlot converted(context);
    const auto base_arg = values.size() == 2 ? std::optional<Value>{values[1]} : base_keyword;
    if (base_arg) {
        if (values.empty()) {
            type_error(context, out);
            return;
        }
        index(context, roots.root(*base_arg), converted);
        if (raised(converted)) {
            out.raised(converted.exception());
            return;
        }
        std::int64_t base;
        if (!int_fits_signed(context, converted.value(), base) ||
            (base != 0 && (base < 2 || base > 36))) {
            raise_error(context, CoreObject::ValueErrorType,
                        U"int() base must be >= 2 and <= 36, or 0", out);
            return;
        }
        parse_object(context, roots.root(values[0]), static_cast<int>(base), true, converted);
    } else if (values.empty())
        make_int(context, 0, converted);
    else if (context.type_of(values[0]) == context.builtin(CoreObject::StrType)) {
        Integer parsed;
        if (!parse_integer(context.payload<StrObject>(values[0]).value, 10, parsed)) {
            invalid_literal(context, roots.root(values[0]), 10, out);
            return;
        }
        publish(context, std::move(parsed), converted);
    } else if (context.type_of(values[0]) == context.builtin(CoreObject::IntType))
        copy_int(context, values[0], converted);
    else {
        auto value = roots.root(values[0]);
        auto slot =
            resolve_protocol(context, roots.root(context.type_of(value)), Protocol::Int, roots);
        if (slot.state != SlotState::Missing)
            invoke_protocol(context, value, slot, {}, converted);
        else {
            auto index_slot = resolve_protocol(context, roots.root(context.type_of(value)),
                                               Protocol::Index, roots);
            auto trunc = index_slot.state == SlotState::Missing
                             ? detail::type_attribute(context, context.type_of(value), U"__trunc__")
                             : std::optional<Value>{};
            if (trunc) {
                ReturnSlot truncated(context);
                detail::invoke_special(context, value, roots.root(*trunc), {{}, {}}, truncated);
                if (raised(truncated)) {
                    out.raised(truncated.exception());
                    return;
                }
                index(context, roots.root(truncated.value()), converted);
            } else if (index_slot.state == SlotState::Missing &&
                       context.has_layout<StrObject>(value.get(context)) &&
                       is_subtype(context, context.type_of(value),
                                  context.builtin(CoreObject::StrType))) {
                Integer parsed;
                if (!parse_integer(context.payload<StrObject>(value).value, 10, parsed)) {
                    invalid_literal(context, value, 10, out);
                    return;
                }
                publish(context, std::move(parsed), converted);
            } else if (index_slot.state == SlotState::Missing &&
                       (is_bytes(context, value.get(context)) ||
                        is_bytearray(context, value.get(context)) ||
                        context.has_layout<MemoryViewObject>(value.get(context))))
                parse_object(context, value, 10, false, converted);
            else
                index(context, value, converted);
        }
        if (raised(converted)) {
            out.raised(converted.exception());
            return;
        }
        if (!is_int(context, converted.value())) {
            type_error(context, out);
            return;
        }
    }
    if (raised(converted)) {
        out.raised(converted.exception());
        return;
    }
    const auto value = converted.value();
    if (cls.get(context) == context.builtin(CoreObject::IntType)) {
        copy_int(context, value, out);
        return;
    }
    const auto &source = integer(context, value);
    if (source.large) {
        auto storage = std::make_unique<detail::BigInt>();
        storage->value = source.large->value;
        context.allocate_instance<IntObject>(cls, out, std::move(storage));
    } else
        context.allocate_instance<IntObject>(cls, out, source.small);
}
template <UnaryOp Op>
void int_unary(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    auto value = expanded(integer(context, args[0]));
    if constexpr (Op == UnaryOp::Negative)
        value = -value;
    if constexpr (Op == UnaryOp::Invert)
        value = ~value;
    publish(context, std::move(value), out);
}
void int_absolute(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0])) {
        type_error(context, out);
        return;
    }
    auto value = expanded(integer(context, args[0]));
    if (value < 0)
        value = -value;
    publish(context, std::move(value), out);
}
template <bool Reflected>
void int_divmod(Attachment &context, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot quotient(context), remainder(context);
    arithmetic<BinaryOp::FloorDivide, Reflected>(context, function, args, quotient);
    if (raised(quotient)) {
        out.raised(quotient.exception());
        return;
    }
    if (quotient.value() == context.builtin(CoreObject::NotImplemented)) {
        out.ok(quotient.value());
        return;
    }
    arithmetic<BinaryOp::Modulo, Reflected>(context, function, args, remainder);
    if (raised(remainder)) {
        out.raised(remainder.exception());
        return;
    }
    const std::array values{quotient.value(), remainder.value()};
    cxxpython::core::make_tuple(context, values, out);
}
void bool_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != context.builtin(CoreObject::BoolType)) {
        type_error(context, out);
        return;
    }
    const auto &values = context.payload<TupleObject>(args[1]).items;
    if (values.size() > 1) {
        type_error(context, out);
        return;
    }
    if (values.empty())
        boolean(context, false, out);
    else {
        RootScope roots(context);
        truth(context, roots.root(values[0]), out);
    }
}
} // namespace
void detail::publish_integer(Attachment &context, Integer value, ReturnSlot &out)
{
    publish(context, std::move(value), out);
}
IntObject::IntObject(InitialType head, std::int64_t value) : header(head), small(value) {}
IntObject::IntObject(InitialType head, std::unique_ptr<detail::BigInt> value)
    : header(head), small(0), large(std::move(value))
{}
IntObject::~IntObject() noexcept = default;
std::size_t IntObject::external_bytes() const noexcept
{
    if (!large)
        return 0;
    const auto &backend = large->value.backend();
    auto data = reinterpret_cast<std::uintptr_t>(backend.limbs());
    auto start = reinterpret_cast<std::uintptr_t>(large.get());
    const bool internal = data >= start && data - start < sizeof(detail::BigInt);
    return sizeof(detail::BigInt) + (internal ? 0 : backend.capacity() * sizeof(*backend.limbs()));
}
bool is_int(Attachment &context, Value value)
{
    return is_subtype(context, context.type_of(value), context.builtin(CoreObject::IntType)) &&
           context.has_layout<IntObject>(value);
}
bool int_fits_signed(Attachment &context, Value value, std::int64_t &result)
{
    const auto &data = integer(context, value);
    if (!data.large) {
        result = data.small;
        return true;
    }
    if (!fits(data.large->value))
        return false;
    result = data.large->value.convert_to<std::int64_t>();
    return true;
}
int int_sign(Attachment &context, Value value)
{
    const auto &data = integer(context, value);
    return data.large ? (data.large->value > 0) - (data.large->value < 0)
                      : (data.small > 0) - (data.small < 0);
}
std::string int_decimal(Attachment &context, Value value)
{
    const auto &data = integer(context, value);
    return data.large ? data.large->value.str() : std::to_string(data.small);
}
void make_int(Attachment &context, std::int64_t value, ReturnSlot &out)
{
    out.check_caller(context);
    if (value == 0 || value == 1) {
        out.ok(context.builtin(value == 0 ? CoreObject::IntZero : CoreObject::IntOne));
        return;
    }
    RootScope roots(context);
    context.allocate_instance<IntObject>(roots.root(context.builtin(CoreObject::IntType)), out,
                                         value);
}
void make_int_decimal(Attachment &context, std::string_view text, ReturnSlot &out)
{
    out.check_caller(context);
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty())
        throw std::invalid_argument("empty integer literal");
    Integer value = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9')
            throw std::invalid_argument("invalid native decimal integer literal");
        value *= 10;
        value += digit - '0';
    }
    if (negative)
        value = -value;
    publish(context, std::move(value), out);
}
void copy_int(Attachment &context, Value value, ReturnSlot &out)
{
    out.check_caller(context);
    RootScope roots(context);
    roots.root(value);
    const auto &data = integer(context, value);
    if (context.type_of(value) == context.builtin(CoreObject::IntType)) {
        out.ok(value);
        return;
    }
    if (data.large)
        publish(context, data.large->value, out);
    else
        make_int(context, data.small, out);
}
namespace detail {
std::int64_t int_hash_value(Attachment &context, Value value)
{
    constexpr std::uint64_t modulus =
        (std::uint64_t{1} << (sizeof(std::intptr_t) == 8 ? 61 : 31)) - 1;
    const auto &data = integer(context, value);
    std::int64_t result;
    if (data.large)
        result = (data.large->value % modulus).convert_to<std::int64_t>();
    else {
        const auto magnitude = data.small < 0
                                   ? std::uint64_t{0} - static_cast<std::uint64_t>(data.small)
                                   : static_cast<std::uint64_t>(data.small);
        result = static_cast<std::int64_t>(magnitude % modulus);
        if (data.small < 0)
            result = -result;
    }
    return result == -1 ? -2 : result;
}
void install_numbers(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    auto install = [&](std::u32string name, NativeEntry entry, bool binary = false) {
        install_native_attribute(context, core, at(CoreObject::IntType), std::move(name),
                                 binary ? pair : unary, entry);
    };
    auto round_signature = unary;
    round_signature.varargs = U"args";
    install_native_attribute(context, core, at(CoreObject::IntType), U"__round__", round_signature,
                             integer_round);
    install(U"__add__", arithmetic<BinaryOp::Add, false>, true);
    install(U"__radd__", arithmetic<BinaryOp::Add, true>, true);
    install(U"__sub__", arithmetic<BinaryOp::Subtract, false>, true);
    install(U"__rsub__", arithmetic<BinaryOp::Subtract, true>, true);
    install(U"__mul__", arithmetic<BinaryOp::Multiply, false>, true);
    install(U"__rmul__", arithmetic<BinaryOp::Multiply, true>, true);
    install(U"__truediv__", true_divide<false>, true);
    install(U"__rtruediv__", true_divide<true>, true);
    install(U"__floordiv__", arithmetic<BinaryOp::FloorDivide, false>, true);
    install(U"__rfloordiv__", arithmetic<BinaryOp::FloorDivide, true>, true);
    install(U"__mod__", arithmetic<BinaryOp::Modulo, false>, true);
    install(U"__rmod__", arithmetic<BinaryOp::Modulo, true>, true);
    install(U"__divmod__", int_divmod<false>, true);
    install(U"__rdivmod__", int_divmod<true>, true);
    auto power_signature = pair;
    power_signature.parameters.push_back(
        {U"mod", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)});
    install_native_attribute(context, core, at(CoreObject::IntType), U"__pow__", power_signature,
                             integer_power<false>);
    install_native_attribute(context, core, at(CoreObject::IntType), U"__rpow__", power_signature,
                             integer_power<true>);
    install(U"__lshift__", integer_extended<BinaryOp::LeftShift, false>, true);
    install(U"__rlshift__", integer_extended<BinaryOp::LeftShift, true>, true);
    install(U"__rshift__", integer_extended<BinaryOp::RightShift, false>, true);
    install(U"__rrshift__", integer_extended<BinaryOp::RightShift, true>, true);
    install(U"__and__", integer_extended<BinaryOp::BitAnd, false>, true);
    install(U"__rand__", integer_extended<BinaryOp::BitAnd, true>, true);
    install(U"__xor__", integer_extended<BinaryOp::BitXor, false>, true);
    install(U"__rxor__", integer_extended<BinaryOp::BitXor, true>, true);
    install(U"__or__", integer_extended<BinaryOp::BitOr, false>, true);
    install(U"__ror__", integer_extended<BinaryOp::BitOr, true>, true);
    for (auto [name, entry] : {std::pair{U"__and__", &bool_bitwise<BinaryOp::BitAnd>},
                               {U"__rand__", bool_bitwise<BinaryOp::BitAnd>},
                               {U"__xor__", bool_bitwise<BinaryOp::BitXor>},
                               {U"__rxor__", bool_bitwise<BinaryOp::BitXor>},
                               {U"__or__", bool_bitwise<BinaryOp::BitOr>},
                               {U"__ror__", bool_bitwise<BinaryOp::BitOr>}})
        install_native_attribute(context, core, at(CoreObject::BoolType), name, pair, entry);
    install(U"__eq__", compare<CompareOp::Eq>, true);
    install(U"__ne__", compare<CompareOp::Ne>, true);
    install(U"__lt__", compare<CompareOp::Lt>, true);
    install(U"__le__", compare<CompareOp::Le>, true);
    install(U"__gt__", compare<CompareOp::Gt>, true);
    install(U"__ge__", compare<CompareOp::Ge>, true);
    install(U"__bool__", int_bool);
    install(U"bit_length", int_bits<false>);
    install(U"bit_count", int_bits<true>);
    {
        RootScope roots(context);
        ReturnSlot word(context);
        context.allocate_instance<StrObject>(at(CoreObject::StrType), word, U"big");
        auto big = roots.root(word.value());
        SignatureLayout from;
        from.parameters = {
            {U"cls", ParameterKind::PositionalOnly},
            {U"bytes", ParameterKind::PositionalOrKeyword},
            {U"byteorder", ParameterKind::PositionalOrKeyword, big.get(context)},
            {U"signed", ParameterKind::KeywordOnly, at(CoreObject::False).get(context)}};
        install_native_attribute(context, core, at(CoreObject::IntType), U"from_bytes", from,
                                 int_from_bytes);
        auto dict = *TypeView(context, at(CoreObject::IntType)).dictionary();
        auto function = roots.root(*name_lookup(context, dict, U"from_bytes"));
        ReturnSlot wrapper(context), key(context);
        context.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), wrapper,
                                                     function.get(context));
        context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"from_bytes");
        name_store(context, dict, key.value(), wrapper.value());
        SignatureLayout to;
        to.parameters = {
            {U"self", ParameterKind::PositionalOnly},
            {U"length", ParameterKind::PositionalOrKeyword, at(CoreObject::IntOne).get(context)},
            {U"byteorder", ParameterKind::PositionalOrKeyword, big.get(context)},
            {U"signed", ParameterKind::KeywordOnly, at(CoreObject::False).get(context)}};
        install_native_attribute(context, core, at(CoreObject::IntType), U"to_bytes", to,
                                 int_to_bytes);
    }
    install(U"__int__", int_identity);
    install(U"__index__", int_identity);
    install(U"__trunc__", int_identity);
    install(U"__floor__", int_identity);
    install(U"__ceil__", int_identity);
    install(U"__getnewargs__", int_getnewargs);
    install(U"__pos__", int_unary<UnaryOp::Positive>);
    install(U"__neg__", int_unary<UnaryOp::Negative>);
    install(U"__abs__", int_absolute);
    install(U"__invert__", int_unary<UnaryOp::Invert>);
    install(U"__hash__", int_hash);
    install(U"__repr__", int_repr);
    install(U"__str__", int_repr);
    install_native_attribute(context, core, at(CoreObject::BoolType), U"__repr__", unary,
                             bool_repr);
    install_native_attribute(context, core, at(CoreObject::BoolType), U"__str__", unary, bool_repr);
    for (auto id : {CoreObject::IntType, CoreObject::BoolType}) {
        SignatureLayout constructor;
        constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
        constructor.varargs = U"args";
        if (id == CoreObject::IntType)
            constructor.varkwargs = U"kwargs";
        install_native_attribute(context, core, at(id), U"__new__", constructor,
                                 id == CoreObject::IntType ? int_new : bool_new);
        RootScope roots(context);
        ReturnSlot out(context);
        const auto dict = *TypeView(context, at(id)).dictionary();
        auto function = *name_lookup(context, dict, U"__new__");
        context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out,
                                                      function);
        auto wrapper = roots.root(out.value());
        context.allocate_instance<StrObject>(at(CoreObject::StrType), out, U"__new__");
        name_store(context, dict, out.value(), wrapper.get(context));
    }
}
} // namespace detail
} // namespace cxxpython::core
