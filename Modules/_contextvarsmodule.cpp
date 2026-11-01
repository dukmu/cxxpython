// Context variable semantics follow CPython 3.12's Python/context.c.
#include "internal/contextvars.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <atomic>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cxxpython::core::detail {
namespace {
struct ContextObject {
    ObjectHeader header;
    AtomicGcEdge values;
    std::mutex mutex;
    std::atomic<bool> entered{false};
    ContextObject(InitialType type, Value snapshot) : header(type), values(snapshot) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(values);
    }
};

struct ContextVarObject {
    ObjectHeader header;
    const Value name, default_value, context_type, token_type, missing, omitted;
    ContextVarObject(InitialType type, Value n, Value d, Value c, Value t, Value m, Value o)
        : header(type), name(n), default_value(d), context_type(c), token_type(t), missing(m),
          omitted(o)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto value : {name, default_value, context_type, token_type, missing, omitted})
            tracer.visit(value);
    }
};

struct TokenObject {
    ObjectHeader header;
    const Value var, context, old_value, missing;
    const bool had_value;
    std::atomic<bool> used{false};
    TokenObject(InitialType type, Value v, Value c, Value old, Value m, bool had)
        : header(type), var(v), context(c), old_value(old), missing(m), had_value(had)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto value : {var, context, old_value, missing})
            tracer.visit(value);
    }
};

struct MissingObject {
    ObjectHeader header;
    explicit MissingObject(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

enum class ViewKind { Keys, Values, Items };
struct ContextIterator {
    ObjectHeader header;
    const Value values;
    const ViewKind kind;
    std::atomic<std::size_t> cursor{0};
    ContextIterator(InitialType type, Value snapshot, ViewKind view)
        : header(type), values(snapshot), kind(view)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(values);
    }
};

bool failed(ReturnSlot &slot)
{
    return slot.tag() == ResultTag::Raised;
}

template <class F> auto locked(Attachment &c, ContextObject &context, F &&operation)
{
    for (;;) {
        std::unique_lock lock(context.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint commit(c);
            return operation();
        }
        NativeWait wait(c);
        context.mutex.lock();
        context.mutex.unlock();
    }
}

template <class T> bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<T>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"invalid contextvars method receiver", out);
    return false;
}

void raise_with_value(Attachment &c, CoreObject type, Value value, ReturnSlot &out)
{
    RootScope roots(c);
    auto arg = roots.root(value);
    ReturnSlot result(c);
    const Value args[] = {arg.get(c)};
    call(c, roots.root(c.builtin(type)), {args}, result);
    if (failed(result))
        out.raised(result.exception());
    else
        out.raised(result.value());
}

void snapshot(Attachment &c, Handle context, ReturnSlot &out)
{
    c.load(context.get(c), c.payload<ContextObject>(context).values, out);
}

std::size_t find_key(Attachment &c, Value tuple, Value key)
{
    const auto &items = c.payload<TupleObject>(tuple).items;
    for (std::size_t i = 0; i < items.size(); i += 2)
        if (items[i] == key)
            return i;
    return items.size();
}

// The source tuple remains rooted throughout allocation of its replacement.
void replacement(Attachment &c, Handle old, Value key, Value value, bool erase, ReturnSlot &out)
{
    const auto &items = c.payload<TupleObject>(old).items;
    std::vector<Value> values(items.begin(), items.end());
    const auto index = find_key(c, old.get(c), key);
    if (index < values.size()) {
        if (erase)
            values.erase(values.begin() + index, values.begin() + index + 2);
        else
            values[index + 1] = value;
    } else if (!erase) {
        values.push_back(key);
        values.push_back(value);
    }
    cxxpython::core::make_tuple(c, values, out);
}

void current_context(Attachment &c, Handle type, ReturnSlot &out)
{
    c.load_context(out);
    if (out.tag() != ResultTag::Empty)
        return;
    RootScope roots(c);
    ReturnSlot empty(c);
    make_tuple(c, {}, empty);
    if (failed(empty)) {
        out.raised(empty.exception());
        return;
    }
    c.allocate_instance<ContextObject>(type, out, empty.value());
    if (!failed(out))
        c.set_context(roots.root(out.value()));
}

void context_new(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != c.payload<NativeFunctionObject>(function).captures[0]) {
        raise_error(c, CoreObject::TypeErrorType, U"Context.__new__ requires Context", out);
        return;
    }
    RootScope roots(c);
    auto type = roots.root(args[0]);
    ReturnSlot empty(c);
    make_tuple(c, {}, empty);
    if (failed(empty)) {
        out.raised(empty.exception());
        return;
    }
    c.allocate_instance<ContextObject>(type, out, empty.value());
}

void var_new(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    const auto &captures = c.payload<NativeFunctionObject>(function).captures;
    if (args[0] != captures[4]) {
        raise_error(c, CoreObject::TypeErrorType, U"ContextVar.__new__ requires ContextVar", out);
        return;
    }
    if (!c.has_layout<StrObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"ContextVar name must be a str", out);
        return;
    }
    RootScope roots(c);
    auto type = roots.root(args[0]);
    c.allocate_instance<ContextVarObject>(type, out, args[1], args[2], captures[0], captures[1],
                                          captures[2], captures[3]);
}

void forbidden_new(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, U"cannot create '_contextvars.Token' instances", out);
}

void var_get(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextVarObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto var = roots.root(args[0]);
    const auto &v = c.payload<ContextVarObject>(var);
    ReturnSlot current(c), values(c);
    c.load_context(current);
    if (current.tag() != ResultTag::Empty) {
        snapshot(c, roots.root(current.value()), values);
        const auto index = find_key(c, values.value(), var.get(c));
        const auto &items = c.payload<TupleObject>(values.value()).items;
        if (index < items.size()) {
            out.ok(items[index + 1]);
            return;
        }
    }
    if (args[1] != v.omitted)
        out.ok(args[1]);
    else if (v.default_value != v.omitted)
        out.ok(v.default_value);
    else
        raise_with_value(c, CoreObject::LookupErrorType, var.get(c), out);
}

void var_set(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextVarObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto var = roots.root(args[0]);
    auto value = roots.root(args[1]);
    const auto &v = c.payload<ContextVarObject>(var);
    auto context_type = roots.root(v.context_type);
    auto token_type = roots.root(v.token_type);
    auto missing = roots.root(v.missing);
    ReturnSlot current(c);
    current_context(c, context_type, current);
    if (failed(current)) {
        out.raised(current.exception());
        return;
    }
    auto context = roots.root(current.value());
    auto &state = c.payload<ContextObject>(context);
    for (;;) {
        RootScope attempt(c);
        ReturnSlot old(c), next(c), token(c), observed(c);
        snapshot(c, context, old);
        auto old_root = attempt.root(old.value());
        const auto index = find_key(c, old.value(), var.get(c));
        const auto &items = c.payload<TupleObject>(old_root).items;
        const bool had_value = index < items.size();
        auto previous = attempt.root(had_value ? items[index + 1] : missing.get(c));
        replacement(c, old_root, var.get(c), value.get(c), false, next);
        if (failed(next)) {
            out.raised(next.exception());
            return;
        }
        c.allocate_instance<TokenObject>(token_type, token, var.get(c), context.get(c),
                                         previous.get(c), missing.get(c), had_value);
        if (failed(token)) {
            out.raised(token.exception());
            return;
        }
        const bool committed = locked(c, state, [&] {
            snapshot(c, context, observed);
            if (observed.value() != old_root.get(c))
                return false;
            c.store(context.get(c), state.values, next.value());
            return true;
        });
        if (committed) {
            out.ok(token.value());
            return;
        }
    }
}

void var_reset(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextVarObject>(c, args[0], out))
        return;
    if (!c.has_layout<TokenObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"expected a Token", out);
        return;
    }
    RootScope roots(c);
    auto var = roots.root(args[0]);
    auto token = roots.root(args[1]);
    auto &t = c.payload<TokenObject>(token);
    if (t.used.load(std::memory_order_acquire)) {
        raise_error(c, CoreObject::RuntimeErrorType, U"Token has already been used", out);
        return;
    }
    if (t.var != var.get(c)) {
        raise_error(c, CoreObject::ValueErrorType, U"Token was created by a different ContextVar",
                    out);
        return;
    }
    ReturnSlot current(c);
    c.load_context(current);
    if (current.tag() == ResultTag::Empty || current.value() != t.context) {
        raise_error(c, CoreObject::ValueErrorType, U"Token was created in a different Context",
                    out);
        return;
    }
    auto context = roots.root(current.value());
    auto &state = c.payload<ContextObject>(context);
    enum class Commit { Retry, Used, Done };
    for (;;) {
        RootScope attempt(c);
        ReturnSlot old(c), next(c), observed(c);
        snapshot(c, context, old);
        auto old_root = attempt.root(old.value());
        replacement(c, old_root, var.get(c), t.old_value, !t.had_value, next);
        if (failed(next)) {
            out.raised(next.exception());
            return;
        }
        const auto result = locked(c, state, [&] {
            if (t.used.load(std::memory_order_acquire))
                return Commit::Used;
            snapshot(c, context, observed);
            if (observed.value() != old_root.get(c))
                return Commit::Retry;
            c.store(context.get(c), state.values, next.value());
            t.used.store(true, std::memory_order_release);
            return Commit::Done;
        });
        if (result == Commit::Used) {
            raise_error(c, CoreObject::RuntimeErrorType, U"Token has already been used", out);
            return;
        }
        if (result == Commit::Done) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
    }
}

void var_name(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver<ContextVarObject>(c, self, out))
        out.ok(c.payload<ContextVarObject>(self).name);
}
void token_var(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver<TokenObject>(c, self, out))
        out.ok(c.payload<TokenObject>(self).var);
}
void token_old(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver<TokenObject>(c, self, out))
        out.ok(c.payload<TokenObject>(self).old_value);
}

std::u32string address(Value value)
{
    char buffer[2 * sizeof(std::uintptr_t)];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer),
                                reinterpret_cast<std::uintptr_t>(value.object()), 16);
    return U"0x" + std::u32string(buffer, result.ptr);
}

void var_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextVarObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    const auto &var = c.payload<ContextVarObject>(self);
    ReturnSlot text(c);
    repr(c, roots.root(var.name), text);
    if (failed(text)) {
        out.raised(text.exception());
        return;
    }
    std::u32string result = U"<ContextVar name=" + c.payload<StrObject>(text.value()).value;
    if (var.default_value != var.omitted) {
        repr(c, roots.root(var.default_value), text);
        if (failed(text)) {
            out.raised(text.exception());
            return;
        }
        result += U" default=" + c.payload<StrObject>(text.value()).value;
    }
    result += U" at " + address(self.get(c)) + U">";
    make_str(c, std::move(result), out);
}

void token_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<TokenObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    const auto &token = c.payload<TokenObject>(self);
    std::u32string result =
        token.used.load(std::memory_order_acquire) ? U"<Token used var=" : U"<Token var=";
    ReturnSlot text(c);
    repr(c, roots.root(token.var), text);
    if (failed(text)) {
        out.raised(text.exception());
        return;
    }
    result += c.payload<StrObject>(text.value()).value;
    result += U" at " + address(self.get(c)) + U">";
    make_str(c, std::move(result), out);
}

template <bool Negate>
void context_compare(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    if (!c.has_layout<ContextObject>(args[1])) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto left = roots.root(args[0]);
    auto right = roots.root(args[1]);
    ReturnSlot a(c), b(c), compared(c), boolean(c);
    snapshot(c, left, a);
    snapshot(c, right, b);
    auto left_values = roots.root(a.value());
    auto right_values = roots.root(b.value());
    const auto &items = c.payload<TupleObject>(left_values).items;
    const auto &other = c.payload<TupleObject>(right_values).items;
    bool equal = items.size() == other.size();
    for (std::size_t i = 0; equal && i < items.size(); i += 2) {
        const auto index = find_key(c, right_values.get(c), items[i]);
        if (index == other.size()) {
            equal = false;
        } else if (items[i + 1] != other[index + 1]) {
            rich_compare(c, roots.root(items[i + 1]), roots.root(other[index + 1]), CompareOp::Eq,
                         compared);
            if (failed(compared)) {
                out.raised(compared.exception());
                return;
            }
            truth(c, roots.root(compared.value()), boolean);
            if (failed(boolean)) {
                out.raised(boolean.exception());
                return;
            }
            equal = boolean.value() == c.builtin(CoreObject::True);
        }
    }
    out.ok(c.builtin(equal != Negate ? CoreObject::True : CoreObject::False));
}

void context_copy(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto type = roots.root(c.type_of(self));
    ReturnSlot values(c);
    snapshot(c, self, values);
    c.allocate_instance<ContextObject>(type, out, values.value());
}

void copy_current(Attachment &c, Value function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot current(c), values(c);
    current_context(c, type, current);
    if (failed(current)) {
        out.raised(current.exception());
        return;
    }
    snapshot(c, roots.root(current.value()), values);
    c.allocate_instance<ContextObject>(type, out, values.value());
}

void context_run(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto callable = roots.root(args[1]);
    auto positional = roots.root(args[2]);
    auto keywords = roots.root(args[3]);
    auto &context = c.payload<ContextObject>(self);
    ReturnSlot previous(c);
    c.load_context(previous);
    std::optional<Handle> previous_handle;
    if (previous.tag() != ResultTag::Empty)
        previous_handle = roots.root(previous.value());
    bool expected = false;
    if (!context.entered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        raise_error(c, CoreObject::RuntimeErrorType, U"Context is already entered", out);
        return;
    }
    struct Restore {
        Attachment &c;
        ContextObject &context;
        std::optional<Handle> previous;
        ~Restore()
        {
            c.set_context(previous);
            context.entered.store(false, std::memory_order_release);
        }
    } restore{c, context, previous_handle};
    c.set_context(self);
    DictView view(c, keywords.get(c));
    std::vector<KeywordArg> named;
    for (const auto &entry : view.entries())
        named.push_back({entry.key, entry.value});
    call(c, callable, {c.payload<TupleObject>(positional).items, named}, out);
}

enum class MapOp { Get, Item, Contains };
template <MapOp op>
void context_lookup(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    if (!c.has_layout<ContextVarObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"ContextVar key was expected", out);
        return;
    }
    RootScope roots(c);
    ReturnSlot values(c);
    snapshot(c, roots.root(args[0]), values);
    const auto &items = c.payload<TupleObject>(values.value()).items;
    auto index = find_key(c, values.value(), args[1]);
    if constexpr (op == MapOp::Contains)
        out.ok(c.builtin(index < items.size() ? CoreObject::True : CoreObject::False));
    else if (index < items.size())
        out.ok(items[index + 1]);
    else if constexpr (op == MapOp::Get)
        out.ok(args[2]);
    else
        raise_with_value(c, CoreObject::KeyErrorType, args[1], out);
}

void context_length(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot values(c);
    snapshot(c, roots.root(args[0]), values);
    make_int(c, static_cast<std::int64_t>(c.payload<TupleObject>(values.value()).items.size() / 2),
             out);
}

template <ViewKind Kind>
void context_view(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot values(c);
    snapshot(c, roots.root(args[0]), values);
    c.allocate_instance<ContextIterator>(type, out, values.value(), Kind);
}

void view_iter(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (receiver<ContextIterator>(c, args[0], out))
        out.ok(args[0]);
}

void view_length(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextIterator>(c, args[0], out))
        return;
    const auto &view = c.payload<ContextIterator>(args[0]);
    make_int(c, static_cast<std::int64_t>(c.payload<TupleObject>(view.values).items.size() / 2),
             out);
}

void view_next(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<ContextIterator>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &view = c.payload<ContextIterator>(self);
    const auto &values = c.payload<TupleObject>(view.values).items;
    const auto index = view.cursor.fetch_add(1, std::memory_order_relaxed);
    if (index >= values.size() / 2) {
        stop_iteration(c, out);
        return;
    }
    if (view.kind == ViewKind::Keys)
        out.ok(values[2 * index]);
    else if (view.kind == ViewKind::Values)
        out.ok(values[2 * index + 1]);
    else {
        const Value pair[] = {values[2 * index], values[2 * index + 1]};
        make_tuple(c, pair, out);
    }
}

void missing_repr(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    make_str(c, U"<Token.MISSING>", out);
}
} // namespace

void initialize_contextvars(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    std::vector<Handle> core;
    core.reserve(core_count);
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject object) { return core[static_cast<std::size_t>(object)]; };
    ReturnSlot context_slot(c), var_slot(c), token_slot(c), missing_type_slot(c), keys_slot(c),
        values_slot(c), items_slot(c), temp(c);
    auto create_type = [&](const char *name, LayoutDescriptor layout, ReturnSlot &slot,
                           const char32_t *module_name = U"_contextvars") {
        NativeTypeSpec spec{{name}, layout};
        spec.type.module = module_name;
        spec.type.slots = std::vector<std::u32string>{};
        spec.type.subclassable = false;
        create_native_type(c, spec, slot);
    };
    create_type("Context", LayoutDescriptor::of<ContextObject>(), context_slot);
    if (failed(context_slot)) {
        out.raised(context_slot.exception());
        return;
    }
    auto context_type = roots.root(context_slot.value());
    create_type("ContextVar", LayoutDescriptor::of<ContextVarObject>(), var_slot);
    if (failed(var_slot)) {
        out.raised(var_slot.exception());
        return;
    }
    auto var_type = roots.root(var_slot.value());
    create_type("Token", LayoutDescriptor::of<TokenObject>(), token_slot);
    if (failed(token_slot)) {
        out.raised(token_slot.exception());
        return;
    }
    auto token_type = roots.root(token_slot.value());
    create_type("Token.MISSING", LayoutDescriptor::of<MissingObject>(), missing_type_slot);
    if (failed(missing_type_slot)) {
        out.raised(missing_type_slot.exception());
        return;
    }
    auto missing_type = roots.root(missing_type_slot.value());
    create_type("keys", LayoutDescriptor::of<ContextIterator>(), keys_slot, U"builtins");
    if (failed(keys_slot)) {
        out.raised(keys_slot.exception());
        return;
    }
    auto keys_type = roots.root(keys_slot.value());
    create_type("values", LayoutDescriptor::of<ContextIterator>(), values_slot, U"builtins");
    if (failed(values_slot)) {
        out.raised(values_slot.exception());
        return;
    }
    auto values_type = roots.root(values_slot.value());
    create_type("items", LayoutDescriptor::of<ContextIterator>(), items_slot, U"builtins");
    if (failed(items_slot)) {
        out.raised(items_slot.exception());
        return;
    }
    auto items_type = roots.root(items_slot.value());
    c.allocate_instance<MissingObject>(missing_type, temp);
    if (failed(temp)) {
        out.raised(temp.exception());
        return;
    }
    auto missing = roots.root(temp.value());
    c.allocate_instance<MissingObject>(missing_type, temp);
    if (failed(temp)) {
        out.raised(temp.exception());
        return;
    }
    auto omitted = roots.root(temp.value());

    auto install = [&](Handle type, std::u32string name, SignatureLayout signature,
                       NativeEntry entry, std::vector<Value> captures = {}) {
        install_native_attribute(c, core, type, std::move(name), std::move(signature), entry,
                                 std::move(captures));
    };
    auto constructor = [&](Handle type, SignatureLayout signature, NativeEntry entry,
                           std::vector<Value> captures = {}) {
        install(type, U"__new__", std::move(signature), entry, std::move(captures));
        auto type_dict = roots.root(*TypeView(c, type).dictionary());
        auto function = roots.root(*name_lookup(c, type_dict.get(c), U"__new__"));
        ReturnSlot wrapper(c), key(c);
        c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                function.get(c));
        make_str(c, U"__new__", key);
        name_store(c, type_dict.get(c), key.value(), wrapper.value());
    };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    for (auto type : {keys_type, values_type, items_type}) {
        install(type, U"__iter__", unary, view_iter);
        install(type, U"__next__", unary, view_next);
        install(type, U"__len__", unary, view_length);
    }
    auto context_constructor = unary;
    constructor(context_type, context_constructor, context_new, {context_type.get(c)});
    SignatureLayout var_constructor;
    var_constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                                  {U"name", ParameterKind::PositionalOnly},
                                  {U"default", ParameterKind::KeywordOnly, omitted.get(c)}};
    constructor(
        var_type, var_constructor, var_new,
        {context_type.get(c), token_type.get(c), missing.get(c), omitted.get(c), var_type.get(c)});
    auto token_constructor = unary;
    token_constructor.varargs = U"args";
    token_constructor.varkwargs = U"kwargs";
    constructor(token_type, token_constructor, forbidden_new);

    auto get = pair;
    get.parameters[1].default_value = omitted.get(c);
    install(var_type, U"get", get, var_get);
    install(var_type, U"set", pair, var_set);
    install(var_type, U"reset", pair, var_reset);
    install(var_type, U"__repr__", unary, var_repr);
    install(token_type, U"__repr__", unary, token_repr);
    install_native_field(c, core, var_type, U"name", var_name);
    install_native_field(c, core, token_type, U"var", token_var);
    install_native_field(c, core, token_type, U"old_value", token_old);
    install(missing_type, U"__repr__", unary, missing_repr);
    auto token_dict = roots.root(*TypeView(c, token_type).dictionary());
    make_str(c, U"MISSING", temp);
    name_store(c, token_dict.get(c), temp.value(), missing.get(c));

    auto run = pair;
    run.parameters[1].name = U"callable";
    run.varargs = U"args";
    run.varkwargs = U"kwargs";
    install(context_type, U"run", run, context_run);
    install(context_type, U"copy", unary, context_copy);
    install(context_type, U"__eq__", pair, context_compare<false>);
    install(context_type, U"__ne__", pair, context_compare<true>);
    auto context_dict = roots.root(*TypeView(c, context_type).dictionary());
    make_str(c, U"__hash__", temp);
    name_store(c, context_dict.get(c), temp.value(), at(CoreObject::None).get(c));
    install(context_type, U"__len__", unary, context_length);
    install(context_type, U"__iter__", unary, context_view<ViewKind::Keys>, {keys_type.get(c)});
    install(context_type, U"__contains__", pair, context_lookup<MapOp::Contains>);
    install(context_type, U"__getitem__", pair, context_lookup<MapOp::Item>);
    auto mapping_get = pair;
    mapping_get.parameters.push_back(
        {U"default", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
    install(context_type, U"get", mapping_get, context_lookup<MapOp::Get>);
    install(context_type, U"keys", unary, context_view<ViewKind::Keys>, {keys_type.get(c)});
    install(context_type, U"values", unary, context_view<ViewKind::Values>, {values_type.get(c)});
    install(context_type, U"items", unary, context_view<ViewKind::Items>, {items_type.get(c)});

    auto publish = [&](const char32_t *name, Handle value) {
        ReturnSlot key(c);
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), value, out);
        return !failed(out);
    };
    if (!publish(U"Context", context_type) || !publish(U"ContextVar", var_type) ||
        !publish(U"Token", token_type))
        return;
    const Value captures[] = {context_type.get(c)};
    make_native_function(c, "copy_context", {}, copy_current, captures, temp,
                         NativeFunctionBinding::Function);
    if (failed(temp)) {
        out.raised(temp.exception());
        return;
    }
    if (!publish(U"copy_context", roots.root(temp.value())))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
