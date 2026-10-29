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
        EpochLeaf(InitialType header, std::atomic<int> *destroyed = nullptr, std::int64_t value = 42)
            : header(header), destroyed(destroyed), value(value) {}
        ~EpochLeaf() noexcept { if (destroyed) ++*destroyed; }
        void trace(Tracer &) const noexcept {}
    };
    struct CollectingConstructor
    {
        ObjectHeader header;
        CollectingConstructor(InitialType head, Attachment &context) : header(head) { context.collect(); }
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

    TEST(FoundationEpochs, PublishedObjectOpsAllowsMultipleActiveMutators)
    {
        Interpreter interpreter; Attachment owner(interpreter); owner.bootstrap_core();
        auto admitted = std::async(std::launch::async, [&] {
            Attachment other(interpreter); EXPECT_TRUE(other.core_ready()); EXPECT_EQ(interpreter.gc_state().active, 2U);
        }); admitted.get();
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

    TEST(FoundationEpochs, RetiredSpansKeepTransferredSurvivorsUntilFinalCollection)
    {
        std::atomic<int> destroyed{0}; Interpreter interpreter; Attachment owner(interpreter);
        RootScope roots(owner); std::promise<Value> published; std::promise<void> release; auto released = release.get_future();
        auto worker = std::async(std::launch::async, [&] {
            Attachment thread(interpreter); ReturnSlot out(thread); thread.allocate<EpochLeaf>(out, &destroyed);
            const auto value = out.result().value(); NativeWait frozen(thread); published.set_value(value); released.get();
        });
        // The sender keeps its root continuously until the receiver has a root.
        auto input = published.get_future(); std::optional<Value> value;
        { NativeWait wait(owner); value = input.get(); }
        const auto keep = roots.root(*value); release.set_value();
        { NativeWait wait(owner); worker.get(); }
        EXPECT_EQ(owner.stats().registered_threads, 1U); EXPECT_EQ(owner.stats().spans, 1U);
        EXPECT_EQ(owner.collect(), 0U); EXPECT_EQ(owner.payload<EpochLeaf>(keep).value, 42);
        roots.release(keep); EXPECT_EQ(owner.collect(), 1U); EXPECT_EQ(destroyed.load(), 1);
        EXPECT_EQ(owner.stats().spans, 0U); EXPECT_EQ(owner.stats().reserved_bytes, 0U);
    }

    TEST(FoundationEpochs, ParallelQuotaCommitsOnlyTheAvailableAllocations)
    {
        std::atomic<int> succeeded{0}, destroyed{0}; Interpreter interpreter; Attachment owner(interpreter);
        owner.set_gc_enabled(false); owner.set_allocation_limit(37); std::barrier start(4);
        {
            NativeWait wait(owner); std::vector<std::future<void>> workers;
            for (int i = 0; i < 4; ++i) workers.push_back(std::async(std::launch::async, [&] {
                Attachment thread(interpreter); ReturnSlot out(thread); start.arrive_and_wait();
                for (int n = 0; n < 100; ++n)
                    try { thread.allocate<EpochLeaf>(out, &destroyed); ++succeeded; }
                    catch (const std::bad_alloc &) {}
            }));
            for (auto &worker : workers) worker.get();
        }
        EXPECT_EQ(succeeded.load(), 37); EXPECT_EQ(owner.stats().objects, 37U);
        // A prepared object may lose the finite-quota commit race and must be
        // destroyed immediately; those rollbacks are not GC reclamations.
        const auto rolled_back = destroyed.load();
        owner.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_EQ(owner.collect(), 37U); EXPECT_EQ(destroyed.load(), rolled_back + 37); EXPECT_EQ(owner.stats().reserved_bytes, 0U);
    }

    TEST(FoundationEpochs, ConcurrentExternalAccountingSurvivesRendezvousAndRetirement)
    {
        Interpreter interpreter; Attachment owner(interpreter); ReturnSlot out(owner); owner.allocate<EpochLeaf>(out);
        const auto shared = out.result().value();
        {
            NativeWait wait(owner); std::vector<std::future<void>> workers;
            for (int i = 0; i < 4; ++i) workers.push_back(std::async(std::launch::async, [&, i] {
                Attachment thread(interpreter); RootScope roots(thread); const auto keep = roots.root(shared);
                for (int n = 0; n < 200; ++n)
                {
                    thread.account_external(keep, 1024 * (1 + (i + n) % 4));
                    if (n % 31 == 0) thread.collect(); else thread.poll();
                }
            }));
            for (auto &worker : workers) worker.get();
        }
        const auto bytes = owner.stats().live_bytes; EXPECT_GE(bytes, sizeof(EpochLeaf) + 1024); EXPECT_LE(bytes, sizeof(EpochLeaf) + 4096);
        EXPECT_THROW(owner.account_external(shared, std::numeric_limits<std::size_t>::max()), std::bad_alloc);
        EXPECT_EQ(owner.stats().live_bytes, bytes); out.reset(); EXPECT_EQ(owner.collect(), 1U); EXPECT_EQ(owner.stats().reserved_bytes, 0U);
    }

    TEST(FoundationEpochs, NoSafepointRejectsStoppingAndGrowingRootsWithoutChangingOutput)
    {
        Interpreter interpreter; Attachment context(interpreter); RootScope roots(context); ReturnSlot out(context);
        context.allocate<EpochLeaf>(out); const auto original = out.result().value(); const auto keep = roots.root(original);
        {
            NoSafepoint outer(context);
            { NoSafepoint inner(context); EXPECT_THROW(context.poll(), std::logic_error); }
            EXPECT_THROW(context.collect(), std::logic_error);
            EXPECT_THROW(context.stats(), std::logic_error);
            EXPECT_THROW(ReturnSlot another(context), std::logic_error);
            EXPECT_THROW(RootScope another(context), std::logic_error);
            EXPECT_THROW(roots.root(original), std::logic_error);
            EXPECT_THROW(roots.release(keep), std::logic_error);
            EXPECT_THROW(context.allocate<EpochLeaf>(out), std::logic_error); EXPECT_EQ(out.result().value(), original);
            EXPECT_THROW(NativeWait wait(context), std::logic_error);
            bool entered = false;
            EXPECT_THROW(entered = context.enter_call(), std::logic_error); if (entered) context.leave_call();
            EXPECT_EQ(keep.get(context), original);
        }
        EXPECT_NO_THROW(context.poll());
        EXPECT_NO_THROW(context.collect());
    }

    TEST(FoundationEpochs, UnpublishedConstructionCannotEnterGcAndCanRetry)
    {
        Interpreter interpreter; Attachment context(interpreter); ReturnSlot out(context);
        EXPECT_THROW(context.allocate<CollectingConstructor>(out, context), std::logic_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty); EXPECT_EQ(context.stats().objects, 0U);
        EXPECT_EQ(context.stats().collections, 0U); context.collect(); EXPECT_EQ(context.stats().reserved_bytes, 0U);
        context.allocate<EpochLeaf>(out); EXPECT_EQ(context.payload<EpochLeaf>(out.result().value()).value, 42);
    }

    TEST(FoundationEpochs, AtomicCellReadsKeepReplacedValuesRootedAndValidateBeforeWriting)
    {
        static_assert(!std::is_copy_constructible_v<AtomicGcEdge>);
        EXPECT_EQ(sizeof(AtomicGcEdge), sizeof(void *));
        std::atomic<int> destroyed{0}; Interpreter interpreter, foreign; Attachment context(interpreter), other(foreign);
        RootScope roots(context); ReturnSlot out(context), other_out(other);
        context.allocate<EpochLeaf>(out, &destroyed); const auto original = out.result().value(); const auto initial = roots.root(original);
        context.allocate<CellObject>(out, initial.get(context)); const auto cell = roots.root(out.result().value()); roots.release(initial);
        context.allocate<CellObject>(out); const auto second = roots.root(out.result().value());
        ASSERT_TRUE(read_cell(context, cell.get(context), out)); const auto view = out.result();
        other.allocate<EpochLeaf>(other_out); const auto foreign_value = other_out.result().value();
        EXPECT_THROW(write_cell(context, cell.get(context), foreign_value), std::logic_error);
        EXPECT_THROW(read_cell(context, cell.get(context), other_out), std::logic_error);
        EXPECT_THROW(context.load(cell.get(context), context.payload<CellObject>(second).contents, out), std::logic_error);
        EXPECT_EQ(view.value(), original); EXPECT_EQ(other_out.result().value(), foreign_value);
        { NoSafepoint protected_read(context); ASSERT_TRUE(read_cell(context, cell.get(context), out)); write_cell(context, cell.get(context), {}); }
        context.collect(); EXPECT_EQ(destroyed.load(), 0); EXPECT_EQ(context.payload<EpochLeaf>(out.result().value()).value, 42);
        EXPECT_FALSE(read_cell(context, cell.get(context), out)); context.collect(); EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(FoundationEpochs, AtomicCellLoadCanReplaceItsOnlyOwnerRoot)
    {
        std::atomic<int> destroyed{0}; Interpreter interpreter; Attachment context(interpreter); ReturnSlot out(context); RootScope roots(context);
        context.allocate<EpochLeaf>(out, &destroyed); const auto value = out.result().value();
        const auto initial = roots.root(value); context.allocate<CellObject>(out, initial.get(context)); roots.release(initial); const auto cell = out.result().value();
        ASSERT_TRUE(read_cell(context, cell, out)); EXPECT_EQ(out.result().value(), value);
        EXPECT_EQ(context.collect(), 1U); EXPECT_EQ(destroyed.load(), 0);
        out.reset(); EXPECT_EQ(context.collect(), 1U); EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(FoundationEpochs, TransferRootsSurviveSenderRetirementAndPublishBeforeUnlink)
    {
        std::atomic<int> destroyed{0}; Interpreter interpreter; Transfer transfer;
        {
            Attachment sender(interpreter); RootScope roots(sender); ReturnSlot out(sender);
            sender.allocate<EpochLeaf>(out, &destroyed); transfer = sender.make_transfer(roots.root(out.result().value()));
        }
        EXPECT_EQ(interpreter.gc_state().registered, 0U); EXPECT_EQ(interpreter.gc_state().transfer_roots, 1U);
        Attachment receiver(interpreter); ReturnSlot out(receiver); EXPECT_EQ(receiver.collect(), 0U); EXPECT_EQ(receiver.stats().objects, 2U);
        receiver.receive(transfer, out); EXPECT_FALSE(transfer); EXPECT_EQ(receiver.stats().transfer_roots, 0U);
        EXPECT_EQ(receiver.collect(), 1U); EXPECT_EQ(receiver.payload<EpochLeaf>(out.result().value()).value, 42);
        out.reset(); EXPECT_EQ(receiver.collect(), 1U); EXPECT_EQ(destroyed.load(), 1); EXPECT_EQ(receiver.stats().reserved_bytes, 0U);
    }

    TEST(FoundationEpochs, TransferMovesAndCancellationUnlinkHeadMiddleAndTail)
    {
        std::atomic<int> destroyed{0}; Interpreter interpreter; std::array<Transfer, 4> tokens;
        {
            Attachment source(interpreter); source.set_gc_enabled(false); ReturnSlot out(source); RootScope roots(source);
            for (std::size_t i = 0; i < tokens.size(); ++i)
            { source.allocate<EpochLeaf>(out, &destroyed, i); tokens[i] = source.make_transfer(roots.root(out.result().value())); }
        }
        Attachment context(interpreter); ReturnSlot out(context); EXPECT_EQ(context.collect(), 0U);
        tokens[2].reset(); tokens[0] = std::move(tokens[3]); Transfer moved(std::move(tokens[1])); tokens[1] = std::move(moved);
        EXPECT_FALSE(tokens[3]); EXPECT_FALSE(moved); EXPECT_EQ(context.stats().transfer_roots, 2U);
        EXPECT_EQ(context.collect(), 4U); EXPECT_EQ(destroyed.load(), 2);
        context.receive(tokens[0], out); EXPECT_EQ(context.payload<EpochLeaf>(out.result().value()).value, 3);
        EXPECT_EQ(context.collect(), 1U); tokens[1].reset(); EXPECT_EQ(context.collect(), 2U);
        out.reset(); EXPECT_EQ(context.collect(), 1U); EXPECT_EQ(destroyed.load(), 4); EXPECT_EQ(context.stats().objects, 0U);
    }

    TEST(FoundationEpochs, TransferRejectsForeignAndProtectedUseWithoutConsumingOrOverwriting)
    {
        Interpreter interpreter, foreign; Attachment context(interpreter), other(foreign); RootScope roots(context), foreign_roots(other);
        ReturnSlot out(context), other_out(other); context.allocate<EpochLeaf>(out); other.allocate<EpochLeaf>(other_out);
        const auto value = roots.root(out.result().value()); auto transfer = context.make_transfer(value);
        const auto before = out.result(); const auto foreign_before = other_out.result();
        EXPECT_THROW(context.receive(transfer, other_out), std::logic_error);
        EXPECT_THROW(other.receive(transfer, other_out), std::logic_error);
        const auto foreign_value = foreign_roots.root(other_out.result().value());
        EXPECT_THROW(context.make_transfer(foreign_value), std::logic_error);
        {
            NoSafepoint protected_read(context);
            EXPECT_THROW(context.receive(transfer, out), std::logic_error);
            EXPECT_THROW(context.make_transfer(value), std::logic_error);
        }
        EXPECT_TRUE(transfer); EXPECT_EQ(before.value(), value.get(context)); EXPECT_EQ(foreign_before.value(), foreign_value.get(other));
        context.receive(transfer, out); const auto delivered = out.result();
        EXPECT_THROW(context.receive(transfer, out), std::logic_error);
        EXPECT_EQ(delivered.value(), value.get(context));
    }

    TEST(FoundationEpochs, TransferAllocationFailureLeavesNoPublishedRootAndCanRetry)
    {
        Interpreter interpreter; Attachment context(interpreter); RootScope roots(context); ReturnSlot out(context);
        context.allocate<EpochLeaf>(out); const auto value = roots.root(out.result().value());
        context.set_allocation_limit(0);
        EXPECT_THROW(context.make_transfer(value), std::bad_alloc);
        EXPECT_EQ(context.stats().transfer_roots, 0U); EXPECT_EQ(context.stats().objects, 1U); EXPECT_EQ(out.result().value(), value.get(context));
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max()); auto transfer = context.make_transfer(value);
        transfer.reset(); EXPECT_EQ(context.collect(), 1U); out.reset(); roots.release(value); EXPECT_EQ(context.collect(), 1U);
    }

    TEST(FoundationEpochs, TransferLeaseKeepsClosedStorageAliveUntilNativeCancellation)
    {
        std::atomic<int> destroyed{0}; Transfer transfer;
        {
            Interpreter interpreter; Attachment context(interpreter); ReturnSlot out(context); RootScope roots(context);
            context.allocate<EpochLeaf>(out, &destroyed); transfer = context.make_transfer(roots.root(out.result().value()));
        }
        EXPECT_EQ(destroyed.load(), 0);
        auto cancel = std::async(std::launch::async, [token = std::move(transfer)]() mutable { token.reset(); }); cancel.get();
        EXPECT_FALSE(transfer); EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(FoundationEpochs, ExistingReceiverCanAcceptAfterInterpreterOwnerCloses)
    {
        std::atomic<int> destroyed{0}; auto interpreter = std::make_unique<Interpreter>(); Transfer transfer;
        {
            Attachment source(*interpreter); RootScope roots(source); ReturnSlot out(source);
            source.allocate<EpochLeaf>(out, &destroyed); transfer = source.make_transfer(roots.root(out.result().value()));
        }
        Attachment receiver(*interpreter); interpreter.reset();
        ReturnSlot out(receiver); receiver.receive(transfer, out); EXPECT_EQ(receiver.collect(), 1U);
        EXPECT_EQ(receiver.payload<EpochLeaf>(out.result().value()).value, 42); out.reset(); receiver.collect(); EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(FoundationEpochs, SharedCellReplacementsUseTransferredInputsAndRootedSnapshotsDuringGc)
    {
        std::atomic<int> destroyed{0}; Interpreter interpreter({.minimum_gc_budget = 4096}); std::array<Transfer, 2> inputs;
        {
            Attachment source(interpreter); RootScope roots(source); ReturnSlot out(source);
            source.allocate<EpochLeaf>(out, &destroyed, 0); const auto initial = roots.root(out.result().value());
            source.allocate<CellObject>(out, initial.get(source)); const auto shared = roots.root(out.result().value());
            for (auto &input : inputs) input = source.make_transfer(shared);
        }
        std::barrier start(2);
        auto reader = std::async(std::launch::async, [&, input = std::move(inputs[0])]() mutable {
            Attachment context(interpreter); ReturnSlot cell(context), value(context); context.receive(input, cell); start.arrive_and_wait();
            for (int n = 0; n < 1000; ++n)
            {
                const bool present = read_cell(context, cell.result().value(), value);
                const auto expected = present ? context.payload<EpochLeaf>(value.result().value()).value : -1;
                if (n % 17 == 0) context.collect(); else context.poll();
                if (present) { EXPECT_EQ(context.payload<EpochLeaf>(value.result().value()).value, expected); }
            }
        });
        auto writer = std::async(std::launch::async, [&, input = std::move(inputs[1])]() mutable {
            Attachment context(interpreter); ReturnSlot cell(context), value(context); context.receive(input, cell); start.arrive_and_wait();
            for (int n = 1; n <= 1000; ++n)
            {
                context.allocate<EpochLeaf>(value, &destroyed, n); write_cell(context, cell.result().value(), value.result().value()); value.reset();
                if (n % 7 == 0) write_cell(context, cell.result().value(), {});
                if (n % 31 == 0) context.collect();
            }
        });
        reader.get(); writer.get(); Attachment observer(interpreter); observer.collect();
        EXPECT_EQ(destroyed.load(), 1001); EXPECT_EQ(observer.stats().objects, 0U); EXPECT_EQ(observer.stats().transfer_roots, 0U);
        EXPECT_EQ(observer.stats().reserved_bytes, 0U);
    }
}
