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
void join(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto separator = roots.root(args[0]);
    if (!is_string(context, separator.get(context))) {
        raise_error(context, CoreObject::TypeErrorType, U"str.join requires a str", out);
        return;
    }
    const auto source = roots.root(args[1]);
    ReturnSlot sequence(context);
    const Value input[] = {source.get(context)};
    call(context, roots.root(context.builtin(CoreObject::TupleType)), {input}, sequence);
    if (sequence.tag() == ResultTag::Raised) {
        out.raised(sequence.exception());
        return;
    }
    const auto &items = context.payload<TupleObject>(sequence.value()).items;
    std::u32string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!is_string(context, items[i])) {
            auto name = TypeView(context, context.type_of(items[i])).name();
            const auto kind = std::holds_alternative<Value>(name)
                                  ? context.payload<StrObject>(std::get<Value>(name)).value
                                  : std::get<std::u32string>(name);
            const auto number = std::to_string(i);
            raise_error(context, CoreObject::TypeErrorType,
                        U"sequence item " + std::u32string(number.begin(), number.end()) +
                            U": expected str instance, " + kind + U" found",
                        out);
            return;
        }
        if (i)
            text += context.payload<StrObject>(separator).value;
        text += context.payload<StrObject>(items[i]).value;
    }
    if (items.size() == 1 && context.type_of(items[0]) == context.builtin(CoreObject::StrType))
        out.ok(items[0]);
    else
        make_str(context, std::move(text), out);
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

template <bool Reverse, bool Raising>
void find(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto self = roots.root(args[0]), part = roots.root(args[1]);
    if (!is_string(context, self.get(context)) || !is_string(context, part.get(context))) {
        raise_error(context, CoreObject::TypeErrorType, U"string search requires str arguments",
                    out);
        return;
    }
    std::intptr_t start = 0, end = std::numeric_limits<std::intptr_t>::max();
    if (!bound(context, roots.root(args[2]), start, out) ||
        !bound(context, roots.root(args[3]), end, out))
        return;
    const auto &text = context.payload<StrObject>(self).value;
    const auto length = static_cast<std::intptr_t>(text.size());
    start = start < 0 ? std::max<std::intptr_t>(0, start + length) : start;
    end = end < 0 ? std::max<std::intptr_t>(0, end + length) : std::min(end, length);
    std::int64_t position = -1;
    if (start <= length && start <= end) {
        const std::u32string_view window(text.data() + start, end - start);
        const auto &needle = context.payload<StrObject>(part).value;
        const auto found = Reverse ? window.rfind(needle) : window.find(needle);
        if (found != window.npos)
            position = static_cast<std::int64_t>(found) + start;
    }
    if constexpr (Raising)
        if (position < 0) {
            raise_error(context, CoreObject::ValueErrorType, U"substring not found", out);
            return;
        }
    make_int(context, position, out);
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
bool string_space(char32_t value)
{
    return detail::numeric_whitespace(value) || (value >= 0x1c && value <= 0x1f);
}
template <bool Reverse>
void split(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto self = roots.root(args[0]), separator = roots.root(args[1]);
    if (!is_string(context, self.get(context)) ||
        (separator.get(context) != context.builtin(CoreObject::None) &&
         !is_string(context, separator.get(context)))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"str.split requires a string separator or None", out);
        return;
    }
    ReturnSlot limit(context), part(context);
    index(context, roots.root(args[2]), limit);
    if (limit.tag() == ResultTag::Raised) {
        out.raised(limit.exception());
        return;
    }
    std::int64_t count;
    if (!int_fits_signed(context, limit.value(), count)) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return;
    }
    if (count < 0)
        count = std::numeric_limits<std::int64_t>::max();
    const auto &text = context.payload<StrObject>(self).value;
    const bool whitespace = separator.get(context) == context.builtin(CoreObject::None);
    const std::u32string_view sep =
        whitespace ? std::u32string_view{} : context.payload<StrObject>(separator).value;
    if (!whitespace && sep.empty()) {
        raise_error(context, CoreObject::ValueErrorType, U"empty separator", out);
        return;
    }
    std::vector<Value> items;
    auto append = [&](std::size_t first, std::size_t last) {
        make_str(context, text.substr(first, last - first), part);
        items.push_back(roots.root(part.value()).get(context));
    };
    std::size_t first = 0, last = text.size();
    for (;;) {
        if (whitespace) {
            if constexpr (Reverse) {
                while (last > first && string_space(text[last - 1]))
                    --last;
            } else {
                while (first < last && string_space(text[first]))
                    ++first;
            }
            if (first == last)
                break;
        }
        if (!count) {
            append(first, last);
            break;
        }
        if constexpr (Reverse) {
            auto position = last;
            if (whitespace) {
                while (position > first && !string_space(text[position - 1]))
                    --position;
                append(position, last);
                last = position;
            } else {
                position = std::u32string_view(text).substr(first, last - first).rfind(sep);
                if (position == std::u32string_view::npos) {
                    append(first, last);
                    break;
                }
                position += first;
                append(position + sep.size(), last);
                last = position;
            }
        } else {
            auto position = first;
            if (whitespace) {
                while (position < last && !string_space(text[position]))
                    ++position;
                append(first, position);
                first = position;
            } else {
                position = text.find(sep, first);
                if (position == std::u32string_view::npos) {
                    append(first, last);
                    break;
                }
                append(first, position);
                first = position + sep.size();
            }
        }
        --count;
    }
    if constexpr (Reverse)
        std::reverse(items.begin(), items.end());
    make_list(context, items, out);
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
    signature.parameters.resize(2);
    signature.parameters[1].name = U"iterable";
    install_native_attribute(context, core, at(CoreObject::StrType), U"join", signature, join);
    signature.parameters[1].name = U"sub";
    signature.parameters.push_back(
        {U"start", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(context)});
    signature.parameters.push_back(
        {U"end", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)});
    install_native_attribute(context, core, at(CoreObject::StrType), U"find", signature,
                             find<false, false>);
    install_native_attribute(context, core, at(CoreObject::StrType), U"rfind", signature,
                             find<true, false>);
    install_native_attribute(context, core, at(CoreObject::StrType), U"index", signature,
                             find<false, true>);
    install_native_attribute(context, core, at(CoreObject::StrType), U"rindex", signature,
                             find<true, true>);
    RootScope roots(context);
    ReturnSlot negative(context);
    context.allocate_instance<IntObject>(at(CoreObject::IntType), negative, -1);
    SignatureLayout splitted;
    splitted.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"sep", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(context)},
        {U"maxsplit", ParameterKind::PositionalOrKeyword, negative.value()}};
    install_native_attribute(context, core, at(CoreObject::StrType), U"split", splitted,
                             split<false>);
    install_native_attribute(context, core, at(CoreObject::StrType), U"rsplit", splitted,
                             split<true>);
}
} // namespace cxxpython::core
