#include "cxxpython/import.hpp"
#include "internal/threadmodule.hpp"

#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

using namespace cxxpython::core;
namespace {
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
