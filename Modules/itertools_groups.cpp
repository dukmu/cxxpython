// Adapted from CPython 3.12.14 Modules/itertoolsmodule.c, PSF licensed.
#include "internal/itertools_groups.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <atomic>
#include <limits>
#include <string_view>

namespace cxxpython::core::detail {
namespace {
struct GroupBy {
    ObjectHeader header;
    const Value iterator, keyfunc;
    AtomicGcEdge target, current_key, current_value;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    std::uint64_t generation = 0;
    GroupBy(InitialType type, Value input, Value key) : header(type), iterator(input), keyfunc(key)
    {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(iterator);
        t.visit(keyfunc);
        t.visit(target);
        t.visit(current_key);
        t.visit(current_value);
    }
};
struct Grouper {
    ObjectHeader header;
    const Value owner, key;
    const std::uint64_t generation;
    Grouper(InitialType type, Value parent, Value target, std::uint64_t serial)
        : header(type), owner(parent), key(target), generation(serial)
    {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(owner);
        t.visit(key);
    }
};
struct TeeSource {
    ObjectHeader header;
    const Value iterator;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    TeeSource(InitialType type, Value input) : header(type), iterator(input) {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(iterator);
    }
};
struct TeeData {
    ObjectHeader header;
    const Value source;
    AtomicGcEdge value, next;
    TeeData(InitialType type, Value state) : header(type), source(state) {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(source);
        t.visit(value);
        t.visit(next);
    }
};
struct Tee {
    ObjectHeader header;
    AtomicGcEdge cursor;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Tee(InitialType type, Value data) : header(type), cursor(data) {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(cursor);
    }
};
struct Lease {
    std::atomic_flag &flag;
    const bool acquired;
    explicit Lease(std::atomic_flag &f)
        : flag(f), acquired(!f.test_and_set(std::memory_order_acquire))
    {}
    ~Lease()
    {
        if (acquired)
            flag.clear(std::memory_order_release);
    }
};
bool acquire(Attachment &c, const Lease &lease, ReturnSlot &out)
{
    if (lease.acquired)
        return true;
    raise_error(c, CoreObject::RuntimeErrorType, U"cannot re-enter the tee iterator", out);
    return false;
}
template <class T> bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<T>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"invalid iterator receiver", out);
    return false;
}
template <class T>
void iter_self(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (receiver<T>(c, args[0], out))
        out.ok(args[0]);
}
void groupby_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(args[0]) ||
        c.payload<TypeObject>(args[0]).instance_layout.native_family !=
            LayoutDescriptor::of<GroupBy>().native_family) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid groupby type", out);
        return;
    }
    auto type = roots.root(args[0]), key = roots.root(args[2]);
    ReturnSlot iter(c);
    get_iter(c, roots.root(args[1]), iter);
    if (iter.tag() == ResultTag::Raised) {
        out.raised(iter.exception());
        return;
    }
    auto input = roots.root(iter.value());
    c.allocate_instance<GroupBy>(type, out, input.get(c), key.get(c));
}

// Called only with the owner's active lease held; the lease is not a mutex.
NextStatus group_step(Attachment &c, Handle owner, ReturnSlot &out)
{
    auto &group = c.payload<GroupBy>(owner);
    RootScope roots(c);
    ReturnSlot item(c), key(c);
    auto status = iter_next(c, roots.root(group.iterator), item);
    if (status != NextStatus::Item) {
        if (status == NextStatus::Raised)
            out.raised(item.exception());
        return status;
    }
    auto value = roots.root(item.value());
    if (group.keyfunc == c.builtin(CoreObject::None))
        key.ok(value.get(c));
    else {
        const Value args[] = {value.get(c)};
        call(c, roots.root(group.keyfunc), {args}, key);
        if (key.tag() == ResultTag::Raised) {
            out.raised(key.exception());
            return NextStatus::Raised;
        }
    }
    c.store(owner.get(c), group.current_key, key.value());
    c.store(owner.get(c), group.current_value, value.get(c));
    return NextStatus::Item;
}

bool group_equal(Attachment &c, Handle left, Handle right, bool &equal, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot comparison(c), result(c);
    container_equal(c, left, right, comparison);
    if (comparison.tag() == ResultTag::Raised) {
        out.raised(comparison.exception());
        return false;
    }
    truth(c, roots.root(comparison.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return false;
    }
    equal = result.value() == c.builtin(CoreObject::True);
    return true;
}

void groupby_next(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<GroupBy>(c, args[0], out))
        return;
    RootScope roots(c);
    auto owner = roots.root(args[0]);
    auto &group = c.payload<GroupBy>(owner);
    Lease lease(group.active);
    if (!acquire(c, lease, out))
        return;
    ++group.generation;
    ReturnSlot current(c), target(c), item(c);
    for (;;) {
        RootScope step_roots(c);
        c.load(owner.get(c), group.current_key, current);
        c.load(owner.get(c), group.target, target);
        if (current.tag() == ResultTag::Ok) {
            if (target.tag() == ResultTag::Empty)
                break;
            bool equal;
            if (!group_equal(c, step_roots.root(target.value()), step_roots.root(current.value()),
                             equal, out))
                return;
            if (!equal)
                break;
        }
        auto status = group_step(c, owner, item);
        if (status == NextStatus::Done) {
            stop_iteration(c, out);
            return;
        }
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
    }
    auto key = roots.root(current.value());
    c.store(owner.get(c), group.target, key.get(c));
    auto grouper_type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot child(c);
    c.allocate_instance<Grouper>(grouper_type, child, owner.get(c), key.get(c), group.generation);
    if (child.tag() == ResultTag::Raised) {
        out.raised(child.exception());
        return;
    }
    const Value pair[] = {key.get(c), child.value()};
    cxxpython::core::make_tuple(c, std::span<const Value>(pair, 2), out);
}

void grouper_next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Grouper>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &child = c.payload<Grouper>(self);
    auto owner = roots.root(child.owner);
    auto &group = c.payload<GroupBy>(owner);
    Lease lease(group.active);
    if (!acquire(c, lease, out))
        return;
    if (child.generation != group.generation) {
        stop_iteration(c, out);
        return;
    }
    ReturnSlot current(c), item(c);
    c.load(owner.get(c), group.current_value, item);
    if (item.tag() == ResultTag::Empty) {
        auto status = group_step(c, owner, out);
        if (status == NextStatus::Done) {
            stop_iteration(c, out);
            return;
        }
        if (status == NextStatus::Raised)
            return;
        c.load(owner.get(c), group.current_value, item);
    }
    c.load(owner.get(c), group.current_key, current);
    bool equal;
    if (!group_equal(c, roots.root(child.key), roots.root(current.value()), equal, out))
        return;
    if (!equal) {
        stop_iteration(c, out);
        return;
    }
    out.ok(item.value());
    c.clear(owner, group.current_key);
    c.clear(owner, group.current_value);
}

void grouper_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!receiver<GroupBy>(c, args[1], out))
        return;
    auto owner = roots.root(args[1]);
    auto key = roots.root(args[2]);
    auto &group = c.payload<GroupBy>(owner);
    Lease lease(group.active);
    if (!acquire(c, lease, out))
        return;
    ++group.generation;
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    c.allocate_instance<Grouper>(type, out, owner.get(c), key.get(c), group.generation);
}

void tee_copy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Tee>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot cursor(c);
    c.load(self.get(c), c.payload<Tee>(self).cursor, cursor);
    auto node = roots.root(cursor.value());
    c.allocate_instance<Tee>(roots.root(c.type_of(self)), out, node.get(c));
}

void tee_next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Tee>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &tee = c.payload<Tee>(self);
    Lease local(tee.active);
    if (!acquire(c, local, out))
        return;
    ReturnSlot cursor(c), value(c), next_node(c);
    c.load(self.get(c), tee.cursor, cursor);
    auto node = roots.root(cursor.value());
    auto &data = c.payload<TeeData>(node);
    c.load(node.get(c), data.value, value);
    if (value.tag() == ResultTag::Empty) {
        auto source = roots.root(data.source);
        auto &shared = c.payload<TeeSource>(source);
        Lease fetch(shared.active);
        if (!acquire(c, fetch, out))
            return;
        c.load(node.get(c), data.value, value);
        if (value.tag() == ResultTag::Empty) {
            auto data_type = roots.root(c.type_of(node));
            c.allocate_instance<TeeData>(data_type, next_node, source.get(c));
            if (next_node.tag() == ResultTag::Raised) {
                out.raised(next_node.exception());
                return;
            }
            auto status = iter_next(c, roots.root(shared.iterator), value);
            if (status == NextStatus::Done) {
                stop_iteration(c, out);
                return;
            }
            if (status == NextStatus::Raised) {
                out.raised(value.exception());
                return;
            }
            c.store(node.get(c), data.next, next_node.value());
            c.store(node.get(c), data.value, value.value());
        }
    }
    c.load(node.get(c), data.next, next_node);
    c.store(self.get(c), tee.cursor, next_node.value());
    out.ok(value.value());
}

void make_tee(Attachment &c, Handle input, Handle tee_type, Handle data_type, Handle source_type,
              ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot iter(c), shared(c), node(c);
    get_iter(c, input, iter);
    if (iter.tag() == ResultTag::Raised) {
        out.raised(iter.exception());
        return;
    }
    auto iterator = roots.root(iter.value());
    if (c.has_layout<Tee>(iterator.get(c))) {
        c.load(iterator.get(c), c.payload<Tee>(iterator).cursor, node);
    } else {
        c.allocate_instance<TeeSource>(source_type, shared, iterator.get(c));
        if (shared.tag() == ResultTag::Raised) {
            out.raised(shared.exception());
            return;
        }
        auto state = roots.root(shared.value());
        c.allocate_instance<TeeData>(data_type, node, state.get(c));
        if (node.tag() == ResultTag::Raised) {
            out.raised(node.exception());
            return;
        }
    }
    auto first = roots.root(node.value());
    c.allocate_instance<Tee>(tee_type, out, first.get(c));
}

void tee_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto &captures = c.payload<NativeFunctionObject>(function).captures;
    if (!c.has_layout<TypeObject>(args[0]) || !is_subtype(c, args[0], captures[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid _tee type", out);
        return;
    }
    make_tee(c, roots.root(args[1]), roots.root(args[0]), roots.root(captures[1]),
             roots.root(captures[2]), out);
}

void tee_function(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto input = roots.root(args[0]);
    ReturnSlot converted(c), item(c);
    index(c, roots.root(args[1]), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    std::int64_t count;
    if (!int_fits_signed(c, converted.value(), count)) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return;
    }
    if (count < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"n must be >= 0", out);
        return;
    }
    if (count == 0) {
        cxxpython::core::make_tuple(c, std::span<const Value>{}, out);
        return;
    }
    const auto &captures = c.payload<NativeFunctionObject>(function).captures;
    auto type = roots.root(captures[0]);
    auto data = roots.root(captures[1]);
    auto source = roots.root(captures[2]);
    make_tee(c, input, type, data, source, item);
    if (item.tag() == ResultTag::Raised) {
        out.raised(item.exception());
        return;
    }
    auto first = roots.root(item.value());
    std::vector<Value> result;
    result.reserve(static_cast<std::size_t>(count));
    result.push_back(first.get(c));
    for (std::int64_t i = 1; i < count; ++i) {
        ReturnSlot cursor(c);
        c.load(first.get(c), c.payload<Tee>(first).cursor, cursor);
        c.allocate_instance<Tee>(type, item, cursor.value());
        if (item.tag() == ResultTag::Raised) {
            out.raised(item.exception());
            return;
        }
        result.push_back(roots.root(item.value()).get(c));
    }
    cxxpython::core::make_tuple(c, std::span<const Value>(result), out);
}

void tee_data_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<ListObject>(args[2])) {
        raise_error(c, CoreObject::TypeErrorType, U"values must be a list", out);
        return;
    }
    auto iterator = roots.root(args[1]);
    auto values = roots.root(args[2]);
    auto next = roots.root(args[3]);
    std::vector<Value> items;
    list_snapshot(c, values, roots, items);
    bool supplied = next.get(c) != c.builtin(CoreObject::None);
    if (items.size() > 57 ||
        (supplied && (items.size() != 57 || !c.has_layout<TeeData>(next.get(c))))) {
        raise_error(c, CoreObject::ValueErrorType, U"Invalid arguments", out);
        return;
    }
    const auto &captures = c.payload<NativeFunctionObject>(function).captures;
    auto type = roots.root(captures[0]);
    auto source_type = roots.root(captures[1]);
    ReturnSlot source(c), tail(c), node(c);
    c.allocate_instance<TeeSource>(source_type, source, iterator.get(c));
    if (source.tag() == ResultTag::Raised) {
        out.raised(source.exception());
        return;
    }
    auto shared = roots.root(source.value());
    if (supplied)
        tail.ok(next.get(c));
    else {
        c.allocate_instance<TeeData>(type, tail, shared.get(c));
        if (tail.tag() == ResultTag::Raised) {
            out.raised(tail.exception());
            return;
        }
    }
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        c.allocate_instance<TeeData>(type, node, shared.get(c));
        if (node.tag() == ResultTag::Raised) {
            out.raised(node.exception());
            return;
        }
        auto current = roots.root(node.value());
        auto &payload = c.payload<TeeData>(current);
        c.store(current.get(c), payload.next, tail.value());
        c.store(current.get(c), payload.value, *it);
        tail.ok(current.get(c));
        roots.release(current);
    }
    out.ok(tail.value());
}

void state_anchor(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(c.builtin(CoreObject::None));
}
} // namespace

void initialize_itertools_groups(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot result(c), key(c), type_slot(c);
    std::vector<Value> previous;
    if (auto anchor = name_lookup(c, dictionary.get(c), U"_itertools_groups_state")) {
        auto saved = roots.root(*anchor);
        if (c.has_layout<NativeFunctionObject>(saved.get(c)) &&
            c.payload<NativeFunctionObject>(saved).entry == state_anchor)
            previous = c.payload<NativeFunctionObject>(saved).captures;
    }
    std::vector<Handle> core, types;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto make_type = [&](const char *name, LayoutDescriptor layout) {
        auto position = types.size();
        if (position < previous.size()) {
            types.push_back(roots.root(previous[position]));
            return true;
        }
        NativeTypeSpec spec{{name}, layout};
        spec.type.module = U"itertools";
        if (std::string_view(name) == "_tee")
            spec.type.slots = std::vector<std::u32string>{U"__weakref__"};
        create_native_type(c, spec, type_slot);
        if (type_slot.tag() == ResultTag::Raised) {
            out.raised(type_slot.exception());
            return false;
        }
        types.push_back(roots.root(type_slot.value()));
        return true;
    };
    if (!make_type("_tee_source", LayoutDescriptor::of<TeeSource>()) ||
        !make_type("_tee_dataobject", LayoutDescriptor::of<TeeData>()) ||
        !make_type("_tee", LayoutDescriptor::of<Tee>()) ||
        !make_type("_grouper", LayoutDescriptor::of<Grouper>()) ||
        !make_type("groupby", LayoutDescriptor::of<GroupBy>()))
        return;
    auto source = types[0], data = types[1], tee = types[2], grouper = types[3], groupby = types[4];
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"iterable", ParameterKind::PositionalOnly});
    auto install = [&](Handle type, std::u32string name, SignatureLayout signature,
                       NativeEntry entry, std::vector<Value> captures = {}) {
        install_native_attribute(c, core, type, std::move(name), std::move(signature), entry,
                                 std::move(captures));
    };
    auto wrap_new = [&](Handle type) {
        auto dict = *TypeView(c, type).dictionary();
        auto function = roots.root(*name_lookup(c, dict, U"__new__"));
        c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), result,
                                                function.get(c));
        auto wrapper = roots.root(result.value());
        make_str(c, U"__new__", key);
        name_store(c, dict, key.value(), wrapper.get(c));
    };
    if (previous.empty()) {
        auto group_signature = unary;
        group_signature.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
        group_signature.parameters.push_back(
            {U"key", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
        install(groupby, U"__new__", group_signature, groupby_new);
        wrap_new(groupby);
        install(groupby, U"__iter__", unary, iter_self<GroupBy>);
        install(groupby, U"__next__", unary, groupby_next, {grouper.get(c)});
        auto grouper_signature = unary;
        grouper_signature.parameters.push_back({U"parent", ParameterKind::PositionalOnly});
        grouper_signature.parameters.push_back({U"tgtkey", ParameterKind::PositionalOnly});
        install(grouper, U"__new__", grouper_signature, grouper_new, {grouper.get(c)});
        wrap_new(grouper);
        install(grouper, U"__iter__", unary, iter_self<Grouper>);
        install(grouper, U"__next__", unary, grouper_next);
        const std::vector<Value> tee_types{tee.get(c), data.get(c), source.get(c)};
        install(tee, U"__new__", pair, tee_new, tee_types);
        wrap_new(tee);
        install(tee, U"__iter__", unary, iter_self<Tee>);
        install(tee, U"__next__", unary, tee_next);
        install(tee, U"__copy__", unary, tee_copy);
        auto data_signature = unary;
        data_signature.parameters.push_back({U"iterable", ParameterKind::PositionalOnly});
        data_signature.parameters.push_back({U"values", ParameterKind::PositionalOnly});
        data_signature.parameters.push_back({U"next", ParameterKind::PositionalOnly});
        install(data, U"__new__", data_signature, tee_data_new, {data.get(c), source.get(c)});
        wrap_new(data);
        SignatureLayout empty;
        std::vector<Value> saved;
        for (auto type : types)
            saved.push_back(type.get(c));
        make_native_function(c, "_itertools_groups_state", empty, state_anchor, saved, result,
                             NativeFunctionBinding::Function);
        auto anchor = roots.root(result.value());
        make_str(c, U"_itertools_groups_state", key);
        dict_store(c, dictionary, roots.root(key.value()), anchor, out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    for (auto [name, type] : {std::pair{U"groupby", groupby},
                              {U"_grouper", grouper},
                              {U"_tee", tee},
                              {U"_tee_dataobject", data}}) {
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), type, out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    make_int(c, 2, result);
    auto two = roots.root(result.value());
    SignatureLayout tee_signature;
    tee_signature.parameters = {{U"iterable", ParameterKind::PositionalOnly},
                                {U"n", ParameterKind::PositionalOnly, two.get(c)}};
    const Value captures[] = {tee.get(c), data.get(c), source.get(c)};
    auto previous_tee = name_lookup(c, dictionary.get(c), U"tee");
    if (previous_tee && c.has_layout<NativeFunctionObject>(*previous_tee) &&
        c.payload<NativeFunctionObject>(*previous_tee).entry == tee_function)
        result.ok(*previous_tee);
    else
        make_native_function(c, "tee", tee_signature, tee_function, captures, result,
                             NativeFunctionBinding::Function);
    auto function = roots.root(result.value());
    make_str(c, U"tee", key);
    dict_store(c, dictionary, roots.root(key.value()), function, out);
    if (out.tag() != ResultTag::Raised)
        out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
