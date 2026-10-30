#include "cxxpython/objects.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <future>

namespace {
using namespace cxxpython::core;
struct InspectionNode {
    ObjectHeader header;
    GcEdge child;
    explicit InspectionNode(InitialType type) : header(type) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(child);
    }
};

TEST(FoundationInspection, HidesStorageAndPublishesRootsWithoutManagedAllocation)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap();
    RootScope owners(context);
    ReturnSlot out(context);
    create_native_type(
        context, NativeTypeSpec{{"Inspectable"}, LayoutDescriptor::of<InspectionNode>()}, out);
    auto type = owners.root(out.value());
    context.allocate_instance<InspectionNode>(type, out);
    auto source = owners.root(out.value());
    context.allocate<InspectionNode>(out);
    auto storage = owners.root(out.value());
    context.allocate_instance<InspectionNode>(type, out);
    auto target = owners.root(out.value());
    context.store(source, context.payload<InspectionNode>(source).child, storage);
    context.store(storage, context.payload<InspectionNode>(storage).child, target);
    const auto value = target.get(context);
    out.reset();
    {
        RootScope snapshot(context);
        std::vector<Value> objects;
        context.set_allocation_limit(0);
        context.inspect_heap(HeapInspection::Objects, {}, snapshot, objects);
        EXPECT_NE(std::find(objects.begin(), objects.end(), value), objects.end());
        EXPECT_EQ(std::find(objects.begin(), objects.end(), storage.get(context)), objects.end());
        objects.clear();
        const Value sources[] = {source.get(context)};
        context.inspect_heap(HeapInspection::Referents, sources, snapshot, objects);
        EXPECT_NE(std::find(objects.begin(), objects.end(), value), objects.end());
        EXPECT_EQ(std::find(objects.begin(), objects.end(), storage.get(context)), objects.end());
        objects.clear();
        const Value targets[] = {value};
        context.inspect_heap(HeapInspection::Referrers, targets, snapshot, objects);
        EXPECT_NE(std::find(objects.begin(), objects.end(), source.get(context)), objects.end());
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        context.clear(source, context.payload<InspectionNode>(source).child);
        owners.release(storage);
        owners.release(target);
        context.collect();
        EXPECT_TRUE(context.has_layout<InspectionNode>(value));
    }
    const auto before = context.stats();
    EXPECT_GE(context.collect(), 1U);
    const auto after = context.stats();
    EXPECT_GE(after.reclaimed_objects, before.reclaimed_objects + 1);
    EXPECT_EQ(after.allocations_since_gc, 0U);
}

TEST(FoundationInspection, CapturesRetiredThreadAllocationsAndSharedGraph)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap();
    RootScope roots(context);
    ReturnSlot out(context);
    create_native_type(
        context, NativeTypeSpec{{"Inspectable"}, LayoutDescriptor::of<InspectionNode>()}, out);
    auto type = roots.root(out.value());
    context.allocate_instance<InspectionNode>(type, out);
    auto source = roots.root(out.value());
    auto shared = source.get(context);
    auto shared_type = type.get(context);
    auto before = context.stats().allocations_since_gc;
    {
        NativeWait parked(context);
        std::async(std::launch::async, [&] {
            Attachment worker(interpreter);
            RootScope local(worker);
            auto owner = local.root(shared);
            auto type = local.root(shared_type);
            ReturnSlot result(worker);
            for (int i = 0; i < 10; ++i) {
                worker.allocate_instance<InspectionNode>(type, result);
                auto child = local.root(result.value());
                worker.store(owner, worker.payload<InspectionNode>(owner).child, child);
                local.release(child);
            }
        }).get();
    }
    EXPECT_EQ(context.stats().allocations_since_gc, before + 10);
    RootScope snapshot(context);
    std::vector<Value> objects;
    const Value targets[] = {shared};
    context.inspect_heap(HeapInspection::Referents, targets, snapshot, objects);
    EXPECT_EQ(objects.size(), 2U); // Actual type edge and the published child.
    context.collect();
    for (auto value : objects)
        EXPECT_NO_THROW(context.type_of(value));
}
} // namespace
