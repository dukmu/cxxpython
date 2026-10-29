#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

namespace cxxpython::core {
namespace {
using detail::Integer;
Integer number(Attachment &context, Value value)
{
    return detail::integer_value(context.payload<IntObject>(value));
}
const RangeObject *range(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.type_of(value) != context.builtin(CoreObject::RangeType) ||
        !context.has_layout<RangeObject>(value)) {
        raise_error(context, CoreObject::TypeErrorType, U"range descriptor requires a range", out);
        return nullptr;
    }
    return &context.payload<RangeObject>(value);
}
void range_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto &values = context.payload<TupleObject>(args[1]).items;
    if (args[0] != context.builtin(CoreObject::RangeType) || values.empty() || values.size() > 3) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"range expects one to three positional arguments", out);
        return;
    }
    RootScope roots(context);
    ReturnSlot converted(context);
    std::vector<Value> normalized;
    for (auto value : values) {
        index(context, roots.root(value), converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return;
        }
        normalized.push_back(roots.root(converted.value()).get(context));
    }
    const auto start = values.size() == 1 ? context.builtin(CoreObject::IntZero) : normalized[0];
    const auto stop = normalized[values.size() == 1 ? 0 : 1];
    const auto step = values.size() == 3 ? normalized[2] : context.builtin(CoreObject::IntOne);
    const auto a = number(context, start), b = number(context, stop),
               stride = number(context, step);
    if (stride == 0) {
        raise_error(context, CoreObject::ValueErrorType, U"range() arg 3 must not be zero", out);
        return;
    }
    Integer length = 0;
    if (stride > 0 && a < b)
        length = (b - a - 1) / stride + 1;
    else if (stride < 0 && a > b)
        length = (a - b - 1) / -stride + 1;
    detail::publish_integer(context, std::move(length), converted);
    context.allocate_instance<RangeObject>(roots.root(context.builtin(CoreObject::RangeType)), out,
                                           start, stop, step, converted.value());
}
void range_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    const auto a = int_decimal(context, value->start), b = int_decimal(context, value->stop),
               step = int_decimal(context, value->step);
    const auto text = "range(" + a + ", " + b + (step == "1" ? "" : ", " + step) + ")";
    make_str(context, {text.begin(), text.end()}, out);
}
void range_bool(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    out.ok(
        context.builtin(int_sign(context, value->length) ? CoreObject::True : CoreObject::False));
}
void range_len(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    std::int64_t length;
    if (!int_fits_signed(context, value->length, length) ||
        length > std::numeric_limits<std::intptr_t>::max()) {
        raise_error(context, CoreObject::OverflowErrorType, U"range length does not fit Py_ssize_t",
                    out);
        return;
    }
    out.ok(value->length);
}
void range_item(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(args[1]), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    auto position = number(context, converted.value());
    const auto length = number(context, value->length);
    if (position < 0)
        position += length;
    if (position < 0 || position >= length) {
        raise_error(context, CoreObject::IndexErrorType, U"range object index out of range", out);
        return;
    }
    detail::publish_integer(
        context, number(context, value->start) + number(context, value->step) * position, out);
}
template <bool Negate>
void range_equal(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *a = range(context, args[0], out);
    if (!a)
        return;
    if (context.type_of(args[1]) != context.builtin(CoreObject::RangeType)) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    const auto &b = context.payload<RangeObject>(args[1]);
    const auto length = number(context, a->length);
    const bool equal =
        length == number(context, b.length) &&
        (length == 0 || (number(context, a->start) == number(context, b.start) &&
                         (length == 1 || number(context, a->step) == number(context, b.step))));
    out.ok(context.builtin(equal != Negate ? CoreObject::True : CoreObject::False));
}
void range_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    const auto length = number(context, value->length);
    const auto none = context.builtin(CoreObject::None);
    std::array values{value->length, length == 0 ? none : value->start,
                      length <= 1 ? none : value->step};
    RootScope roots(context);
    ReturnSlot tuple(context);
    cxxpython::core::make_tuple(context, values, tuple);
    hash(context, roots.root(tuple.value()), out);
}
void range_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto *value = range(context, args[0], out);
    if (!value)
        return;
    RootScope roots(context);
    context.allocate_instance<RangeIteratorObject>(
        roots.root(context.builtin(CoreObject::RangeIteratorType)), out, value->stop, value->step,
        int_sign(context, value->length) ? std::optional<Value>{value->start}
                                         : std::optional<Value>{});
}
void range_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.builtin(CoreObject::RangeIteratorType) ||
        !context.has_layout<RangeIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"range iterator required", out);
        return;
    }
    auto &iterator = context.payload<RangeIteratorObject>(args[0]);
    ReturnSlot current(context), next(context);
    for (;;) {
        context.load(args[0], iterator.current, current);
        if (current.tag() == ResultTag::Empty) {
            detail::stop_iteration(context, out);
            return;
        }
        const auto stride = number(context, iterator.step);
        const auto stop = number(context, iterator.stop);
        const Integer following = number(context, current.value()) + stride;
        std::optional<Value> desired;
        if (stride > 0 ? following < stop : following > stop) {
            detail::publish_integer(context, following, next);
            desired = next.value();
        }
        if (context.compare_exchange(args[0], iterator.current, current.value(), desired)) {
            out.ok(current.value());
            return;
        }
        context.poll();
    }
}
void iterator_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void range_start(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<RangeObject>(value).start);
}
void range_stop(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<RangeObject>(value).stop);
}
void range_step(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<RangeObject>(value).step);
}
} // namespace
namespace detail {
void install_ranges(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"key", ParameterKind::PositionalOnly});
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    const auto owner = at(CoreObject::RangeType);
    install_native_attribute(context, core, owner, U"__new__", constructor, range_new);
    RootScope roots(context);
    ReturnSlot temporary(context);
    const auto dict = *TypeView(context, owner).dictionary();
    const auto function = *name_lookup(context, dict, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                  function);
    const auto wrapper = roots.root(temporary.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    name_store(context, dict, temporary.value(), wrapper.get(context));
    for (auto [name, entry] : {std::pair{U"__repr__", range_repr},
                               {U"__str__", range_repr},
                               {U"__bool__", range_bool},
                               {U"__len__", range_len},
                               {U"__hash__", range_hash},
                               {U"__iter__", range_iter}})
        install_native_attribute(context, core, owner, name, unary, entry);
    install_native_attribute(context, core, owner, U"__getitem__", pair, range_item);
    install_native_attribute(context, core, owner, U"__eq__", pair, range_equal<false>);
    install_native_attribute(context, core, owner, U"__ne__", pair, range_equal<true>);
    install_native_field(context, core, owner, U"start", range_start);
    install_native_field(context, core, owner, U"stop", range_stop);
    install_native_field(context, core, owner, U"step", range_step);
    install_native_attribute(context, core, at(CoreObject::RangeIteratorType), U"__iter__", unary,
                             iterator_self);
    install_native_attribute(context, core, at(CoreObject::RangeIteratorType), U"__next__", unary,
                             range_next);
}
} // namespace detail
} // namespace cxxpython::core
