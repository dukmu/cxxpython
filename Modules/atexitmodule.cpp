// Callback semantics follow CPython 3.12.14 Modules/atexitmodule.c (PSF license).
#include "internal/atexitmodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/errors.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <thread>

namespace cxxpython::core::detail {
namespace {
struct ExitState {
    ObjectHeader header;
    struct Node {
        std::uint64_t id;
        Value function, args, kwargs;
        bool active;
    };
    struct Storage {
        std::mutex mutex;
        std::condition_variable ready;
        std::list<Node> callbacks;
        std::uint64_t next_id = 0;
        std::thread::id runner;
        std::size_t run_depth = 0;
    };
    const NativeOwner<Storage> storage;
    explicit ExitState(InitialType type) : header(type), storage(std::make_unique<Storage>()) {}
    void trace(Tracer &tracer) const noexcept
    {
        for (const auto &node : storage->callbacks) {
            if (node.active) {
                tracer.visit(node.function);
                tracer.visit(node.args);
                tracer.visit(node.kwargs);
            }
        }
    }
    std::size_t external_bytes() const noexcept
    {
        return sizeof(Storage) + storage->callbacks.size() * (sizeof(Node) + 2 * sizeof(void *));
    }
};

template <class F> decltype(auto) locked(Attachment &c, ExitState::Storage &s, F &&f)
{
    for (;;) {
        std::unique_lock lock(s.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return f();
        }
        NativeWait wait(c);
        s.mutex.lock();
        s.mutex.unlock();
    }
}

class RunClaim {
    Attachment &c;
    ExitState::Storage &s;

  public:
    RunClaim(Attachment &context, ExitState::Storage &state) : c(context), s(state)
    {
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                NativeWait wait(c);
                std::unique_lock parked(s.mutex);
                continue;
            }
            if (s.run_depth == 0 || s.runner == std::this_thread::get_id()) {
                s.runner = std::this_thread::get_id();
                ++s.run_depth;
                return;
            }
            lock.unlock();
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
            s.ready.wait(parked, [&] { return s.run_depth == 0; });
        }
    }
    ~RunClaim()
    {
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                if (--s.run_depth == 0) {
                    s.runner = {};
                    s.ready.notify_all();
                }
                return;
            }
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
        }
    }
};

Handle owner(Attachment &c, Value function, RootScope &roots)
{
    return roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
}

void register_callback(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    if (!type_attribute(c, c.type_of(a[0]), U"__call__")) {
        raise_error(c, CoreObject::TypeErrorType, U"the first argument must be callable", out);
        return;
    }
    RootScope roots(c);
    auto self = owner(c, function, roots);
    auto callback = roots.root(a[0]);
    auto args = roots.root(a[1]);
    auto kwargs = roots.root(a[2]);
    auto &state = c.payload<ExitState>(self);
    std::list<ExitState::Node> prepared;
    prepared.push_back({0, callback.get(c), args.get(c), kwargs.get(c), true});
    locked(c, *state.storage, [&] {
        prepared.front().id = ++state.storage->next_id;
        state.storage->callbacks.splice(state.storage->callbacks.end(), prepared);
        c.account_external(self, state.external_bytes());
    });
    out.ok(callback.get(c));
}

void unregister_callback(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = owner(c, function, roots);
    auto target = roots.root(a[0]);
    auto &state = c.payload<ExitState>(self);
    auto &s = *state.storage;
    std::deque<ReturnSlot> callbacks;
    std::vector<std::uint64_t> ids;
    std::size_t count = 0;
    for (;;) {
        auto size = locked(c, s, [&] { return s.callbacks.size(); });
        while (callbacks.size() < size)
            callbacks.emplace_back(c);
        ids.resize(size);
        if (locked(c, s, [&] {
                if (ids.size() < s.callbacks.size())
                    return false;
                count = 0;
                for (const auto &node : s.callbacks) {
                    if (!node.active)
                        continue;
                    callbacks[count].ok(node.function);
                    ids[count++] = node.id;
                }
                return true;
            }))
            break;
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto candidate = roots.root(callbacks[i].value());
        bool present = locked(c, s, [&] {
            return std::any_of(s.callbacks.begin(), s.callbacks.end(),
                               [&](const auto &node) { return node.id == ids[i] && node.active; });
        });
        if (!present)
            continue;
        bool equal = candidate.get(c) == target.get(c);
        if (!equal) {
            ReturnSlot compared(c), truth_value(c);
            rich_compare(c, candidate, target, CompareOp::Eq, compared);
            if (compared.tag() == ResultTag::Raised) {
                out.raised(compared.exception());
                return;
            }
            truth(c, roots.root(compared.value()), truth_value);
            if (truth_value.tag() == ResultTag::Raised) {
                out.raised(truth_value.exception());
                return;
            }
            equal = truth_value.value() != c.builtin(CoreObject::False);
        }
        if (equal)
            locked(c, s, [&] {
                for (auto &node : s.callbacks) {
                    if (node.id == ids[i] && node.active) {
                        // Retain the count slot without retaining any callback objects.
                        node.active = false;
                        node.function = node.args = node.kwargs = c.builtin(CoreObject::None);
                        break;
                    }
                }
            });
    }
    out.ok(c.builtin(CoreObject::None));
}

void clear_state(Attachment &c, Handle self)
{
    auto &state = c.payload<ExitState>(self);
    std::list<ExitState::Node> discarded;
    locked(c, *state.storage, [&] {
        discarded.splice(discarded.end(), state.storage->callbacks);
        c.account_external(self, state.external_bytes());
    });
}
void clear(Attachment &c, Value function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = owner(c, function, roots);
    clear_state(c, self);
    out.ok(c.builtin(CoreObject::None));
}
void count(Attachment &c, Value function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = owner(c, function, roots);
    auto &s = *c.payload<ExitState>(self).storage;
    auto size = locked(c, s, [&] { return s.callbacks.size(); });
    make_int(c, static_cast<std::int64_t>(size), out);
}

void run(Attachment &c, Handle self, ReturnSlot &out)
{
    RootScope roots(c);
    auto &state = c.payload<ExitState>(self);
    auto &s = *state.storage;
    RunClaim claim(c, s);
    auto last = locked(c, s, [&] { return s.next_id; });
    for (;;) {
        ReturnSlot fn(c), args(c), kwargs(c);
        std::list<ExitState::Node> removed;
        bool found = locked(c, s, [&] {
            auto it = s.callbacks.end();
            while (it != s.callbacks.begin()) {
                --it;
                if (it->id > last)
                    continue;
                if (it->active) {
                    fn.ok(it->function);
                    args.ok(it->args);
                    kwargs.ok(it->kwargs);
                }
                removed.splice(removed.end(), s.callbacks, it);
                c.account_external(self, state.external_bytes());
                return true;
            }
            return false;
        });
        if (!found)
            break;
        if (!removed.front().active)
            continue;
        auto callback = roots.root(fn.value());
        auto positional = roots.root(args.value());
        auto keywords = roots.root(kwargs.value());
        DictView view(c, keywords.get(c));
        std::vector<KeywordArg> named;
        for (const auto &entry : view.entries())
            named.push_back({entry.key, entry.value});
        ReturnSlot result(c);
        call(c, callback, {c.payload<TupleObject>(positional).items, named}, result);
        if (result.tag() == ResultTag::Raised) {
            ReturnSlot message(c);
            make_str(c, U"Exception ignored in atexit callback", message);
            write_unraisable(c, roots.root(result.exception()), callback,
                             roots.root(message.value()));
        }
    }
    clear_state(c, self);
    out.ok(c.builtin(CoreObject::None));
}
void run_method(Attachment &c, Value function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    run(c, owner(c, function, roots), out);
}

} // namespace

void initialize_atexit(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot state(c), key(c), actual(c), value(c);
    c.load_exit_callbacks(actual);
    if (actual.tag() == ResultTag::Empty) {
        c.allocate<ExitState>(state);
        if (state.tag() == ResultTag::Raised) {
            out.raised(state.exception());
            return;
        }
        c.publish_exit_callbacks(roots.root(state.value()), actual);
    }
    auto shared = roots.root(actual.value());
    if (!c.has_layout<ExitState>(shared.get(c))) {
        raise_error(c, CoreObject::RuntimeErrorType, U"invalid atexit interpreter state", out);
        return;
    }
    SignatureLayout unary;
    unary.parameters = {{U"func", ParameterKind::PositionalOnly}};
    auto registration = unary;
    registration.varargs = U"args";
    registration.varkwargs = U"kwargs";
    auto install = [&](const char *name, SignatureLayout signature, NativeEntry entry) {
        Value capture = shared.get(c);
        make_native_function(c, name, std::move(signature), entry, {&capture, 1}, value,
                             NativeFunctionBinding::Function);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return false;
        }
        auto function = roots.root(value.value());
        make_str(c, std::u32string(name, name + std::char_traits<char>::length(name)), key);
        dict_store(c, dictionary, roots.root(key.value()), function, out);
        return out.tag() != ResultTag::Raised;
    };
    if (!install("register", registration, register_callback) ||
        !install("unregister", unary, unregister_callback) || !install("_clear", {}, clear) ||
        !install("_ncallbacks", {}, count) || !install("_run_exitfuncs", {}, run_method))
        return;
    out.ok(c.builtin(CoreObject::None));
}

void run_exitfuncs(Attachment &c, ReturnSlot &out)
{
    out.reset(c);
    RootScope roots(c);
    ReturnSlot state(c);
    c.load_exit_callbacks(state);
    if (state.tag() == ResultTag::Empty) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    if (!c.has_layout<ExitState>(state.value())) {
        raise_error(c, CoreObject::RuntimeErrorType, U"invalid atexit interpreter state", out);
        return;
    }
    run(c, roots.root(state.value()), out);
}
} // namespace cxxpython::core::detail
