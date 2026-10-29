#include "cxxpython/core/objects.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <future>
#include <thread>

namespace
{
    using namespace cxxpython::core;
    struct EpochLeaf
    {
        ObjectHeader header;
        std::atomic<int> *destroyed;
        const std::int64_t value;
        EpochLeaf(ObjectHeader header, std::atomic<int> *destroyed = nullptr, std::int64_t value = 42)
            : header(header), destroyed(destroyed), value(value) {}
        ~EpochLeaf() noexcept { if (destroyed) ++*destroyed; }
        void trace(Tracer &) const noexcept {}
    };
    void wait_request(Interpreter &interpreter)
    {
        while (interpreter.gc_state().phase != GcPhase::Requesting) std::this_thread::yield();
    }

    TEST(FoundationEpochs, MultipleActiveMutatorsAllocateAndCollectInOneHeap)
    {
        for (int count : {2, 4})
        {
            std::atomic<int> destroyed{0}; Interpreter interpreter({.minimum_gc_budget = 4096}); Attachment sender(interpreter);
            RootScope roots(sender); ReturnSlot shared(sender); sender.allocate<EpochLeaf>(shared, &destroyed);
            const auto value = shared.result().value(); auto keep = roots.root(value); shared.reset();
            std::barrier start(count + 1); std::vector<std::future<void>> workers;
            {
                NativeWait frozen(sender);
                for (int i = 0; i < count; ++i)
                    workers.push_back(std::async(std::launch::async, [&] {
                        Attachment thread(interpreter); RootScope local(thread); auto input = local.root(value); ReturnSlot out(thread);
                        start.arrive_and_wait();
                        for (int n = 0; n < 500; ++n)
                        {
                            EXPECT_EQ(thread.payload<EpochLeaf>(input).value, 42);
                            thread.allocate<EpochLeaf>(out, &destroyed);
                            if (n % 31 == 0) thread.collect();
                        }
                    }));
                while (interpreter.gc_state().active != static_cast<std::size_t>(count)) std::this_thread::yield();
                EXPECT_EQ(interpreter.gc_state().native_wait, 1U); start.arrive_and_wait();
                for (auto &worker : workers) worker.get();
            }
            sender.collect(); EXPECT_EQ(destroyed.load(), count * 500); EXPECT_EQ(sender.stats().objects, 1U);
            EXPECT_EQ(sender.stats().registered_threads, 1U); EXPECT_EQ(keep.get(sender), value);
            roots.release(keep); sender.collect(); EXPECT_EQ(destroyed.load(), count * 500 + 1);
        }
    }

    TEST(FoundationEpochs, SimultaneousCollectorsJoinOneEpochAndShareItsResult)
    {
        Interpreter interpreter; std::barrier start(2); std::array<std::future<void>, 2> workers;
        for (auto &worker : workers)
            worker = std::async(std::launch::async, [&] {
                Attachment thread(interpreter); ReturnSlot out(thread);
                for (int n = 0; n < 100; ++n)
                {
                    thread.allocate<EpochLeaf>(out); out.reset(); start.arrive_and_wait();
                    EXPECT_EQ(thread.collect(), 2U);
                }
            });
        for (auto &worker : workers) worker.get();
        const auto state = interpreter.gc_state(); EXPECT_EQ(state.collections, 100U);
        EXPECT_EQ(state.request_epoch, state.completed_epoch); EXPECT_EQ(state.registered, 0U);
    }

    TEST(FoundationEpochs, NativeWaitResumeAndNewAdmissionCannotEscapeAStopRequest)
    {
        Interpreter interpreter; std::promise<void> active_ready, allow_poll, parked_ready, leave_wait, collector_ready;
        auto poll_allowed = allow_poll.get_future(); auto wait_done = leave_wait.get_future(); std::atomic<bool> resumed{false}, attached{false};
        auto active = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ReturnSlot out(thread); thread.allocate<EpochLeaf>(out); active_ready.set_value();
            poll_allowed.get(); thread.poll(); EXPECT_EQ(thread.payload<EpochLeaf>(out.result().value()).value, 42);
        });
        active_ready.get_future().get();
        auto waiter = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ReturnSlot out(thread); thread.allocate<EpochLeaf>(out);
            { NativeWait wait(thread); parked_ready.set_value(); wait_done.get(); }
            resumed = true; thread.poll();
        });
        parked_ready.get_future().get();
        auto collector = std::async(std::launch::async, [&] { Attachment thread(interpreter); collector_ready.set_value(); return thread.collect(); });
        collector_ready.get_future().get(); wait_request(interpreter);
        std::promise<void> attach_started;
        auto incoming = std::async(std::launch::async, [&] { attach_started.set_value(); Attachment thread(interpreter); attached = true; });
        attach_started.get_future().get(); leave_wait.set_value();
        EXPECT_EQ(interpreter.gc_state().native_wait, 1U); EXPECT_FALSE(resumed.load()); EXPECT_FALSE(attached.load());
        EXPECT_EQ(interpreter.gc_state().completed_epoch, 0U);
        allow_poll.set_value(); active.get(); collector.get(); waiter.get(); incoming.get();
        EXPECT_TRUE(resumed.load()); EXPECT_TRUE(attached.load()); EXPECT_EQ(interpreter.gc_state().registered, 0U);
    }

    TEST(FoundationEpochs, AllocationFreePollingAcknowledgesGcEvenWhenAutomaticGcIsDisabled)
    {
        Interpreter interpreter; std::atomic<bool> done{false}; std::promise<void> ready;
        auto worker = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); thread.set_gc_enabled(false); ready.set_value();
            while (!done.load(std::memory_order_acquire)) thread.poll();
        });
        ready.get_future().get();
        { Attachment collector(interpreter); EXPECT_EQ(collector.collect(), 0U); done.store(true, std::memory_order_release); }
        worker.get(); EXPECT_EQ(interpreter.gc_state().collections, 1U);
    }

    TEST(FoundationEpochs, DiagnosticStopDoesNotSatisfyAnExplicitCollection)
    {
        Interpreter interpreter; std::atomic<int> destroyed{0}; std::atomic<bool> finished{false}; std::promise<void> ready, go;
        auto go_collect = go.get_future();
        auto collector = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ready.set_value(); go_collect.get(); const auto result = thread.collect();
            finished.store(true, std::memory_order_release); return result;
        });
        ready.get_future().get();
        auto observer = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ReturnSlot out(thread); thread.allocate<EpochLeaf>(out, &destroyed); out.reset();
            const auto before = thread.stats(); EXPECT_EQ(before.collections, 0U);
            while (!finished.load(std::memory_order_acquire)) thread.poll();
        });
        wait_request(interpreter); go.set_value(); EXPECT_EQ(collector.get(), 1U); observer.get(); EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(FoundationEpochs, UnconvertedObjectOpsParallelAdmissionIsExplicitlyRejected)
    {
        Interpreter interpreter; Attachment owner(interpreter); owner.bootstrap_core();
        auto denied = std::async(std::launch::async, [&] { EXPECT_THROW(Attachment other(interpreter), std::logic_error); }); denied.get();
        {
            NativeWait wait(owner);
            auto admitted = std::async(std::launch::async, [&] { Attachment other(interpreter); EXPECT_TRUE(other.core_ready()); other.collect(); }); admitted.get();
        }
        EXPECT_TRUE(owner.core_ready()); EXPECT_EQ(interpreter.gc_state().active, 1U);
    }

    TEST(FoundationEpochs, ClosingWakesPendingAttachWhileExitAcknowledgesTheCollector)
    {
        Interpreter interpreter; std::atomic<int> destroyed{0}; std::promise<void> ready, exit, attempting;
        auto exit_allowed = exit.get_future();
        auto owner = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ReturnSlot out(thread); thread.allocate<EpochLeaf>(out, &destroyed);
            ready.set_value(); exit_allowed.get();
        });
        ready.get_future().get();
        auto collector = std::async(std::launch::async, [&] { Attachment thread(interpreter); return thread.collect(); });
        wait_request(interpreter);
        auto pending = std::async(std::launch::async, [&] {
            attempting.set_value(); EXPECT_THROW(Attachment denied(interpreter), std::logic_error);
        });
        attempting.get_future().get(); interpreter.close(); pending.get();
        EXPECT_EQ(interpreter.gc_state().phase, GcPhase::Requesting);
        exit.set_value(); owner.get(); EXPECT_EQ(collector.get(), 1U);
        EXPECT_EQ(destroyed.load(), 1); EXPECT_EQ(interpreter.gc_state().registered, 0U);
    }

    TEST(FoundationEpochs, UnsafeBootstrapRejectsParallelStorageAdmissionWithoutPublishingSeeds)
    {
        Interpreter interpreter; Attachment owner(interpreter);
        auto worker = std::async(std::launch::async, [&] {
            Attachment other(interpreter); EXPECT_THROW(other.bootstrap(), std::logic_error);
            EXPECT_THROW(other.type_type(), std::logic_error);
        });
        worker.get(); EXPECT_EQ(owner.stats().objects, 0U); owner.bootstrap(); EXPECT_EQ(owner.stats().objects, 2U);
    }
}
