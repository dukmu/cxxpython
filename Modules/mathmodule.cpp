// Native math foundation. Only isqrt is implemented; the rest of CPython's
// math API, including gcd, lcm, factorial, rounding, predicates, transcendental
// functions and constants, remains an explicit compatibility gate.
#include "internal/mathmodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/moduleobject.hpp"
#include "internal/long.hpp"

namespace cxxpython::core::detail {
namespace {
void math_isqrt(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
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
    if (out.tag() != ResultTag::Raised)
        out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
