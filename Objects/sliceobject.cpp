#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <bit>

namespace cxxpython::core {
namespace {
using detail::Integer;
bool slice_receiver(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.type_of(value) == context.builtin(CoreObject::SliceType) &&
        context.has_layout<SliceObject>(value))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"slice descriptor requires a slice", out);
    return false;
}
void slice_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto &values = context.payload<TupleObject>(args[1]).items;
    if (args[0] != context.builtin(CoreObject::SliceType) || values.empty() || values.size() > 3) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"slice expects one to three positional arguments", out);
        return;
    }
    RootScope roots(context);
    const auto none = context.builtin(CoreObject::None);
    const auto start = roots.root(values.size() == 1 ? none : values[0]);
    const auto stop = roots.root(values[values.size() == 1 ? 0 : 1]);
    const auto step = roots.root(values.size() == 3 ? values[2] : none);
    make_slice(context, start, stop, step, out);
}
template <const Value SliceObject::*Field>
void slice_field(Attachment &context, Value self, ReturnSlot &out)
{
    if (slice_receiver(context, self, out))
        out.ok(context.payload<SliceObject>(self).*Field);
}
void slice_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!slice_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    const auto &slice = context.payload<SliceObject>(self);
    std::u32string text = U"slice(";
    ReturnSlot part(context);
    std::size_t position = 0;
    for (auto value : {slice.start, slice.stop, slice.step}) {
        repr(context, roots.root(value), part);
        if (part.tag() == ResultTag::Raised) {
            out.raised(part.exception());
            return;
        }
        if (position++)
            text += U", ";
        text += context.payload<StrObject>(part.value()).value;
    }
    make_str(context, text + U")", out);
}
void slice_reduce(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!slice_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    const auto &value = context.payload<SliceObject>(self);
    const std::array fields{value.start, value.stop, value.step};
    ReturnSlot arguments(context);
    cxxpython::core::make_tuple(context, fields, arguments);
    const std::array result{context.type_of(self), arguments.value()};
    cxxpython::core::make_tuple(context, result, out);
}
template <CompareOp Op>
void slice_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!slice_receiver(context, args[0], out))
        return;
    if (context.type_of(args[1]) != context.builtin(CoreObject::SliceType)) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    if (args[0] == args[1]) {
        constexpr bool equal = Op == CompareOp::Eq || Op == CompareOp::Le || Op == CompareOp::Ge;
        out.ok(context.builtin(equal ? CoreObject::True : CoreObject::False));
        return;
    }
    RootScope roots(context);
    const auto left = roots.root(args[0]), right = roots.root(args[1]);
    const auto &a = context.payload<SliceObject>(left);
    const auto &b = context.payload<SliceObject>(right);
    const std::array av{a.start, a.stop, a.step}, bv{b.start, b.stop, b.step};
    ReturnSlot first(context), second(context);
    cxxpython::core::make_tuple(context, av, first);
    cxxpython::core::make_tuple(context, bv, second);
    rich_compare(context, roots.root(first.value()), roots.root(second.value()), Op, out);
}
void slice_indices_method(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    detail::slice_indices(context, roots.root(args[0]), roots.root(args[1]), out);
}
void slice_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!slice_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    const auto &value = context.payload<SliceObject>(self);
    constexpr std::uintptr_t prime1 =
        sizeof(std::uintptr_t) == 8 ? 11400714785074694791ULL : 2654435761UL;
    constexpr std::uintptr_t prime2 =
        sizeof(std::uintptr_t) == 8 ? 14029467366897019727ULL : 2246822519UL;
    std::uintptr_t accumulator = sizeof(std::uintptr_t) == 8 ? 2870177450012600261ULL : 374761393UL;
    ReturnSlot part(context);
    for (auto field : {value.start, value.stop, value.step}) {
        hash(context, roots.root(field), part);
        if (part.tag() == ResultTag::Raised) {
            out.raised(part.exception());
            return;
        }
        std::int64_t lane = 0;
        (void)int_fits_signed(context, part.value(), lane);
        accumulator += static_cast<std::uintptr_t>(lane) * prime2;
        accumulator = std::rotl(accumulator, sizeof(std::uintptr_t) == 8 ? 31 : 13);
        accumulator *= prime1;
    }
    const auto signed_value = std::bit_cast<std::intptr_t>(accumulator);
    make_int(context, signed_value == -1 ? 1546275796 : signed_value, out);
}
bool clipped_index(Attachment &context, Value value, std::int64_t &result, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(value), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(context, converted.value(), result))
        result = int_sign(context, converted.value()) < 0
                     ? std::numeric_limits<std::intptr_t>::min()
                     : std::numeric_limits<std::intptr_t>::max();
    result = std::clamp<std::int64_t>(result, std::numeric_limits<std::intptr_t>::min(),
                                      std::numeric_limits<std::intptr_t>::max());
    return true;
}
bool long_index(Attachment &context, Value value, Integer &number, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(value), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    number = detail::integer_value(context.payload<IntObject>(converted.value()));
    return true;
}
} // namespace
void make_slice(Attachment &context, Handle start, Handle stop, Handle step, ReturnSlot &out)
{
    RootScope roots(context);
    const auto a = roots.root(start.get(context)), b = roots.root(stop.get(context)),
               c = roots.root(step.get(context));
    context.allocate_instance<SliceObject>(roots.root(context.builtin(CoreObject::SliceType)), out,
                                           a.get(context), b.get(context), c.get(context));
}
namespace detail {
SliceIndices SliceBounds::adjust(std::size_t size) const noexcept
{
    const auto length = static_cast<std::int64_t>(size);
    auto a = start, b = stop;
    const auto low = step < 0 ? -1 : 0;
    const auto high = step < 0 ? length - 1 : length;
    if (a < 0)
        a = std::max<std::int64_t>(low, a + length);
    else
        a = std::min(a, high);
    if (b < 0)
        b = std::max<std::int64_t>(low, b + length);
    else
        b = std::min(b, high);
    const auto count =
        step < 0 ? (b < a ? (a - b - 1) / -step + 1 : 0) : (a < b ? (b - a - 1) / step + 1 : 0);
    return {a, b, step, count};
}
bool unpack_slice(Attachment &context, Handle self, SliceBounds &bounds, ReturnSlot &out)
{
    if (!slice_receiver(context, self.get(context), out))
        return false;
    const auto &slice = context.payload<SliceObject>(self);
    const auto none = context.builtin(CoreObject::None);
    bounds.step = 1;
    // CPython 3.12.14 PySlice_Unpack: callbacks run step, start, stop, before length lookup.
    if (slice.step != none && !clipped_index(context, slice.step, bounds.step, out))
        return false;
    if (!bounds.step) {
        raise_error(context, CoreObject::ValueErrorType, U"slice step cannot be zero", out);
        return false;
    }
    const auto limit = std::numeric_limits<std::intptr_t>::max();
    bounds.step = std::max<std::int64_t>(bounds.step, -limit);
    bounds.start = bounds.step < 0 ? limit : 0;
    bounds.stop = bounds.step < 0 ? std::numeric_limits<std::intptr_t>::min() : limit;
    if (slice.start != none && !clipped_index(context, slice.start, bounds.start, out))
        return false;
    return slice.stop == none || clipped_index(context, slice.stop, bounds.stop, out);
}
void slice_indices(Attachment &context, Handle self, Handle length, ReturnSlot &out)
{
    if (!slice_receiver(context, self.get(context), out))
        return;
    RootScope roots(context);
    self = roots.root(self.get(context));
    const auto &slice = context.payload<SliceObject>(self);
    Integer size;
    if (!long_index(context, length.get(context), size, out))
        return;
    if (size < 0) {
        raise_error(context, CoreObject::ValueErrorType, U"length should not be negative", out);
        return;
    }
    const auto none = context.builtin(CoreObject::None);
    Integer step = 1;
    if (slice.step != none && !long_index(context, slice.step, step, out))
        return;
    if (step == 0) {
        raise_error(context, CoreObject::ValueErrorType, U"slice step cannot be zero", out);
        return;
    }
    const Integer lower = step < 0 ? -1 : 0;
    const Integer upper = step < 0 ? Integer(size - 1) : size;
    Integer start = step < 0 ? upper : lower;
    Integer stop = step < 0 ? lower : upper;
    auto normalize = [&](Value value, Integer &number) {
        if (value == none)
            return true;
        if (!long_index(context, value, number, out))
            return false;
        if (number < 0)
            number += size;
        if (number < lower)
            number = lower;
        else if (number > upper)
            number = upper;
        return true;
    };
    if (!normalize(slice.start, start) || !normalize(slice.stop, stop))
        return;
    ReturnSlot a(context), b(context), c(context);
    publish_integer(context, std::move(start), a);
    publish_integer(context, std::move(stop), b);
    publish_integer(context, std::move(step), c);
    const std::array values{a.value(), b.value(), c.value()};
    cxxpython::core::make_tuple(context, values, out);
}
void install_slices(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    const auto owner = at(CoreObject::SliceType);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    install_native_attribute(context, core, owner, U"__new__", constructor, slice_new);
    RootScope roots(context);
    ReturnSlot temporary(context);
    const auto dictionary = *TypeView(context, owner).dictionary();
    const auto function = *name_lookup(context, dictionary, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                  function);
    const auto wrapper = roots.root(temporary.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    name_store(context, dictionary, temporary.value(), wrapper.get(context));
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, owner, U"__repr__", unary, slice_repr);
    install_native_attribute(context, core, owner, U"__hash__", unary, slice_hash);
    install_native_attribute(context, core, owner, U"__reduce__", unary, slice_reduce);
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, owner, U"indices", pair, slice_indices_method);
    const std::array<std::u32string, 6> names{U"__eq__", U"__ne__", U"__lt__",
                                              U"__le__", U"__gt__", U"__ge__"};
    const std::array entries{slice_compare<CompareOp::Eq>, slice_compare<CompareOp::Ne>,
                             slice_compare<CompareOp::Lt>, slice_compare<CompareOp::Le>,
                             slice_compare<CompareOp::Gt>, slice_compare<CompareOp::Ge>};
    for (std::size_t i = 0; i < names.size(); ++i)
        install_native_attribute(context, core, owner, names[i], pair, entries[i]);
    install_native_field(context, core, owner, U"start", slice_field<&SliceObject::start>);
    install_native_field(context, core, owner, U"stop", slice_field<&SliceObject::stop>);
    install_native_field(context, core, owner, U"step", slice_field<&SliceObject::step>);
}
} // namespace detail
} // namespace cxxpython::core
