// Signal policy follows CPython 3.12.14 Modules/signalmodule.c (PSF license).
#include "internal/signalmodule.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "internal/iomodule.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <csignal>
#include <limits>
#include <mutex>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace cxxpython::core::detail {
namespace {
// POSIX dispositions belong to the process. No interpreter lock covers execution.
// The async handler touches only lock-free, process-lifetime notification cells.
struct ProcessSignals {
    std::atomic<std::uint64_t> owner{0};
    std::array<std::atomic<std::uint64_t>, NSIG> pending{};
    std::atomic<bool> tripped{false};
    std::uint64_t next = 0;
};
constinit ProcessSignals process;
std::mutex registration;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::is_trivially_destructible_v<ProcessSignals>);

void notify_signal(int number) noexcept
{
    const auto owner = process.owner.load(std::memory_order_acquire);
    if (!owner || number <= 0 || number >= NSIG)
        return;
    auto old = process.pending[number].load(std::memory_order_relaxed);
    while (old < owner && !process.pending[number].compare_exchange_weak(
                              old, owner, std::memory_order_release, std::memory_order_relaxed)) {
    }
    process.tripped.store(true, std::memory_order_release);
}

bool main_thread()
{
    return syscall(SYS_gettid) == getpid();
}

struct SignalNative {
    std::uint64_t generation = 0;
    std::array<struct sigaction, NSIG> previous{}, installed{};
    std::array<bool, NSIG> changed{};
    void restore() noexcept
    {
        if (!generation)
            return;
        std::lock_guard guard(registration);
        if (process.owner.load(std::memory_order_relaxed) == generation) {
            process.owner.store(0, std::memory_order_release);
            for (int number = 1; number < NSIG; ++number) {
                struct sigaction current {};
                if (changed[number] && sigaction(number, nullptr, &current) == 0 &&
                    current.sa_handler == installed[number].sa_handler)
                    sigaction(number, &previous[number], nullptr);
            }
        }
        generation = 0;
    }
    ~SignalNative()
    {
        restore();
    }
};

struct SignalState {
    ObjectHeader header;
    SignalNative native;
    std::array<AtomicGcEdge, NSIG> handlers;
    AtomicGcEdge default_handler;
    explicit SignalState(InitialType head) : header(head) {}
    void trace(Tracer &tracer) const noexcept
    {
        for (const auto &handler : handlers)
            tracer.visit(handler);
        tracer.visit(default_handler);
    }
};

bool number(Attachment &c, Value value, int &result, ReturnSlot &out, bool validate = true)
{
    RootScope roots(c);
    index(c, roots.root(value), out);
    if (out.tag() == ResultTag::Raised)
        return false;
    std::int64_t integer;
    if (!int_fits_signed(c, out.value(), integer) || integer < INT_MIN || integer > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C long",
                    out);
        return false;
    }
    if (validate && (integer < 1 || integer >= NSIG)) {
        raise_error(c, CoreObject::ValueErrorType, U"signal number out of range", out);
        return false;
    }
    result = static_cast<int>(integer);
    return true;
}

Handle state_of(Attachment &c, Handle function, RootScope &roots)
{
    return roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
}

void observe_dispositions(Attachment &c, Handle owner)
{
    auto &state = c.payload<SignalState>(owner);
    for (int signal = 1; signal < NSIG; ++signal) {
        auto &previous = state.native.previous[signal];
        Value handler = c.builtin(CoreObject::None);
        if (sigaction(signal, nullptr, &previous) == 0) {
            if (previous.sa_handler == SIG_DFL)
                handler = c.builtin(CoreObject::IntZero);
            else if (previous.sa_handler == SIG_IGN)
                handler = c.builtin(CoreObject::IntOne);
        }
        c.store(owner.get(c), state.handlers[signal], handler);
    }
}

bool claim(Attachment &c, Handle owner, ReturnSlot &out)
{
    auto &state = c.payload<SignalState>(owner);
    if (state.native.generation)
        return true;
    RootScope roots(c);
    ReturnSlot default_handler(c);
    c.load(owner.get(c), state.default_handler, default_handler);
    int error = 0;
    bool occupied = false;
    {
        std::lock_guard guard(registration);
        NoSafepoint commit(c);
        occupied = process.owner.load(std::memory_order_relaxed) != 0;
        if (!occupied) {
            if (process.next == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("signal ownership generations exhausted");
            state.native.generation = ++process.next;
            observe_dispositions(c, owner);
            process.owner.store(state.native.generation, std::memory_order_release);
            for (int signal : {SIGINT, SIGPIPE}) {
                if (state.native.previous[signal].sa_handler != SIG_DFL)
                    continue;
                struct sigaction action {};
                sigemptyset(&action.sa_mask);
                action.sa_handler = signal == SIGINT ? notify_signal : SIG_IGN;
                if (sigaction(signal, &action, nullptr) != 0) {
                    error = errno;
                    break;
                }
                state.native.changed[signal] = true;
                state.native.installed[signal] = action;
                c.store(owner.get(c), state.handlers[signal],
                        signal == SIGINT ? default_handler.value() : c.builtin(CoreObject::IntOne));
            }
        }
    }
    if (occupied) {
        raise_error(c, CoreObject::RuntimeErrorType,
                    U"another interpreter owns process signal handlers", out);
        return false;
    }
    if (error) {
        state.native.restore();
        io_os_error(c, error, out);
        return false;
    }
    return true;
}

void default_interrupt(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    int signal;
    if (!number(c, args[0], signal, out, false))
        return;
    RootScope roots(c);
    call(c, roots.root(c.builtin(CoreObject::KeyboardInterruptType)), {}, out);
    if (out.tag() != ResultTag::Raised)
        out.raised(out.value());
}

void get_handler(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    int signal;
    if (!number(c, args[0], signal, out))
        return;
    RootScope roots(c);
    auto owner = state_of(c, function, roots);
    c.load(owner.get(c), c.payload<SignalState>(owner).handlers[signal], out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(CoreObject::None));
}

void set_handler(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    int signal;
    if (!number(c, args[0], signal, out))
        return;
    if (!main_thread()) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"signal only works in main thread of the main interpreter", out);
        return;
    }
    RootScope roots(c);
    auto owner = state_of(c, function, roots), handler = roots.root(args[1]);
    bool callable = bool(type_attribute(c, c.type_of(handler), U"__call__"));
    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    if (callable)
        action.sa_handler = notify_signal;
    else {
        std::int64_t kind;
        if (!is_int(c, handler.get(c)) || !int_fits_signed(c, handler.get(c), kind) ||
            (kind != 0 && kind != 1)) {
            raise_error(
                c, CoreObject::TypeErrorType,
                U"signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object",
                out);
            return;
        }
        action.sa_handler = kind ? SIG_IGN : SIG_DFL;
    }
    if (!claim(c, owner, out) || check_signals(c, out))
        return;
    auto &state = c.payload<SignalState>(owner);
    c.load(owner.get(c), state.handlers[signal], out);
    auto previous =
        roots.root(out.tag() == ResultTag::Empty ? c.builtin(CoreObject::None) : out.value());
    int error;
    {
        std::lock_guard guard(registration);
        NoSafepoint commit(c);
        error = sigaction(signal, &action, nullptr) == 0 ? 0 : errno;
        if (!error) {
            state.native.changed[signal] = true;
            state.native.installed[signal] = action;
            c.store(owner.get(c), state.handlers[signal], handler.get(c));
        }
    }
    if (error)
        io_os_error(c, error, out);
    else
        out.ok(previous.get(c));
}

void raise_signal(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    int signal;
    if (!number(c, args[0], signal, out, false))
        return;
    if (std::raise(signal) != 0) {
        io_os_error(c, errno, out);
        return;
    }
    if (!check_signals(c, out))
        out.ok(c.builtin(CoreObject::None));
}

void valid_signals(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    sigset_t valid;
    sigfillset(&valid);
    std::vector<Value> values;
    for (int signal = 1; signal < NSIG; ++signal)
        if (sigismember(&valid, signal) == 1) {
            make_int(c, signal, out);
            values.push_back(roots.root(out.value()).get(c));
        }
    make_set(c, values, out);
}
} // namespace

bool check_signals(Attachment &c, ReturnSlot &out)
{
    if (!process.tripped.load(std::memory_order_acquire) || !main_thread())
        return false;
    RootScope roots(c);
    ReturnSlot loaded(c);
    c.load_signal_state(loaded);
    if (loaded.tag() == ResultTag::Empty)
        return false;
    auto owner = roots.root(loaded.value());
    auto &state = c.payload<SignalState>(owner);
    const auto generation = state.native.generation;
    if (!generation || process.owner.load(std::memory_order_acquire) != generation)
        return false;
    process.tripped.store(false, std::memory_order_release);
    for (int signal = 1; signal < NSIG; ++signal) {
        if (process.pending[signal].exchange(0, std::memory_order_acq_rel) != generation)
            continue;
        c.load(owner.get(c), state.handlers[signal], loaded);
        if (loaded.tag() != ResultTag::Ok || loaded.value() == c.builtin(CoreObject::None) ||
            !type_attribute(c, c.type_of(loaded.value()), U"__call__"))
            continue;
        auto handler = roots.root(loaded.value());
        make_int(c, signal, loaded);
        const auto argument = roots.root(loaded.value());
        const auto frame = roots.root(c.current_frame().value_or(c.builtin(CoreObject::None)));
        const std::array arguments{argument.get(c), frame.get(c)};
        call(c, handler, {arguments, {}}, out);
        if (out.tag() == ResultTag::Raised) {
            // Keep any remaining signals visible after this handler's exception.
            process.tripped.store(true, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void initialize_signal(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot state_slot(c), value(c), key(c);
    c.load_signal_state(state_slot);
    if (state_slot.tag() == ResultTag::Empty) {
        c.allocate<SignalState>(state_slot);
        auto created = roots.root(state_slot.value());
        observe_dispositions(c, created);
        SignatureLayout interrupt;
        interrupt.parameters = {{U"signalnum", ParameterKind::PositionalOnly},
                                {U"frame", ParameterKind::PositionalOnly}};
        make_native_function(c, "default_int_handler", interrupt, default_interrupt, {}, value,
                             NativeFunctionBinding::Function);
        c.store(created.get(c), c.payload<SignalState>(created).default_handler, value.value());
        c.publish_signal_state(created, state_slot);
    }
    auto owner = roots.root(state_slot.value());
    const auto &native = c.payload<SignalState>(owner).native;
    if (main_thread() && (native.generation || !process.owner.load(std::memory_order_acquire)) &&
        !claim(c, owner, out))
        return;
    const auto dictionary = roots.root(module_dict(c, module.get(c)));
    auto put = [&](const char32_t *name, Value item) {
        auto rooted = roots.root(item);
        make_str(c, name, key);
        name_store(c, dictionary.get(c), key.value(), rooted.get(c));
    };
    auto constant = [&](const char32_t *name, int number) {
        make_int(c, number, value);
        put(name, value.value());
    };
#define SIGNAL(name) constant(U## #name, name)
    SIGNAL(SIGHUP);
    SIGNAL(SIGINT);
    SIGNAL(SIGQUIT);
    SIGNAL(SIGILL);
    SIGNAL(SIGTRAP);
    SIGNAL(SIGABRT);
    SIGNAL(SIGBUS);
    SIGNAL(SIGFPE);
    SIGNAL(SIGKILL);
    SIGNAL(SIGUSR1);
    SIGNAL(SIGSEGV);
    SIGNAL(SIGUSR2);
    SIGNAL(SIGPIPE);
    SIGNAL(SIGALRM);
    SIGNAL(SIGTERM);
    SIGNAL(SIGCHLD);
    SIGNAL(SIGCONT);
    SIGNAL(SIGSTOP);
    SIGNAL(SIGTSTP);
    SIGNAL(SIGTTIN);
    SIGNAL(SIGTTOU);
    SIGNAL(SIGURG);
    SIGNAL(SIGXCPU);
    SIGNAL(SIGXFSZ);
    SIGNAL(SIGVTALRM);
    SIGNAL(SIGPROF);
    SIGNAL(SIGWINCH);
    SIGNAL(SIGIO);
    SIGNAL(SIGSYS);
    SIGNAL(SIGRTMIN);
    SIGNAL(SIGRTMAX);
    SIGNAL(NSIG);
#ifdef SIGIOT
    SIGNAL(SIGIOT);
#endif
#ifdef SIGCLD
    SIGNAL(SIGCLD);
#endif
#ifdef SIGPOLL
    SIGNAL(SIGPOLL);
#endif
#ifdef SIGPWR
    SIGNAL(SIGPWR);
#endif
#ifdef SIGSTKFLT
    SIGNAL(SIGSTKFLT);
#endif
#undef SIGNAL
    constant(U"SIG_DFL", 0);
    constant(U"SIG_IGN", 1);
    c.load(owner.get(c), c.payload<SignalState>(owner).default_handler, value);
    put(U"default_int_handler", value.value());
    auto add = [&](const char *name, const char32_t *wide, std::size_t count, NativeEntry entry) {
        SignatureLayout signature;
        if (count)
            signature.parameters.push_back({U"signalnum", ParameterKind::PositionalOnly});
        if (count == 2)
            signature.parameters.push_back({U"handler", ParameterKind::PositionalOnly});
        const Value capture = owner.get(c);
        make_native_function(c, name, signature, entry, {&capture, 1}, value,
                             NativeFunctionBinding::Function);
        put(wide, value.value());
    };
    add("signal", U"signal", 2, set_handler);
    add("getsignal", U"getsignal", 1, get_handler);
    add("raise_signal", U"raise_signal", 1, raise_signal);
    add("valid_signals", U"valid_signals", 0, valid_signals);
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
