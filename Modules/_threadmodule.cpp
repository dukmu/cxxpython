#include "internal/threadmodule.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/errors.hpp"
#include "internal/long.hpp"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace cxxpython::core::detail {
namespace {
// CPython 3.12's _PyTime_t limit, in whole seconds.
constexpr double timeout_max = 9223372036.0;

struct Lock {
    ObjectHeader header;
    std::mutex mutex;
    std::condition_variable changed;
    bool locked = false;
    explicit Lock(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

struct RLock {
    ObjectHeader header;
    std::mutex mutex;
    std::condition_variable changed;
    unsigned long owner = 0;
    unsigned long count = 0;
    bool held = false;
    explicit RLock(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void allocate_lock(Attachment &ctx, Value function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    ctx.allocate_instance<Lock>(type, out);
}

void set_sentinel(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    allocate_lock(ctx, function, args, out);
    RootScope roots(ctx);
    ctx.set_exit_sentinel(
        roots.root(out.value()), +[](ObjectHeader *header) noexcept {
            auto &lock = *reinterpret_cast<Lock *>(header);
            std::lock_guard guard(lock.mutex);
            if (lock.locked) {
                lock.locked = false;
                lock.changed.notify_all();
            }
        });
}

void threads_allowed(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(ctx.builtin(CoreObject::True));
}

void stack_size(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    // CPython 3.12 also restores the default on the no-argument call.
    std::optional<std::size_t> replacement = 0;
    if (args[0] != ctx.payload<NativeFunctionObject>(function).captures[0]) {
        RootScope roots(ctx);
        index(ctx, roots.root(args[0]), out);
        if (out.tag() == ResultTag::Raised)
            return;
        std::int64_t value;
        if (!int_fits_signed(ctx, out.value(), value)) {
            raise_error(ctx, CoreObject::OverflowErrorType,
                        U"Python int too large to convert to C ssize_t", out);
            return;
        }
        if (value < 0) {
            raise_error(ctx, CoreObject::ValueErrorType, U"size must be 0 or a positive value",
                        out);
            return;
        }
        if (value) {
            pthread_attr_t attributes;
            int error = pthread_attr_init(&attributes);
            if (!error) {
                error = value < 32768 ? EINVAL : pthread_attr_setstacksize(&attributes, value);
                pthread_attr_destroy(&attributes);
            }
            if (error) {
                const auto number = std::to_string(value);
                raise_error(ctx, CoreObject::ValueErrorType,
                            U"size not valid: " + std::u32string(number.begin(), number.end()) +
                                U" bytes",
                            out);
                return;
            }
        }
        replacement = static_cast<std::size_t>(value);
    }
    make_int_decimal(ctx, std::to_string(ctx.native_thread_stack_size(replacement)), out);
}

bool parse_acquire(Attachment &ctx, std::span<const Value> args, bool &blocking,
                   std::int64_t &ticks, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    truth(ctx, roots.root(args[1]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    blocking = converted.value() == ctx.builtin(CoreObject::True);
    double timeout;
    if (is_float(ctx, args[2])) {
        timeout = ctx.payload<FloatObject>(args[2]).value;
    } else {
        index(ctx, roots.root(args[2]), converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return false;
        }
        std::int64_t number;
        if (!int_fits_signed(ctx, converted.value(), number)) {
            raise_error(ctx, CoreObject::OverflowErrorType, U"timestamp too large to convert", out);
            return false;
        }
        timeout = static_cast<double>(number);
    }
    if (std::isnan(timeout)) {
        raise_error(ctx, CoreObject::ValueErrorType, U"Invalid value NaN (not a number)", out);
        return false;
    }
    const long double nanos = std::ceil(static_cast<long double>(timeout) * 1000000000.0L);
    if (!std::isfinite(timeout) ||
        nanos > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
        nanos < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
        raise_error(ctx, CoreObject::OverflowErrorType,
                    U"timestamp out of range for platform time_t", out);
        return false;
    }
    ticks = static_cast<std::int64_t>(nanos);
    if (!blocking && ticks != -1000000000) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"can't specify a timeout for a non-blocking call", out);
        return false;
    }
    if (ticks < 0 && ticks != -1000000000) {
        raise_error(ctx, CoreObject::ValueErrorType, U"timeout value must be positive", out);
        return false;
    }
    return true;
}

void acquire(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<Lock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"lock method requires a lock instance", out);
        return;
    }
    bool blocking;
    std::int64_t ticks;
    if (!parse_acquire(ctx, args, blocking, ticks, out))
        return;
    auto &lock = ctx.payload<Lock>(self);
    bool acquired;
    {
        NativeWait wait(ctx);
        std::unique_lock guard(lock.mutex);
        if (!blocking || ticks == 0)
            acquired = !lock.locked;
        else if (ticks == -1000000000) {
            lock.changed.wait(guard, [&] { return !lock.locked; });
            acquired = true;
        } else {
            const auto now = std::chrono::steady_clock::now();
            const auto duration = std::chrono::nanoseconds(ticks);
            const auto remaining = std::chrono::steady_clock::time_point::max() - now;
            const auto deadline = duration > remaining
                                      ? std::chrono::steady_clock::time_point::max()
                                      : now + duration;
            acquired = lock.changed.wait_until(guard, deadline, [&] { return !lock.locked; });
        }
        if (acquired)
            lock.locked = true;
    }
    out.ok(ctx.builtin(acquired ? CoreObject::True : CoreObject::False));
}

void release(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<Lock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"lock method requires a lock instance", out);
        return;
    }
    auto &lock = ctx.payload<Lock>(self);
    bool was_locked;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        was_locked = lock.locked;
        lock.locked = false;
        if (was_locked)
            lock.changed.notify_one();
    }
    if (!was_locked)
        raise_error(ctx, CoreObject::RuntimeErrorType, U"release unlocked lock", out);
    else
        out.ok(ctx.builtin(CoreObject::None));
}

void locked(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<Lock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"lock method requires a lock instance", out);
        return;
    }
    auto &lock = ctx.payload<Lock>(self);
    bool value;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        value = lock.locked;
    }
    out.ok(ctx.builtin(value ? CoreObject::True : CoreObject::False));
}

void lock_repr(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    if (!ctx.has_layout<Lock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"lock method requires a lock instance", out);
        return;
    }
    auto &lock = ctx.payload<Lock>(self);
    bool is_locked;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        is_locked = lock.locked;
    }
    char address[2 + 2 * sizeof(std::uintptr_t) + 1];
    std::snprintf(address, sizeof(address), "%p", static_cast<void *>(&lock));
    const std::string suffix(address);
    make_str(
        ctx,
        (is_locked ? U"<locked _thread.lock object at " : U"<unlocked _thread.lock object at ") +
            std::u32string(suffix.begin(), suffix.end()) + U">",
        out);
}

void rlock_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    if (!ctx.has_layout<TypeObject>(args[0]) ||
        ctx.payload<TypeObject>(args[0]).instance_layout.native_family !=
            LayoutDescriptor::of<RLock>().native_family) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock.__new__ requires an RLock subtype",
                    out);
        return;
    }
    ctx.allocate_instance<RLock>(roots.root(args[0]), out);
}

void rlock_acquire(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    bool blocking;
    std::int64_t ticks;
    if (!parse_acquire(ctx, args, blocking, ticks, out))
        return;
    auto &lock = ctx.payload<RLock>(self);
    const auto owner = static_cast<unsigned long>(pthread_self());
    bool acquired = false, overflow = false;
    {
        NativeWait wait(ctx);
        std::unique_lock guard(lock.mutex);
        if (lock.count && lock.owner == owner) {
            if (lock.count == std::numeric_limits<unsigned long>::max())
                overflow = true;
            else {
                ++lock.count;
                acquired = true;
            }
        } else {
            if (!blocking || ticks == 0)
                acquired = !lock.held;
            else if (ticks == -1000000000) {
                lock.changed.wait(guard, [&] { return !lock.held; });
                acquired = true;
            } else {
                const auto now = std::chrono::steady_clock::now();
                const auto duration = std::chrono::nanoseconds(ticks);
                const auto remaining = std::chrono::steady_clock::time_point::max() - now;
                const auto deadline = duration > remaining
                                          ? std::chrono::steady_clock::time_point::max()
                                          : now + duration;
                acquired = lock.changed.wait_until(guard, deadline, [&] { return !lock.held; });
            }
            if (acquired) {
                lock.owner = owner;
                lock.count = 1;
                lock.held = true;
            }
        }
    }
    if (overflow)
        raise_error(ctx, CoreObject::OverflowErrorType, U"Internal lock count overflowed", out);
    else
        out.ok(ctx.builtin(acquired ? CoreObject::True : CoreObject::False));
}

void rlock_release(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    auto &lock = ctx.payload<RLock>(self);
    bool owned;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        owned = lock.count && lock.owner == static_cast<unsigned long>(pthread_self());
        if (owned && --lock.count == 0) {
            lock.owner = 0;
            lock.held = false;
            lock.changed.notify_one();
        }
    }
    if (!owned)
        raise_error(ctx, CoreObject::RuntimeErrorType, U"cannot release un-acquired lock", out);
    else
        out.ok(ctx.builtin(CoreObject::None));
}

void rlock_count(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    auto &lock = ctx.payload<RLock>(self);
    unsigned long count;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        count = lock.owner == static_cast<unsigned long>(pthread_self()) ? lock.count : 0;
    }
    make_int_decimal(ctx, std::to_string(count), out);
}

void rlock_owned(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    ReturnSlot count(ctx);
    rlock_count(ctx, function, args, count);
    if (failed(count)) {
        out.raised(count.exception());
        return;
    }
    RootScope roots(ctx);
    truth(ctx, roots.root(count.value()), out);
}

void rlock_save(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    auto &lock = ctx.payload<RLock>(self);
    unsigned long count, owner;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        count = lock.count;
        owner = lock.owner;
        if (count) {
            lock.count = lock.owner = 0;
            lock.held = false;
            lock.changed.notify_one();
        }
    }
    if (!count) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"cannot release un-acquired lock", out);
        return;
    }
    ReturnSlot first(ctx), second(ctx);
    make_int_decimal(ctx, std::to_string(count), first);
    if (failed(first)) {
        out.raised(first.exception());
        return;
    }
    auto count_root = roots.root(first.value());
    make_int_decimal(ctx, std::to_string(owner), second);
    if (failed(second)) {
        out.raised(second.exception());
        return;
    }
    const Value items[] = {count_root.get(ctx), second.value()};
    make_tuple(ctx, items, out);
}

void rlock_restore(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto state = roots.root(args[1]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    if (!ctx.has_layout<TupleObject>(state.get(ctx)) ||
        ctx.payload<TupleObject>(state).items.size() != 2) {
        raise_error(ctx, CoreObject::TypeErrorType, U"_acquire_restore requires a 2-tuple", out);
        return;
    }
    unsigned long values[2];
    for (int i = 0; i < 2; ++i) {
        auto item = roots.root(ctx.payload<TupleObject>(state).items[i]);
        ReturnSlot converted(ctx);
        index(ctx, item, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        const auto &number = ctx.payload<IntObject>(converted.value());
        if (number.large) {
            const Integer mask = std::numeric_limits<unsigned long>::max();
            values[i] = (number.large->value & mask).convert_to<unsigned long>();
        } else
            values[i] = static_cast<unsigned long>(number.small);
    }
    auto &lock = ctx.payload<RLock>(self);
    {
        NativeWait wait(ctx);
        std::unique_lock guard(lock.mutex);
        lock.changed.wait(guard, [&] { return !lock.held; });
        lock.held = true;
        lock.count = values[0];
        lock.owner = values[1];
    }
    out.ok(ctx.builtin(CoreObject::None));
}

void rlock_repr(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<RLock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"RLock method requires an RLock instance",
                    out);
        return;
    }
    auto &lock = ctx.payload<RLock>(self);
    unsigned long count, owner;
    {
        NativeWait wait(ctx);
        std::lock_guard guard(lock.mutex);
        count = lock.count;
        owner = lock.owner;
    }
    TypeView type(ctx, ctx.type_of(self.get(ctx)));
    auto name = type.name();
    std::u32string display = std::holds_alternative<std::u32string>(name)
                                 ? std::get<std::u32string>(name)
                                 : ctx.payload<StrObject>(std::get<Value>(name)).value;
    DictView attributes(ctx, type.storage());
    auto module = attributes.find(U"__module__");
    if (display == U"RLock" && module && ctx.has_layout<StrObject>(*module) &&
        ctx.payload<StrObject>(*module).value == U"_thread")
        display = U"_thread.RLock";
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), " object owner=%lu count=%lu at %p>", owner, count,
                  static_cast<void *>(&lock));
    std::string suffix(buffer);
    make_str(ctx,
             (count ? U"<locked " : U"<unlocked ") + display +
                 std::u32string(suffix.begin(), suffix.end()),
             out);
}

void rlock_fork(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::NotImplementedErrorType,
                U"RLock reinitialization after fork is not supported", out);
}

void ident(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    make_int_decimal(ctx, std::to_string(static_cast<unsigned long>(pthread_self())), out);
}
void native_id(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    make_int(ctx, static_cast<std::int64_t>(syscall(SYS_gettid)), out);
}

void thread_exit(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot exception(ctx);
    call(ctx, roots.root(ctx.builtin(CoreObject::SystemExitType)), {}, exception);
    if (failed(exception))
        out.raised(exception.exception());
    else
        out.raised(exception.value());
}

struct ThreadLaunch {
    Transfer transfer;
    std::shared_ptr<std::promise<unsigned long>> started;
};

void *thread_bootstrap(void *data) noexcept
{
    std::unique_ptr<ThreadLaunch> launch(static_cast<ThreadLaunch *>(data));
    bool admitted = false;
    try {
        Attachment child(launch->transfer);
        launch->started->set_value(static_cast<unsigned long>(pthread_self()));
        admitted = true;
        RootScope child_roots(child);
        ReturnSlot received(child), answer(child);
        child.receive(launch->transfer, received);
        auto tuple = child_roots.root(received.value());
        const auto &values = child.payload<TupleObject>(tuple).items;
        auto target = child_roots.root(values[0]);
        auto positional = child_roots.root(values[1]);
        auto kwargs = child_roots.root(values[2]);
        std::vector<KeywordArg> named;
        if (kwargs.get(child) != child.builtin(CoreObject::None)) {
            DictView view(child, kwargs.get(child));
            for (const auto &entry : view.entries()) {
                child_roots.root(entry.key);
                child_roots.root(entry.value);
                named.push_back({entry.key, entry.value});
            }
        }
        call(child, target, {child.payload<TupleObject>(positional).items, named}, answer);
        if (failed(answer) && !is_subtype(child, child.type_of(answer.exception()),
                                          child.builtin(CoreObject::SystemExitType))) {
            ReturnSlot message(child);
            make_str(child, U"Exception ignored in thread started by", message);
            write_unraisable(child, child_roots.root(answer.exception()), target,
                             child_roots.root(message.value()));
        }
    } catch (...) {
        if (!admitted) {
            try {
                launch->started->set_exception(std::current_exception());
            } catch (...) {
            }
        } else
            std::fputs("Exception in native thread runtime\n", stderr);
    }
    return nullptr;
}

void thread_start(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto callable = roots.root(args[0]);
    auto arguments = roots.root(args[1]);
    auto keywords = roots.root(args[2]);
    const auto callable_type = ctx.type_of(callable.get(ctx));
    if (callable_type != ctx.builtin(CoreObject::NativeFunctionType) &&
        callable_type != ctx.builtin(CoreObject::FunctionType) &&
        callable_type != ctx.builtin(CoreObject::BoundMethodType) &&
        callable_type != ctx.builtin(CoreObject::MethodDescriptorType) &&
        !type_attribute(ctx, callable_type, U"__call__")) {
        raise_error(ctx, CoreObject::TypeErrorType, U"first arg must be callable", out);
        return;
    }
    if (!ctx.has_layout<TupleObject>(arguments.get(ctx)) ||
        !is_subtype(ctx, ctx.type_of(arguments.get(ctx)), ctx.builtin(CoreObject::TupleType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"2nd arg must be a tuple", out);
        return;
    }
    const bool omitted =
        keywords.get(ctx) == ctx.payload<NativeFunctionObject>(function).captures[0];
    if (!omitted &&
        (!ctx.has_layout<DictObject>(keywords.get(ctx)) ||
         !is_subtype(ctx, ctx.type_of(keywords.get(ctx)), ctx.builtin(CoreObject::DictType)))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"optional 3rd arg must be a dictionary", out);
        return;
    }
    const Value items[] = {callable.get(ctx), arguments.get(ctx),
                           omitted ? ctx.builtin(CoreObject::None) : keywords.get(ctx)};
    ReturnSlot payload(ctx);
    make_tuple(ctx, items, payload);
    auto transfer = ctx.make_transfer(roots.root(payload.value()));
    auto started = std::make_shared<std::promise<unsigned long>>();
    auto result = started->get_future();
    auto launch = std::make_unique<ThreadLaunch>(ThreadLaunch{std::move(transfer), started});
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (!error) {
        error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        if (!error)
            if (const auto size = ctx.native_thread_stack_size())
                error = pthread_attr_setstacksize(&attributes, size);
        if (!error) {
            pthread_t worker;
            NativeWait wait(ctx);
            error = pthread_create(&worker, &attributes, thread_bootstrap, launch.get());
            if (!error)
                launch.release();
        }
        pthread_attr_destroy(&attributes);
    }
    if (error) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"can't start new thread", out);
        return;
    }
    unsigned long id;
    try {
        NativeWait wait(ctx);
        id = result.get();
    } catch (const std::exception &) {
        raise_error(ctx, CoreObject::RuntimeErrorType,
                    U"can't start new thread at interpreter shutdown", out);
        return;
    }
    make_int_decimal(ctx, std::to_string(id), out);
}
} // namespace

void shutdown_threads(Attachment &ctx, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto entry = name_lookup(ctx, ctx.builtin(CoreObject::Modules), U"threading");
    if (!entry) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    auto module = roots.root(*entry);
    make_str(ctx, U"_shutdown", out);
    get_attribute(ctx, module, roots.root(out.value()), out);
    if (!failed(out))
        call(ctx, roots.root(out.value()), {}, out);
    if (failed(out)) {
        auto exception = roots.root(out.exception());
        make_str(ctx, U"Exception ignored on threading shutdown", out);
        write_unraisable(ctx, exception, module, roots.root(out.value()));
    }
    out.ok(ctx.builtin(CoreObject::None));
}

void initialize_thread(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"lock"}, LayoutDescriptor::of<Lock>()};
    spec.type.module = U"_thread";
    spec.type.immutable = true;
    spec.type.subclassable = false;
    ReturnSlot value(ctx), minus_one(ctx);
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    const auto previous = name_lookup(ctx, dictionary.get(ctx), U"allocate_lock");
    const bool reuse = previous && ctx.has_layout<NativeFunctionObject>(*previous) &&
                       ctx.payload<NativeFunctionObject>(*previous).entry == allocate_lock &&
                       ctx.payload<NativeFunctionObject>(*previous).captures.size() == 1;
    if (reuse)
        value.ok(ctx.payload<NativeFunctionObject>(*previous).captures[0]);
    else
        create_native_type(ctx, spec, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto type = roots.root(value.value());
    make_int(ctx, -1, minus_one);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto acquire_signature = unary;
    acquire_signature.parameters.push_back(
        {U"blocking", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::True)});
    acquire_signature.parameters.push_back(
        {U"timeout", ParameterKind::PositionalOrKeyword, minus_one.value()});
    auto exit_signature = unary;
    for (auto name : {U"exc_type", U"exc_value", U"traceback"})
        exit_signature.parameters.push_back({name, ParameterKind::PositionalOnly});
    if (!reuse) {
        for (auto name : {U"acquire", U"acquire_lock", U"__enter__"})
            install_native_attribute(ctx, core, type, name, acquire_signature, acquire);
        for (auto name : {U"release", U"release_lock"})
            install_native_attribute(ctx, core, type, name, unary, release);
        install_native_attribute(ctx, core, type, U"__exit__", exit_signature, release);
        for (auto name : {U"locked", U"locked_lock"})
            install_native_attribute(ctx, core, type, name, unary, locked);
        install_native_attribute(ctx, core, type, U"__repr__", unary, lock_repr);
    }
    const auto old_rlock = name_lookup(ctx, dictionary.get(ctx), U"RLock");
    const bool reuse_rlock = old_rlock && ctx.has_layout<TypeObject>(*old_rlock) &&
                             ctx.payload<TypeObject>(*old_rlock).instance_layout.native_family ==
                                 LayoutDescriptor::of<RLock>().native_family;
    if (reuse_rlock)
        value.ok(*old_rlock);
    else {
        NativeTypeSpec rlock_spec{{"RLock"}, LayoutDescriptor::of<RLock>()};
        rlock_spec.type.module = U"_thread";
        rlock_spec.type.immutable = true;
        create_native_type(ctx, rlock_spec, value);
    }
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto rlock_type = roots.root(value.value());
    if (!reuse_rlock) {
        SignatureLayout constructor;
        constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
        constructor.varargs = U"args";
        constructor.varkwargs = U"kwargs";
        install_native_attribute(ctx, core, rlock_type, U"__new__", constructor, rlock_new);
        auto type_dict = roots.root(*TypeView(ctx, rlock_type).dictionary());
        auto function = roots.root(*name_lookup(ctx, type_dict.get(ctx), U"__new__"));
        ReturnSlot wrapper(ctx), key(ctx);
        ctx.allocate_instance<StaticMethodObject>(
            roots.root(ctx.builtin(CoreObject::StaticMethodType)), wrapper, function.get(ctx));
        make_str(ctx, U"__new__", key);
        name_store(ctx, type_dict.get(ctx), key.value(), wrapper.value());
        install_native_attribute(ctx, core, rlock_type, U"acquire", acquire_signature,
                                 rlock_acquire);
        install_native_attribute(ctx, core, rlock_type, U"__enter__", acquire_signature,
                                 rlock_acquire);
        install_native_attribute(ctx, core, rlock_type, U"release", unary, rlock_release);
        auto rlock_exit = unary;
        rlock_exit.varargs = U"args";
        install_native_attribute(ctx, core, rlock_type, U"__exit__", rlock_exit, rlock_release);
        install_native_attribute(ctx, core, rlock_type, U"_is_owned", unary, rlock_owned);
        install_native_attribute(ctx, core, rlock_type, U"_recursion_count", unary, rlock_count);
        install_native_attribute(ctx, core, rlock_type, U"_release_save", unary, rlock_save);
        auto restore_signature = unary;
        restore_signature.parameters.push_back({U"state", ParameterKind::PositionalOnly});
        install_native_attribute(ctx, core, rlock_type, U"_acquire_restore", restore_signature,
                                 rlock_restore);
        install_native_attribute(ctx, core, rlock_type, U"_at_fork_reinit", unary, rlock_fork);
        install_native_attribute(ctx, core, rlock_type, U"__repr__", unary, rlock_repr);
    }
    auto store = [&](const char32_t *name, Value item) {
        auto item_root = roots.root(item);
        make_str(ctx, name, value);
        dict_store(ctx, dictionary, roots.root(value.value()), item_root, out);
        return !failed(out);
    };
    if (!store(U"RLock", rlock_type.get(ctx)) || !store(U"LockType", type.get(ctx)) ||
        !store(U"error", ctx.builtin(CoreObject::RuntimeErrorType)))
        return;
    make_float(ctx, timeout_max, value);
    if (!store(U"TIMEOUT_MAX", value.value()))
        return;
    const Value capture = type.get(ctx);
    make_native_function(ctx, "allocate_lock", {}, allocate_lock, {&capture, 1}, value,
                         NativeFunctionBinding::Function);
    auto allocator = roots.root(value.value());
    if (!store(U"allocate_lock", allocator.get(ctx)) || !store(U"allocate", allocator.get(ctx)))
        return;
    make_native_function(ctx, "_set_sentinel", {}, set_sentinel, {&capture, 1}, value,
                         NativeFunctionBinding::Function);
    if (!store(U"_set_sentinel", value.value()))
        return;
    for (auto name : {U"daemon_threads_allowed", U"_is_main_interpreter"}) {
        make_native_function(ctx,
                             name == std::u32string_view(U"daemon_threads_allowed")
                                 ? "daemon_threads_allowed"
                                 : "_is_main_interpreter",
                             {}, threads_allowed, {}, value, NativeFunctionBinding::Function);
        if (!store(name, value.value()))
            return;
    }
    make_native_function(ctx, "get_ident", {}, ident, {}, value, NativeFunctionBinding::Function);
    if (!store(U"get_ident", value.value()))
        return;
    make_native_function(ctx, "get_native_id", {}, native_id, {}, value,
                         NativeFunctionBinding::Function);
    if (!store(U"get_native_id", value.value()))
        return;
    make_list(ctx, {}, value);
    auto sentinel = roots.root(value.value());
    SignatureLayout start_signature;
    start_signature.parameters = {{U"function", ParameterKind::PositionalOnly},
                                  {U"args", ParameterKind::PositionalOnly},
                                  {U"kwargs", ParameterKind::PositionalOnly, sentinel.get(ctx)}};
    const Value sentinel_value = sentinel.get(ctx);
    make_native_function(ctx, "start_new_thread", start_signature, thread_start,
                         {&sentinel_value, 1}, value, NativeFunctionBinding::Function);
    auto starter = roots.root(value.value());
    if (!store(U"start_new_thread", starter.get(ctx)) || !store(U"start_new", starter.get(ctx)))
        return;
    SignatureLayout size_signature;
    size_signature.parameters = {{U"size", ParameterKind::PositionalOnly, sentinel.get(ctx)}};
    make_native_function(ctx, "stack_size", size_signature, stack_size, {&sentinel_value, 1}, value,
                         NativeFunctionBinding::Function);
    if (!store(U"stack_size", value.value()))
        return;
    make_native_function(ctx, "exit", {}, thread_exit, {}, value, NativeFunctionBinding::Function);
    auto exit_function = roots.root(value.value());
    if (store(U"exit", exit_function.get(ctx)) && store(U"exit_thread", exit_function.get(ctx)))
        out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
