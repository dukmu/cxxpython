// Native scalar math foundation; the complete CPython math API remains a gate.
#include "internal/mathmodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "internal/long.hpp"
#include "internal/math_integer.hpp"
#include "internal/math_reductions.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace cxxpython::core::detail {
namespace {
// Adapted from CPython 3.12.14 Modules/mathmodule.c (PSF license).
constexpr double lanczos_g = 6.024680040776729583740234375;
constexpr double lanczos_half = 5.524680040776729583740234375;
constexpr double lanczos_num[] = {23531376880.410759688572007674451636754734846804940,
                                  42919803642.649098768957899047001988850926355848959,
                                  35711959237.355668049440185451547166705960488635843,
                                  17921034426.037209699919755754458931112671403265390,
                                  6039542586.3520280050642916443072979210699388420708,
                                  1439720407.3117216736632230727949123939715485786772,
                                  248874557.86205415651146038641322942321632125127801,
                                  31426415.585400194380614231628318205362874684987640,
                                  2876370.6289353724412254090516208496135991145378768,
                                  186056.26539522349504029498971604569928220784236328,
                                  8071.6720023658162106380029022722506138218516325024,
                                  210.82427775157934587250973392071336271166969580291,
                                  2.5066282746310002701649081771338373386264310793408};
constexpr double lanczos_den[] = {0.0,        39916800.0, 120543840.0, 150917976.0, 105258076.0,
                                  45995730.0, 13339535.0, 2637558.0,   357423.0,    32670.0,
                                  1925.0,     66.0,       1.0};
constexpr double gamma_integral[] = {1.0,
                                     1.0,
                                     2.0,
                                     6.0,
                                     24.0,
                                     120.0,
                                     720.0,
                                     5040.0,
                                     40320.0,
                                     362880.0,
                                     3628800.0,
                                     39916800.0,
                                     479001600.0,
                                     6227020800.0,
                                     87178291200.0,
                                     1307674368000.0,
                                     20922789888000.0,
                                     355687428096000.0,
                                     6402373705728000.0,
                                     121645100408832000.0,
                                     2432902008176640000.0,
                                     51090942171709440000.0,
                                     1124000727777607680000.0};

double lanczos_sum(double x)
{
    double num = 0.0, den = 0.0;
    for (int i = 0; i != 13; ++i) {
        int j = x < 5.0 ? 12 - i : i;
        num = x < 5.0 ? num * x + lanczos_num[j] : num / x + lanczos_num[j];
        den = x < 5.0 ? den * x + lanczos_den[j] : den / x + lanczos_den[j];
    }
    return num / den;
}

double sinpi(double x)
{
    double y = std::fmod(std::fabs(x), 2.0), r;
    switch (static_cast<int>(std::round(2.0 * y))) {
    case 0:
        r = std::sin(std::numbers::pi * y);
        break;
    case 1:
        r = std::cos(std::numbers::pi * (y - 0.5));
        break;
    case 2:
        r = std::sin(std::numbers::pi * (1.0 - y));
        break;
    case 3:
        r = -std::cos(std::numbers::pi * (y - 1.5));
        break;
    default:
        r = std::sin(std::numbers::pi * (y - 2.0));
        break;
    }
    return std::copysign(1.0, x) * r;
}

double gamma_value(double x)
{
    if (!std::isfinite(x))
        return x;
    if (x == std::floor(x) && x > 0 && x <= 23)
        return gamma_integral[static_cast<int>(x) - 1];
    double a = std::fabs(x);
    if (a < 1e-20)
        return 1.0 / x;
    if (a > 200.0)
        return x < 0 ? 0.0 / sinpi(x) : std::numeric_limits<double>::infinity();
    double y = a + lanczos_half;
    double z = (a > lanczos_half ? (y - a) - lanczos_half : (y - lanczos_half) - a) * lanczos_g / y;
    double r;
    if (x < 0) {
        r = -std::numbers::pi / sinpi(a) / a * std::exp(y) / lanczos_sum(a);
        r -= z * r;
        if (a < 140)
            r /= std::pow(y, a - 0.5);
        else {
            double p = std::pow(y, a / 2.0 - 0.25);
            r /= p;
            r /= p;
        }
    } else {
        r = lanczos_sum(a) / std::exp(y);
        r += z * r;
        if (a < 140)
            r *= std::pow(y, a - 0.5);
        else {
            double p = std::pow(y, a / 2.0 - 0.25);
            r *= p;
            r *= p;
        }
    }
    return r;
}

double lgamma_value(double x)
{
    if (std::isnan(x))
        return x;
    if (std::isinf(x))
        return std::numeric_limits<double>::infinity();
    if (x == 1.0 || x == 2.0)
        return 0.0;
    double a = std::fabs(x);
    if (a < 1e-20)
        return -std::log(a);
    double r = std::log(lanczos_sum(a)) - lanczos_g;
    r += (a - 0.5) * (std::log(a + lanczos_g - 0.5) - 1);
    if (x < 0)
        r = 1.14472988584940017414342735135305871 - std::log(std::fabs(sinpi(a))) - std::log(a) - r;
    return r;
}

enum class LogKind { Natural, Base2, Base10 };
double native_log(double x, LogKind kind)
{
    if (kind == LogKind::Base2)
        return std::log2(x);
    if (kind == LogKind::Base10)
        return std::log10(x);
    return std::log(x);
}

bool log_value(Attachment &c, Handle value, LogKind kind, double &result, ReturnSlot &out)
{
    double x;
    if (is_int(c, value.get(c))) {
        Integer n = integer_value(c.payload<IntObject>(value));
        if (n <= 0) {
            raise_error(c, CoreObject::ValueErrorType, U"math domain error", out);
            return false;
        }
        auto bits = boost::multiprecision::msb(n) + 1;
        if (bits <= 53) {
            result = native_log(n.convert_to<double>(), kind);
            return true;
        }
        {
            auto shift = bits - 53;
            Integer top = n >> shift;
            Integer remainder = n - (top << shift);
            Integer half = Integer(1) << (shift - 1);
            if (remainder > half || (remainder == half && static_cast<bool>(top & 1)))
                ++top;
            double fraction = std::ldexp(top.convert_to<double>(), -53);
            if (fraction == 1.0) {
                fraction = 0.5;
                ++bits;
            }
            if (bits <= 1024) {
                x = std::ldexp(fraction, static_cast<int>(bits));
                if (std::isfinite(x)) {
                    result = native_log(x, kind);
                    return true;
                }
            }
            result = native_log(fraction, kind) + native_log(2.0, kind) * static_cast<double>(bits);
            return true;
        }
    }
    if (!float_as_double(c, value, x, out))
        return false;
    if (x <= 0) {
        raise_error(c, CoreObject::ValueErrorType, U"math domain error", out);
        return false;
    }
    result = native_log(x, kind);
    return true;
}

template <LogKind kind>
void logarithm(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto input = roots.root(args[0]);
    Handle extra = roots.root(args[1]);
    const auto &items = c.payload<TupleObject>(extra).items;
    if (items.size() > 1) {
        raise_error(c, CoreObject::TypeErrorType, U"log expected at most 2 arguments", out);
        return;
    }
    bool has_base = !items.empty();
    auto base = roots.root(has_base ? items[0] : c.builtin(CoreObject::None));
    double numerator;
    if (!log_value(c, input, kind, numerator, out))
        return;
    if (has_base) {
        double denominator;
        if (!log_value(c, base, kind, denominator, out))
            return;
        if (denominator == 0.0) {
            raise_error(c, CoreObject::ZeroDivisionErrorType, U"float division by zero", out);
            return;
        }
        numerator /= denominator;
    }
    make_float(c, numerator, out);
}

template <LogKind kind>
void single_logarithm(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    double result;
    if (log_value(c, roots.root(args[0]), kind, result, out))
        make_float(c, result, out);
}

enum class Op {
    Modf,
    Frexp,
    Isfinite,
    Isinf,
    Isnan,
    Sqrt,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Sinh,
    Cosh,
    Tanh,
    Asinh,
    Acosh,
    Atanh,
    Gamma,
    Lgamma,
    Exp,
    Expm1,
    Log1p,
    Fabs
};
template <Op op> void unary(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    double x;
    if (!float_as_double(c, roots.root(args[0]), x, out))
        return;
    if constexpr (op == Op::Isfinite || op == Op::Isinf || op == Op::Isnan) {
        bool value = op == Op::Isfinite ? std::isfinite(x)
                     : op == Op::Isinf  ? std::isinf(x)
                                        : std::isnan(x);
        out.ok(c.builtin(value ? CoreObject::True : CoreObject::False));
    } else if constexpr (op == Op::Modf || op == Op::Frexp) {
        double first, second = 0;
        int exponent = 0;
        if constexpr (op == Op::Modf)
            first = std::modf(x, &second);
        else
            first = std::frexp(x, &exponent);
        ReturnSlot a(c), b(c);
        make_float(c, first, a);
        if (a.tag() == ResultTag::Raised) {
            out.raised(a.exception());
            return;
        }
        auto rooted = roots.root(a.value());
        if constexpr (op == Op::Modf)
            make_float(c, second, b);
        else
            make_int(c, exponent, b);
        if (b.tag() == ResultTag::Raised) {
            out.raised(b.exception());
            return;
        }
        const std::array values{rooted.get(c), b.value()};
        cxxpython::core::make_tuple(c, values, out);
    } else {
        double y;
        if constexpr (op == Op::Sqrt)
            y = std::sqrt(x);
        else if constexpr (op == Op::Sin)
            y = std::sin(x);
        else if constexpr (op == Op::Cos)
            y = std::cos(x);
        else if constexpr (op == Op::Tan)
            y = std::tan(x);
        else if constexpr (op == Op::Asin)
            y = std::asin(x);
        else if constexpr (op == Op::Acos)
            y = std::acos(x);
        else if constexpr (op == Op::Atan)
            y = std::atan(x);
        else if constexpr (op == Op::Sinh)
            y = std::sinh(x);
        else if constexpr (op == Op::Cosh)
            y = std::cosh(x);
        else if constexpr (op == Op::Tanh)
            y = std::tanh(x);
        else if constexpr (op == Op::Asinh)
            y = std::asinh(x);
        else if constexpr (op == Op::Acosh)
            y = std::acosh(x);
        else if constexpr (op == Op::Atanh)
            y = std::atanh(x);
        else if constexpr (op == Op::Gamma)
            y = gamma_value(x);
        else if constexpr (op == Op::Lgamma)
            y = lgamma_value(x);
        else if constexpr (op == Op::Exp)
            y = std::exp(x);
        else if constexpr (op == Op::Expm1)
            y = std::expm1(x);
        else if constexpr (op == Op::Log1p)
            y = std::log1p(x);
        else
            y = std::fabs(x);
        if ((std::isnan(y) && !std::isnan(x)) || (op == Op::Log1p && x == -1) ||
            (op == Op::Atanh && std::fabs(x) == 1) ||
            ((op == Op::Gamma || op == Op::Lgamma) && std::isfinite(x) && x <= 0 &&
             x == std::floor(x)) ||
            (op == Op::Gamma && std::isinf(x) && x < 0)) {
            raise_error(c, CoreObject::ValueErrorType, U"math domain error", out);
        } else if (std::isinf(y) && std::isfinite(x)) {
            raise_error(c, CoreObject::OverflowErrorType, U"math range error", out);
        } else
            make_float(c, y, out);
    }
}

enum class Binary { Fmod, Remainder, Copysign, Ldexp };
template <Binary op>
void binary_math(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto first = roots.root(args[0]);
    auto second = roots.root(args[1]);
    double x, y;
    if (!float_as_double(c, first, x, out))
        return;
    if constexpr (op == Binary::Ldexp) {
        if (!is_int(c, second.get(c))) {
            raise_error(c, CoreObject::TypeErrorType,
                        U"Expected an int as second argument to ldexp.", out);
            return;
        }
        std::int64_t exponent;
        if (!int_fits_signed(c, second.get(c), exponent)) {
            if (x == 0 || !std::isfinite(x)) {
                make_float(c, x, out);
                return;
            }
            if (int_sign(c, second.get(c)) < 0) {
                make_float(c, std::copysign(0.0, x), out);
                return;
            }
            raise_error(c, CoreObject::OverflowErrorType, U"math range error", out);
            return;
        }
        if (exponent > 100000)
            exponent = 100000;
        if (exponent < -100000)
            exponent = -100000;
        y = std::ldexp(x, static_cast<int>(exponent));
        if (std::isinf(y) && std::isfinite(x)) {
            raise_error(c, CoreObject::OverflowErrorType, U"math range error", out);
            return;
        }
    } else {
        if (!float_as_double(c, second, y, out))
            return;
        if constexpr (op == Binary::Copysign)
            y = std::copysign(x, y);
        else {
            if ((std::isinf(x) && !std::isnan(y)) || (y == 0 && !std::isnan(x))) {
                raise_error(c, CoreObject::ValueErrorType, U"math domain error", out);
                return;
            }
            if constexpr (op == Binary::Fmod)
                y = std::fmod(x, y);
            else
                y = std::remainder(x, y);
        }
    }
    make_float(c, y, out);
}

template <int mode>
void rounding(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto value = roots.root(args[0]);
    auto method = type_attribute(c, c.type_of(value),
                                 mode == 0   ? U"__floor__"
                                 : mode == 1 ? U"__ceil__"
                                             : U"__trunc__");
    if (method) {
        invoke_special(c, value, roots.root(*method), {{}, {}}, out);
        return;
    }
    if constexpr (mode == 2) {
        raise_error(c, CoreObject::TypeErrorType, U"type doesn't define __trunc__ method", out);
        return;
    }
    double x;
    if (!float_as_double(c, value, x, out))
        return;
    ReturnSlot rounded(c);
    make_float(c, mode == 0 ? std::floor(x) : std::ceil(x), rounded);
    if (rounded.tag() == ResultTag::Raised) {
        out.raised(rounded.exception());
        return;
    }
    const std::array values{rounded.value()};
    call(c, roots.root(c.builtin(CoreObject::IntType)), {values, {}}, out);
}
void math_isqrt(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot indexed(c);
    index(c, roots.root(args[0]), indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return;
    }
    Integer n = integer_value(c.payload<IntObject>(indexed.value()));
    if (n < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"isqrt() argument must be nonnegative", out);
        return;
    }

    Integer result;
    {
        // Only owned native integers are accessed while the attachment is parked.
        NativeWait wait(c);
        if (n != 0) {
            result = Integer(1) << ((boost::multiprecision::msb(n) + 2) / 2);
            for (;;) {
                Integer next = (result + n / result) >> 1;
                if (next >= result)
                    break;
                result = std::move(next);
            }
        }
    }
    publish_integer(c, std::move(result), out);
}
} // namespace

void initialize_math(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dict = roots.root(module_dict(c, module.get(c)));
    SignatureLayout signature;
    signature.parameters = {{U"n", ParameterKind::PositionalOnly}};
    ReturnSlot function(c), key(c);
    make_native_function(c, "isqrt", signature, math_isqrt, {}, function,
                         NativeFunctionBinding::Function);
    if (function.tag() == ResultTag::Raised) {
        out.raised(function.exception());
        return;
    }
    const auto rooted = roots.root(function.value());
    make_str(c, U"isqrt", key);
    if (key.tag() == ResultTag::Raised) {
        out.raised(key.exception());
        return;
    }
    dict_store(c, dict, roots.root(key.value()), rooted, out);
    if (out.tag() == ResultTag::Raised)
        return;
    struct Entry {
        const char *name;
        NativeEntry entry;
        int arity;
    };
    const Entry entries[] = {
        {"modf", unary<Op::Modf>, 1},
        {"frexp", unary<Op::Frexp>, 1},
        {"isfinite", unary<Op::Isfinite>, 1},
        {"isinf", unary<Op::Isinf>, 1},
        {"isnan", unary<Op::Isnan>, 1},
        {"sqrt", unary<Op::Sqrt>, 1},
        {"sin", unary<Op::Sin>, 1},
        {"cos", unary<Op::Cos>, 1},
        {"tan", unary<Op::Tan>, 1},
        {"asin", unary<Op::Asin>, 1},
        {"acos", unary<Op::Acos>, 1},
        {"atan", unary<Op::Atan>, 1},
        {"sinh", unary<Op::Sinh>, 1},
        {"cosh", unary<Op::Cosh>, 1},
        {"tanh", unary<Op::Tanh>, 1},
        {"asinh", unary<Op::Asinh>, 1},
        {"acosh", unary<Op::Acosh>, 1},
        {"atanh", unary<Op::Atanh>, 1},
        {"gamma", unary<Op::Gamma>, 1},
        {"lgamma", unary<Op::Lgamma>, 1},
        {"exp", unary<Op::Exp>, 1},
        {"expm1", unary<Op::Expm1>, 1},
        {"log", logarithm<LogKind::Natural>, -1},
        {"log2", single_logarithm<LogKind::Base2>, 1},
        {"log10", single_logarithm<LogKind::Base10>, 1},
        {"log1p", unary<Op::Log1p>, 1},
        {"fabs", unary<Op::Fabs>, 1},
        {"fmod", binary_math<Binary::Fmod>, 2},
        {"remainder", binary_math<Binary::Remainder>, 2},
        {"copysign", binary_math<Binary::Copysign>, 2},
        {"ldexp", binary_math<Binary::Ldexp>, 2},
        {"floor", rounding<0>, 1},
        {"ceil", rounding<1>, 1},
        {"trunc", rounding<2>, 1},
    };
    for (const auto &entry : entries) {
        SignatureLayout sig;
        sig.parameters = {{U"x", ParameterKind::PositionalOnly}};
        if (entry.arity == 2)
            sig.parameters.push_back({U"y", ParameterKind::PositionalOnly});
        if (entry.arity == -1)
            sig.varargs = U"args";
        make_native_function(c, entry.name, sig, entry.entry, {}, function,
                             NativeFunctionBinding::Function);
        if (function.tag() == ResultTag::Raised) {
            out.raised(function.exception());
            return;
        }
        auto item = roots.root(function.value());
        std::u32string name;
        for (const char *p = entry.name; *p; ++p)
            name.push_back(static_cast<char32_t>(*p));
        make_str(c, name, key);
        if (key.tag() == ResultTag::Raised) {
            out.raised(key.exception());
            return;
        }
        dict_store(c, dict, roots.root(key.value()), item, out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    struct Constant {
        const char32_t *name;
        double value;
    };
    const Constant constants[] = {{U"pi", std::numbers::pi},
                                  {U"e", std::numbers::e},
                                  {U"tau", 2 * std::numbers::pi},
                                  {U"inf", std::numeric_limits<double>::infinity()},
                                  {U"nan", std::numeric_limits<double>::quiet_NaN()}};
    for (const auto &constant : constants) {
        make_float(c, constant.value, function);
        if (function.tag() == ResultTag::Raised) {
            out.raised(function.exception());
            return;
        }
        auto item = roots.root(function.value());
        make_str(c, constant.name, key);
        if (key.tag() == ResultTag::Raised) {
            out.raised(key.exception());
            return;
        }
        dict_store(c, dict, roots.root(key.value()), item, out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    initialize_math_integer(c, module, out);
    if (out.tag() != ResultTag::Raised)
        initialize_math_reductions(c, module, out);
}
} // namespace cxxpython::core::detail
