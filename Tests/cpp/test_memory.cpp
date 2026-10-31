#include "cxxpython/objects.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace cxxpython::core;

// Synthetic storage layouts, not Python types or a second dispatch engine.
struct Node {
    ObjectHeader header;
    GcEdge child;
    int *destroyed;
    explicit Node(InitialType head, int *count = nullptr) : header(head), destroyed(count) {}
    ~Node() noexcept
    {
        if (destroyed)
            ++*destroyed;
    }
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(child);
    }
};

struct alignas(128) AlignedLeaf {
    ObjectHeader header;
    std::byte payload[128]{};
    explicit AlignedLeaf(InitialType head) : header(head) {}
    void trace(Tracer &) const noexcept {}
};

struct FailingNode {
    ObjectHeader header;
    explicit FailingNode(InitialType head) : header(head)
    {
        throw std::bad_alloc();
    }
    void trace(Tracer &) const noexcept {}
};

struct CountedStorage {
    int &destroyed;
    explicit CountedStorage(int &count) : destroyed(count) {}
    ~CountedStorage()
    {
        ++destroyed;
    }
};
struct OwningNode {
    ObjectHeader header;
    const NativeOwner<CountedStorage> storage;
    OwningNode(InitialType head, int &destroyed, bool fail)
        : header(head), storage(std::make_unique<CountedStorage>(destroyed))
    {
        if (fail)
            throw std::bad_alloc();
    }
    void trace(Tracer &) const noexcept {}
};

struct alignas(4096) LargeLeaf {
    ObjectHeader header;
    std::byte payload[128 * 1024]{};
    explicit LargeLeaf(InitialType head) : header(head) {}
    void trace(Tracer &) const noexcept {}
};

Handle make_node(Attachment &context, RootScope &scope, int *destroyed = nullptr)
{
    ReturnSlot out(context);
    context.allocate<Node>(out, destroyed);
    return scope.root(out.result().value());
}

template <typename F> void expect_logic_error(F &&operation, const char *message)
{
    try {
        operation();
        ADD_FAILURE() << "Expected std::logic_error: " << message;
    } catch (const std::logic_error &error) {
        EXPECT_STREQ(error.what(), message);
    }
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

TEST(FoundationMemory, InterleavedScopeReleaseAndCellReusePreserveEachOwner)
{
    int destroyed = 0;
    Interpreter interpreter;
    Attachment context(interpreter);
    RootScope outer(context);
    auto retained = make_node(context, outer, &destroyed);
    {
        RootScope inner(context);
        auto tail = make_node(context, inner, &destroyed);
        auto middle = make_node(context, inner, &destroyed);
        auto head = make_node(context, inner, &destroyed);
        inner.release(middle);
        auto reused = make_node(context, outer, &destroyed);
        inner.release(head);
        inner.release(tail);
        make_node(context, inner, &destroyed);
        context.collect();
        EXPECT_EQ(destroyed, 3);
        EXPECT_NO_THROW(reused.get(context));
    }
    context.collect();
    EXPECT_EQ(destroyed, 4);
    EXPECT_NO_THROW(retained.get(context));
    EXPECT_EQ(context.stats().objects, 2U);
}

TEST(FoundationMemory, BulkScopeTeardownWithInterleavedSlotsAndHoles)
{
    int destroyed = 0;
    Interpreter interpreter;
    Attachment context(interpreter);
    const auto baseline = context.stats().roots;
    {
        RootScope outer(context);
        auto retained = make_node(context, outer, &destroyed);
        std::size_t first_cycle_cells = 0;
        for (int cycle = 0; cycle < 8; ++cycle) {
            Handle stale;
            Handle outer_interleaved;
            std::optional<ResultView> stale_view;
            {
                RootScope inner(context);
                stale = make_node(context, inner, &destroyed);
                auto hole = make_node(context, inner, &destroyed);
                {
                    ReturnSlot slot(context);
                    context.allocate<Node>(slot, &destroyed);
                    stale_view = slot.result();
                    outer_interleaved = make_node(context, outer, &destroyed);
                    make_node(context, inner, &destroyed);
                    inner.release(hole);
                    EXPECT_THROW(hole.get(context), std::logic_error);
                    EXPECT_EQ(context.collect(), 1U);
                    EXPECT_NO_THROW(stale_view->value());
                }
                EXPECT_THROW(stale_view->value(), std::logic_error);
                EXPECT_EQ(context.collect(), 1U);
                EXPECT_NO_THROW(stale.get(context));
                EXPECT_NO_THROW(outer_interleaved.get(context));
            }
            EXPECT_THROW(stale.get(context), std::logic_error);
            EXPECT_THROW(stale_view->value(), std::logic_error);
            EXPECT_EQ(context.collect(), 2U);
            EXPECT_NO_THROW(retained.get(context));
            EXPECT_NO_THROW(outer_interleaved.get(context));

            auto replacement = make_node(context, outer, &destroyed);
            ReturnSlot replacement_slot(context);
            context.allocate<Node>(replacement_slot, &destroyed);
            EXPECT_THROW(stale.get(context), std::logic_error);
            EXPECT_THROW(stale_view->value(), std::logic_error);
            EXPECT_NO_THROW(replacement.get(context));
            outer.release(replacement);
            outer.release(outer_interleaved);
            replacement_slot.reset();
            EXPECT_EQ(context.collect(), 3U);
            EXPECT_EQ(destroyed, (cycle + 1) * 7);
            EXPECT_EQ(context.stats().roots, baseline + 2U);
            if (cycle == 0)
                first_cycle_cells = context.stats().root_cells;
            else
                EXPECT_EQ(context.stats().root_cells, first_cycle_cells);
        }
        EXPECT_NO_THROW(retained.get(context));
        EXPECT_EQ(context.stats().roots, baseline + 1U);
    }
    EXPECT_EQ(context.stats().roots, baseline);
    EXPECT_EQ(context.collect(), 1U);
    EXPECT_EQ(destroyed, 57);
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

TEST(FoundationMemory, CurrentSlotReadsTrackWritesWhileSavedViewsExpire)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    ReturnSlot out(context);
    EXPECT_EQ(out.tag(), ResultTag::Empty);
    EXPECT_THROW(out.value(), std::logic_error);
    EXPECT_THROW(out.exception(), std::logic_error);
    context.allocate<Node>(out);
    const auto value = out.value();
    auto previous = out.result();
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    out.raised(value);
    EXPECT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(out.exception(), value);
    EXPECT_THROW(previous.value(), std::logic_error);
    EXPECT_THROW(out.value(), std::logic_error);
    {
        NativeWait wait(context);
        EXPECT_THROW(out.tag(), std::logic_error);
        EXPECT_THROW(out.exception(), std::logic_error);
    }
    out.reset();
    EXPECT_EQ(out.tag(), ResultTag::Empty);
    EXPECT_THROW(out.exception(), std::logic_error);
}

TEST(FoundationMemory, ResultViewsRejectReleasedAndReusedCells)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    ReturnSlot live(context);
    context.allocate<Node>(live);
    auto old_view = live.result();
    live.reset();
    expect_logic_error([&] { old_view.value(); }, "result view was overwritten");
    EXPECT_EQ(live.result().tag(), ResultTag::Empty);
    expect_logic_error([&] { live.result().value(); }, "result is not Ok");
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

TEST(FoundationMemory, CompoundRootAndPayloadValidationRetainsEveryBoundary)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    RootScope roots(context);
    auto node = make_node(context, roots);
    auto value = node.get(context);
    ReturnSlot result(context);
    result.ok(value);
    expect_logic_error([&] { context.payload<Node>(Handle{}); }, "foreign or expired root handle");
    expect_logic_error([&] { context.payload<AlignedLeaf>(node); },
                       "object has incompatible native layout");
    const auto count = context.stats().roots;
    EXPECT_EQ(context.try_payload<Node>(value), &context.payload<Node>(node));
    EXPECT_EQ(context.try_payload<AlignedLeaf>(value), nullptr);
    EXPECT_EQ(result.tag(), ResultTag::Ok);
    EXPECT_EQ(result.value(), value);
    EXPECT_EQ(context.stats().roots, count);
    {
        NoSafepoint commit(context);
        expect_logic_error([&] { roots.root(value); },
                           "operation may stop or allocate during a protected native operation");
        EXPECT_NO_THROW(context.payload<Node>(node));
        EXPECT_EQ(context.try_payload<Node>(value), &context.payload<Node>(node));
        EXPECT_EQ(context.try_payload<AlignedLeaf>(value), nullptr);
    }
    EXPECT_EQ(context.stats().roots, count);
    {
        NativeWait wait(context);
        expect_logic_error([&] { context.payload<Node>(node); },
                           "Python heap access requires an attached owning thread");
        expect_logic_error([&] { context.try_payload<Node>(value); },
                           "Python heap access requires an attached owning thread");
        expect_logic_error([&] { roots.root(value); },
                           "Python heap access requires an attached owning thread");
    }
    EXPECT_EQ(context.stats().roots, count);
    EXPECT_EQ(node.get(context), value);
    roots.release(node);
    auto replacement = make_node(context, roots);
    expect_logic_error([&] { context.payload<Node>(node); }, "foreign or expired root handle");
    EXPECT_NO_THROW(context.payload<Node>(replacement));
    EXPECT_EQ(context.stats().roots, count);
}

TEST(FoundationMemory, StandardLayoutNativeOwnersReleaseOnCollectionAndConstructorFailure)
{
    static_assert(std::is_standard_layout_v<OwningNode>);
    static_assert(!std::is_copy_constructible_v<NativeOwner<CountedStorage>>);
    static_assert(!std::is_move_constructible_v<NativeOwner<CountedStorage>>);
    Interpreter interpreter;
    Attachment context(interpreter);
    int destroyed = 0;
    {
        ReturnSlot out(context);
        context.allocate<OwningNode>(out, destroyed, false);
        context.collect();
        EXPECT_EQ(destroyed, 0);
    }
    context.collect();
    EXPECT_EQ(destroyed, 1);
    ReturnSlot out(context);
    EXPECT_THROW(context.allocate<OwningNode>(out, destroyed, true), std::bad_alloc);
    EXPECT_EQ(destroyed, 2);
    EXPECT_EQ(context.stats().objects, 0u);
}

TEST(FoundationMemory, LocalSpansReuseSweptSlotsAndReleaseEmptyStorage)
{
    int destroyed = 0;
    Interpreter interpreter;
    Attachment context(interpreter);
    context.set_gc_enabled(false);
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate<Node>(out, &destroyed);
    EXPECT_LT(context.stats().reserved_bytes, 16 * 1024U);
    for (int i = 1; i < 500; ++i)
        context.allocate<Node>(out, &destroyed);
    const auto survivor = make_node(context, roots, &destroyed);
    out.reset();
    EXPECT_EQ(context.collect(), 500U);
    const auto first = context.stats();
    EXPECT_EQ(first.spans, 1U);
    EXPECT_GT(first.reserved_bytes, sizeof(Node));
    for (int i = 0; i < 500; ++i)
        context.allocate<Node>(out, &destroyed);
    EXPECT_EQ(context.stats().spans, first.spans);
    EXPECT_EQ(context.stats().reserved_bytes, first.reserved_bytes);
    out.reset();
    roots.release(survivor);
    EXPECT_EQ(context.collect(), 501U);
    EXPECT_EQ(destroyed, 1001);
    EXPECT_EQ(context.stats().spans, 0U);
    EXPECT_EQ(context.stats().reserved_bytes, 0U);
}

TEST(FoundationMemory, ConstructorFailureReusesPrivateSlotsWithoutPublishing)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    ReturnSlot out(context);
    context.set_gc_enabled(false);
    for (int i = 0; i < 100; ++i)
        EXPECT_THROW(context.allocate<FailingNode>(out), std::bad_alloc);
    EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    EXPECT_EQ(context.stats().objects, 0U);
    EXPECT_EQ(context.stats().spans, 1U);
    EXPECT_EQ(context.stats().live_bytes, 0U);
    context.collect();
    EXPECT_EQ(context.stats().reserved_bytes, 0U);
}

TEST(FoundationMemory, AlternatingLayoutsReuseSweptSpansAndFailedConstructions)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    context.set_gc_enabled(false);
    RootScope roots(context);
    ReturnSlot out(context);
    std::vector<Handle> survivors;
    for (int i = 0; i < 1200; ++i) {
        context.allocate<Node>(out);
        if (i % 40 == 0)
            survivors.push_back(roots.root(out.value()));
        context.allocate<AlignedLeaf>(out);
        if (i % 40 == 0)
            survivors.push_back(roots.root(out.value()));
        EXPECT_THROW(context.allocate<FailingNode>(out), std::bad_alloc);
    }
    out.reset();
    context.collect();
    const auto before = context.stats();
    EXPECT_EQ(before.objects, survivors.size());
    for (int i = 0; i < 600; ++i) {
        context.allocate<AlignedLeaf>(out);
        context.allocate<Node>(out);
    }
    EXPECT_EQ(context.stats().spans, before.spans);
    EXPECT_EQ(context.stats().reserved_bytes, before.reserved_bytes);
    out.reset();
    context.collect();
    EXPECT_EQ(context.stats().objects, survivors.size());
    for (auto survivor : survivors)
        roots.release(survivor);
    context.collect();
    EXPECT_EQ(context.stats().spans, 0U);
    EXPECT_EQ(context.stats().reserved_bytes, 0U);
    // Empty-layout pruning must not leave stale cursor or span identities.
    context.allocate<AlignedLeaf>(out);
    context.allocate<Node>(out);
    EXPECT_EQ(context.stats().objects, 2U);
}

TEST(FoundationMemory, LargeOveralignedStorageUsesIndependentSpans)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    ReturnSlot out(context);
    RootScope roots(context);
    context.allocate<LargeLeaf>(out);
    const auto keep = roots.root(out.result().value());
    context.allocate<LargeLeaf>(out);
    out.reset();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&context.payload<LargeLeaf>(keep)) %
                  alignof(LargeLeaf),
              0U);
    EXPECT_EQ(context.stats().spans, 2U);
    EXPECT_GE(context.stats().reserved_bytes, 2 * sizeof(LargeLeaf));
    EXPECT_EQ(context.collect(), 1U);
    EXPECT_EQ(context.stats().spans, 1U);
    roots.release(keep);
    context.collect();
    EXPECT_EQ(context.stats().reserved_bytes, 0U);
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
    for (int i = 0; i < 100; ++i) {
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
    for (int i = 0; i < 20; ++i) {
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
    for (int i = 0; i < 100; ++i) {
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
    for (int round = 0; round < 30; ++round) {
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
    const auto left_value = a.get(left);
    const auto right_value = b.get(right);
    const auto right_roots = right.stats().roots;
    expect_logic_error([&] { right_scope.root(left_value); },
                       "value does not belong to this interpreter");
    expect_logic_error([&] { right.try_payload<Node>(left_value); },
                       "value does not belong to this interpreter");
    expect_logic_error([&] { a.get(right); }, "foreign or expired root handle");
    EXPECT_EQ(right.stats().roots, right_roots);
    ReturnSlot out(right);
    out.ok(right_value);
    expect_logic_error([&] { out.ok(left_value); }, "value does not belong to this interpreter");
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), right_value);
    expect_logic_error([&] { left.allocate<Node>(out); },
                       "operation requires a caller-owned return slot");
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), right_value);
    EXPECT_THROW(left.store(a, left.payload<Node>(a).child, b), std::logic_error);
    EXPECT_THROW(left.payload<AlignedLeaf>(b), std::logic_error);
    EXPECT_EQ(left.collect(), 0U);
    EXPECT_EQ(right.collect(), 0U);
}

TEST(FoundationThreads, NativeWaitPublishesRootsAndRestoresAdmission)
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

TEST(FoundationThreads, NativeCallbacksPublishResultsAndRefreezeRootsOnUnwind)
{
    int destroyed = 0;
    Interpreter interpreter;
    Attachment context(interpreter);
    RootScope caller(context);
    auto original = make_node(context, caller, &destroyed);
    ReturnSlot result(context);
    auto collect_elsewhere = [&] {
        return std::async(std::launch::async,
                          [&] {
                              Attachment other(interpreter);
                              return other.collect();
                          })
            .get();
    };
    {
        NativeWait native(context);
        {
            NativeCallback callback(native);
            RootScope local(context);
            auto value = make_node(context, local, &destroyed);
            result.ok(value.get(context));
            EXPECT_EQ(context.collect(), 0U);
            EXPECT_THROW(NativeCallback duplicate(native), std::logic_error);
            {
                NativeWait nested(context);
                EXPECT_EQ(collect_elsewhere(), 0U);
            }
        }
        EXPECT_THROW(result.value(), std::logic_error);
        EXPECT_EQ(collect_elsewhere(), 0U);
        EXPECT_THROW(
            {
                NativeCallback callback(native);
                RootScope local(context);
                make_node(context, local, &destroyed);
                throw std::runtime_error("callback failure");
            },
            std::runtime_error);
        EXPECT_THROW(context.require_attached(), std::logic_error);
        EXPECT_EQ(collect_elsewhere(), 1U);
        auto foreign = std::async(std::launch::async, [&] {
            EXPECT_THROW(NativeCallback wrong_thread(native), std::logic_error);
        });
        foreign.get();
    }
    EXPECT_NO_THROW(original.get(context));
    EXPECT_TRUE(context.has_layout<Node>(result.value()));
    EXPECT_EQ(destroyed, 1);
    result.reset();
    EXPECT_EQ(context.collect(), 1U);
    EXPECT_EQ(destroyed, 2);
}

TEST(FoundationThreads, SameInterpreterHandlesBelongToTheirOwningArena)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    RootScope scope(owner);
    auto node = make_node(owner, scope);
    const auto value = node.get(owner);
    {
        NativeWait wait(owner);
        auto reader = std::async(std::launch::async, [&] {
            Attachment other(interpreter);
            RootScope local(other);
            EXPECT_EQ(other.stats().registered_threads, 2U);
            const auto root_count = other.stats().roots;
            expect_logic_error([&] { node.get(other); }, "foreign or expired root handle");
            EXPECT_THROW(other.payload<Node>(node), std::logic_error);
            expect_logic_error([&] { local.release(node); }, "foreign or expired root handle");
            EXPECT_EQ(other.stats().roots, root_count);
            // Owner stays frozen/rooted until this bounded handoff ends.
            auto own_handle = local.root(value);
            EXPECT_EQ(own_handle.get(other), value);
            EXPECT_EQ(other.collect(), 0U);
        });
        reader.get();
        Attachment nested(interpreter);
        EXPECT_EQ(nested.stats().registered_threads, 2U);
        EXPECT_THROW(node.get(nested), std::logic_error);
    }
    EXPECT_EQ(node.get(owner), value);
    EXPECT_EQ(owner.stats().registered_threads, 1U);
}

TEST(FoundationThreads, RetiredRootArenasReleaseTheirStorageAndRejectOldHandles)
{
    Interpreter interpreter;
    Handle stale;
    for (int round = 0; round < 20; ++round) {
        Attachment context(interpreter);
        EXPECT_EQ(context.stats().root_cells, 0U);
        EXPECT_EQ(context.stats().registered_threads, 1U);
        if (round) {
            EXPECT_THROW(stale.get(context), std::logic_error);
        }
        context.collect();
        RootScope roots(context);
        for (int i = 0; i < 50; ++i)
            stale = make_node(context, roots);
        EXPECT_EQ(context.stats().roots, 50U);
    }
    Attachment observer(interpreter);
    EXPECT_EQ(observer.stats().root_cells, 0U);
    EXPECT_THROW(stale.get(observer), std::logic_error);
    EXPECT_EQ(observer.collect(), 50U);
}

TEST(FoundationThreads, CollectorScansEveryRegisteredFrozenArena)
{
    int destroyed = 0;
    Interpreter interpreter;
    Attachment owner(interpreter);
    RootScope roots(owner);
    auto first = make_node(owner, roots, &destroyed);
    std::promise<void> ready, resume;
    auto resume_requested = resume.get_future();
    {
        NativeWait wait(owner);
        auto worker = std::async(std::launch::async, [&] {
            Attachment other(interpreter);
            RootScope local(other);
            auto second = make_node(other, local, &destroyed);
            {
                NativeWait parked(other);
                ready.set_value();
                resume_requested.get();
            }
            EXPECT_NO_THROW(second.get(other));
        });
        ready.get_future().get();
        {
            Attachment collector(interpreter);
            EXPECT_EQ(collector.stats().roots, 2U);
            EXPECT_EQ(collector.stats().registered_threads, 3U);
            EXPECT_EQ(collector.collect(), 0U);
            EXPECT_THROW(first.get(collector), std::logic_error);
            {
                RootScope temporary(collector);
                make_node(collector, temporary, &destroyed);
            }
            EXPECT_EQ(collector.collect(), 1U);
            EXPECT_EQ(destroyed, 1);
        }
        resume.set_value();
        worker.get();
    }
    EXPECT_EQ(owner.stats().roots, 1U);
    EXPECT_LE(owner.stats().root_cells, 2U);
    EXPECT_EQ(owner.stats().registered_threads, 1U);
    EXPECT_EQ(owner.collect(), 1U);
    EXPECT_EQ(destroyed, 2);
    EXPECT_NO_THROW(first.get(owner));
    roots.release(first);
    EXPECT_EQ(owner.collect(), 1U);
    EXPECT_EQ(destroyed, 3);
}

TEST(FoundationThreads, RepeatedAdmissionAllocationAndRetirementRemainSafe)
{
    Interpreter interpreter;
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;
    for (int tid = 0; tid < 4; ++tid)
        threads.emplace_back([&] {
            for (int i = 0; i < 100; ++i) {
                Attachment context(interpreter);
                ++counter;
                ReturnSlot out(context);
                context.allocate<Node>(out);
                context.collect();
            }
        });
    for (auto &thread : threads)
        thread.join();
    EXPECT_EQ(counter.load(), 400);
}

TEST(FoundationThreads, SeparateInterpretersHaveIndependentCoordinators)
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
    try {
        NativeWait wait(context);
        throw std::runtime_error("host failure");
    } catch (const std::runtime_error &) {
    }
    EXPECT_NO_THROW(context.collect());
}

TEST(FoundationThreads, AttachmentRejectsAccessFromAnotherOsThread)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    auto failure =
        std::async(std::launch::async, [&] { EXPECT_THROW(context.collect(), std::logic_error); });
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
TEST(FoundationMemory, InternedStringsDoNotKeepUnusedManagedObjectsAlive)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap_core();
    context.collect();
    const auto baseline = context.stats().objects;
    {
        RootScope roots(context);
        ReturnSlot string(context), interned(context);
        for (unsigned n = 0; n < 100; ++n) {
            const auto digits = std::to_string(n);
            make_str(context, U"mortal intern " + std::u32string(digits.begin(), digits.end()),
                     string);
            auto input = roots.root(string.value());
            context.intern(input, interned);
            ASSERT_EQ(interned.tag(), ResultTag::Ok);
            EXPECT_EQ(interned.value(), string.value());
            roots.release(input);
            string.reset();
            interned.reset();
        }
    }
    context.collect();
    EXPECT_EQ(context.stats().objects, baseline);
}
} // namespace
