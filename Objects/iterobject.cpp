#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
ResultView get_iter(Attachment &context, Handle value, ReturnSlot &out)
{
    out.check_caller(context);
    try {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        auto slot =
            resolve_protocol(context, roots.root(context.type_of(self)), Protocol::Iter, roots);
        if (slot.state == SlotState::Missing) {
            auto item = resolve_protocol(context, roots.root(context.type_of(self)),
                                         Protocol::GetItem, roots);
            if (item.state != SlotState::Missing) {
                context.allocate_instance<SequenceIteratorObject>(
                    roots.root(context.builtin(CoreObject::SequenceIteratorType)), out,
                    self.get(context));
                return out.result();
            }
        }
        invoke_protocol(context, self, slot, {}, out);
        if (out.tag() == ResultTag::Ok) {
            auto next = resolve_protocol(context, roots.root(context.type_of(out.value())),
                                         Protocol::Next, roots);
            if (next.state == SlotState::Missing)
                raise_error(context, CoreObject::TypeErrorType, U"iter() returned a non-iterator",
                            out);
        }
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
NextStatus iter_next(Attachment &context, Handle iterator, ReturnSlot &out)
{
    out.check_caller(context);
    try {
        RootScope roots(context);
        const auto self = roots.root(iterator.get(context));
        auto slot =
            resolve_protocol(context, roots.root(context.type_of(self)), Protocol::Next, roots);
        invoke_protocol(context, self, slot, {}, out);
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    if (out.tag() == ResultTag::Ok)
        return NextStatus::Item;
    return is_subtype(context, context.type_of(out.exception()),
                      context.builtin(CoreObject::StopIterationType))
               ? NextStatus::Done
               : NextStatus::Raised;
}
namespace {
template <class T, CoreObject Type> bool sequence(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.has_layout<T>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(Type)))
        return true;
    raise_error(context, CoreObject::TypeErrorType,
                U"sequence descriptor received an incompatible value", out);
    return false;
}
template <class T, CoreObject Type>
void sequence_item(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!sequence<T, Type>(context, args[0], out))
        return;
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(args[1]), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    const auto &data = context.payload<T>(args[0]);
    const auto length = [&] {
        if constexpr (Type == CoreObject::TupleType)
            return data.items.size();
        else
            return data.value.size();
    }();
    std::int64_t position;
    if (!int_fits_signed(context, converted.value(), position)) {
        raise_error(context, CoreObject::IndexErrorType, U"sequence index is out of range", out);
        return;
    }
    if (position < 0)
        position += static_cast<std::int64_t>(length);
    if (position < 0 || static_cast<std::size_t>(position) >= length) {
        raise_error(context, CoreObject::IndexErrorType, U"sequence index is out of range", out);
        return;
    }
    if constexpr (Type == CoreObject::TupleType)
        out.ok(data.items[position]);
    else
        make_str(context, std::u32string(1, data.value[position]), out);
}
template <class T, CoreObject Type, CoreObject Iterator>
void sequence_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!sequence<T, Type>(context, args[0], out))
        return;
    RootScope roots(context);
    context.allocate_instance<SequenceIteratorObject>(roots.root(context.builtin(Iterator)), out,
                                                      args[0]);
}
template <class T, CoreObject Iterator>
void sequence_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!sequence<SequenceIteratorObject, Iterator>(context, args[0], out))
        return;
    auto &iterator = context.payload<SequenceIteratorObject>(args[0]);
    ReturnSlot owner(context), item(context);
    for (;;) {
        context.load(args[0], iterator.owner, owner);
        if (owner.tag() == ResultTag::Empty) {
            detail::stop_iteration(context, out);
            return;
        }
        const auto &data = context.payload<T>(owner.value());
        const auto length = [&] {
            if constexpr (Iterator == CoreObject::TupleIteratorType)
                return data.items.size();
            else
                return data.value.size();
        }();
        auto position = iterator.index.load(std::memory_order_relaxed);
        if (position >= length) {
            context.store(args[0], iterator.owner, {});
            detail::stop_iteration(context, out);
            return;
        }
        if constexpr (Iterator == CoreObject::TupleIteratorType)
            item.ok(data.items[position]);
        else
            make_str(context, std::u32string(1, data.value[position]), item);
        if (iterator.index.compare_exchange_weak(position, position + 1,
                                                 std::memory_order_relaxed)) {
            out.ok(item.value());
            return;
        }
        context.poll();
    }
}
void iterator_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void indexed_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!sequence<SequenceIteratorObject, CoreObject::SequenceIteratorType>(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    auto &state = context.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(context), key(context), value(context);
    for (;;) {
        context.load(self.get(context), state.owner, owner);
        if (owner.tag() == ResultTag::Empty) {
            detail::stop_iteration(context, out);
            return;
        }
        auto position = state.index.load(std::memory_order_relaxed);
        if (position == static_cast<std::size_t>(std::numeric_limits<std::intptr_t>::max())) {
            raise_error(context, CoreObject::OverflowErrorType, U"iterator index is too large",
                        out);
            return;
        }
        make_int(context, static_cast<std::int64_t>(position), key);
        RootScope iteration(context);
        get_item(context, iteration.root(owner.value()), iteration.root(key.value()), value);
        if (value.tag() == ResultTag::Raised) {
            const auto type = context.type_of(value.exception());
            if (is_subtype(context, type, context.builtin(CoreObject::IndexErrorType)) ||
                is_subtype(context, type, context.builtin(CoreObject::StopIterationType))) {
                context.store(self.get(context), state.owner, {});
                detail::stop_iteration(context, out);
            } else
                out.raised(value.exception());
            return;
        }
        if (state.index.compare_exchange_strong(position, position + 1,
                                                std::memory_order_relaxed)) {
            out.ok(value.value());
            return;
        }
        context.poll();
    }
}
void stop_value(Attachment &context, Value value, ReturnSlot &out)
{
    context.load(value, context.instance_slot(value, 0), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
void stop_value_set(Attachment &context, Value owner, std::optional<Value> value, ReturnSlot &out)
{
    context.store(owner, context.instance_slot(owner, 0),
                  value.value_or(context.builtin(CoreObject::None)));
    out.ok(context.builtin(CoreObject::None));
}
void builtin_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    get_iter(context, roots.root(args[0]), out);
}
void builtin_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto &defaults = context.payload<TupleObject>(args[1]).items;
    if (defaults.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"next expects at most one default", out);
        return;
    }
    RootScope roots(context);
    const auto status = iter_next(context, roots.root(args[0]), out);
    if (status == NextStatus::Done && !defaults.empty())
        out.ok(defaults[0]);
}
} // namespace
namespace detail {
void stop_iteration(Attachment &context, ReturnSlot &out)
{
    RootScope roots(context);
    context.allocate_instance<ExceptionObject>(
        roots.root(context.builtin(CoreObject::StopIterationType)), out,
        context.builtin(CoreObject::EmptyTuple));
    out.raised(out.value());
}
void install_iteration(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"key", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, at(CoreObject::TupleType), U"__getitem__", pair,
                             sequence_item<TupleObject, CoreObject::TupleType>);
    install_native_attribute(context, core, at(CoreObject::StrType), U"__getitem__", pair,
                             sequence_item<StrObject, CoreObject::StrType>);
    install_native_attribute(
        context, core, at(CoreObject::TupleType), U"__iter__", unary,
        sequence_iter<TupleObject, CoreObject::TupleType, CoreObject::TupleIteratorType>);
    install_native_attribute(
        context, core, at(CoreObject::StrType), U"__iter__", unary,
        sequence_iter<StrObject, CoreObject::StrType, CoreObject::StrIteratorType>);
    install_native_attribute(context, core, at(CoreObject::TupleIteratorType), U"__next__", unary,
                             sequence_next<TupleObject, CoreObject::TupleIteratorType>);
    install_native_attribute(context, core, at(CoreObject::StrIteratorType), U"__next__", unary,
                             sequence_next<StrObject, CoreObject::StrIteratorType>);
    for (auto id : {CoreObject::TupleIteratorType, CoreObject::StrIteratorType})
        install_native_attribute(context, core, at(id), U"__iter__", unary, iterator_self);
    install_native_attribute(context, core, at(CoreObject::SequenceIteratorType), U"__iter__",
                             unary, iterator_self);
    install_native_attribute(context, core, at(CoreObject::SequenceIteratorType), U"__next__",
                             unary, indexed_next);
    install_native_field(context, core, at(CoreObject::StopIterationType), U"value", stop_value,
                         stop_value_set);
}
void install_iterator_builtins(Attachment &context, std::span<const Handle> core, Value mapping)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(context);
    ReturnSlot function(context), name(context);
    for (bool next : {false, true}) {
        SignatureLayout signature;
        signature.parameters = {{U"iterator", ParameterKind::PositionalOnly}};
        if (next)
            signature.varargs = U"default";
        context.allocate_instance<NativeFunctionObject>(
            at(CoreObject::NativeFunctionType), function, next ? "next" : "iter", signature,
            next ? builtin_next : builtin_iter, std::vector<Value>{});
        context.allocate_instance<StrObject>(at(CoreObject::StrType), name,
                                             next ? U"next" : U"iter");
        name_store(context, mapping, name.value(), function.value());
    }
}
} // namespace detail
} // namespace cxxpython::core
