#include "cxxpython/objects.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

TEST(FoundationInspection, HidesTypeConstructionUntilSnapshotAndFlagsArePublished)
{
    Interpreter interpreter({.collect_every_allocation = true});
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    auto layout = LayoutDescriptor::of<InstanceObject>();
    layout.fields.slots = 1;
    context.allocate_instance<TypeObject>(roots.root(context.type_type()), out, U"Staged", layout);
    const auto type = roots.root(out.value());
    detail::initialize_type_dictionary(context, type);
    const auto dictionary = roots.root(*context.payload<TypeObject>(type).dictionary);
    make_str(context, U"field", out);
    const auto name = roots.root(out.value());
    context.allocate_instance<GetSetDescriptorObject>(
        roots.root(context.builtin(CoreObject::GetSetDescriptorType)), out, type.get(context),
        name.get(context), nullptr, nullptr);
    const auto getset = roots.root(out.value());
    context.allocate_instance<MemberDescriptorObject>(
        roots.root(context.builtin(CoreObject::MemberDescriptorType)), out, type.get(context),
        name.get(context), std::size_t{0});
    const auto member = roots.root(out.value());
    context.allocate_instance<MethodDescriptorObject>(
        roots.root(context.builtin(CoreObject::MethodDescriptorType)), out,
        context.builtin(CoreObject::None), type.get(context));
    const auto method = roots.root(out.value());
    const std::array staged{type.get(context), dictionary.get(context), getset.get(context),
                            member.get(context), method.get(context)};
    cxxpython::core::make_tuple(context, staged, out);
    const auto container = roots.root(out.value());
    const auto assert_hidden = [&] {
        RootScope snapshot(context);
        std::vector<Value> objects;
        context.set_allocation_limit(0);
        context.inspect_heap(HeapInspection::Objects, {}, snapshot, objects);
        for (auto value : staged)
            EXPECT_EQ(std::find(objects.begin(), objects.end(), value), objects.end());
        objects.clear();
        const std::array targets{container.get(context)};
        context.inspect_heap(HeapInspection::Referents, targets, snapshot, objects);
        for (auto value : staged)
            EXPECT_EQ(std::find(objects.begin(), objects.end(), value), objects.end());
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    };
    assert_hidden();
    auto &data = context.payload<TypeObject>(type);
    data.bases = {context.object_type()};
    data.base = context.object_type();
    data.solid_base = context.object_type();
    data.mro = {type.get(context), context.object_type()};
    data.slot_names = {U"field"};
    data.immutable = true;
    data.subclassable = false;
    data.state = TypeState::Structural;
    assert_hidden();
    detail::type_ready(context, type);
    ASSERT_EQ(data.state.load(), TypeState::Ready);
    ASSERT_TRUE(TypeView(context, type).snapshot());
    RootScope snapshot(context);
    std::vector<Value> objects;
    context.inspect_heap(HeapInspection::Objects, {}, snapshot, objects);
    for (auto value : staged)
        EXPECT_NE(std::find(objects.begin(), objects.end(), value), objects.end());

    TypeSpec spec{U"FrozenNative"};
    spec.immutable = true;
    spec.subclassable = false;
    create_type(context, spec, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto &published = context.payload<TypeObject>(out.value());
    EXPECT_TRUE(published.immutable);
    EXPECT_FALSE(published.subclassable);
    EXPECT_EQ(published.state.load(), TypeState::Ready);
    EXPECT_TRUE(TypeView(context, out.value()).snapshot());
}

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
