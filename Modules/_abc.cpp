// Based on CPython 3.12.14 Modules/_abc.c.
#include "internal/abc.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "cxxpython/type.hpp"
#include "cxxpython/weakrefobject.hpp"

#include <atomic>

namespace cxxpython::core::detail {
namespace {
struct AbcData {
    ObjectHeader header;
    AtomicGcEdge registry, positive, negative;
    std::atomic<std::uint64_t> counter{0}, version{0};
    explicit AbcData(InitialType type) : header(type) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(registry);
        tracer.visit(positive);
        tracer.visit(negative);
    }
};
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void boolean(Attachment &ctx, bool value, ReturnSlot &out)
{
    out.ok(ctx.builtin(value ? CoreObject::True : CoreObject::False));
}
bool type_check(Attachment &ctx, Value value)
{
    return ctx.has_layout<TypeObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.type_type());
}
void attribute(Attachment &ctx, Handle object, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, name, key);
    get_attribute(ctx, object, roots.root(key.value()), out);
}
bool missing(Attachment &ctx, ReturnSlot &out)
{
    return failed(out) && is_subtype(ctx, ctx.type_of(out.exception()),
                                     ctx.builtin(CoreObject::AttributeErrorType));
}
void set_attr(Attachment &ctx, Handle object, const char32_t *name, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, name, key);
    set_attribute(ctx, object, roots.root(key.value()), value, out);
}
void method(Attachment &ctx, Handle object, const char32_t *name, std::span<const Value> args,
            ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot function(ctx);
    attribute(ctx, object, name, function);
    if (failed(function)) {
        out.raised(function.exception());
        return;
    }
    call(ctx, roots.root(function.value()), {args, {}}, out);
}
void abstract(Attachment &ctx, Handle object, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot value(ctx);
    attribute(ctx, object, U"__isabstractmethod__", value);
    if (missing(ctx, value)) {
        boolean(ctx, false, out);
        return;
    }
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    truth(ctx, roots.root(value.value()), out);
}
bool impl(Attachment &ctx, Handle function, Handle cls, ReturnSlot &out)
{
    attribute(ctx, cls, U"_abc_impl", out);
    if (failed(out))
        return false;
    auto expected = ctx.payload<NativeFunctionObject>(function).captures[1];
    if (ctx.type_of(out.value()) != expected) {
        raise_error(ctx, CoreObject::TypeErrorType, U"_abc_impl is set to a wrong type", out);
        return false;
    }
    return true;
}
void discard_dead(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto set = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    auto table = roots.root(ctx.payload<SetObject>(set).table);
    dict_erase(ctx, table, roots.root(args[0]), out);
    if (!failed(out))
        out.ok(ctx.builtin(CoreObject::None));
}
void weak_ref(Attachment &ctx, Handle target, std::optional<Handle> set, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot callback(ctx), candidate(ctx);
    auto ref_type = roots.root(ctx.builtin(CoreObject::WeakRefType));
    if (!set) {
        ctx.weakref_lookup(target, ref_type, out);
        if (out.tag() != ResultTag::Empty)
            return;
    }
    Value callback_value = ctx.builtin(CoreObject::None);
    if (set) {
        SignatureLayout signature;
        signature.parameters = {{U"ref", ParameterKind::PositionalOnly}};
        const Value capture[] = {set->get(ctx)};
        make_native_function(ctx, "_abc_remove", signature, discard_dead, capture, callback,
                             NativeFunctionBinding::Function);
        callback_value = callback.value();
    }
    ctx.allocate_instance<WeakRefObject>(ref_type, candidate, target.get(ctx), callback_value);
    auto weak = roots.root(candidate.value());
    if (!set)
        ctx.clear(weak, ctx.payload<WeakRefObject>(weak).callback);
    ctx.register_weakref(weak, target, !set, out);
}
void ensure_set(Attachment &ctx, Handle data, AtomicGcEdge &edge, ReturnSlot &out)
{
    RootScope roots(ctx);
    for (;;) {
        ctx.load(data.get(ctx), edge, out);
        if (out.tag() == ResultTag::Ok)
            return;
        ReturnSlot candidate(ctx);
        make_set(ctx, {}, candidate);
        if (failed(candidate)) {
            out.raised(candidate.exception());
            return;
        }
        if (ctx.compare_exchange(data.get(ctx), edge, {}, candidate.value())) {
            out.ok(candidate.value());
            return;
        }
    }
}
void set_contains_ref(Attachment &ctx, Handle set, Handle target, ReturnSlot &out)
{
    RootScope roots(ctx);
    if (dictionary_size(ctx, ctx.payload<SetObject>(set).table) == 0) {
        boolean(ctx, false, out);
        return;
    }
    if (!ctx.supports_weakrefs(target)) {
        boolean(ctx, false, out);
        return;
    }
    ReturnSlot ref(ctx);
    weak_ref(ctx, target, {}, ref);
    if (failed(ref)) {
        out.raised(ref.exception());
        return;
    }
    auto table = roots.root(ctx.payload<SetObject>(set).table);
    dict_lookup(ctx, table, roots.root(ref.value()), out);
    if (!failed(out))
        boolean(ctx, out.tag() == ResultTag::Ok, out);
}
void contains(Attachment &ctx, Handle data, AtomicGcEdge &edge, Handle target, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot set(ctx);
    ctx.load(data.get(ctx), edge, set);
    if (set.tag() == ResultTag::Empty) {
        boolean(ctx, false, out);
        return;
    }
    set_contains_ref(ctx, roots.root(set.value()), target, out);
}
void add_to_set(Attachment &ctx, Handle set, Handle target, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot ref(ctx);
    weak_ref(ctx, target, set, ref);
    if (failed(ref)) {
        out.raised(ref.exception());
        return;
    }
    set_add(ctx, set, roots.root(ref.value()), out);
}
void add(Attachment &ctx, Handle data, AtomicGcEdge &edge, Handle target, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot set(ctx);
    ensure_set(ctx, data, edge, set);
    if (failed(set)) {
        out.raised(set.exception());
        return;
    }
    add_to_set(ctx, roots.root(set.value()), target, out);
}
std::optional<std::uint64_t> negative_version(Attachment &ctx, Value snapshot)
{
    const auto &items = ctx.payload<TupleObject>(snapshot).items;
    std::int64_t version;
    if (items.size() != 2 || !int_fits_signed(ctx, items[0], version) || version < 0)
        return {};
    return static_cast<std::uint64_t>(version);
}
void negative_prepare(Attachment &ctx, Handle data, std::uint64_t epoch, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot version(ctx);
    make_int_decimal(ctx, std::to_string(epoch), version);
    auto number = roots.root(version.value());
    auto &edge = ctx.payload<AbcData>(data).negative;
    for (;;) {
        ReturnSlot old(ctx), next(ctx), set(ctx);
        ctx.load(data.get(ctx), edge, old);
        if (old.tag() == ResultTag::Ok) {
            auto current = negative_version(ctx, old.value());
            if (current && *current == epoch) {
                out.ok(ctx.payload<TupleObject>(old.value()).items[1]);
                return;
            }
            if (current && *current > epoch) {
                out.reset(ctx);
                return;
            }
        }
        make_set(ctx, {}, set);
        if (failed(set)) {
            out.raised(set.exception());
            return;
        }
        const Value entries[] = {number.get(ctx), set.value()};
        make_tuple(ctx, entries, next);
        auto expected =
            old.tag() == ResultTag::Ok ? std::optional<Value>(old.value()) : std::optional<Value>();
        if (ctx.compare_exchange(data.get(ctx), edge, expected, next.value())) {
            ctx.payload<AbcData>(data).version.store(epoch);
            out.ok(set.value());
            return;
        }
    }
}
void token(Attachment &ctx, Handle function, std::span<const Value>, ReturnSlot &out)
{
    auto state = ctx.payload<NativeFunctionObject>(function).captures[0];
    make_int_decimal(ctx, std::to_string(ctx.payload<AbcData>(state).counter.load()), out);
}
void reset_registry(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot data(ctx);
    if (!impl(ctx, function, roots.root(args[0]), data)) {
        out.raised(data.exception());
        return;
    }
    auto owner = roots.root(data.value());
    ctx.clear(owner, ctx.payload<AbcData>(owner).registry);
    out.ok(ctx.builtin(CoreObject::None));
}
void reset_caches(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot data(ctx);
    if (!impl(ctx, function, roots.root(args[0]), data)) {
        out.raised(data.exception());
        return;
    }
    auto owner = roots.root(data.value());
    auto &payload = ctx.payload<AbcData>(owner);
    ctx.clear(owner, payload.positive);
    ctx.clear(owner, payload.negative);
    out.ok(ctx.builtin(CoreObject::None));
}
void subclass_of(Attachment &ctx, Handle child, Handle parent, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto hook = type_attribute(ctx, ctx.type_of(parent), U"__subclasscheck__");
    if (ctx.type_of(parent) != ctx.type_type() && hook) {
        const Value args[] = {child.get(ctx)};
        ReturnSlot result(ctx);
        invoke_special(ctx, parent, roots.root(*hook), {args, {}}, result);
        if (failed(result))
            out.raised(result.exception());
        else
            truth(ctx, roots.root(result.value()), out);
        return;
    }
    if (!type_check(ctx, parent.get(ctx)) || !type_check(ctx, child.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"issubclass() arguments must be classes", out);
        return;
    }
    boolean(ctx, is_subtype(ctx, child, parent), out);
}
void subclasscheck(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    auto child = roots.root(args[1]);
    if (!type_check(ctx, child.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"issubclass() arg 1 must be a class", out);
        return;
    }
    ReturnSlot result(ctx);
    if (!impl(ctx, function, cls, result)) {
        out.raised(result.exception());
        return;
    }
    auto data = roots.root(result.value());
    auto &payload = ctx.payload<AbcData>(data);
    auto state = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    auto &counter = ctx.payload<AbcData>(state).counter;
    contains(ctx, data, payload.positive, child, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == ctx.builtin(CoreObject::True)) {
        boolean(ctx, true, out);
        return;
    }
    auto epoch = counter.load();
    ReturnSlot negative(ctx);
    negative_prepare(ctx, data, epoch, negative);
    if (failed(negative)) {
        out.raised(negative.exception());
        return;
    }
    if (negative.tag() == ResultTag::Ok) {
        set_contains_ref(ctx, roots.root(negative.value()), child, result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        if (result.value() == ctx.builtin(CoreObject::True) && counter.load() == epoch) {
            boolean(ctx, false, out);
            return;
        }
    }
    auto cache = [&](bool positive) {
        if (positive)
            add(ctx, data, payload.positive, child, result);
        else if (negative.tag() == ResultTag::Ok && counter.load() == epoch)
            add_to_set(ctx, roots.root(negative.value()), child, result);
        else
            result.ok(ctx.builtin(CoreObject::None));
        if (failed(result)) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    const Value argument[] = {child.get(ctx)};
    method(ctx, cls, U"__subclasshook__", argument, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto answer = result.value();
    if (answer == ctx.builtin(CoreObject::True)) {
        if (!cache(true))
            return;
        boolean(ctx, true, out);
        return;
    }
    if (answer == ctx.builtin(CoreObject::False)) {
        if (!cache(false))
            return;
        boolean(ctx, false, out);
        return;
    }
    if (answer != ctx.builtin(CoreObject::NotImplemented)) {
        raise_error(ctx, CoreObject::AssertionErrorType,
                    U"__subclasshook__ must return either False, True, or NotImplemented", out);
        return;
    }
    if (is_subtype(ctx, child, cls)) {
        if (!cache(true))
            return;
        boolean(ctx, true, out);
        return;
    }
    contains(ctx, data, payload.registry, child, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == ctx.builtin(CoreObject::True)) {
        boolean(ctx, true, out);
        return;
    }
    ReturnSlot snapshot(ctx), target(ctx);
    ctx.load(data.get(ctx), payload.registry, snapshot);
    if (snapshot.tag() == ResultTag::Ok) {
        auto set = roots.root(snapshot.value());
        DictView view(ctx, ctx.payload<SetObject>(set).table);
        for (const auto &entry : view.entries()) {
            auto ref = entry.key;
            ctx.load(ref, ctx.payload<WeakRefObject>(ref).target, target);
            if (target.tag() != ResultTag::Ok)
                continue;
            auto parent = roots.root(target.value());
            subclass_of(ctx, child, parent, result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            if (result.value() == ctx.builtin(CoreObject::True)) {
                if (!cache(true))
                    return;
                boolean(ctx, true, out);
                return;
            }
        }
    }
    method(ctx, cls, U"__subclasses__", {}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (!ctx.has_layout<ListObject>(result.value()) ||
        !is_subtype(ctx, ctx.type_of(result.value()), ctx.builtin(CoreObject::ListType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"__subclasses__() must return a list", out);
        return;
    }
    std::vector<Value> children;
    list_snapshot(ctx, roots.root(result.value()), roots, children);
    for (auto item : children) {
        subclass_of(ctx, child, roots.root(item), result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        if (result.value() == ctx.builtin(CoreObject::True)) {
            if (!cache(true))
                return;
            boolean(ctx, true, out);
            return;
        }
    }
    if (!cache(false))
        return;
    boolean(ctx, false, out);
}
void set_collection_kind(Attachment &ctx, Handle type, CollectionKind kind)
{
    RootScope roots(ctx);
    std::vector<Handle> pending{roots.root(type.get(ctx))};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto current = pending[i];
        auto &data = ctx.payload<TypeObject>(current);
        if (data.immutable || data.collection.exchange(kind, std::memory_order_acq_rel) == kind)
            continue;
        ReturnSlot children(ctx);
        direct_subclasses(ctx, current, children);
        std::vector<Value> values;
        list_snapshot(ctx, roots.root(children.value()), roots, values);
        for (auto child : values)
            pending.push_back(roots.root(child));
    }
}
void register_class(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    auto child = roots.root(args[1]);
    if (!type_check(ctx, child.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"Can only register classes", out);
        return;
    }
    ReturnSlot result(ctx);
    subclass_of(ctx, child, cls, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == ctx.builtin(CoreObject::True)) {
        out.ok(child.get(ctx));
        return;
    }
    subclass_of(ctx, cls, child, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == ctx.builtin(CoreObject::True)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"Refusing to create an inheritance cycle",
                    out);
        return;
    }
    if (!impl(ctx, function, cls, result)) {
        out.raised(result.exception());
        return;
    }
    auto data = roots.root(result.value());
    add(ctx, data, ctx.payload<AbcData>(data).registry, child, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto state = ctx.payload<NativeFunctionObject>(function).captures[0];
    ctx.payload<AbcData>(state).counter.fetch_add(1);
    const auto kind = ctx.payload<TypeObject>(cls).collection.load(std::memory_order_acquire);
    if (kind != CollectionKind::None)
        set_collection_kind(ctx, child, kind);
    out.ok(child.get(ctx));
}
void instancecheck(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    auto instance = roots.root(args[1]);
    ReturnSlot data(ctx), result(ctx);
    if (!impl(ctx, function, cls, data)) {
        out.raised(data.exception());
        return;
    }
    auto owner = roots.root(data.value());
    attribute(ctx, instance, U"__class__", result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto apparent = roots.root(result.value());
    contains(ctx, owner, ctx.payload<AbcData>(owner).positive, apparent, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == ctx.builtin(CoreObject::True)) {
        boolean(ctx, true, out);
        return;
    }
    Value argument[] = {apparent.get(ctx)};
    method(ctx, cls, U"__subclasscheck__", argument, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (apparent.get(ctx) == ctx.type_of(instance)) {
        out.ok(result.value());
        return;
    }
    truth(ctx, roots.root(result.value()), out);
    if (failed(out) || out.value() == ctx.builtin(CoreObject::True))
        return;
    argument[0] = ctx.type_of(instance);
    method(ctx, cls, U"__subclasscheck__", argument, out);
}
void initialize_class(Attachment &ctx, Handle function, std::span<const Value> args,
                      ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    ReturnSlot result(ctx), item(ctx), key(ctx);
    make_set(ctx, {}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto names = roots.root(result.value());
    attribute(ctx, cls, U"__dict__", result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    method(ctx, roots.root(result.value()), U"items", {}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    get_iter(ctx, roots.root(result.value()), result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto iterator = roots.root(result.value());
    std::vector<Value> direct;
    for (;;) {
        auto status = iter_next(ctx, iterator, item);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        direct.push_back(roots.root(item.value()).get(ctx));
    }
    for (auto entry : direct) {
        if (!ctx.has_layout<TupleObject>(entry) ||
            ctx.payload<TupleObject>(entry).items.size() != 2) {
            raise_error(ctx, CoreObject::TypeErrorType, U"items() returned invalid item", out);
            return;
        }
        auto pair = roots.root(entry);
        auto name = roots.root(ctx.payload<TupleObject>(pair).items[0]);
        abstract(ctx, roots.root(ctx.payload<TupleObject>(pair).items[1]), result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        if (result.value() == ctx.builtin(CoreObject::True)) {
            set_add(ctx, names, name, result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
        }
    }
    attribute(ctx, cls, U"__bases__", result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (!ctx.has_layout<TupleObject>(result.value())) {
        raise_error(ctx, CoreObject::TypeErrorType, U"__bases__ is not tuple", out);
        return;
    }
    auto bases = roots.root(result.value());
    for (auto base : ctx.payload<TupleObject>(bases).items) {
        attribute(ctx, roots.root(base), U"__abstractmethods__", result);
        if (missing(ctx, result))
            continue;
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        get_iter(ctx, roots.root(result.value()), result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        auto inherited = roots.root(result.value());
        for (;;) {
            auto status = iter_next(ctx, inherited, key);
            if (status == NextStatus::Done)
                break;
            if (status == NextStatus::Raised) {
                out.raised(key.exception());
                return;
            }
            auto name = roots.root(key.value());
            get_attribute(ctx, cls, name, result);
            if (missing(ctx, result))
                continue;
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            abstract(ctx, roots.root(result.value()), result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            if (result.value() == ctx.builtin(CoreObject::True)) {
                set_add(ctx, names, name, result);
                if (failed(result)) {
                    out.raised(result.exception());
                    return;
                }
            }
        }
    }
    const Value values[] = {names.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::FrozenSetType)), {values, {}}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    set_attr(ctx, cls, U"__abstractmethods__", roots.root(result.value()), result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto captures = ctx.payload<NativeFunctionObject>(function).captures;
    auto state = roots.root(captures[0]);
    auto data_type = roots.root(captures[1]);
    ctx.allocate_instance<AbcData>(data_type, result);
    auto data = roots.root(result.value());
    ctx.payload<AbcData>(data).version.store(ctx.payload<AbcData>(state).counter.load());
    set_attr(ctx, cls, U"_abc_impl", data, out);
    if (failed(out))
        return;
    auto dictionary = TypeView(ctx, cls).dictionary();
    if (dictionary) {
        auto owner = roots.root(*dictionary);
        auto flags = name_lookup(ctx, owner.get(ctx), U"__abc_tpflags__");
        if (flags) {
            auto value = roots.root(*flags);
            CollectionKind kind = CollectionKind::None;
            if (ctx.type_of(value) == ctx.builtin(CoreObject::IntType)) {
                std::int64_t bits;
                if (!int_fits_signed(ctx, value.get(ctx), bits)) {
                    raise_error(ctx, CoreObject::OverflowErrorType,
                                U"Python int too large to convert to C long", out);
                    return;
                }
                if ((bits & 96) == 96) {
                    raise_error(ctx, CoreObject::TypeErrorType,
                                U"__abc_tpflags__ cannot be both Py_TPFLAGS_SEQUENCE and "
                                U"Py_TPFLAGS_MAPPING",
                                out);
                    return;
                }
                kind = (bits & 32)   ? CollectionKind::Sequence
                       : (bits & 64) ? CollectionKind::Mapping
                                     : CollectionKind::None;
            }
            type_namespace_change(ctx, cls.get(ctx), U"__abc_tpflags__", {}, {});
            if (kind != CollectionKind::None)
                ctx.payload<TypeObject>(cls).collection.store(kind, std::memory_order_release);
        }
    }
    out.ok(ctx.builtin(CoreObject::None));
}
void dump(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot result(ctx), snapshot(ctx);
    if (!impl(ctx, function, roots.root(args[0]), result)) {
        out.raised(result.exception());
        return;
    }
    auto data = roots.root(result.value());
    auto &payload = ctx.payload<AbcData>(data);
    std::vector<Value> output;
    for (auto edge : {&AbcData::registry, &AbcData::positive, &AbcData::negative}) {
        ctx.load(data.get(ctx), payload.*edge, snapshot);
        std::vector<Value> refs;
        if (snapshot.tag() == ResultTag::Ok) {
            auto set = edge == &AbcData::negative
                           ? ctx.payload<TupleObject>(snapshot.value()).items[1]
                           : snapshot.value();
            auto owner = roots.root(set);
            DictView view(ctx, ctx.payload<SetObject>(owner).table);
            for (const auto &entry : view.entries())
                refs.push_back(roots.root(entry.key).get(ctx));
        }
        make_set(ctx, refs, result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        output.push_back(roots.root(result.value()).get(ctx));
    }
    auto version = snapshot.tag() == ResultTag::Ok ? negative_version(ctx, snapshot.value())
                                                   : std::optional<std::uint64_t>{};
    make_int_decimal(ctx, std::to_string(version.value_or(payload.version.load())), result);
    output.push_back(roots.root(result.value()).get(ctx));
    make_tuple(ctx, std::span<const Value>(output), out);
}
} // namespace

void initialize_abc(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    NativeTypeSpec spec{{"_abc_data"}, LayoutDescriptor::of<AbcData>()};
    spec.type.module = U"_abc";
    ReturnSlot result(ctx), type_slot(ctx), state_slot(ctx);
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    auto previous = name_lookup(ctx, dictionary.get(ctx), U"get_cache_token");
    if (previous && ctx.has_layout<NativeFunctionObject>(*previous) &&
        ctx.payload<NativeFunctionObject>(*previous).entry == token &&
        ctx.payload<NativeFunctionObject>(*previous).captures.size() == 2) {
        const auto &captures = ctx.payload<NativeFunctionObject>(*previous).captures;
        state_slot.ok(captures[0]);
        type_slot.ok(captures[1]);
    } else {
        create_native_type(ctx, spec, type_slot);
        if (failed(type_slot)) {
            out.raised(type_slot.exception());
            return;
        }
        ctx.allocate_instance<AbcData>(roots.root(type_slot.value()), state_slot);
        if (failed(state_slot)) {
            out.raised(state_slot.exception());
            return;
        }
    }
    auto type = roots.root(type_slot.value());
    auto state = roots.root(state_slot.value());
    const Value captures[] = {state.get(ctx), type.get(ctx)};
    auto publish = [&](const char *name, const char32_t *key, NativeEntry entry, int count) {
        SignatureLayout signature;
        if (count >= 1)
            signature.parameters.push_back({U"self", ParameterKind::PositionalOnly});
        if (count >= 2)
            signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
        make_native_function(ctx, name, signature, entry, captures, result,
                             NativeFunctionBinding::Function);
        auto function = roots.root(result.value());
        make_str(ctx, key, result);
        dict_store(ctx, dictionary, roots.root(result.value()), function, out);
        return !failed(out);
    };
    if (!publish("get_cache_token", U"get_cache_token", token, 0) ||
        !publish("_abc_init", U"_abc_init", initialize_class, 1) ||
        !publish("_abc_register", U"_abc_register", register_class, 2) ||
        !publish("_abc_instancecheck", U"_abc_instancecheck", instancecheck, 2) ||
        !publish("_abc_subclasscheck", U"_abc_subclasscheck", subclasscheck, 2) ||
        !publish("_get_dump", U"_get_dump", dump, 1) ||
        !publish("_reset_registry", U"_reset_registry", reset_registry, 1) ||
        !publish("_reset_caches", U"_reset_caches", reset_caches, 1))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
