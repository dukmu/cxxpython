#include "internal/threadmodule.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/errors.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <future>
#include <limits>
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

bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void allocate_lock(Attachment &ctx, Handle function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    ctx.allocate_instance<Lock>(type, out);
}

void acquire(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    if (!ctx.has_layout<Lock>(self.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"lock method requires a lock instance", out);
        return;
    }
    ReturnSlot converted(ctx);
    truth(ctx, roots.root(args[1]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    const bool blocking = converted.value() == ctx.builtin(CoreObject::True);
    double timeout;
    if (is_float(ctx, args[2])) {
        timeout = ctx.payload<FloatObject>(args[2]).value;
    } else {
        index(ctx, roots.root(args[2]), converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        std::int64_t number;
        if (!int_fits_signed(ctx, converted.value(), number)) {
            raise_error(ctx, CoreObject::OverflowErrorType, U"timestamp too large to convert", out);
            return;
        }
        timeout = static_cast<double>(number);
    }
    if (std::isnan(timeout)) {
        raise_error(ctx, CoreObject::ValueErrorType, U"Invalid value NaN (not a number)", out);
        return;
    }
    const long double nanos = std::ceil(static_cast<long double>(timeout) * 1000000000.0L);
    if (!std::isfinite(timeout) ||
        nanos > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
        nanos < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
        raise_error(ctx, CoreObject::OverflowErrorType,
                    U"timestamp out of range for platform time_t", out);
        return;
    }
    const auto ticks = static_cast<std::int64_t>(nanos);
    if (!blocking && ticks != -1000000000) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"can't specify a timeout for a non-blocking call", out);
        return;
    }
    if (ticks < 0 && ticks != -1000000000) {
        raise_error(ctx, CoreObject::ValueErrorType, U"timeout value must be positive", out);
        return;
    }
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

void release(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
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

void locked(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
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

void ident(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    make_int_decimal(ctx, std::to_string(static_cast<unsigned long>(pthread_self())), out);
}
void native_id(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    make_int(ctx, static_cast<std::int64_t>(syscall(SYS_gettid)), out);
}

void thread_exit(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot exception(ctx);
    call(ctx, roots.root(ctx.builtin(CoreObject::SystemExitType)), {}, exception);
    if (failed(exception))
        out.raised(exception.exception());
    else
        out.raised(exception.value());
}

void thread_start(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
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
    try {
        std::thread worker([launch = std::move(transfer), started]() mutable {
            bool admitted = false;
            try {
                Attachment child(launch);
                started->set_value(static_cast<unsigned long>(pthread_self()));
                admitted = true;
                RootScope child_roots(child);
                ReturnSlot received(child), answer(child);
                child.receive(launch, received);
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
                        started->set_exception(std::current_exception());
                    } catch (...) {
                    }
                } else {
                    std::fputs("Exception in native thread runtime\n", stderr);
                }
            }
        });
        worker.detach();
    } catch (const std::system_error &) {
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

void initialize_thread(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"lock"}, LayoutDescriptor::of<Lock>()};
    spec.type.module = U"_thread";
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
    }
    auto store = [&](const char32_t *name, Value item) {
        auto item_root = roots.root(item);
        make_str(ctx, name, value);
        dict_store(ctx, dictionary, roots.root(value.value()), item_root, out);
        return !failed(out);
    };
    if (!store(U"LockType", type.get(ctx)) ||
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
    make_native_function(ctx, "exit", {}, thread_exit, {}, value, NativeFunctionBinding::Function);
    auto exit_function = roots.root(value.value());
    if (store(U"exit", exit_function.get(ctx)) && store(U"exit_thread", exit_function.get(ctx)))
        out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
