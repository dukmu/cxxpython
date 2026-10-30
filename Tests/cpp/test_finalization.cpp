#include "cxxpython/compiler.hpp"
#include "cxxpython/genobject.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;
struct FinalizerGate {
    std::latch entered{1};
    std::atomic<bool> release{false};
    std::atomic<unsigned> calls{0}, destroyed{0};
    std::atomic<bool> fail{false};
};
struct FinalizedObject {
    ObjectHeader header;
    FinalizerGate *const gate;
    const Value child;
    FinalizedObject(InitialType type, FinalizerGate *gate, Value child)
        : header(type), gate(gate), child(child)
    {}
    ~FinalizedObject() noexcept
    {
        ++gate->destroyed;
    }
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(child);
    }
    static void finalize(Attachment &context, Handle self, ReturnSlot &out)
    {
        auto &data = context.payload<FinalizedObject>(self);
        ++data.gate->calls;
        if (data.gate->fail.exchange(false))
            throw std::runtime_error("host finalizer failure");
        data.gate->entered.count_down();
        while (!data.gate->release.load(std::memory_order_acquire)) {
            context.poll();
            std::this_thread::yield();
        }
        context.collect();
        EXPECT_EQ(context.payload<StrObject>(data.child).value, U"retained child");
        out.ok(context.builtin(CoreObject::None));
    }
};
TEST(FinalizerFaults, AutomaticCollectionTriggersNativeFinalizer)
{
    FinalizerGate gate;
    gate.release = true;
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    create_native_type(
        context, {TypeSpec{"AutomaticFinalizer"}, LayoutDescriptor::of<FinalizedObject>()}, out);
    const auto type = roots.root(out.value());
    make_str(context, U"retained child", out);
    const auto child = roots.root(out.value());
    context.allocate_instance<FinalizedObject>(type, out, &gate, child.get(context));
    out.reset();
    make_str(context, U"trigger", out);
    EXPECT_EQ(gate.calls.load(), 1U);
    EXPECT_EQ(gate.destroyed.load(), 1U);
}
TEST(FinalizerFaults, HostFaultDoesNotRepeatTheCallbackOrStrandRemainingCandidates)
{
    FinalizerGate gate;
    gate.release = true;
    gate.fail = true;
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    create_native_type(context,
                       {TypeSpec{"FinalizerFault"}, LayoutDescriptor::of<FinalizedObject>()}, out);
    const auto type = roots.root(out.value());
    make_str(context, U"retained child", out);
    const auto child = roots.root(out.value());
    for (unsigned i = 0; i < 2; ++i) {
        context.allocate_instance<FinalizedObject>(type, out, &gate, child.get(context));
        out.reset();
    }
    EXPECT_THROW(context.collect(), std::runtime_error);
    EXPECT_EQ(gate.calls.load(), 1U);
    EXPECT_NO_THROW(context.collect());
    EXPECT_EQ(gate.calls.load(), 2U);
    EXPECT_EQ(gate.destroyed.load(), 2U);
}
TEST(ParallelFinalizers, RunningBatchAllowsOtherActiveBytecodeAndGc)
{
    FinalizerGate gate;
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    create_native_type(owner,
                       {TypeSpec{"FinalizedObject"}, LayoutDescriptor::of<FinalizedObject>()}, out);
    const auto type = roots.root(out.value());
    {
        RootScope local(owner);
        make_str(owner, U"retained child", out);
        const auto child = local.root(out.value());
        owner.allocate_instance<FinalizedObject>(type, out, &gate, child.get(owner));
        out.reset();
    }
    std::future<void> worker;
    {
        NativeWait parked(owner);
        worker = std::async(std::launch::async, [&] {
            Attachment context(interpreter);
            context.collect();
        });
        gate.entered.wait();
    }
    EXPECT_EQ(interpreter.gc_state().active, 2U);
    SourceUnit unit{SourceBuffer("total = 0\nfor i in range(200):\n    total += i\n")};
    run(owner, {&unit}, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    owner.collect();
    EXPECT_EQ(gate.destroyed.load(), 0U);
    gate.release.store(true, std::memory_order_release);
    {
        NativeWait parked(owner);
        EXPECT_NO_THROW(worker.get());
    }
    EXPECT_EQ(gate.calls.load(), 1U);
    EXPECT_EQ(gate.destroyed.load(), 1U);
}
} // namespace
