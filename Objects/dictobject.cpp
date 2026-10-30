#include "cxxpython/protocol.hpp"

#include <tuple>

namespace cxxpython::core {
namespace {
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool dictionary(Attachment &context, Value object, ReturnSlot &out)
{
    if (context.has_layout<DictObject>(object) &&
        is_subtype(context, context.type_of(object), context.builtin(CoreObject::DictType)))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"dict descriptor requires a dictionary", out);
    return false;
}
bool proxy(Attachment &context, Value object, ReturnSlot &out)
{
    if (context.has_layout<MappingProxyObject>(object) &&
        context.type_of(object) == context.builtin(CoreObject::MappingProxyType))
        return true;
    raise_error(context, CoreObject::TypeErrorType,
                U"mappingproxy descriptor requires mappingproxy", out);
    return false;
}
void missing_key(Attachment &context, Handle key, ReturnSlot &out)
{
    RootScope roots(context);
    const auto type = roots.root(context.builtin(CoreObject::KeyErrorType));
    const std::array values{key.get(context)};
    call(context, type, {values}, out);
    if (!failed(out))
        out.raised(out.value());
}
void getitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]), key = roots.root(args[1]);
    dict_lookup(context, self, key, out);
    if (out.tag() != ResultTag::Empty)
        return;
    if (context.type_of(self) != context.builtin(CoreObject::DictType))
        if (auto method = detail::type_attribute(context, context.type_of(self), U"__missing__")) {
            detail::invoke_special(context, self, roots.root(*method), {{args.data() + 1, 1}}, out);
            return;
        }
    missing_key(context, key, out);
}
void proxy_getitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!proxy(context, args[0], out))
        return;
    RootScope roots(context);
    auto key = roots.root(args[1]);
    get_item(context, roots.root(context.payload<MappingProxyObject>(args[0]).mapping), key, out);
}
void proxy_forward(Attachment &context, std::span<const Value> args, const char32_t *name,
                   ReturnSlot &out)
{
    RootScope roots(context);
    auto source = roots.root(context.payload<MappingProxyObject>(args[0]).mapping);
    ReturnSlot key(context), method(context);
    make_str(context, name, key);
    get_attribute(context, source, roots.root(key.value()), method);
    if (failed(method)) {
        out.raised(method.exception());
        return;
    }
    call(context, roots.root(method.value()), {args.subspan(1)}, out);
}
void proxy_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(args[0]), source = roots.root(args[1]);
    if (type.get(context) != context.builtin(CoreObject::MappingProxyType)) {
        raise_error(context, CoreObject::TypeErrorType, U"mappingproxy requires its native type",
                    out);
        return;
    }
    const auto source_type = context.type_of(source);
    const auto slot = resolve_protocol(context, roots.root(source_type), Protocol::GetItem, roots);
    if (slot.state == SlotState::Missing ||
        is_subtype(context, source_type, context.builtin(CoreObject::ListType)) ||
        is_subtype(context, source_type, context.builtin(CoreObject::TupleType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"mappingproxy() argument must be a mapping", out);
        return;
    }
    context.allocate_instance<MappingProxyObject>(type, out, source.get(context));
}
template <CompareOp op>
void proxy_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!proxy(context, args[0], out))
        return;
    RootScope roots(context);
    auto left = roots.root(context.payload<MappingProxyObject>(args[0]).mapping);
    auto right = roots.root(args[1]);
    if (context.has_layout<MappingProxyObject>(right.get(context)))
        right = roots.root(context.payload<MappingProxyObject>(right).mapping);
    rich_compare(context, left, right, op, out);
}
void setitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    dict_store(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void delitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    auto key = roots.root(args[1]);
    dict_erase(context, roots.root(args[0]), key, out);
    if (out.tag() == ResultTag::Empty)
        missing_key(context, key, out);
    else if (!failed(out))
        out.ok(context.builtin(CoreObject::None));
}
template <bool Proxy> Value mapping(Attachment &context, Value self)
{
    if constexpr (Proxy)
        return context.payload<MappingProxyObject>(self).mapping;
    return self;
}
template <bool Proxy> bool receiver(Attachment &context, Value self, ReturnSlot &out)
{
    if constexpr (Proxy)
        return proxy(context, self, out);
    return dictionary(context, self, out);
}
template <bool Proxy>
void get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Proxy>(context, args[0], out))
        return;
    if constexpr (Proxy) {
        proxy_forward(context, args, U"get", out);
        return;
    }
    RootScope roots(context);
    dict_lookup(context, roots.root(mapping<Proxy>(context, args[0])), roots.root(args[1]), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(args[2]);
}
void setdefault(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    dict_setdefault(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void pop(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    const auto &defaults = context.payload<TupleObject>(args[2]).items;
    if (defaults.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"pop expected at most two arguments", out);
        return;
    }
    RootScope roots(context);
    auto key = roots.root(args[1]);
    dict_erase(context, roots.root(args[0]), key, out);
    if (out.tag() == ResultTag::Empty) {
        if (defaults.empty())
            missing_key(context, key, out);
        else
            out.ok(defaults[0]);
    }
}
void clear(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    dict_clear(context, roots.root(args[0]), out);
}
template <bool Proxy>
void contains(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Proxy>(context, args[0], out))
        return;
    RootScope roots(context);
    if constexpr (Proxy) {
        rich_compare(context, roots.root(args[1]), roots.root(mapping<Proxy>(context, args[0])),
                     CompareOp::In, out);
        return;
    }
    dict_lookup(context, roots.root(mapping<Proxy>(context, args[0])), roots.root(args[1]), out);
    if (!failed(out))
        out.ok(context.builtin(out.tag() == ResultTag::Ok ? CoreObject::True : CoreObject::False));
}
template <bool Proxy>
void copy(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Proxy>(context, args[0], out))
        return;
    if constexpr (Proxy) {
        proxy_forward(context, args, U"copy", out);
        return;
    }
    RootScope roots(context);
    DictView view(context, mapping<Proxy>(context, args[0]));
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    detail::copy_dictionary_entries(context, out.value(), view.entries());
}
bool merge(Attachment &context, Handle self, Handle source, ReturnSlot &out, bool pairs = true,
           bool unique = false)
{
    RootScope roots(context);
    ReturnSlot key(context), value(context), iterator(context), next(context);
    if (context.type_of(source) == context.builtin(CoreObject::DictType)) {
        DictView view(context, source.get(context));
        for (const auto &entry : view.entries()) {
            if (unique) {
                detail::dict_lookup_hash(context, self, roots.root(entry.key), entry.hash, out);
                if (failed(out))
                    return false;
                if (out.tag() == ResultTag::Ok) {
                    raise_error(context, CoreObject::TypeErrorType,
                                U"multiple values for keyword argument", out);
                    return false;
                }
            }
            detail::dict_store_hash(context, self, roots.root(entry.key), roots.root(entry.value),
                                    entry.hash, out);
            if (failed(out))
                return false;
        }
        return true;
    }
    make_str(context, U"keys", key);
    get_attribute(context, source, roots.root(key.value()), value);
    if (!failed(value)) {
        call(context, roots.root(value.value()), {}, value);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        get_iter(context, roots.root(value.value()), iterator);
        if (failed(iterator)) {
            out.raised(iterator.exception());
            return false;
        }
        auto keys = roots.root(iterator.value());
        for (;;) {
            const auto status = iter_next(context, keys, key);
            if (status == NextStatus::Done)
                return true;
            if (status == NextStatus::Raised) {
                out.raised(key.exception());
                return false;
            }
            const auto name = roots.root(key.value());
            std::int64_t saved_hash = 0;
            if (unique) {
                hash(context, name, out);
                if (failed(out))
                    return false;
                if (!int_fits_signed(context, out.value(), saved_hash))
                    throw std::logic_error("invalid hash result");
                detail::dict_lookup_hash(context, self, name, saved_hash, out);
                if (failed(out))
                    return false;
                if (out.tag() == ResultTag::Ok) {
                    raise_error(context, CoreObject::TypeErrorType,
                                U"multiple values for keyword argument", out);
                    return false;
                }
            }
            get_item(context, source, name, value);
            if (failed(value)) {
                out.raised(value.exception());
                return false;
            }
            if (unique)
                detail::dict_store_hash(context, self, name, roots.root(value.value()), saved_hash,
                                        out);
            else
                dict_store(context, self, name, roots.root(value.value()), out);
            if (failed(out))
                return false;
        }
    }
    if (!is_subtype(context, context.type_of(value.exception()),
                    context.builtin(CoreObject::AttributeErrorType))) {
        out.raised(value.exception());
        return false;
    }
    if (!pairs) {
        raise_error(context, CoreObject::TypeErrorType, U"argument after ** must be a mapping",
                    out);
        return false;
    }
    get_iter(context, source, iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return false;
    }
    auto entries = roots.root(iterator.value());
    for (;;) {
        const auto status = iter_next(context, entries, next);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(next.exception());
            return false;
        }
        RootScope pair(context);
        get_iter(context, pair.root(next.value()), iterator);
        if (failed(iterator)) {
            out.raised(iterator.exception());
            return false;
        }
        const auto items = pair.root(iterator.value());
        auto read = [&](ReturnSlot &item) {
            const auto result = iter_next(context, items, item);
            if (result == NextStatus::Raised)
                out.raised(item.exception());
            else if (result == NextStatus::Done)
                raise_error(context, CoreObject::ValueErrorType,
                            U"dictionary update sequence element has length less than 2", out);
            return result == NextStatus::Item;
        };
        if (!read(key) || !read(value))
            return false;
        const auto extra = iter_next(context, items, next);
        if (extra == NextStatus::Raised) {
            out.raised(next.exception());
            return false;
        }
        if (extra == NextStatus::Item) {
            raise_error(context, CoreObject::ValueErrorType,
                        U"dictionary update sequence element has length greater than 2", out);
            return false;
        }
        dict_store(context, self, pair.root(key.value()), pair.root(value.value()), out);
        if (failed(out))
            return false;
    }
}
void update(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const auto &sources = context.payload<TupleObject>(args[1]).items;
    if (sources.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"dict update expected at most one positional argument", out);
        return;
    }
    if (!sources.empty() && !merge(context, self, roots.root(sources[0]), out))
        return;
    if (!merge(context, self, roots.root(args[2]), out))
        return;
    out.ok(context.builtin(CoreObject::None));
}
template <bool Reflected>
void union_with(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    if (!context.has_layout<DictObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::DictType))) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(context);
    const auto left = roots.root(args[Reflected ? 1 : 0]);
    const auto right = roots.root(args[Reflected ? 0 : 1]);
    DictView view(context, left.get(context));
    ReturnSlot result(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          result);
    const auto target = roots.root(result.value());
    detail::copy_dictionary_entries(context, target.get(context), view.entries());
    if (merge(context, target, right, out))
        out.ok(target.get(context));
}
void inplace_union(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    if (merge(context, self, roots.root(args[1]), out))
        out.ok(self.get(context));
}
void create(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::DictType))) {
        raise_error(context, CoreObject::TypeErrorType, U"dict.__new__ requires a dict subtype",
                    out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<DictObject>(roots.root(args[0]), out);
}
template <bool Proxy>
void iterator(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Proxy>(context, args[0], out))
        return;
    RootScope roots(context);
    if constexpr (Proxy) {
        get_iter(context, roots.root(mapping<Proxy>(context, args[0])), out);
        return;
    }
    detail::make_dict_iterator(context, roots.root(mapping<Proxy>(context, args[0])),
                               DictViewKind::Keys, out);
}
template <DictViewKind Kind, bool Proxy>
void view(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Proxy>(context, args[0], out))
        return;
    if constexpr (Proxy) {
        proxy_forward(context, args,
                      Kind == DictViewKind::Keys     ? U"keys"
                      : Kind == DictViewKind::Values ? U"values"
                                                     : U"items",
                      out);
        return;
    }
    RootScope roots(context);
    const auto type =
        roots.root(context.builtin(Kind == DictViewKind::Keys     ? CoreObject::DictKeysType
                                   : Kind == DictViewKind::Values ? CoreObject::DictValuesType
                                                                  : CoreObject::DictItemsType));
    context.allocate_instance<DictViewObject>(type, out, mapping<Proxy>(context, args[0]), Kind);
}
void view_iterator(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<DictViewObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"view iterator requires a dictionary view",
                    out);
        return;
    }
    RootScope roots(context);
    const auto &view = context.payload<DictViewObject>(args[0]);
    detail::make_dict_iterator(context, roots.root(view.dictionary), view.kind, out);
}
void view_length(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<DictViewObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"view length requires a dictionary view",
                    out);
        return;
    }
    make_int(context,
             detail::dictionary_size(context, context.payload<DictViewObject>(args[0]).dictionary),
             out);
}
void proxy_length(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!proxy(context, args[0], out))
        return;
    RootScope roots(context);
    length(context, roots.root(context.payload<MappingProxyObject>(args[0]).mapping), out);
}
void next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<DictIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"dict iterator descriptor requires dict iterator", out);
        return;
    }
    RootScope roots(context);
    detail::next_dict_item(context, roots.root(args[0]), out);
}
void self_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<DictIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"dict iterator descriptor requires dict iterator", out);
        return;
    }
    out.ok(args[0]);
}
void equal(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!dictionary(context, args[0], out))
        return;
    if (!context.has_layout<DictObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::DictType))) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    if (args[0] == args[1]) {
        out.ok(context.builtin(CoreObject::True));
        return;
    }
    RootScope roots(context);
    const auto other = roots.root(args[1]);
    DictView view(context, args[0]);
    if (view.entries().size() != detail::dictionary_size(context, args[1])) {
        out.ok(context.builtin(CoreObject::False));
        return;
    }
    ReturnSlot value(context), comparison(context);
    for (const auto &entry : view.entries()) {
        detail::dict_lookup_hash(context, other, roots.root(entry.key), entry.hash, value);
        if (failed(value)) {
            out.raised(value.exception());
            return;
        }
        if (value.tag() == ResultTag::Empty) {
            out.ok(context.builtin(CoreObject::False));
            return;
        }
        container_equal(context, roots.root(entry.value), roots.root(value.value()), comparison);
        if (failed(comparison)) {
            out.raised(comparison.exception());
            return;
        }
        if (comparison.value() == context.builtin(CoreObject::False)) {
            out.ok(comparison.value());
            return;
        }
    }
    out.ok(context.builtin(CoreObject::True));
}
// CPython 3.12.14 dict_fromkeys: subclass construction and writes stay ordinary
// calls, so overridden constructors/setitem and their failures remain visible.
void fromkeys(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::DictType))) {
        raise_error(context, CoreObject::TypeErrorType, U"fromkeys requires a dict subtype", out);
        return;
    }
    ReturnSlot created(context), iterator(context), item(context), stored(context);
    call(context, roots.root(args[0]), {}, created);
    if (failed(created)) {
        out.raised(created.exception());
        return;
    }
    auto target = roots.root(created.value());
    get_iter(context, roots.root(args[1]), iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    auto sequence = roots.root(iterator.value());
    auto value = roots.root(args[2]);
    for (;;) {
        auto status = iter_next(context, sequence, item);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        auto key = roots.root(item.value());
        set_item(context, target, key, value, stored);
        if (failed(stored)) {
            out.raised(stored.exception());
            return;
        }
        roots.release(key);
    }
    out.ok(target.get(context));
}
} // namespace
void detail::dict_merge(Attachment &context, Handle self, Handle source, bool unique,
                        ReturnSlot &out)
{
    if (merge(context, self, source, out, false, unique))
        out.ok(context.builtin(CoreObject::None));
}
void detail::install_dictionaries(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"key", ParameterKind::PositionalOnly});
    auto set = pair;
    set.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto optional = pair;
    optional.parameters.push_back(
        {U"default", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    auto pop_args = pair;
    pop_args.varargs = U"default";
    auto install = [&](CoreObject type, std::u32string name, SignatureLayout signature,
                       NativeEntry entry) {
        install_native_attribute(context, core, at(type), std::move(name), std::move(signature),
                                 entry);
    };
    install(CoreObject::DictType, U"__new__", variadic, create);
    RootScope roots(context);
    ReturnSlot key(context), wrapper(context);
    auto new_function = *name_lookup(
        context, *TypeView(context, at(CoreObject::DictType)).dictionary(), U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  new_function);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(context, *TypeView(context, at(CoreObject::DictType)).dictionary(), key.value(),
               wrapper.value());
    auto fromkeys_signature = optional;
    fromkeys_signature.parameters[1].name = U"iterable";
    fromkeys_signature.parameters[2].name = U"value";
    install(CoreObject::DictType, U"fromkeys", fromkeys_signature, fromkeys);
    auto factory = roots.root(*name_lookup(
        context, *TypeView(context, at(CoreObject::DictType)).dictionary(), U"fromkeys"));
    context.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), wrapper,
                                                 factory.get(context));
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"fromkeys");
    name_store(context, *TypeView(context, at(CoreObject::DictType)).dictionary(), key.value(),
               wrapper.value());
    install(CoreObject::DictType, U"__init__", variadic, update);
    install(CoreObject::DictType, U"__getitem__", pair, getitem);
    install(CoreObject::DictType, U"__eq__", pair, equal);
    install(CoreObject::DictType, U"__contains__", pair, contains<false>);
    install(CoreObject::DictType, U"__setitem__", set, setitem);
    install(CoreObject::DictType, U"__delitem__", pair, delitem);
    install(CoreObject::DictType, U"get", optional, get<false>);
    install(CoreObject::DictType, U"setdefault", optional, setdefault);
    install(CoreObject::DictType, U"pop", pop_args, pop);
    install(CoreObject::DictType, U"clear", unary, clear);
    install(CoreObject::DictType, U"copy", unary, copy<false>);
    install(CoreObject::DictType, U"update", variadic, update);
    install(CoreObject::DictType, U"__or__", pair, union_with<false>);
    install(CoreObject::DictType, U"__ror__", pair, union_with<true>);
    install(CoreObject::DictType, U"__ior__", pair, inplace_union);
    install(CoreObject::DictType, U"__iter__", unary, iterator<false>);
    install(CoreObject::MappingProxyType, U"__getitem__", pair, proxy_getitem);
    auto proxy_constructor = pair;
    proxy_constructor.parameters[1].name = U"mapping";
    install(CoreObject::MappingProxyType, U"__new__", proxy_constructor, proxy_new);
    auto proxy_dictionary = *TypeView(context, at(CoreObject::MappingProxyType)).dictionary();
    auto proxy_factory = roots.root(*name_lookup(context, proxy_dictionary, U"__new__"));
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  proxy_factory.get(context));
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(context, proxy_dictionary, key.value(), wrapper.value());
    install(CoreObject::MappingProxyType, U"__eq__", pair, proxy_compare<CompareOp::Eq>);
    install(CoreObject::MappingProxyType, U"__ne__", pair, proxy_compare<CompareOp::Ne>);
    install(CoreObject::MappingProxyType, U"__lt__", pair, proxy_compare<CompareOp::Lt>);
    install(CoreObject::MappingProxyType, U"__le__", pair, proxy_compare<CompareOp::Le>);
    install(CoreObject::MappingProxyType, U"__gt__", pair, proxy_compare<CompareOp::Gt>);
    install(CoreObject::MappingProxyType, U"__ge__", pair, proxy_compare<CompareOp::Ge>);
    install(CoreObject::MappingProxyType, U"__contains__", pair, contains<true>);
    install(CoreObject::MappingProxyType, U"__len__", unary, proxy_length);
    install(CoreObject::MappingProxyType, U"__iter__", unary, iterator<true>);
    install(CoreObject::MappingProxyType, U"get", optional, get<true>);
    install(CoreObject::MappingProxyType, U"copy", unary, copy<true>);
    for (auto [type, keys, values, items] :
         {std::tuple{CoreObject::DictType, view<DictViewKind::Keys, false>,
                     view<DictViewKind::Values, false>, view<DictViewKind::Items, false>},
          {CoreObject::MappingProxyType, view<DictViewKind::Keys, true>,
           view<DictViewKind::Values, true>, view<DictViewKind::Items, true>}}) {
        install(type, U"keys", unary, keys);
        install(type, U"values", unary, values);
        install(type, U"items", unary, items);
    }
    for (auto type :
         {CoreObject::DictKeysType, CoreObject::DictValuesType, CoreObject::DictItemsType}) {
        install(type, U"__len__", unary, view_length);
        install(type, U"__iter__", unary, view_iterator);
    }
    for (auto type : {CoreObject::DictKeyIteratorType, CoreObject::DictValueIteratorType,
                      CoreObject::DictItemIteratorType}) {
        install(type, U"__iter__", unary, self_iter);
        install(type, U"__next__", unary, next);
    }
    for (auto type : {CoreObject::DictType, CoreObject::DictKeysType, CoreObject::DictValuesType,
                      CoreObject::DictItemsType}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
        name_store(context, *TypeView(context, at(type)).dictionary(), key.value(),
                   at(CoreObject::None).get(context));
    }
}
} // namespace cxxpython::core
