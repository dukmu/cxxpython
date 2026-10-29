#include "cxxpython/core/runtime.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    using namespace cxxpython::core;

    // Synthetic storage layouts, not Python types or a second dispatch engine.
    struct Node
    {
        ObjectHeader header;
        GcEdge child;
        int *destroyed;
        explicit Node(ObjectHeader head, int *count = nullptr)
            : header(head), destroyed(count) {}
        ~Node() noexcept { if (destroyed) ++*destroyed; }
        void trace(Tracer &tracer) const noexcept { tracer.visit(child); }
    };

    struct alignas(128) AlignedLeaf
    {
        ObjectHeader header;
        std::byte payload[128]{};
        explicit AlignedLeaf(ObjectHeader head) : header(head) {}
        void trace(Tracer &) const noexcept {}
    };

    struct FailingNode
    {
        ObjectHeader header;
        explicit FailingNode(ObjectHeader head) : header(head)
        { throw std::bad_alloc(); }
        void trace(Tracer &) const noexcept {}
    };

    Handle make_node(Attachment &context, RootScope &scope, int *destroyed = nullptr)
    {
        ReturnSlot out(context);
        context.allocate<Node>(out, destroyed);
        return scope.root(out.result().value());
    }

    TEST(FoundationMemory, BoxedValuesHaveStableIdentityAndSmallHeader)
    {
        EXPECT_EQ(sizeof(Value), sizeof(void *));
        EXPECT_EQ(sizeof(ObjectHeader), sizeof(void *));
        int destroyed = 0;
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope scope(context);
        auto node = make_node(context, scope, &destroyed);
        Value before = node.get(context);
        context.collect();
        EXPECT_EQ(node.get(context), before);
        EXPECT_EQ(destroyed, 0);
        EXPECT_EQ(context.stats().objects, 1U);
    }

    TEST(FoundationMemory, RootsSurviveGrowthAndNonLifoRelease)
    {
        int destroyed = 0;
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope scope(context);
        std::vector<Handle> handles;
        for (int i = 0; i < 300; ++i)
            handles.push_back(make_node(context, scope, &destroyed));
        auto saved = handles.back().get(context);
        for (std::size_t i = 0; i < handles.size(); i += 2)
            scope.release(handles[i]);
        EXPECT_EQ(context.collect(), 150U);
        EXPECT_EQ(destroyed, 150);
        EXPECT_EQ(handles.back().get(context), saved);
        auto replacement = make_node(context, scope, &destroyed);
        EXPECT_THROW(handles[0].get(context), std::logic_error);
        EXPECT_NO_THROW(replacement.get(context));
        // Scope cleanup must not release a newer root occupying an old cell.
        EXPECT_EQ(context.stats().roots, 151U);
    }

    TEST(FoundationMemory, StaleScopeEntriesDoNotReleaseReusedRootCells)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope outer(context);
        Handle survivor;
        {
            RootScope inner(context);
            auto released = make_node(context, inner);
            inner.release(released);
            survivor = make_node(context, outer);
        }
        context.collect();
        EXPECT_NO_THROW(survivor.get(context));
        EXPECT_EQ(context.stats().objects, 1U);
    }

    TEST(FoundationMemory, HeaderTypeAndPayloadEdgesKeepCyclesAlive)
    {
        int destroyed = 0;
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope scope(context);
        auto type = make_node(context, scope, &destroyed);
        auto instance = make_node(context, scope, &destroyed);
        context.store(type, context.payload<Node>(type).child, instance);
        context.store(instance, context.payload<Node>(instance).header.type, type);
        scope.release(type);
        EXPECT_EQ(context.collect(), 0U);
        scope.release(instance);
        EXPECT_EQ(context.collect(), 2U);
        EXPECT_EQ(destroyed, 2);
    }

    TEST(FoundationMemory, ReturnAndRaisedPayloadsOutliveCalleeRoots)
    {
        int destroyed = 0;
        Interpreter interpreter;
        Attachment context(interpreter);
        ReturnSlot out(context);
        {
            RootScope callee(context);
            auto value = make_node(context, callee, &destroyed);
            out.ok(value.get(context));
        }
        auto view = out.result();
        EXPECT_EQ(view.tag(), ResultTag::Ok);
        EXPECT_EQ(context.collect(), 0U);
        EXPECT_NO_THROW(view.value());
        out.raised(view.value());
        EXPECT_THROW(view.value(), std::logic_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_THROW(out.result().value(), std::logic_error);
        EXPECT_NO_THROW(out.result().exception());
        EXPECT_EQ(context.collect(), 0U);
        out.reset();
        EXPECT_EQ(context.collect(), 1U);
        EXPECT_EQ(destroyed, 1);
    }

    TEST(FoundationMemory, ResultViewsRejectReleasedAndReusedCells)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        ReturnSlot live(context);
        context.allocate<Node>(live);
        auto old_view = live.result();
        live.reset();
        EXPECT_THROW(old_view.value(), std::logic_error);
        EXPECT_EQ(live.result().tag(), ResultTag::Empty);
        EXPECT_THROW(live.result().value(), std::logic_error);
        context.allocate<Node>(live);
        EXPECT_THROW(old_view.value(), std::logic_error);
    }

    TEST(FoundationMemory, ResultViewsRejectSlotDestructionAndReuse)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        std::optional<ResultView> view;
        {
            ReturnSlot callee(context);
            context.allocate<Node>(callee);
            view = callee.result();
        }
        ReturnSlot replacement(context);
        context.allocate<Node>(replacement);
        EXPECT_THROW(view->value(), std::logic_error);
    }

    TEST(FoundationMemory, RootReleaseChecksScopeAndDoesNotInvalidateAnotherRoot)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope first(context);
        RootScope second(context);
        auto root = make_node(context, first);
        EXPECT_THROW(second.release(root), std::logic_error);
        EXPECT_NO_THROW(root.get(context));
        first.release(root);
        EXPECT_THROW(first.release(root), std::logic_error);
    }

    TEST(FoundationMemory, LayoutAndDestructionDoNotDependOnPythonTypeLifetime)
    {
        int destroyed = 0;
        {
            Interpreter interpreter;
            Attachment context(interpreter);
            RootScope scope(context);
            // Type is inserted first and may be destroyed before its instance.
            auto type = make_node(context, scope, &destroyed);
            auto instance = make_node(context, scope, &destroyed);
            context.store(instance, context.payload<Node>(instance).header.type, type);
            scope.release(type);
            scope.release(instance);
            EXPECT_EQ(context.collect(), 2U);
            EXPECT_EQ(destroyed, 2);
        }
        EXPECT_EQ(destroyed, 2);
    }

    TEST(FoundationMemory, AlignmentAndNativeLayoutChecksAreIndependentOfType)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        ReturnSlot out(context);
        context.allocate<AlignedLeaf>(out);
        RootScope scope(context);
        auto aligned = scope.root(out.result().value());
        auto &payload = context.payload<AlignedLeaf>(aligned);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&payload) % alignof(AlignedLeaf), 0U);
        EXPECT_THROW(context.payload<Node>(aligned), std::logic_error);
    }

    TEST(FoundationMemory, FailedConstructionDoesNotPublishOrLeak)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        ReturnSlot out(context);
        EXPECT_THROW(context.allocate<FailingNode>(out), std::bad_alloc);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        EXPECT_EQ(context.stats().objects, 0U);
        EXPECT_EQ(context.stats().live_bytes, 0U);
    }

    TEST(FoundationMemory, ForcedGcAtAllocationPreservesCallerRoots)
    {
        int destroyed = 0;
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter);
        RootScope scope(context);
        auto owner = make_node(context, scope, &destroyed);
        {
            RootScope temporary(context);
            auto child = make_node(context, temporary, &destroyed);
            context.store(owner, context.payload<Node>(owner).child, child);
        }
        for (int i = 0; i < 100; ++i)
        {
            ReturnSlot out(context);
            context.allocate<Node>(out, &destroyed);
        }
        context.collect();
        EXPECT_EQ(context.stats().objects, 2U);
        EXPECT_EQ(destroyed, 100);
        EXPECT_GE(context.stats().collections, 100U);
    }

    TEST(FoundationMemory, AutomaticGcCanBeDisabledWithoutDisablingExplicitCollection)
    {
        int destroyed = 0;
        Interpreter interpreter({.minimum_gc_budget = sizeof(Node)});
        Attachment context(interpreter);
        context.set_gc_enabled(false);
        for (int i = 0; i < 20; ++i)
        {
            ReturnSlot out(context);
            context.allocate<Node>(out, &destroyed);
        }
        EXPECT_EQ(destroyed, 0);
        EXPECT_EQ(context.collect(), 20U);
        EXPECT_EQ(destroyed, 20);
    }

    TEST(FoundationMemory, LeafAllocationsTriggerAutomaticFullCollection)
    {
        Interpreter interpreter({.minimum_gc_budget = 1024});
        Attachment context(interpreter);
        for (int i = 0; i < 100; ++i)
        {
            ReturnSlot out(context);
            context.allocate<AlignedLeaf>(out);
        }
        EXPECT_GT(context.stats().collections, 0U);
        EXPECT_LT(context.stats().live_bytes, 1024U);
    }

    TEST(FoundationMemory, ExternalBuffersCountTowardPressureAndReclaimedBytes)
    {
        Interpreter interpreter({.minimum_gc_budget = 1024});
        Attachment context(interpreter);
        RootScope scope(context);
        auto root = make_node(context, scope);
        std::vector<std::byte> buffer(2048);
        context.account_external(root, buffer.capacity());
        EXPECT_EQ(context.stats().live_bytes, sizeof(Node) + buffer.capacity());
        const auto before = context.stats().collections;
        ReturnSlot out(context);
        context.allocate<Node>(out);
        EXPECT_EQ(context.stats().collections, before + 1);
        context.account_external(root, 1024);
        EXPECT_EQ(context.stats().live_bytes, 2 * sizeof(Node) + 1024);
        out.reset();
        scope.release(root);
        EXPECT_EQ(context.collect(), 2U);
        EXPECT_EQ(context.stats().reclaimed_bytes, 2 * sizeof(Node) + 1024);
        EXPECT_EQ(context.stats().live_bytes, 0U);
    }

    TEST(FoundationMemory, StoreRejectsEdgesOutsideTheOwnerAllocation)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope scope(context);
        auto a = make_node(context, scope);
        auto b = make_node(context, scope);
        EXPECT_THROW(context.store(a, context.payload<Node>(b).child, a), std::logic_error);
        context.store(a, context.payload<Node>(a).child, b);
        context.clear(a, context.payload<Node>(a).child);
        scope.release(b);
        EXPECT_EQ(context.collect(), 1U);
    }

    TEST(FoundationMemory, StorageDoesNotGrowWithHistoricalAllocations)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        for (int round = 0; round < 30; ++round)
        {
            {
                RootScope scope(context);
                for (int i = 0; i < 50; ++i)
                    make_node(context, scope);
            }
            EXPECT_EQ(context.collect(), 50U);
        }
        EXPECT_EQ(context.stats().objects, 0U);
        EXPECT_LE(context.stats().root_cells, 51U);
        EXPECT_EQ(context.stats().live_bytes, 0U);
    }

    TEST(FoundationMemory, InterpreterIsolationRejectsForeignRootsResultsAndEdges)
    {
        Interpreter first;
        Interpreter second;
        Attachment left(first);
        Attachment right(second);
        RootScope left_scope(left);
        RootScope right_scope(right);
        auto a = make_node(left, left_scope);
        auto b = make_node(right, right_scope);
        EXPECT_THROW(right_scope.root(a.get(left)), std::logic_error);
        EXPECT_THROW(a.get(right), std::logic_error);
        ReturnSlot out(right);
        EXPECT_THROW(out.ok(a.get(left)), std::logic_error);
        EXPECT_THROW(left.allocate<Node>(out), std::logic_error);
        EXPECT_THROW(left.store(a, left.payload<Node>(a).child, b), std::logic_error);
        EXPECT_THROW(left.payload<AlignedLeaf>(b), std::logic_error);
        EXPECT_EQ(left.collect(), 0U);
        EXPECT_EQ(right.collect(), 0U);
    }

    TEST(FoundationThreads, NativeWaitPublishesRootsAndRestoresExecutionPermission)
    {
        int destroyed = 0;
        Interpreter interpreter;
        Attachment context(interpreter);
        RootScope scope(context);
        auto node = make_node(context, scope, &destroyed);
        std::future<std::size_t> collected;
        {
            NativeWait wait(context);
            EXPECT_THROW(node.get(context), std::logic_error);
            EXPECT_THROW(context.collect(), std::logic_error);
            collected = std::async(std::launch::async, [&] {
                Attachment other(interpreter);
                return other.collect();
            });
            EXPECT_EQ(collected.get(), 0U);
        }
        EXPECT_NO_THROW(node.get(context));
        EXPECT_EQ(destroyed, 0);
    }

    TEST(FoundationThreads, ExecutionPermissionSerializesSharedHeapAccess)
    {
        Interpreter interpreter;
        int counter = 0;
        std::vector<std::thread> threads;
        for (int tid = 0; tid < 4; ++tid)
            threads.emplace_back([&] {
                for (int i = 0; i < 100; ++i)
                {
                    Attachment context(interpreter);
                    ++counter;
                    ReturnSlot out(context);
                    context.allocate<Node>(out);
                    context.collect();
                }
            });
        for (auto &thread : threads)
            thread.join();
        EXPECT_EQ(counter, 400);
    }

    TEST(FoundationThreads, SeparateInterpretersDoNotShareAnExecutionLock)
    {
        Interpreter first;
        Interpreter second;
        Attachment held(first);
        auto progress = std::async(std::launch::async, [&] {
            Attachment independent(second);
            ReturnSlot out(independent);
            independent.allocate<Node>(out);
            return independent.stats().objects;
        });
        EXPECT_EQ(progress.get(), 1U);
    }

    TEST(FoundationThreads, NativeWaitRestoresPermissionDuringHostExceptionUnwind)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        try
        {
            NativeWait wait(context);
            throw std::runtime_error("host failure");
        }
        catch (const std::runtime_error &)
        {
        }
        EXPECT_NO_THROW(context.collect());
    }

    TEST(FoundationThreads, AttachmentRejectsAccessFromAnotherOsThread)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        auto failure = std::async(std::launch::async, [&] {
            EXPECT_THROW(context.collect(), std::logic_error);
        });
        failure.get();
    }

    TEST(FoundationThreads, ClosingRejectsAdmissionButExistingLeaseCanResume)
    {
        auto interpreter = std::make_unique<Interpreter>();
        Attachment context(*interpreter);
        RootScope scope(context);
        auto node = make_node(context, scope);
        {
            NativeWait wait(context);
            interpreter->close();
            EXPECT_THROW(Attachment denied(*interpreter), std::logic_error);
            interpreter.reset();
        }
        EXPECT_NO_THROW(node.get(context));
        EXPECT_EQ(context.collect(), 0U);
    }
}
