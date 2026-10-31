#include "cxxpython/import.hpp"
#include "internal/threadmodule.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

using namespace cxxpython::core;
namespace {
TEST(NativeSignals, DispositionsAreRestoredAndIndependentInterpretersDoNotShareHandlers)
{
    struct sigaction original {
    }, ignored{}, restored{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    ASSERT_EQ(sigaction(SIGUSR1, &ignored, &original), 0);
    {
        Interpreter first;
        Attachment context(first);
        context.bootstrap_core();
        RootScope roots(context);
        ReturnSlot module(context), method(context), key(context), number(context), result(context);
        import_module(context, U"_signal", module);
        ASSERT_EQ(module.tag(), ResultTag::Ok);
        make_str(context, U"signal", key);
        get_attribute(context, roots.root(module.value()), roots.root(key.value()), method);
        ASSERT_EQ(method.tag(), ResultTag::Ok);
        make_int(context, SIGUSR1, number);
        const std::array args{number.value(), context.builtin(CoreObject::IntZero)};
        call(context, roots.root(method.value()), {args, {}}, result);
        ASSERT_EQ(result.tag(), ResultTag::Ok);
        EXPECT_EQ(result.value(), context.builtin(CoreObject::IntOne));
        {
            Interpreter second;
            Attachment other(second);
            other.bootstrap_core();
            RootScope second_roots(other);
            ReturnSlot imported(other), function(other), name(other), signal(other), answer(other);
            import_module(other, U"_signal", imported);
            ASSERT_EQ(imported.tag(), ResultTag::Ok);
            make_str(other, U"signal", name);
            get_attribute(other, second_roots.root(imported.value()),
                          second_roots.root(name.value()), function);
            ASSERT_EQ(function.tag(), ResultTag::Ok);
            make_int(other, SIGUSR1, signal);
            const std::array values{signal.value(), other.builtin(CoreObject::IntOne)};
            call(other, second_roots.root(function.value()), {values, {}}, answer);
            ASSERT_EQ(answer.tag(), ResultTag::Raised);
            EXPECT_EQ(other.type_of(answer.exception()),
                      other.builtin(CoreObject::RuntimeErrorType));
        }
    }
    ASSERT_EQ(sigaction(SIGUSR1, nullptr, &restored), 0);
    EXPECT_EQ(restored.sa_handler, SIG_IGN);
    EXPECT_EQ(sigaction(SIGUSR1, &original, nullptr), 0);
}

struct ExitSentinel {
    ObjectHeader header;
    std::atomic<int> *released, *destroyed;
    ExitSentinel(InitialType type, std::atomic<int> *release, std::atomic<int> *destroy)
        : header(type), released(release), destroyed(destroy)
    {}
    ~ExitSentinel()
    {
        ++*destroyed;
    }
    void trace(Tracer &) const noexcept {}
};

TEST(NativeThreads, ExitSentinelRemainsRootedUntilAttachmentRetires)
{
    std::atomic<int> released{0}, destroyed{0};
    Interpreter interpreter;
    Attachment context(interpreter);
    std::promise<void> ready, finish;
    auto started = ready.get_future();
    auto finishing = finish.get_future();
    std::thread worker([&] {
        Attachment child(interpreter);
        {
            ReturnSlot owner(child);
            child.allocate<ExitSentinel>(owner, &released, &destroyed);
            RootScope roots(child);
            child.set_exit_sentinel(
                roots.root(owner.value()), +[](ObjectHeader *header) noexcept {
                    ++*reinterpret_cast<ExitSentinel *>(header)->released;
                });
        }
        NativeWait parked(child);
        ready.set_value();
        finishing.wait();
    });
    {
        NativeWait parked(context);
        started.wait();
    }
    context.collect();
    EXPECT_EQ(destroyed, 0);
    EXPECT_EQ(released, 0);
    finish.set_value();
    {
        NativeWait parked(context);
        worker.join();
    }
    EXPECT_EQ(released, 1);
    context.collect();
    EXPECT_EQ(destroyed, 1);
}

void invoke(Attachment &ctx, Handle object, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx), method(ctx);
    make_str(ctx, name, key);
    get_attribute(ctx, object, roots.root(key.value()), method);
    if (method.tag() == ResultTag::Raised) {
        out.raised(method.exception());
        return;
    }
    call(ctx, roots.root(method.value()), {}, out);
}

TEST(NativeThreads, LockWaitParksForCollectionAndTransferKeepsObjectAlive)
{
    Interpreter interpreter;
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot out(ctx), key(ctx), function(ctx);
    initialize_imports(ctx, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    register_native_module(ctx, U"_thread", detail::initialize_thread);
    import_module(ctx, U"_thread", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto module = roots.root(out.value());
    make_str(ctx, U"allocate_lock", key);
    get_attribute(ctx, module, roots.root(key.value()), function);
    ASSERT_EQ(function.tag(), ResultTag::Ok);
    call(ctx, roots.root(function.value()), {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto lock = roots.root(out.value());
    invoke(ctx, lock, U"acquire", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto transfer = ctx.make_transfer(lock);
    std::promise<void> ready;
    auto ready_future = ready.get_future();
    bool acquired = false;
    std::thread worker([&ready, &acquired, launch = std::move(transfer)]() mutable {
        Attachment child(launch);
        RootScope child_roots(child);
        ReturnSlot value(child), result(child);
        child.receive(launch, value);
        auto child_lock = child_roots.root(value.value());
        ready.set_value();
        invoke(child, child_lock, U"acquire", result);
        acquired =
            result.tag() == ResultTag::Ok && result.value() == child.builtin(CoreObject::True);
        if (acquired)
            invoke(child, child_lock, U"release", result);
    });
    bool parked = false;
    {
        NativeWait wait(ctx);
        ready_future.wait();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (interpreter.gc_state().native_wait >= 2) {
                parked = true;
                break;
            }
            std::this_thread::yield();
        }
    }
    if (parked)
        ctx.collect();
    invoke(ctx, lock, U"release", out);
    {
        NativeWait wait(ctx);
        worker.join();
    }
    EXPECT_TRUE(parked);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_TRUE(acquired);
}

TEST(NativeThreads, ClosedInterpreterRejectsTransferAdmissionWithoutLosingPayload)
{
    Interpreter interpreter;
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    auto transfer = ctx.make_transfer(roots.root(ctx.builtin(CoreObject::True)));
    interpreter.close();
    bool rejected = false;
    std::thread worker([&transfer, &rejected] {
        try {
            Attachment child(transfer);
        } catch (const std::logic_error &) {
            rejected = true;
        }
    });
    {
        NativeWait wait(ctx);
        worker.join();
    }
    EXPECT_TRUE(rejected);
    ReturnSlot received(ctx);
    ctx.receive(transfer, received);
    EXPECT_EQ(received.tag(), ResultTag::Ok);
    EXPECT_EQ(received.value(), ctx.builtin(CoreObject::True));
}
} // namespace
