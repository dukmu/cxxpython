#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cxxpython::core {
namespace {
struct Parts {
    double real, imag;
};

void error(Attachment &ctx, CoreObject type, const char32_t *message, ReturnSlot &out)
{
    raise_error(ctx, type, message, out);
}

bool require_complex(Attachment &ctx, Value value, Parts &parts, ReturnSlot &out)
{
    if (!is_complex(ctx, value)) {
        error(ctx, CoreObject::TypeErrorType, U"complex descriptor requires a complex", out);
        return false;
    }
    const auto &object = ctx.payload<ComplexObject>(value);
    parts = {object.real, object.imag};
    return true;
}

bool as_float(Attachment &ctx, Handle value, double &number, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    const std::array arguments{value.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::FloatType)), {arguments, {}}, converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    number = ctx.payload<FloatObject>(converted.value()).value;
    return true;
}

bool operand(Attachment &ctx, Handle value, Parts &parts, ReturnSlot &out)
{
    if (is_complex(ctx, value.get(ctx))) {
        const auto &object = ctx.payload<ComplexObject>(value);
        parts = {object.real, object.imag};
        return true;
    }
    if (is_float(ctx, value.get(ctx))) {
        parts = {ctx.payload<FloatObject>(value).value, 0.0};
        return true;
    }
    if (!is_int(ctx, value.get(ctx))) {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return false;
    }
    RootScope roots(ctx);
    ReturnSlot integer(ctx);
    detail::publish_integer(ctx, detail::integer_value(ctx.payload<IntObject>(value)), integer);
    parts.imag = 0.0;
    return as_float(ctx, roots.root(integer.value()), parts.real, out);
}

// Parsing follows CPython 3.12 Objects/complexobject.c. parse_float validates
// decimal digits, underscores, exponents and special float spellings.
bool parse_complex(std::u32string_view text, Parts &value)
{
    while (!text.empty() && detail::numeric_whitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && detail::numeric_whitespace(text.back()))
        text.remove_suffix(1);
    if (!text.empty() && text.front() == U'(') {
        if (text.size() < 2 || text.back() != U')')
            return false;
        text.remove_prefix(1);
        text.remove_suffix(1);
        while (!text.empty() && detail::numeric_whitespace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && detail::numeric_whitespace(text.back()))
            text.remove_suffix(1);
    }
    if (text.empty())
        return false;
    for (auto c : text)
        if (detail::numeric_whitespace(c))
            return false;
    value = {0.0, 0.0};
    if (text.back() != U'j' && text.back() != U'J')
        return detail::parse_float(text, value.real);
    text.remove_suffix(1);
    std::size_t split = std::u32string_view::npos;
    for (std::size_t i = 1; i < text.size(); ++i)
        if ((text[i] == U'+' || text[i] == U'-') && text[i - 1] != U'e' && text[i - 1] != U'E') {
            split = i;
            break;
        }
    if (split != std::u32string_view::npos) {
        if (!detail::parse_float(text.substr(0, split), value.real))
            return false;
        text.remove_prefix(split);
    }
    if (text.empty() || text == U"+")
        value.imag = 1.0;
    else if (text == U"-")
        value.imag = -1.0;
    else if (!detail::parse_float(text, value.imag))
        return false;
    return true;
}

bool string_value(Attachment &ctx, Value value)
{
    return ctx.has_layout<StrObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::StrType));
}

void complex_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!ctx.has_layout<TypeObject>(args[0]) ||
        !is_subtype(ctx, args[0], ctx.builtin(CoreObject::ComplexType))) {
        error(ctx, CoreObject::TypeErrorType, U"complex.__new__ requires a complex type", out);
        return;
    }
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    const auto &pos = ctx.payload<TupleObject>(args[1]).items;
    auto keywords = roots.root(args[2]);
    if (pos.size() > 2) {
        error(ctx, CoreObject::TypeErrorType, U"complex() takes at most 2 arguments", out);
        return;
    }
    auto real_kw = detail::name_lookup(ctx, keywords.get(ctx), U"real");
    auto imag_kw = detail::name_lookup(ctx, keywords.get(ctx), U"imag");
    if (detail::dictionary_size(ctx, keywords.get(ctx)) !=
            static_cast<std::size_t>(bool(real_kw) + bool(imag_kw)) ||
        (real_kw && !pos.empty()) || (imag_kw && pos.size() > 1)) {
        error(ctx, CoreObject::TypeErrorType, U"invalid complex() arguments", out);
        return;
    }
    std::optional<Handle> real, imag;
    if (!pos.empty())
        real = roots.root(pos[0]);
    else if (real_kw)
        real = roots.root(*real_kw);
    if (pos.size() > 1)
        imag = roots.root(pos[1]);
    else if (imag_kw)
        imag = roots.root(*imag_kw);
    Parts a{0.0, 0.0}, b{0.0, 0.0};
    if (real && string_value(ctx, real->get(ctx))) {
        if (imag) {
            error(ctx, CoreObject::TypeErrorType,
                  U"complex() can't take second arg if first is a string", out);
            return;
        }
        if (!parse_complex(ctx.payload<StrObject>(*real).value, a)) {
            error(ctx, CoreObject::ValueErrorType, U"complex() arg is a malformed string", out);
            return;
        }
    } else {
        if (imag && string_value(ctx, imag->get(ctx))) {
            error(ctx, CoreObject::TypeErrorType, U"complex() second arg can't be a string", out);
            return;
        }
        if (real) {
            if (!imag && ctx.type_of(real->get(ctx)) == ctx.builtin(CoreObject::ComplexType) &&
                cls.get(ctx) == ctx.builtin(CoreObject::ComplexType)) {
                out.ok(real->get(ctx));
                return;
            }
            auto method = detail::type_attribute(ctx, ctx.type_of(real->get(ctx)), U"__complex__");
            ReturnSlot converted(ctx);
            if (method) {
                detail::invoke_special(ctx, *real, roots.root(*method), {{}, {}}, converted);
                if (converted.tag() == ResultTag::Raised) {
                    out.raised(converted.exception());
                    return;
                }
                if (!is_complex(ctx, converted.value())) {
                    error(ctx, CoreObject::TypeErrorType, U"__complex__ returned non-complex", out);
                    return;
                }
                real = roots.root(converted.value());
            }
            if (is_complex(ctx, real->get(ctx))) {
                const auto &object = ctx.payload<ComplexObject>(*real);
                a = {object.real, object.imag};
            } else if (!as_float(ctx, *real, a.real, out))
                return;
        }
        if (imag) {
            if (is_complex(ctx, imag->get(ctx))) {
                const auto &object = ctx.payload<ComplexObject>(*imag);
                b = {object.real, object.imag};
                a.real -= b.imag;
            } else if (!as_float(ctx, *imag, b.real, out))
                return;
            if (real && is_complex(ctx, real->get(ctx)))
                b.real += a.imag;
            a.imag = b.real;
        }
    }
    ctx.allocate_instance<ComplexObject>(cls, out, a.real, a.imag);
}

void complex_field_real(Attachment &ctx, Value value, ReturnSlot &out)
{
    Parts p;
    if (require_complex(ctx, value, p, out))
        make_float(ctx, p.real, out);
}
void complex_field_imag(Attachment &ctx, Value value, ReturnSlot &out)
{
    Parts p;
    if (require_complex(ctx, value, p, out))
        make_float(ctx, p.imag, out);
}
void complex_getnewargs(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts parts;
    if (!require_complex(ctx, args[0], parts, out))
        return;
    RootScope roots(ctx);
    roots.root(args[0]);
    ReturnSlot real(ctx), imag(ctx);
    make_float(ctx, parts.real, real);
    make_float(ctx, parts.imag, imag);
    const std::array arguments{real.value(), imag.value()};
    cxxpython::core::make_tuple(ctx, arguments, out);
}

enum class Unary { Bool, Pos, Neg, Abs, Conjugate, Identity };
template <Unary Op>
void complex_unary(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts p;
    if (!require_complex(ctx, args[0], p, out))
        return;
    if constexpr (Op == Unary::Bool)
        out.ok(ctx.builtin(p.real != 0.0 || p.imag != 0.0 ? CoreObject::True : CoreObject::False));
    else if constexpr (Op == Unary::Abs) {
        double result = std::hypot(p.real, p.imag);
        if (std::isinf(result) && std::isfinite(p.real) && std::isfinite(p.imag))
            error(ctx, CoreObject::OverflowErrorType, U"absolute value too large", out);
        else
            make_float(ctx, result, out);
    } else if constexpr (Op == Unary::Pos || Op == Unary::Identity) {
        if (ctx.type_of(args[0]) == ctx.builtin(CoreObject::ComplexType))
            out.ok(args[0]);
        else
            make_complex(ctx, p.real, p.imag, out);
    } else if constexpr (Op == Unary::Neg)
        make_complex(ctx, -p.real, -p.imag, out);
    else
        make_complex(ctx, p.real, -p.imag, out);
}

Parts multiply(Parts a, Parts b)
{
    return {a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real};
}
bool divide(Parts a, Parts b, Parts &result)
{
    double r = std::abs(b.real), i = std::abs(b.imag);
    if (r >= i) {
        if (r == 0.0)
            return false;
        double ratio = b.imag / b.real, denom = b.real + b.imag * ratio;
        result = {(a.real + a.imag * ratio) / denom, (a.imag - a.real * ratio) / denom};
    } else if (i >= r) {
        double ratio = b.real / b.imag, denom = b.real * ratio + b.imag;
        result = {(a.real * ratio + a.imag) / denom, (a.imag * ratio - a.real) / denom};
    } else
        result = {std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::quiet_NaN()};
    return true;
}

template <BinaryOp Op, bool Reflected>
void complex_binary(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts a, b, result;
    if (!require_complex(ctx, args[0], a, out))
        return;
    RootScope roots(ctx);
    if (!operand(ctx, roots.root(args[1]), b, out))
        return;
    if constexpr (Reflected)
        std::swap(a, b);
    if constexpr (Op == BinaryOp::Add)
        result = {a.real + b.real, a.imag + b.imag};
    else if constexpr (Op == BinaryOp::Subtract)
        result = {a.real - b.real, a.imag - b.imag};
    else if constexpr (Op == BinaryOp::Multiply)
        result = multiply(a, b);
    else if constexpr (Op == BinaryOp::TrueDivide) {
        if (!divide(a, b, result)) {
            error(ctx, CoreObject::ZeroDivisionErrorType, U"complex division by zero", out);
            return;
        }
    } else {
        if (b.imag == 0.0 && b.real == std::floor(b.real) && std::abs(b.real) <= 100.0) {
            int n = static_cast<int>(b.real);
            unsigned count = static_cast<unsigned>(n < 0 ? -n : n);
            result = {1.0, 0.0};
            Parts power = a;
            while (count) {
                if (count & 1)
                    result = multiply(result, power);
                count >>= 1;
                if (count)
                    power = multiply(power, power);
            }
            if (n < 0 && !divide({1.0, 0.0}, result, result)) {
                error(ctx, CoreObject::ZeroDivisionErrorType, U"0.0 to a negative or complex power",
                      out);
                return;
            }
        } else if (a.real == 0.0 && a.imag == 0.0) {
            if (b.imag != 0.0 || b.real < 0.0) {
                error(ctx, CoreObject::ZeroDivisionErrorType, U"0.0 to a negative or complex power",
                      out);
                return;
            }
            result = {0.0, 0.0};
        } else {
            double magnitude = std::hypot(a.real, a.imag), angle = std::atan2(a.imag, a.real);
            double length = std::pow(magnitude, b.real), phase = angle * b.real;
            if (b.imag != 0.0) {
                length *= std::exp(-angle * b.imag);
                phase += b.imag * std::log(magnitude);
            }
            result = {length * std::cos(phase), length * std::sin(phase)};
        }
        if (std::isinf(result.real) || std::isinf(result.imag)) {
            error(ctx, CoreObject::OverflowErrorType, U"complex exponentiation", out);
            return;
        }
    }
    make_complex(ctx, result.real, result.imag, out);
}

template <bool NotEqual>
void complex_compare(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts a;
    if (!require_complex(ctx, args[0], a, out))
        return;
    bool equal;
    if (is_complex(ctx, args[1])) {
        const auto &b = ctx.payload<ComplexObject>(args[1]);
        equal = a.real == b.real && a.imag == b.imag;
    } else if (is_float(ctx, args[1]))
        equal = a.imag == 0.0 && a.real == ctx.payload<FloatObject>(args[1]).value;
    else if (is_int(ctx, args[1])) {
        if (a.imag != 0.0 || !std::isfinite(a.real) || std::trunc(a.real) != a.real)
            equal = false;
        else
            equal =
                detail::Integer(a.real) == detail::integer_value(ctx.payload<IntObject>(args[1]));
    } else {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return;
    }
    out.ok(ctx.builtin(equal != NotEqual ? CoreObject::True : CoreObject::False));
}

bool render_float(Attachment &ctx, double number, std::u32string &result, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot value(ctx), rendered(ctx);
    make_float(ctx, number, value);
    repr(ctx, roots.root(value.value()), rendered);
    if (rendered.tag() == ResultTag::Raised) {
        out.raised(rendered.exception());
        return false;
    }
    result = ctx.payload<StrObject>(rendered.value()).value;
    if (result.size() >= 2 && result.substr(result.size() - 2) == U".0")
        result.resize(result.size() - 2);
    return true;
}

void complex_repr(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts p;
    if (!require_complex(ctx, args[0], p, out))
        return;
    std::u32string real, imag;
    if (!render_float(ctx, p.imag, imag, out))
        return;
    if (p.real == 0.0 && !std::signbit(p.real)) {
        make_str(ctx, imag + U"j", out);
        return;
    }
    if (!render_float(ctx, p.real, real, out))
        return;
    if (imag.empty() || imag[0] != U'-')
        imag.insert(imag.begin(), U'+');
    make_str(ctx, U"(" + real + imag + U"j)", out);
}

bool hash_part(Attachment &ctx, Value owner, double number, std::uint64_t &result, ReturnSlot &out)
{
    if (std::isnan(number)) {
        auto address = reinterpret_cast<std::uintptr_t>(owner.object());
        result = (address >> 4) | (address << (sizeof(address) * 8 - 4));
        if (result == std::numeric_limits<std::uint64_t>::max())
            --result;
        return true;
    }
    RootScope roots(ctx);
    ReturnSlot number_object(ctx), hashed(ctx);
    make_float(ctx, number, number_object);
    hash(ctx, roots.root(number_object.value()), hashed);
    if (hashed.tag() == ResultTag::Raised) {
        out.raised(hashed.exception());
        return false;
    }
    const auto integer = detail::integer_value(ctx.payload<IntObject>(hashed.value()));
    result = static_cast<std::uint64_t>(integer.convert_to<std::int64_t>());
    return true;
}

void complex_hash(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Parts p;
    if (!require_complex(ctx, args[0], p, out))
        return;
    RootScope roots(ctx);
    auto owner = roots.root(args[0]);
    std::uint64_t real, imag;
    if (!hash_part(ctx, owner.get(ctx), p.real, real, out) ||
        !hash_part(ctx, owner.get(ctx), p.imag, imag, out))
        return;
    auto combined = real + std::uint64_t{1000003} * imag;
    if (combined == std::numeric_limits<std::uint64_t>::max())
        --combined;
    make_int(ctx, static_cast<std::int64_t>(combined), out);
}
} // namespace

bool is_complex(Attachment &context, Value value)
{
    return context.has_layout<ComplexObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::ComplexType));
}

void make_complex(Attachment &context, double real, double imag, ReturnSlot &out)
{
    RootScope roots(context);
    context.allocate_instance<ComplexObject>(roots.root(context.builtin(CoreObject::ComplexType)),
                                             out, real, imag);
}

void detail::install_complex(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto type = at(CoreObject::ComplexType);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    auto install = [&](std::u32string name, NativeEntry entry, bool binary = false) {
        install_native_attribute(context, core, type, std::move(name), binary ? pair : unary,
                                 entry);
    };
    install(U"__bool__", complex_unary<Unary::Bool>);
    install(U"__pos__", complex_unary<Unary::Pos>);
    install(U"__neg__", complex_unary<Unary::Neg>);
    install(U"__abs__", complex_unary<Unary::Abs>);
    install(U"__complex__", complex_unary<Unary::Identity>);
    install(U"__getnewargs__", complex_getnewargs);
    install(U"conjugate", complex_unary<Unary::Conjugate>);
    install(U"__repr__", complex_repr);
    install(U"__str__", complex_repr);
    install(U"__hash__", complex_hash);
    install(U"__eq__", complex_compare<false>, true);
    install(U"__ne__", complex_compare<true>, true);
    install(U"__add__", complex_binary<BinaryOp::Add, false>, true);
    install(U"__radd__", complex_binary<BinaryOp::Add, true>, true);
    install(U"__sub__", complex_binary<BinaryOp::Subtract, false>, true);
    install(U"__rsub__", complex_binary<BinaryOp::Subtract, true>, true);
    install(U"__mul__", complex_binary<BinaryOp::Multiply, false>, true);
    install(U"__rmul__", complex_binary<BinaryOp::Multiply, true>, true);
    install(U"__truediv__", complex_binary<BinaryOp::TrueDivide, false>, true);
    install(U"__rtruediv__", complex_binary<BinaryOp::TrueDivide, true>, true);
    install(U"__pow__", complex_binary<BinaryOp::Power, false>, true);
    install(U"__rpow__", complex_binary<BinaryOp::Power, true>, true);
    install_native_field(context, core, type, U"real", complex_field_real);
    install_native_field(context, core, type, U"imag", complex_field_imag);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(context, core, type, U"__new__", constructor, complex_new);
    RootScope roots(context);
    ReturnSlot out(context);
    const auto dict = *TypeView(context, type).dictionary();
    auto function = *name_lookup(context, dict, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out, function);
    auto wrapper = roots.root(out.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), out, U"__new__");
    name_store(context, dict, out.value(), wrapper.get(context));
}
} // namespace cxxpython::core
