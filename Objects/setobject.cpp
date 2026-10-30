// Set semantics follow CPython 3.12.14 Objects/setobject.c (PSF license).
// Storage and synchronization are provided by this runtime's dictionary.
#include "cxxpython/setobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/setobject.hpp"

#include <bit>

namespace cxxpython::core {
namespace {
bool receiver(Attachment &ctx, Value value, ReturnSlot &out, bool mutable_only = false)
{
    if (is_set(ctx, value) &&
        (!mutable_only || is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::SetType))))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"set descriptor requires a set", out);
    return false;
}
bool fill(Attachment &ctx, Handle table, Handle iterable, ReturnSlot &out)
{
    RootScope roots(ctx);
    if (is_set(ctx, iterable.get(ctx)) ||
        ctx.type_of(iterable) == ctx.builtin(CoreObject::DictType)) {
        Value source = is_set(ctx, iterable.get(ctx)) ? ctx.payload<SetObject>(iterable).table
                                                      : iterable.get(ctx);
        DictView view(ctx, source);
        for (const auto &entry : view.entries()) {
            RootScope step(ctx);
            detail::dict_store_hash(ctx, table, step.root(entry.key),
                                    step.root(ctx.builtin(CoreObject::None)), entry.hash, out);
            if (out.tag() == ResultTag::Raised)
                return false;
        }
        return true;
    }
    ReturnSlot iterator(ctx), item(ctx);
    get_iter(ctx, iterable, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return false;
    }
    auto it = roots.root(iterator.value());
    for (;;) {
        auto status = iter_next(ctx, it, item);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return false;
        }
        RootScope step(ctx);
        dict_store(ctx, table, step.root(item.value()), step.root(ctx.builtin(CoreObject::None)),
                   out);
        if (out.tag() == ResultTag::Raised)
            return false;
    }
}
void copy_as(Attachment &ctx, Handle source, CoreObject type, ReturnSlot &out)
{
    RootScope roots(ctx);
    DictView view(ctx, ctx.payload<SetObject>(source).table);
    ReturnSlot table_result(ctx);
    ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), table_result);
    auto table = roots.root(table_result.value());
    detail::copy_dictionary_entries(ctx, table.get(ctx), view.entries());
    ctx.allocate_instance<SetObject>(roots.root(ctx.builtin(type)), out, table.get(ctx));
}
bool mutable_key_error(Attachment &ctx, Handle key, ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised && is_set(ctx, key.get(ctx)) &&
           is_subtype(ctx, ctx.type_of(key), ctx.builtin(CoreObject::SetType)) &&
           is_subtype(ctx, ctx.type_of(out.exception()), ctx.builtin(CoreObject::TypeErrorType));
}
void lookup_key(Attachment &ctx, Handle table, Handle key, bool erase, ReturnSlot &out)
{
    if (erase)
        dict_erase(ctx, table, key, out);
    else
        dict_lookup(ctx, table, key, out);
    if (!mutable_key_error(ctx, key, out))
        return;
    RootScope roots(ctx);
    ReturnSlot frozen(ctx);
    copy_as(ctx, key, CoreObject::FrozenSetType, frozen);
    if (frozen.tag() == ResultTag::Raised) {
        out.raised(frozen.exception());
        return;
    }
    auto converted = roots.root(frozen.value());
    if (erase)
        dict_erase(ctx, table, converted, out);
    else
        dict_lookup(ctx, table, converted, out);
}
void missing_key(Attachment &ctx, Handle key, ReturnSlot &out)
{
    RootScope roots(ctx);
    const std::array args{key.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::KeyErrorType)), {args}, out);
    if (out.tag() != ResultTag::Raised)
        out.raised(out.value());
}
void repr_set(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto name_value = TypeView(ctx, ctx.type_of(self)).name();
    std::u32string name = std::holds_alternative<Value>(name_value)
                              ? ctx.payload<StrObject>(std::get<Value>(name_value)).value
                              : std::get<std::u32string>(name_value);
    if (!ctx.enter_repr(self)) {
        make_str(ctx, name + U"(...)", out);
        return;
    }
    struct Guard {
        Attachment &ctx;
        Value value;
        ~Guard()
        {
            ctx.leave_repr(value);
        }
    } guard{ctx, self.get(ctx)};
    if (detail::dictionary_size(ctx, ctx.payload<SetObject>(self).table) == 0) {
        make_str(ctx, name + U"()", out);
        return;
    }
    ReturnSlot iter(ctx), item(ctx);
    get_iter(ctx, self, iter);
    if (iter.tag() == ResultTag::Raised) {
        out.raised(iter.exception());
        return;
    }
    auto iterator = roots.root(iter.value());
    std::vector<Handle> items;
    for (;;) {
        auto status = iter_next(ctx, iterator, item);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        items.push_back(roots.root(item.value()));
    }
    const bool exact = ctx.type_of(self) == ctx.builtin(CoreObject::SetType);
    std::u32string text = exact ? U"{" : name + U"({";
    ReturnSlot item_repr(ctx);
    bool first = true;
    for (auto entry : items) {
        repr(ctx, entry, item_repr);
        if (item_repr.tag() == ResultTag::Raised) {
            out.raised(item_repr.exception());
            return;
        }
        if (!first)
            text += U", ";
        first = false;
        text += ctx.payload<StrObject>(item_repr.value()).value;
    }
    text += exact ? U"}" : U"})";
    make_str(ctx, std::move(text), out);
}
void set_reduce(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    ReturnSlot keys(ctx), arguments(ctx), name(ctx), method(ctx), state(ctx);
    const std::array iterable{self.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::ListType)), {iterable}, keys);
    if (keys.tag() == ResultTag::Raised) {
        out.raised(keys.exception());
        return;
    }
    const std::array values{keys.value()};
    cxxpython::core::make_tuple(ctx, values, arguments);
    make_str(ctx, U"__getstate__", name);
    get_attribute(ctx, self, roots.root(name.value()), method);
    if (method.tag() == ResultTag::Raised) {
        out.raised(method.exception());
        return;
    }
    call(ctx, roots.root(method.value()), {}, state);
    if (state.tag() == ResultTag::Raised) {
        out.raised(state.exception());
        return;
    }
    const std::array result{ctx.type_of(self), arguments.value(), state.value()};
    cxxpython::core::make_tuple(ctx, result, out);
}
void frozen_hash(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out) ||
        !is_subtype(ctx, ctx.type_of(args[0]), ctx.builtin(CoreObject::FrozenSetType))) {
        if (out.tag() != ResultTag::Raised)
            raise_error(ctx, CoreObject::TypeErrorType, U"frozenset hash requires frozenset", out);
        return;
    }
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &cached = ctx.payload<SetObject>(self).hash;
    auto result = cached.load(std::memory_order_acquire);
    if (result == -1) {
        DictView view(ctx, ctx.payload<SetObject>(self).table);
        std::uint64_t h = 0;
        for (const auto &entry : view.entries()) {
            auto x = static_cast<std::uint64_t>(entry.hash);
            h ^= ((x ^ 89869747ULL) ^ (x << 16)) * 3644798167ULL;
        }
        h ^= (static_cast<std::uint64_t>(view.entries().size()) + 1) * 1927868237ULL;
        h ^= (h >> 11) ^ (h >> 25);
        h = h * 69069ULL + 907133923ULL;
        if (h == UINT64_MAX)
            h = 590923713ULL;
        result = std::bit_cast<std::int64_t>(h);
        cached.store(result, std::memory_order_release);
    }
    make_int(ctx, result, out);
}
enum class Comparison { Eq, Ne, Lt, Le, Gt, Ge };
template <Comparison op>
void compare_set(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    if (!is_set(ctx, args[1])) {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(ctx);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    DictView a(ctx, ctx.payload<SetObject>(left).table);
    DictView b(ctx, ctx.payload<SetObject>(right).table);
    auto as = a.entries().size(), bs = b.entries().size();
    bool eligible;
    if constexpr (op == Comparison::Eq || op == Comparison::Ne)
        eligible = as == bs;
    else if constexpr (op == Comparison::Lt)
        eligible = as < bs;
    else if constexpr (op == Comparison::Le)
        eligible = as <= bs;
    else if constexpr (op == Comparison::Gt)
        eligible = as > bs;
    else
        eligible = as >= bs;
    if constexpr (op == Comparison::Eq || op == Comparison::Ne) {
        if (eligible) {
            const auto ah = ctx.payload<SetObject>(left).hash.load(std::memory_order_acquire);
            const auto bh = ctx.payload<SetObject>(right).hash.load(std::memory_order_acquire);
            if (ah != -1 && bh != -1 && ah != bh)
                eligible = false;
        }
    }
    bool included = eligible;
    if (included) {
        constexpr bool reverse = op == Comparison::Gt || op == Comparison::Ge;
        auto target = roots.root(ctx.payload<SetObject>(reverse ? left : right).table);
        auto entries = reverse ? b.entries() : a.entries();
        ReturnSlot found(ctx);
        for (const auto &entry : entries) {
            RootScope step(ctx);
            detail::dict_lookup_hash(ctx, target, step.root(entry.key), entry.hash, found);
            if (found.tag() == ResultTag::Raised) {
                out.raised(found.exception());
                return;
            }
            if (found.tag() == ResultTag::Empty) {
                included = false;
                break;
            }
        }
    }
    if constexpr (op == Comparison::Ne)
        included = !included;
    out.ok(ctx.builtin(included ? CoreObject::True : CoreObject::False));
}
void copy(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    if (ctx.type_of(args[0]) == ctx.builtin(CoreObject::FrozenSetType)) {
        out.ok(args[0]);
        return;
    }
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    copy_as(ctx, self,
            is_subtype(ctx, ctx.type_of(self), ctx.builtin(CoreObject::SetType))
                ? CoreObject::SetType
                : CoreObject::FrozenSetType,
            out);
}
template <bool Required>
void remove_key(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out, true))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]), key = roots.root(args[1]);
    lookup_key(ctx, roots.root(ctx.payload<SetObject>(self).table), key, true, out);
    if (out.tag() == ResultTag::Raised)
        return;
    if constexpr (Required) {
        if (out.tag() == ResultTag::Empty) {
            missing_key(ctx, key, out);
            return;
        }
    }
    out.ok(ctx.builtin(CoreObject::None));
}
void pop(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out, true))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    ReturnSlot value(ctx);
    detail::dict_popitem(ctx, roots.root(ctx.payload<SetObject>(self).table), out, value);
    if (out.tag() == ResultTag::Empty)
        raise_error(ctx, CoreObject::KeyErrorType, U"pop from an empty set", out);
}
template <bool Frozen>
void create(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    if (!ctx.has_layout<TypeObject>(args[0]) ||
        !is_subtype(ctx, args[0],
                    ctx.builtin(Frozen ? CoreObject::FrozenSetType : CoreObject::SetType))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    Frozen ? U"frozenset.__new__ requires a frozenset subtype"
                           : U"set.__new__ requires a set subtype",
                    out);
        return;
    }
    const auto &arguments = ctx.payload<TupleObject>(args[1]).items;
    constexpr bool frozen = Frozen;
    if (frozen && arguments.size() > 1) {
        raise_error(ctx, CoreObject::TypeErrorType, U"set expected at most one argument", out);
        return;
    }
    if (frozen && detail::dictionary_size(ctx, args[2]) != 0) {
        auto init = detail::type_attribute(ctx, args[0], U"__init__");
        if (init)
            roots.root(*init);
        auto base_init =
            detail::type_attribute(ctx, ctx.builtin(CoreObject::FrozenSetType), U"__init__");
        if (init == base_init) {
            raise_error(ctx, CoreObject::TypeErrorType, U"frozenset takes no keyword arguments",
                        out);
            return;
        }
    }
    if (frozen && args[0] == ctx.builtin(CoreObject::FrozenSetType) && arguments.size() == 1 &&
        ctx.type_of(arguments[0]) == ctx.builtin(CoreObject::FrozenSetType)) {
        out.ok(arguments[0]);
        return;
    }
    ReturnSlot table_out(ctx);
    ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), table_out);
    auto table = roots.root(table_out.value());
    if (frozen && !arguments.empty() && !fill(ctx, table, roots.root(arguments[0]), out))
        return;
    ctx.allocate_instance<SetObject>(type, out, table.get(ctx));
}
void init(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out, true))
        return;
    if (detail::dictionary_size(ctx, args[2]) != 0) {
        raise_error(ctx, CoreObject::TypeErrorType, U"set takes no keyword arguments", out);
        return;
    }
    const auto &arguments = ctx.payload<TupleObject>(args[1]).items;
    if (arguments.size() > 1) {
        raise_error(ctx, CoreObject::TypeErrorType, U"set expected at most one argument", out);
        return;
    }
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto table = roots.root(ctx.payload<SetObject>(self).table);
    dict_clear(ctx, table, out);
    if (!arguments.empty() && !fill(ctx, table, roots.root(arguments[0]), out))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
void add(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    set_add(ctx, roots.root(args[0]), roots.root(args[1]), out);
}
void contains(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    lookup_key(ctx, roots.root(ctx.payload<SetObject>(self).table), roots.root(args[1]), false,
               out);
    if (out.tag() != ResultTag::Raised)
        out.ok(ctx.builtin(out.tag() == ResultTag::Ok ? CoreObject::True : CoreObject::False));
}
void length(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    make_int(ctx, detail::dictionary_size(ctx, ctx.payload<SetObject>(args[0]).table), out);
}
void clear(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out, true))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    dict_clear(ctx, roots.root(ctx.payload<SetObject>(self).table), out);
}
void iterator(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    detail::make_dict_iterator(ctx, roots.root(ctx.payload<SetObject>(self).table),
                               DictViewKind::Keys, out,
                               roots.root(ctx.builtin(CoreObject::SetIteratorType)));
}
void iter_self(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!ctx.has_layout<DictIteratorObject>(args[0]) ||
        ctx.type_of(args[0]) != ctx.builtin(CoreObject::SetIteratorType)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"expected set iterator", out);
        return;
    }
    out.ok(args[0]);
}
void next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    iter_self(ctx, {}, args, out);
    if (out.tag() == ResultTag::Raised)
        return;
    RootScope roots(ctx);
    detail::next_dict_item(ctx, roots.root(args[0]), out);
}
template <CoreObject Base, NativeEntry Entry, bool Constructor = false>
void typed_entry(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    const bool valid =
        Constructor
            ? ctx.has_layout<TypeObject>(args[0]) && is_subtype(ctx, args[0], ctx.builtin(Base))
            : is_set(ctx, args[0]) && is_subtype(ctx, ctx.type_of(args[0]), ctx.builtin(Base));
    if (!valid) {
        raise_error(ctx, CoreObject::TypeErrorType, U"set descriptor has incompatible receiver",
                    out);
        return;
    }
    Entry(ctx, function, args, out);
}
} // namespace
bool is_set(Attachment &ctx, Value value)
{
    return ctx.has_layout<SetObject>(value) &&
           (is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::SetType)) ||
            is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::FrozenSetType)));
}
void set_add(Attachment &ctx, Handle set, Handle key, ReturnSlot &out)
{
    if (!receiver(ctx, set.get(ctx), out, true))
        return;
    RootScope roots(ctx);
    dict_store(ctx, roots.root(ctx.payload<SetObject>(set).table), key,
               roots.root(ctx.builtin(CoreObject::None)), out);
    if (out.tag() != ResultTag::Raised)
        out.ok(ctx.builtin(CoreObject::None));
}
void make_set(Attachment &ctx, std::span<const Value> values, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Handle> items;
    for (auto value : values)
        items.push_back(roots.root(value));
    ReturnSlot table_out(ctx), result(ctx);
    ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), table_out);
    auto table = roots.root(table_out.value());
    for (auto item : items) {
        dict_store(ctx, table, item, roots.root(ctx.builtin(CoreObject::None)), result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
    }
    ctx.allocate_instance<SetObject>(roots.root(ctx.builtin(CoreObject::SetType)), out,
                                     table.get(ctx));
}
void detail::install_sets(Attachment &ctx, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    auto install = [&](CoreObject type, std::u32string name, SignatureLayout signature,
                       NativeEntry entry) {
        install_native_attribute(ctx, core, at(type), std::move(name), std::move(signature), entry);
    };
    RootScope roots(ctx);
    ReturnSlot key(ctx), wrapper(ctx);
    for (auto type : {CoreObject::SetType, CoreObject::FrozenSetType}) {
        const bool frozen = type == CoreObject::FrozenSetType;
        install(type, U"__new__", variadic, frozen ? create<true> : create<false>);
        auto function = *name_lookup(ctx, *TypeView(ctx, at(type)).dictionary(), U"__new__");
        ctx.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  function);
        ctx.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
        name_store(ctx, *TypeView(ctx, at(type)).dictionary(), key.value(), wrapper.value());
        install(type, U"__contains__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, contains>
                       : typed_entry<CoreObject::SetType, contains>);
        install(type, U"__len__", unary,
                frozen ? typed_entry<CoreObject::FrozenSetType, length>
                       : typed_entry<CoreObject::SetType, length>);
        install(type, U"__iter__", unary,
                frozen ? typed_entry<CoreObject::FrozenSetType, iterator>
                       : typed_entry<CoreObject::SetType, iterator>);
        install(type, U"__repr__", unary,
                frozen ? typed_entry<CoreObject::FrozenSetType, repr_set>
                       : typed_entry<CoreObject::SetType, repr_set>);
        install(type, U"copy", unary,
                frozen ? typed_entry<CoreObject::FrozenSetType, copy>
                       : typed_entry<CoreObject::SetType, copy>);
        install(type, U"__reduce__", unary,
                frozen ? typed_entry<CoreObject::FrozenSetType, set_reduce>
                       : typed_entry<CoreObject::SetType, set_reduce>);
        install(type, U"__eq__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Eq>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Eq>>);
        install(type, U"__ne__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Ne>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Ne>>);
        install(type, U"__lt__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Lt>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Lt>>);
        install(type, U"__le__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Le>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Le>>);
        install(type, U"__gt__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Gt>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Gt>>);
        install(type, U"__ge__", pair,
                frozen ? typed_entry<CoreObject::FrozenSetType, compare_set<Comparison::Ge>>
                       : typed_entry<CoreObject::SetType, compare_set<Comparison::Ge>>);
    }
    install(CoreObject::FrozenSetType, U"__hash__", unary, frozen_hash);
    install(CoreObject::SetType, U"__init__", variadic, init);
    install(CoreObject::SetType, U"add", pair, add);
    install(CoreObject::SetType, U"clear", unary, clear);
    install(CoreObject::SetType, U"discard", pair, remove_key<false>);
    install(CoreObject::SetType, U"remove", pair, remove_key<true>);
    install(CoreObject::SetType, U"pop", unary, pop);
    install(CoreObject::SetIteratorType, U"__iter__", unary, iter_self);
    install(CoreObject::SetIteratorType, U"__next__", unary, next);
    ctx.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
    name_store(ctx, *TypeView(ctx, at(CoreObject::SetType)).dictionary(), key.value(),
               at(CoreObject::None).get(ctx));
    install_set_operations(ctx, core);
}
} // namespace cxxpython::core
