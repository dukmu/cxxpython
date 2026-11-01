// Integer algorithms and argument ordering follow CPython 3.12 Modules/mathmodule.c.
#include "internal/math_integer.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/moduleobject.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <vector>

namespace cxxpython::core::detail {
namespace {
bool copy_index(Attachment &c, Handle value, Integer &number, ReturnSlot &out)
{
    ReturnSlot indexed(c);
    index(c, value, indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return false;
    }
    number = integer_value(c.payload<IntObject>(indexed.value()));
    return true;
}

Integer gcd(Integer a, Integer b)
{
    if (a < 0)
        a = -a;
    if (b < 0)
        b = -b;
    while (b != 0) {
        Integer remainder = a % b;
        a = std::move(b);
        b = std::move(remainder);
    }
    return a;
}

// Product of count consecutive integers starting at first, with logarithmic depth.
Integer product(const Integer &first, unsigned long long count)
{
    if (count == 0)
        return 1;
    if (count == 1)
        return first;
    const auto left = count / 2;
    Integer a = product(first, left);
    Integer b = product(first + left, count - left);
    return a * b;
}

void aggregate(Attachment &c, std::span<const Value> args, ReturnSlot &out, bool lcm)
{
    RootScope roots(c);
    const auto tuple = roots.root(args[0]);
    const auto count = c.payload<TupleObject>(tuple).items.size();
    std::vector<Integer> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto item = roots.root(c.payload<TupleObject>(tuple).items[i]);
        Integer number;
        if (!copy_index(c, item, number, out))
            return;
        values.push_back(std::move(number));
    }
    Integer result = lcm ? 1 : 0;
    {
        NativeWait wait(c);
        for (const auto &value : values) {
            if (lcm) {
                if (result == 0 || value == 0)
                    result = 0;
                else {
                    result = (result / gcd(result, value)) * value;
                    if (result < 0)
                        result = -result;
                }
            } else
                result = gcd(result, value);
        }
    }
    publish_integer(c, std::move(result), out);
}

void math_gcd(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    aggregate(c, args, out, false);
}
void math_lcm(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    aggregate(c, args, out, true);
}

void factorial(Attachment &c, Handle arg, ReturnSlot &out)
{
    Integer n;
    if (!copy_index(c, arg, n, out))
        return;
    if (n < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"factorial() not defined for negative values",
                    out);
        return;
    }
    if (n > LONG_MAX) {
#if LONG_MAX == 9223372036854775807L
        raise_error(c, CoreObject::OverflowErrorType,
                    U"factorial() argument should not exceed 9223372036854775807", out);
#else
        raise_error(c, CoreObject::OverflowErrorType,
                    U"factorial() argument should not exceed 2147483647", out);
#endif
        return;
    }
    const auto count = n.convert_to<unsigned long long>();
    Integer result;
    {
        NativeWait wait(c);
        result = product(Integer(1), count);
    }
    publish_integer(c, std::move(result), out);
}
void math_factorial(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    factorial(c, roots.root(args[0]), out);
}

void choose(Attachment &c, std::span<const Value> args, ReturnSlot &out, bool comb)
{
    RootScope roots(c);
    const auto first = roots.root(args[0]);
    const auto second = roots.root(args[1]);
    if (!comb && second.get(c) == c.builtin(CoreObject::None)) {
        factorial(c, first, out);
        return;
    }
    Integer n, k;
    if (!copy_index(c, first, n, out) || !copy_index(c, second, k, out))
        return;
    if (n < 0 || k < 0) {
        raise_error(
            c, CoreObject::ValueErrorType,
            n < 0 ? U"n must be a non-negative integer" : U"k must be a non-negative integer", out);
        return;
    }
    if (k > n) {
        publish_integer(c, 0, out);
        return;
    }
    if (comb && n - k < k)
        k = n - k;
    if (k > LLONG_MAX) {
        raise_error(c, CoreObject::OverflowErrorType,
                    comb ? U"min(n - k, k) must not exceed 9223372036854775807"
                         : U"k must not exceed 9223372036854775807",
                    out);
        return;
    }
    const auto count = k.convert_to<unsigned long long>();
    Integer result;
    {
        NativeWait wait(c);
        result = product(n - k + 1, count);
        if (comb)
            result /= product(Integer(1), count);
    }
    publish_integer(c, std::move(result), out);
}
void math_comb(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    choose(c, args, out, true);
}
void math_perm(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    choose(c, args, out, false);
}
} // namespace

void initialize_math_integer(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dict = roots.root(module_dict(c, module.get(c)));
    SignatureLayout variadic, unary, binary, optional;
    variadic.varargs = U"integers";
    unary.parameters = {{U"n", ParameterKind::PositionalOnly}};
    binary.parameters = {{U"n", ParameterKind::PositionalOnly},
                         {U"k", ParameterKind::PositionalOnly}};
    optional.parameters = {{U"n", ParameterKind::PositionalOnly},
                           {U"k", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)}};
    const auto install = [&](const char *name, const char32_t *key, SignatureLayout signature,
                             auto function) {
        ReturnSlot value(c), text(c);
        make_native_function(c, name, std::move(signature), function, {}, value,
                             NativeFunctionBinding::Function);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return false;
        }
        const auto rooted = roots.root(value.value());
        make_str(c, key, text);
        if (text.tag() == ResultTag::Raised) {
            out.raised(text.exception());
            return false;
        }
        dict_store(c, dict, roots.root(text.value()), rooted, out);
        return out.tag() != ResultTag::Raised;
    };
    if (!install("gcd", U"gcd", variadic, math_gcd) ||
        !install("lcm", U"lcm", variadic, math_lcm) ||
        !install("factorial", U"factorial", unary, math_factorial) ||
        !install("comb", U"comb", binary, math_comb) ||
        !install("perm", U"perm", optional, math_perm))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
