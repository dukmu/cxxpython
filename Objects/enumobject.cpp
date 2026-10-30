#include "cxxpython/protocol.hpp"
#include "internal/reversed.hpp"

#include <array>

namespace cxxpython::core {
namespace {
bool raised(ReturnSlot &slot)
{
    return slot.tag() == ResultTag::Raised;
}
bool iterator_receiver(Attachment &context, Value value, CoreObject type, ReturnSlot &out)
{
    if (context.has_layout<SequenceIteratorObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(type)))
        return true;
    raise_error(context, CoreObject::TypeErrorType,
                U"reverse iterator descriptor requires an iterator", out);
    return false;
}
template <class F> decltype(auto) locked(Attachment &context, ListObject::Storage &data, F &&body)
{
    for (;;) {
        std::unique_lock lock(data.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint commit(context);
            return body();
        }
        NativeWait parked(context);
        data.mutex.lock();
        data.mutex.unlock();
    }
}
void not_reversible(Attachment &context, Handle type, ReturnSlot &out)
{
    const auto name = TypeView(context, type).name();
    const auto text = std::holds_alternative<std::u32string>(name)
                          ? std::get<std::u32string>(name)
                          : context.payload<StrObject>(std::get<Value>(name)).value;
    raise_error(context, CoreObject::TypeErrorType, U"'" + text + U"' object is not reversible",
                out);
}
void reversed_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto cls = roots.root(args[0]), sequence = roots.root(args[1]);
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::ReversedType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"reversed.__new__ requires a reversed type", out);
        return;
    }
    const auto type = roots.root(context.type_of(sequence));
    const auto special = detail::type_attribute(context, type.get(context), U"__reversed__");
    if (special) {
        if (*special == context.builtin(CoreObject::None)) {
            not_reversible(context, type, out);
            return;
        }
        ReturnSlot bound(context);
        detail::bind_descriptor(context, *special, sequence.get(context), type.get(context), bound);
        if (raised(bound)) {
            out.raised(bound.exception());
            return;
        }
        call(context, roots.root(bound.value()), {}, out);
        return;
    }
    const auto item = resolve_protocol(context, type, Protocol::GetItem, roots);
    if (item.state == SlotState::Missing ||
        is_subtype(context, type.get(context), context.builtin(CoreObject::DictType))) {
        not_reversible(context, type, out);
        return;
    }
    ReturnSlot size(context);
    length(context, sequence, size);
    if (raised(size)) {
        out.raised(size.exception());
        return;
    }
    std::int64_t count;
    int_fits_signed(context, size.value(), count);
    context.allocate_instance<SequenceIteratorObject>(cls, out, sequence.get(context));
    if (!raised(out))
        context.payload<SequenceIteratorObject>(out.value())
            .index.store(static_cast<std::size_t>(count), std::memory_order_relaxed);
}
void list_reversed(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<ListObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]), context.builtin(CoreObject::ListType))) {
        raise_error(context, CoreObject::TypeErrorType, U"list descriptor requires a list", out);
        return;
    }
    RootScope roots(context);
    const auto list = roots.root(args[0]);
    auto &data = *context.payload<ListObject>(list).contents;
    const auto count = locked(context, data, [&] { return data.items.size(); });
    context.allocate_instance<SequenceIteratorObject>(
        roots.root(context.builtin(CoreObject::ListReverseIteratorType)), out, list.get(context));
    if (!raised(out))
        context.payload<SequenceIteratorObject>(out.value())
            .index.store(count, std::memory_order_relaxed);
}
template <CoreObject Type>
void iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (iterator_receiver(context, args[0], Type, out))
        out.ok(args[0]);
}
template <CoreObject Type>
void next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!iterator_receiver(context, args[0], Type, out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    auto &state = context.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(context);
    context.load(self.get(context), state.owner, owner);
    if (owner.tag() == ResultTag::Empty) {
        detail::stop_iteration(context, out);
        return;
    }
    const auto sequence = roots.root(owner.value());
    if constexpr (Type == CoreObject::ListReverseIteratorType) {
        auto &data = *context.payload<ListObject>(sequence).contents;
        const bool found = locked(context, data, [&] {
            const auto count = state.index.load(std::memory_order_relaxed);
            if (!count || count > data.items.size()) {
                state.index.store(0, std::memory_order_relaxed);
                context.store(self.get(context), state.owner, {});
                return false;
            }
            out.ok(data.items[count - 1]);
            state.index.store(count - 1, std::memory_order_relaxed);
            return true;
        });
        if (!found)
            detail::stop_iteration(context, out);
    } else {
        auto count = state.index.load(std::memory_order_relaxed);
        while (count &&
               !state.index.compare_exchange_weak(count, count - 1, std::memory_order_relaxed)) {
        }
        if (!count) {
            context.store(self.get(context), state.owner, {});
            detail::stop_iteration(context, out);
            return;
        }
        ReturnSlot index_value(context);
        make_int(context, static_cast<std::int64_t>(count - 1), index_value);
        get_item(context, sequence, roots.root(index_value.value()), out);
        if (raised(out)) {
            const auto exception = roots.root(out.exception());
            state.index.store(0, std::memory_order_relaxed);
            context.store(self.get(context), state.owner, {});
            if (is_subtype(context, context.type_of(exception),
                           context.builtin(CoreObject::IndexErrorType)) ||
                is_subtype(context, context.type_of(exception),
                           context.builtin(CoreObject::StopIterationType)))
                detail::stop_iteration(context, out);
        }
    }
}
template <CoreObject Type>
void reverse_hint(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!iterator_receiver(context, args[0], Type, out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    auto &state = context.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(context), size(context);
    context.load(self.get(context), state.owner, owner);
    if (owner.tag() == ResultTag::Empty) {
        make_int(context, 0, out);
        return;
    }
    const auto sequence = roots.root(owner.value());
    std::size_t count = state.index.load(std::memory_order_relaxed), current;
    if constexpr (Type == CoreObject::ListReverseIteratorType) {
        auto &data = *context.payload<ListObject>(sequence).contents;
        current = locked(context, data, [&] { return data.items.size(); });
    } else {
        length(context, sequence, size);
        if (raised(size)) {
            out.raised(size.exception());
            return;
        }
        std::int64_t n;
        int_fits_signed(context, size.value(), n);
        current = static_cast<std::size_t>(n);
    }
    make_int(context, current < count ? 0 : static_cast<std::int64_t>(count), out);
}
bool check_type(Attachment &context, Value value, CoreObject type, ReturnSlot &out)
{
    if (context.has_layout<TypeObject>(value) && is_subtype(context, value, context.builtin(type)))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"invalid iterator type", out);
    return false;
}
void enumerate_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_type(context, args[0], CoreObject::EnumerateType, out))
        return;
    RootScope roots(context);
    const auto cls = roots.root(args[0]);
    ReturnSlot start(context), iterator(context);
    index(context, roots.root(args[2]), start);
    if (raised(start)) {
        out.raised(start.exception());
        return;
    }
    const auto initial = roots.root(start.value());
    get_iter(context, roots.root(args[1]), iterator);
    if (raised(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    context.allocate_instance<EnumObject>(cls, out, iterator.value(), initial.get(context));
}
void enumerate_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<EnumObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]),
                    context.builtin(CoreObject::EnumerateType))) {
        raise_error(context, CoreObject::TypeErrorType, U"enumerate iterator required", out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    auto &state = context.payload<EnumObject>(self);
    ReturnSlot item(context), current(context), one(context), next_index(context);
    const auto status = iter_next(context, roots.root(state.iterator), item);
    if (status == NextStatus::Done) {
        detail::stop_iteration(context, out);
        return;
    }
    if (status == NextStatus::Raised) {
        out.raised(item.exception());
        return;
    }
    make_int(context, 1, one);
    const auto increment = roots.root(one.value());
    for (;;) {
        context.load(self.get(context), state.index, current);
        const auto old = roots.root(current.value());
        binary(context, old, increment, BinaryOp::Add, next_index);
        if (raised(next_index)) {
            out.raised(next_index.exception());
            return;
        }
        if (context.compare_exchange(self.get(context), state.index, old.get(context),
                                     next_index.value())) {
            const std::array values{old.get(context), item.value()};
            cxxpython::core::make_tuple(context, values, out);
            return;
        }
        roots.release(old);
    }
}
template <CoreObject Type>
void multi_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_type(context, args[0], Type, out))
        return;
    RootScope roots(context);
    const auto cls = roots.root(args[0]);
    bool strict = false;
    std::optional<Value> function;
    std::size_t rest_index;
    if constexpr (Type == CoreObject::ZipType) {
        ReturnSlot converted(context);
        truth(context, roots.root(args[1]), converted);
        if (raised(converted)) {
            out.raised(converted.exception());
            return;
        }
        strict = converted.value() == context.builtin(CoreObject::True);
        rest_index = 2;
    } else {
        function = roots.root(args[1]).get(context);
        rest_index = 2;
    }
    const auto tuple = roots.root(args[rest_index]);
    const auto &inputs = context.payload<TupleObject>(tuple).items;
    if constexpr (Type == CoreObject::MapType) {
        if (inputs.empty()) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"map() must have at least two arguments", out);
            return;
        }
    }
    std::vector<Value> iterators;
    iterators.reserve(inputs.size());
    ReturnSlot iterator(context);
    for (auto input : inputs) {
        get_iter(context, roots.root(input), iterator);
        if (raised(iterator)) {
            out.raised(iterator.exception());
            return;
        }
        iterators.push_back(roots.root(iterator.value()).get(context));
    }
    context.allocate_instance<MultiIteratorObject>(cls, out, std::move(iterators), function,
                                                   strict);
}
void zip_mismatch(Attachment &context, std::size_t index, bool longer, ReturnSlot &out)
{
    const auto argument = std::to_string(index + 1), previous = std::to_string(index);
    auto widen = [](const std::string &value) {
        return std::u32string(value.begin(), value.end());
    };
    raise_error(context, CoreObject::ValueErrorType,
                U"zip() argument " + widen(argument) +
                    (longer ? U" is longer than argument" : U" is shorter than argument") +
                    (index == 1 ? U" " : U"s 1-") + widen(previous),
                out);
}
template <CoreObject Type>
void multi_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<MultiIteratorObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]), context.builtin(Type))) {
        raise_error(context, CoreObject::TypeErrorType, U"iterator descriptor requires an iterator",
                    out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    const auto &state = context.payload<MultiIteratorObject>(self);
    if (state.iterators.empty()) {
        detail::stop_iteration(context, out);
        return;
    }
    std::vector<Value> values;
    values.reserve(state.iterators.size());
    ReturnSlot item(context);
    for (std::size_t i = 0; i < state.iterators.size(); ++i) {
        const auto status = iter_next(context, roots.root(state.iterators[i]), item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done) {
            if (state.strict && i) {
                zip_mismatch(context, i, false, out);
                return;
            }
            if (state.strict) {
                for (std::size_t j = 1; j < state.iterators.size(); ++j) {
                    const auto probe = iter_next(context, roots.root(state.iterators[j]), item);
                    if (probe == NextStatus::Raised) {
                        out.raised(item.exception());
                        return;
                    }
                    if (probe == NextStatus::Item) {
                        zip_mismatch(context, j, true, out);
                        return;
                    }
                }
            }
            detail::stop_iteration(context, out);
            return;
        }
        values.push_back(roots.root(item.value()).get(context));
    }
    if constexpr (Type == CoreObject::ZipType)
        cxxpython::core::make_tuple(context, values, out);
    else
        call(context, roots.root(*state.callable), {values, {}}, out);
}
template <CoreObject Type, class Layout>
void other_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<Layout>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]), context.builtin(Type))) {
        raise_error(context, CoreObject::TypeErrorType, U"iterator descriptor requires an iterator",
                    out);
        return;
    }
    out.ok(args[0]);
}
} // namespace
namespace detail {
void install_reversed(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto constructor = unary;
    constructor.parameters.push_back({U"sequence", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, at(CoreObject::ReversedType), U"__new__", constructor,
                             reversed_new);
    RootScope roots(context);
    ReturnSlot name(context), wrapper(context);
    const auto dictionary = *TypeView(context, at(CoreObject::ReversedType)).dictionary();
    const auto function = *name_lookup(context, dictionary, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  function);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), name, U"__new__");
    name_store(context, dictionary, name.value(), wrapper.value());
    install_native_attribute(context, core, at(CoreObject::ListType), U"__reversed__", unary,
                             list_reversed);
    install_native_attribute(context, core, at(CoreObject::ReversedType), U"__iter__", unary,
                             iter<CoreObject::ReversedType>);
    install_native_attribute(context, core, at(CoreObject::ReversedType), U"__next__", unary,
                             next<CoreObject::ReversedType>);
    install_native_attribute(context, core, at(CoreObject::ReversedType), U"__length_hint__", unary,
                             reverse_hint<CoreObject::ReversedType>);
    install_native_attribute(context, core, at(CoreObject::ListReverseIteratorType), U"__iter__",
                             unary, iter<CoreObject::ListReverseIteratorType>);
    install_native_attribute(context, core, at(CoreObject::ListReverseIteratorType), U"__next__",
                             unary, next<CoreObject::ListReverseIteratorType>);
    install_native_attribute(context, core, at(CoreObject::ListReverseIteratorType),
                             U"__length_hint__", unary,
                             reverse_hint<CoreObject::ListReverseIteratorType>);
    auto wrap_new = [&](CoreObject type) {
        const auto dictionary = *TypeView(context, at(type)).dictionary();
        const auto function = *name_lookup(context, dictionary, U"__new__");
        context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                      function);
        name_store(context, dictionary, name.value(), wrapper.value());
    };
    ReturnSlot zero(context);
    zero.ok(at(CoreObject::IntZero).get(context));
    auto enumerate_signature = unary;
    enumerate_signature.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
    enumerate_signature.parameters.push_back(
        {U"start", ParameterKind::PositionalOrKeyword, zero.value()});
    install_native_attribute(context, core, at(CoreObject::EnumerateType), U"__new__",
                             enumerate_signature, enumerate_new);
    wrap_new(CoreObject::EnumerateType);
    install_native_attribute(context, core, at(CoreObject::EnumerateType), U"__iter__", unary,
                             other_iter<CoreObject::EnumerateType, EnumObject>);
    install_native_attribute(context, core, at(CoreObject::EnumerateType), U"__next__", unary,
                             enumerate_next);
    auto zip_signature = unary;
    zip_signature.parameters.push_back(
        {U"strict", ParameterKind::KeywordOnly, at(CoreObject::False).get(context)});
    zip_signature.varargs = U"iterables";
    install_native_attribute(context, core, at(CoreObject::ZipType), U"__new__", zip_signature,
                             multi_new<CoreObject::ZipType>);
    wrap_new(CoreObject::ZipType);
    install_native_attribute(context, core, at(CoreObject::ZipType), U"__iter__", unary,
                             other_iter<CoreObject::ZipType, MultiIteratorObject>);
    install_native_attribute(context, core, at(CoreObject::ZipType), U"__next__", unary,
                             multi_next<CoreObject::ZipType>);
    auto map_signature = unary;
    map_signature.parameters.push_back({U"function", ParameterKind::PositionalOnly});
    map_signature.varargs = U"iterables";
    install_native_attribute(context, core, at(CoreObject::MapType), U"__new__", map_signature,
                             multi_new<CoreObject::MapType>);
    wrap_new(CoreObject::MapType);
    install_native_attribute(context, core, at(CoreObject::MapType), U"__iter__", unary,
                             other_iter<CoreObject::MapType, MultiIteratorObject>);
    install_native_attribute(context, core, at(CoreObject::MapType), U"__next__", unary,
                             multi_next<CoreObject::MapType>);
}
} // namespace detail
} // namespace cxxpython::core
