// Reduction algorithms adapted from CPython 3.12.14 Modules/mathmodule.c,
// under the Python Software Foundation license. Runtime ownership/dispatch
// uses this interpreter's precise roots and caller-owned result slots.
#include "internal/math_reductions.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/errors.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &value)
{
    return value.tag() == ResultTag::Raised;
}

bool real_number(Attachment &c, Handle value, double &number, ReturnSlot &out)
{
    if (is_float(c, value.get(c))) {
        number = c.payload<FloatObject>(value).value;
        return true;
    }
    RootScope roots(c);
    const auto type = roots.root(c.type_of(value));
    if (!type_attribute(c, type.get(c), U"__float__") &&
        !type_attribute(c, type.get(c), U"__index__")) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"must be real number, not " + diagnostic_type_name(c, type), out);
        return false;
    }
    ReturnSlot converted(c);
    const std::array args{value.get(c)};
    call(c, roots.root(c.builtin(CoreObject::FloatType)), {args, {}}, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    number = c.payload<FloatObject>(converted.value()).value;
    return true;
}

bool native_long(Attachment &c, Value value, long &number)
{
    std::int64_t integer;
    if (!int_fits_signed(c, value, integer) || integer < LONG_MIN || integer > LONG_MAX)
        return false;
    number = static_cast<long>(integer);
    return true;
}

struct DoubleLength {
    double hi, lo;
};
struct TripleLength {
    double hi = 0.0, lo = 0.0, tiny = 0.0;
};

DoubleLength fast_sum(double a, double b)
{
    const double x = a + b;
    return {x, (a - x) + b};
}
DoubleLength extended_sum(double a, double b)
{
    const double x = a + b;
    const double z = x - a;
    return {x, (a - (x - z)) + (b - z)};
}
DoubleLength extended_product(double x, double y)
{
    const double z = x * y;
    return {z, std::fma(x, y, -z)};
}
TripleLength extended_dot(double x, double y, TripleLength total)
{
    const auto pr = extended_product(x, y);
    const auto sm = extended_sum(total.hi, pr.hi);
    const auto r1 = extended_sum(total.lo, pr.lo);
    const auto r2 = extended_sum(r1.hi, sm.lo);
    return {sm.hi, r2.hi, total.tiny + r1.lo + r2.lo};
}
double rounded_dot(TripleLength total)
{
    const auto last = extended_sum(total.lo, total.hi);
    return total.tiny + last.lo + last.hi;
}

void math_fsum(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot iterator_result(c);
    get_iter(c, roots.root(args[0]), iterator_result);
    if (failed(iterator_result)) {
        out.raised(iterator_result.exception());
        return;
    }
    const auto iterator = roots.root(iterator_result.value());
    std::vector<double> partials;
    partials.reserve(32);
    double special_sum = 0.0, inf_sum = 0.0;
    for (;;) {
        RootScope iteration(c);
        ReturnSlot item(c);
        const auto status = iter_next(c, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        double x;
        if (!real_number(c, iteration.root(item.value()), x, out))
            return;
        const double saved = x;
        std::size_t count = 0;
        for (double y : partials) {
            if (std::fabs(x) < std::fabs(y))
                std::swap(x, y);
            const double hi = x + y;
            const double yr = hi - x;
            const double lo = y - yr;
            if (lo != 0.0)
                partials[count++] = lo;
            x = hi;
        }
        partials.resize(count);
        if (x != 0.0) {
            if (!std::isfinite(x)) {
                if (std::isfinite(saved)) {
                    raise_error(c, CoreObject::OverflowErrorType, U"intermediate overflow in fsum",
                                out);
                    return;
                }
                if (std::isinf(saved))
                    inf_sum += saved;
                special_sum += saved;
                partials.clear();
            } else
                partials.push_back(x);
        }
    }
    if (special_sum != 0.0) {
        if (std::isnan(inf_sum))
            raise_error(c, CoreObject::ValueErrorType, U"-inf + inf in fsum", out);
        else
            make_float(c, special_sum, out);
        return;
    }
    double hi = 0.0, lo = 0.0;
    auto count = partials.size();
    if (count) {
        hi = partials[--count];
        while (count) {
            const double x = hi;
            const double y = partials[--count];
            hi = x + y;
            const double yr = hi - x;
            lo = y - yr;
            if (lo != 0.0)
                break;
        }
        if (count &&
            ((lo < 0.0 && partials[count - 1] < 0.0) || (lo > 0.0 && partials[count - 1] > 0.0))) {
            const double y = lo * 2.0;
            const double x = hi + y;
            const double yr = x - hi;
            if (y == yr)
                hi = x;
        }
    }
    make_float(c, hi, out);
}

double vector_norm(std::span<double> values, double maximum, bool found_nan)
{
    if (std::isinf(maximum))
        return maximum;
    if (found_nan)
        return std::numeric_limits<double>::quiet_NaN();
    if (maximum == 0.0 || values.size() <= 1)
        return maximum;
    int exponent;
    std::frexp(maximum, &exponent);
    constexpr double minimum = std::numeric_limits<double>::min();
    if (exponent < -1023) {
        for (auto &value : values)
            value /= minimum;
        return minimum * vector_norm(values, maximum / minimum, found_nan);
    }
    const double scale = std::ldexp(1.0, -exponent);
    double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    for (double value : values) {
        const double x = value * scale;
        const auto pr = extended_product(x, x);
        const auto sm = fast_sum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    const auto pr = extended_product(-h, h);
    const auto sm = fast_sum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    const double x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}

void publish_norm(Attachment &c, std::vector<double> &values, double maximum, bool found_nan,
                  ReturnSlot &out)
{
    double result;
    {
        // All coordinates are owned native doubles; no protocols run while parked.
        NativeWait wait(c);
        result = vector_norm(values, maximum, found_nan);
    }
    make_float(c, result, out);
}

void math_hypot(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto coordinates = roots.root(args[0]);
    const auto count = c.payload<TupleObject>(coordinates).items.size();
    std::vector<double> values;
    values.reserve(count);
    double maximum = 0.0;
    bool found_nan = false;
    for (std::size_t i = 0; i != count; ++i) {
        RootScope iteration(c);
        double x;
        if (!real_number(c, iteration.root(c.payload<TupleObject>(coordinates).items[i]), x, out))
            return;
        x = std::fabs(x);
        values.push_back(x);
        maximum = std::max(maximum, x);
        found_nan |= std::isnan(x);
    }
    publish_norm(c, values, maximum, found_nan, out);
}

void point_tuple(Attachment &c, Handle value, ReturnSlot &out)
{
    if (c.has_layout<TupleObject>(value.get(c)) &&
        is_subtype(c, c.type_of(value), c.builtin(CoreObject::TupleType))) {
        out.ok(value.get(c));
        return;
    }
    RootScope roots(c);
    const std::array args{value.get(c)};
    call(c, roots.root(c.builtin(CoreObject::TupleType)), {args, {}}, out);
}

void math_dist(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto p = roots.root(args[0]);
    const auto q = roots.root(args[1]);
    ReturnSlot p_tuple(c), q_tuple(c);
    point_tuple(c, p, p_tuple);
    if (failed(p_tuple)) {
        out.raised(p_tuple.exception());
        return;
    }
    point_tuple(c, q, q_tuple);
    if (failed(q_tuple)) {
        out.raised(q_tuple.exception());
        return;
    }
    const auto p_values = roots.root(p_tuple.value());
    const auto q_values = roots.root(q_tuple.value());
    const auto count = c.payload<TupleObject>(p_values).items.size();
    if (count != c.payload<TupleObject>(q_values).items.size()) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"both points must have the same number of dimensions", out);
        return;
    }
    std::vector<double> differences;
    differences.reserve(count);
    double maximum = 0.0;
    bool found_nan = false;
    for (std::size_t i = 0; i != count; ++i) {
        RootScope iteration(c);
        double px, qx;
        if (!real_number(c, iteration.root(c.payload<TupleObject>(p_values).items[i]), px, out) ||
            !real_number(c, iteration.root(c.payload<TupleObject>(q_values).items[i]), qx, out))
            return;
        const double x = std::fabs(px - qx);
        differences.push_back(x);
        maximum = std::max(maximum, x);
        found_nan |= std::isnan(x);
    }
    publish_norm(c, differences, maximum, found_nan, out);
}

void math_prod(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot iterator_result(c), total(c);
    const auto start = roots.root(args[1]);
    get_iter(c, roots.root(args[0]), iterator_result);
    if (failed(iterator_result)) {
        out.raised(iterator_result.exception());
        return;
    }
    const auto iterator = roots.root(iterator_result.value());
    total.ok(start.get(c));
    enum class Mode { Integer, Float, General };
    Mode mode = Mode::General;
    long integer_total = 0;
    double float_total = 0.0;
    if (c.type_of(start) == c.builtin(CoreObject::IntType) &&
        native_long(c, start.get(c), integer_total))
        mode = Mode::Integer;
    else if (c.type_of(start) == c.builtin(CoreObject::FloatType)) {
        mode = Mode::Float;
        float_total = c.payload<FloatObject>(start).value;
    }
    for (;;) {
        RootScope iteration(c);
        ReturnSlot item(c), next(c);
        const auto status = iter_next(c, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done) {
            if (mode == Mode::Integer)
                make_int(c, integer_total, out);
            else if (mode == Mode::Float)
                make_float(c, float_total, out);
            else
                out.ok(total.value());
            return;
        }
        const auto value = iteration.root(item.value());
        const auto type = c.type_of(value);
        const auto previous_mode = mode;
        if (mode == Mode::Integer) {
            long b, product;
            if (type == c.builtin(CoreObject::IntType) && native_long(c, value.get(c), b) &&
                !__builtin_mul_overflow(integer_total, b, &product)) {
                integer_total = product;
                continue;
            }
            make_int(c, integer_total, total);
            if (failed(total)) {
                out.raised(total.exception());
                return;
            }
        } else if (mode == Mode::Float) {
            if (type == c.builtin(CoreObject::FloatType)) {
                float_total *= c.payload<FloatObject>(value).value;
                continue;
            }
            long b;
            if (type == c.builtin(CoreObject::IntType) && native_long(c, value.get(c), b)) {
                float_total *= static_cast<double>(b);
                continue;
            }
            make_float(c, float_total, total);
            if (failed(total)) {
                out.raised(total.exception());
                return;
            }
        }
        binary(c, iteration.root(total.value()), value, BinaryOp::Multiply, next);
        if (failed(next)) {
            out.raised(next.exception());
            return;
        }
        total.ok(next.value());
        // CPython enters the float path only once, immediately after the integer path.
        if (previous_mode == Mode::Integer &&
            c.type_of(total.value()) == c.builtin(CoreObject::FloatType)) {
            mode = Mode::Float;
            float_total = c.payload<FloatObject>(total.value()).value;
        } else
            mode = Mode::General;
    }
}

bool add_term(Attachment &c, ReturnSlot &total, Handle term, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot next(c);
    binary(c, roots.root(total.value()), term, BinaryOp::Add, next);
    if (failed(next)) {
        out.raised(next.exception());
        return false;
    }
    total.ok(next.value());
    return true;
}

void math_sumprod(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto p = roots.root(args[0]);
    const auto q = roots.root(args[1]);
    ReturnSlot p_result(c), q_result(c), total(c);
    get_iter(c, p, p_result);
    if (failed(p_result)) {
        out.raised(p_result.exception());
        return;
    }
    const auto p_iterator = roots.root(p_result.value());
    get_iter(c, q, q_result);
    if (failed(q_result)) {
        out.raised(q_result.exception());
        return;
    }
    const auto q_iterator = roots.root(q_result.value());
    make_int(c, 0, total);
    if (failed(total)) {
        out.raised(total.exception());
        return;
    }
    bool int_enabled = true, int_in_use = false;
    bool float_enabled = true, float_in_use = false;
    long int_total = 0;
    TripleLength float_total;
    for (;;) {
        RootScope iteration(c);
        ReturnSlot p_item(c), q_item(c), term(c);
        const auto ps = iter_next(c, p_iterator, p_item);
        if (ps == NextStatus::Raised) {
            out.raised(p_item.exception());
            return;
        }
        const auto qs = iter_next(c, q_iterator, q_item);
        if (qs == NextStatus::Raised) {
            out.raised(q_item.exception());
            return;
        }
        if ((ps == NextStatus::Done) != (qs == NextStatus::Done)) {
            raise_error(c, CoreObject::ValueErrorType, U"Inputs are not the same length", out);
            return;
        }
        const bool finished = ps == NextStatus::Done;
        const auto pv = iteration.root(finished ? c.builtin(CoreObject::None) : p_item.value());
        const auto qv = iteration.root(finished ? c.builtin(CoreObject::None) : q_item.value());
        const auto pt = c.type_of(pv);
        const auto qt = c.type_of(qv);
        if (int_enabled) {
            long a, b, product, sum;
            if (!finished && pt == c.builtin(CoreObject::IntType) &&
                qt == c.builtin(CoreObject::IntType) && native_long(c, pv.get(c), a) &&
                native_long(c, qv.get(c), b) && !__builtin_mul_overflow(a, b, &product) &&
                !__builtin_add_overflow(int_total, product, &sum)) {
                int_total = sum;
                int_in_use = true;
                continue;
            }
            int_enabled = false;
            if (int_in_use) {
                make_int(c, int_total, term);
                if (failed(term)) {
                    out.raised(term.exception());
                    return;
                }
                if (!add_term(c, total, iteration.root(term.value()), out))
                    return;
                int_in_use = false;
            }
        }
        if (float_enabled) {
            double a = 0.0, b = 0.0;
            bool eligible = false;
            const bool p_float = pt == c.builtin(CoreObject::FloatType);
            const bool q_float = qt == c.builtin(CoreObject::FloatType);
            if (!finished && p_float && q_float) {
                a = c.payload<FloatObject>(pv).value;
                b = c.payload<FloatObject>(qv).value;
                eligible = true;
            } else if (!finished && p_float &&
                       (qt == c.builtin(CoreObject::IntType) ||
                        qt == c.builtin(CoreObject::BoolType))) {
                a = c.payload<FloatObject>(pv).value;
                ReturnSlot conversion(c);
                eligible = real_number(c, qv, b, conversion);
            } else if (!finished && q_float &&
                       (pt == c.builtin(CoreObject::IntType) ||
                        pt == c.builtin(CoreObject::BoolType))) {
                b = c.payload<FloatObject>(qv).value;
                ReturnSlot conversion(c);
                eligible = real_number(c, pv, a, conversion);
            }
            if (eligible) {
                const auto next = extended_dot(a, b, float_total);
                if (std::isfinite(next.hi)) {
                    float_total = next;
                    float_in_use = true;
                    continue;
                }
            }
            float_enabled = false;
            if (float_in_use) {
                make_float(c, rounded_dot(float_total), term);
                if (failed(term)) {
                    out.raised(term.exception());
                    return;
                }
                if (!add_term(c, total, iteration.root(term.value()), out))
                    return;
                float_in_use = false;
            }
        }
        if (finished) {
            out.ok(total.value());
            return;
        }
        binary(c, pv, qv, BinaryOp::Multiply, term);
        if (failed(term)) {
            out.raised(term.exception());
            return;
        }
        if (!add_term(c, total, iteration.root(term.value()), out))
            return;
    }
}
} // namespace

void initialize_math_reductions(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dict = roots.root(module_dict(c, module.get(c)));
    SignatureLayout unary, binary, variadic, product;
    unary.parameters = {{U"seq", ParameterKind::PositionalOnly}};
    binary.parameters = {{U"p", ParameterKind::PositionalOnly},
                         {U"q", ParameterKind::PositionalOnly}};
    variadic.varargs = U"coordinates";
    ReturnSlot one(c);
    make_int(c, 1, one);
    if (failed(one)) {
        out.raised(one.exception());
        return;
    }
    product.parameters = {{U"iterable", ParameterKind::PositionalOnly},
                          {U"start", ParameterKind::KeywordOnly, one.value()}};
    const auto install = [&](const char *name, const char32_t *key, SignatureLayout signature,
                             NativeEntry entry) {
        ReturnSlot value(c), text(c);
        make_native_function(c, name, std::move(signature), entry, {}, value,
                             NativeFunctionBinding::Function);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        const auto rooted = roots.root(value.value());
        make_str(c, key, text);
        if (failed(text)) {
            out.raised(text.exception());
            return false;
        }
        dict_store(c, dict, roots.root(text.value()), rooted, out);
        return !failed(out);
    };
    if (!install("fsum", U"fsum", unary, math_fsum) ||
        !install("prod", U"prod", product, math_prod) ||
        !install("sumprod", U"sumprod", binary, math_sumprod) ||
        !install("hypot", U"hypot", variadic, math_hypot) ||
        !install("dist", U"dist", binary, math_dist))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
