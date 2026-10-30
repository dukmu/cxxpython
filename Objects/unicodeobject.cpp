// Tail matching follows CPython 3.12.14 Objects/unicodeobject.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/unicodeobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <limits>

namespace cxxpython::core {
namespace {
bool is_string(Attachment &context, Value value)
{
    return context.has_layout<StrObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType));
}

bool bound(Attachment &context, Handle value, std::intptr_t &number, ReturnSlot &out)
{
    if (value.get(context) == context.builtin(CoreObject::None))
        return true;
    ReturnSlot converted(context);
    index(context, value, converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    const auto n = detail::integer_value(context.payload<IntObject>(converted.value()));
    const detail::Integer low = std::numeric_limits<std::intptr_t>::min();
    const detail::Integer high = std::numeric_limits<std::intptr_t>::max();
    number = n < low    ? std::numeric_limits<std::intptr_t>::min()
             : n > high ? std::numeric_limits<std::intptr_t>::max()
                        : n.convert_to<std::intptr_t>();
    return true;
}

template <bool Suffix>
void tailmatch(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_string(context, args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"str descriptor requires a str", out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]), candidate = roots.root(args[1]);
    const auto start_arg = roots.root(args[2]), end_arg = roots.root(args[3]);
    std::intptr_t start = 0, end = std::numeric_limits<std::intptr_t>::max();
    if (!bound(context, start_arg, start, out) || !bound(context, end_arg, end, out))
        return;
    const auto &text = context.payload<StrObject>(self).value;
    const auto length = static_cast<std::intptr_t>(text.size());
    if (end > length)
        end = length;
    else if (end < 0)
        end = std::max<std::intptr_t>(0, end + length);
    if (start < 0)
        start = std::max<std::intptr_t>(0, start + length);
    auto match = [&](Value value) {
        const auto &part = context.payload<StrObject>(value).value;
        if (part.size() > static_cast<std::size_t>(end) ||
            end - static_cast<std::intptr_t>(part.size()) < start)
            return false;
        const auto offset = Suffix ? end - static_cast<std::intptr_t>(part.size()) : start;
        return text.compare(static_cast<std::size_t>(offset), part.size(), part) == 0;
    };
    const auto value = candidate.get(context);
    if (context.has_layout<TupleObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::TupleType))) {
        for (auto item : context.payload<TupleObject>(value).items) {
            if (!is_string(context, item)) {
                raise_error(context, CoreObject::TypeErrorType,
                            Suffix ? U"tuple for endswith must only contain str"
                                   : U"tuple for startswith must only contain str",
                            out);
                return;
            }
            if (match(item)) {
                out.ok(context.builtin(CoreObject::True));
                return;
            }
        }
        out.ok(context.builtin(CoreObject::False));
        return;
    }
    if (!is_string(context, value)) {
        raise_error(context, CoreObject::TypeErrorType,
                    Suffix ? U"endswith first arg must be str or a tuple of str"
                           : U"startswith first arg must be str or a tuple of str",
                    out);
        return;
    }
    out.ok(context.builtin(match(value) ? CoreObject::True : CoreObject::False));
}
} // namespace

void detail::install_unicode_methods(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout signature;
    signature.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"prefix", ParameterKind::PositionalOnly},
        {U"start", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(context)},
        {U"end", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)}};
    install_native_attribute(context, core, at(CoreObject::StrType), U"startswith", signature,
                             tailmatch<false>, {});
    signature.parameters[1].name = U"suffix";
    install_native_attribute(context, core, at(CoreObject::StrType), U"endswith", signature,
                             tailmatch<true>, {});
}
} // namespace cxxpython::core
