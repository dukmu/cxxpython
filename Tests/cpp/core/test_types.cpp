#include "cxxpython/core/type.hpp"

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace cxxpython::core;

    struct NativeNumber
    {
        ObjectHeader header;
        std::int64_t value;
        NativeNumber(ObjectHeader head, std::int64_t value_in) : header(head), value(value_in) {}
        void trace(Tracer &) const noexcept {}
    };

    struct OtherLayout
    {
        ObjectHeader header;
        double value;
        OtherLayout(ObjectHeader head, double value_in) : header(head), value(value_in) {}
        void trace(Tracer &) const noexcept {}
    };

    Handle make_type(Attachment &context, RootScope &scope, std::string name,
                     std::initializer_list<Handle> bases = {})
    {
        ReturnSlot out(context);
        create_type(context, TypeSpec{std::move(name), bases}, out);
        return scope.root(out.result().value());
    }

    std::vector<std::string> mro_names(Attachment &context, Handle type)
    {
        RootScope scope(context);
        std::vector<std::string> names;
        for (Value entry : context.payload<TypeObject>(type).mro)
        {
            const auto &name = std::get<std::u32string>(context.payload<TypeObject>(scope.root(entry)).name);
            names.emplace_back(name.begin(), name.end());
        }
        return names;
    }

    TEST(FoundationTypes, BootstrapClosesObjectTypeCyclesAndBasicMro)
    {
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter);
        EXPECT_THROW(context.object_type(), std::logic_error);
        context.bootstrap();
        RootScope scope(context);
        auto object = scope.root(context.object_type());
        auto type = scope.root(context.type_type());
        EXPECT_EQ(context.type_of(object), type.get(context));
        EXPECT_EQ(context.type_of(type), type.get(context));
        EXPECT_EQ(context.payload<TypeObject>(object).state, TypeState::Structural);
        EXPECT_EQ(context.payload<TypeObject>(type).state, TypeState::Structural);
        EXPECT_EQ(mro_names(context, object), (std::vector<std::string>{"object"}));
        EXPECT_EQ(mro_names(context, type), (std::vector<std::string>{"type", "object"}));
        EXPECT_TRUE(is_subtype(context, type, object));
        EXPECT_FALSE(is_subtype(context, object, type));
        EXPECT_EQ(context.stats().objects, 2U);
        EXPECT_EQ(context.stats().collections, 0U);
        EXPECT_EQ(context.collect(), 0U);
    }

    TEST(FoundationTypes, BootstrapIsIdempotentAndSurvivesAllTemporaryRoots)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        const Value identity = context.type_type();
        const auto bytes = context.stats().live_bytes;
        for (int i = 0; i < 10; ++i)
        {
            context.bootstrap();
            context.collect();
        }
        EXPECT_EQ(context.type_type(), identity);
        EXPECT_EQ(context.stats().objects, 2U);
        EXPECT_EQ(context.stats().roots, 0U);
        EXPECT_EQ(context.stats().live_bytes, bytes);
    }

    TEST(FoundationTypes, BootstrapRollsBackEachManagedAllocationFailureAndCanRetry)
    {
        for (std::size_t limit = 0; limit < 2; ++limit)
        {
            Interpreter interpreter({.collect_every_allocation = true});
            Attachment context(interpreter);
            context.set_allocation_limit(limit);
            EXPECT_THROW(context.bootstrap(), std::bad_alloc);
            EXPECT_THROW(context.type_type(), std::logic_error);
            EXPECT_EQ(context.stats().objects, 0U);
            EXPECT_EQ(context.stats().roots, 0U);
            EXPECT_EQ(context.stats().live_bytes, 0U);
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            context.bootstrap();
            EXPECT_EQ(context.stats().objects, 2U);
            const auto collections = context.stats().collections;
            ReturnSlot out(context);
            context.allocate<InstanceObject>(out);
            EXPECT_GT(context.stats().collections, collections);
        }
    }

    TEST(FoundationTypes, FailedBootstrapPreservesExistingCallerRootsAndGcPolicy)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        ReturnSlot sentinel(context);
        context.allocate<InstanceObject>(sentinel);
        const Value saved = sentinel.result().value();
        context.set_gc_enabled(false);
        context.set_allocation_limit(1);
        EXPECT_THROW(context.bootstrap(), std::bad_alloc);
        EXPECT_EQ(sentinel.result().value(), saved);
        EXPECT_EQ(context.stats().objects, 1U);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        const auto before = context.stats().collections;
        context.allocate<InstanceObject>(sentinel);
        EXPECT_EQ(context.stats().collections, before);
    }

    TEST(FoundationTypes, PublishedSeedGraphIdentitySurvivesSequentialCoreAdmission)
    {
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter); context.bootstrap(); const auto identity = context.type_type();
        {
            NativeWait wait(context);
            for (int i = 0; i < 4; ++i)
            {
                auto worker = std::async(std::launch::async, [&] {
                    Attachment other(interpreter); other.bootstrap(); other.collect(); return other.type_type();
                });
                EXPECT_EQ(worker.get(), identity);
            }
        }
        EXPECT_EQ(context.type_type(), identity);
        EXPECT_EQ(context.stats().objects, 2U);
    }

    TEST(FoundationTypes, EachInterpreterHasItsOwnUniqueTypeSeed)
    {
        Interpreter a;
        Interpreter b;
        Attachment left(a);
        Attachment right(b);
        left.bootstrap();
        right.bootstrap();
        EXPECT_NE(left.type_type(), right.type_type());
        RootScope left_scope(left);
        RootScope right_scope(right);
        auto x = make_type(left, left_scope, "X");
        auto y = make_type(left, left_scope, "Y");
        EXPECT_EQ(left.type_of(x), left.type_type());
        EXPECT_EQ(left.type_of(y), left.type_type());
        auto foreign = right_scope.root(right.object_type());
        ReturnSlot out(left);
        EXPECT_THROW(create_type(left, TypeSpec{"Bad", {&foreign, 1}}, out), std::logic_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        ReturnSlot foreign_out(right);
        foreign_out.ok(right.type_type());
        EXPECT_THROW(create_type(left, TypeSpec{"WrongOutput"}, foreign_out), std::logic_error);
        EXPECT_EQ(foreign_out.result().value(), right.type_type());
    }

    TEST(FoundationTypes, C3DiamondPreservesLocalOrderAndMonotonicity)
    {
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        auto a = make_type(context, scope, "A");
        auto b = make_type(context, scope, "B", {a});
        auto c = make_type(context, scope, "C", {a});
        auto d = make_type(context, scope, "D", {b, c});
        EXPECT_EQ(mro_names(context, d), (std::vector<std::string>{"D", "B", "C", "A", "object"}));
        EXPECT_TRUE(is_subtype(context, d, a));
        EXPECT_FALSE(is_subtype(context, a, d));
        EXPECT_EQ(context.collect(), 0U);
    }

    TEST(FoundationTypes, DuplicateBasesAndInconsistentMroAreRejectedWithoutPublishing)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        auto x = make_type(context, scope, "X");
        auto y = make_type(context, scope, "Y");
        auto a = make_type(context, scope, "A", {x, y});
        auto b = make_type(context, scope, "B", {y, x});
        auto before = context.stats().objects;
        ReturnSlot out(context);
        std::array duplicate{x, x};
        EXPECT_THROW(create_type(context, TypeSpec{"Duplicate", duplicate}, out), std::invalid_argument);
        std::array inconsistent{a, b};
        EXPECT_THROW(create_type(context, TypeSpec{"Impossible", inconsistent}, out), std::invalid_argument);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        EXPECT_EQ(context.stats().objects, before);
    }

    TEST(FoundationTypes, UnreadyBasesCannotBeUsedForStructuralTypeCreation)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        ReturnSlot staged(context);
        context.allocate<TypeObject>(staged, U"Unready", LayoutDescriptor::of<InstanceObject>());
        auto unready = scope.root(staged.result().value());
        ReturnSlot out(context);
        EXPECT_THROW(create_type(context, TypeSpec{"Bad", {&unready, 1}}, out), std::invalid_argument);
        EXPECT_THROW(context.allocate_instance<InstanceObject>(unready, out), std::invalid_argument);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    }

    TEST(FoundationTypes, TypeCreationAllocationFailureLeavesCallerOutputEmpty)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        context.set_allocation_limit(0);
        ReturnSlot out(context);
        out.ok(context.object_type());
        EXPECT_THROW(create_type(context, TypeSpec{"Failing"}, out), std::bad_alloc);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        EXPECT_EQ(context.stats().objects, 2U);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_NO_THROW(create_type(context, TypeSpec{"Retry"}, out));
    }

    TEST(FoundationTypes, MetaclassSelectionUsesTheMostDerivedCompatibleType)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        auto type = scope.root(context.type_type());
        auto meta = make_type(context, scope, "Meta", {type});
        auto submeta = make_type(context, scope, "SubMeta", {meta});
        auto unrelated = make_type(context, scope, "OtherMeta", {type});
        ReturnSlot out(context);
        create_type(context, TypeSpec{"A", {}, meta}, out);
        auto a = scope.root(out.result().value());
        create_type(context, TypeSpec{"B", {}, submeta}, out);
        auto b = scope.root(out.result().value());
        std::array bases{a, b};
        create_type(context, TypeSpec{"C", bases}, out);
        auto c = scope.root(out.result().value());
        EXPECT_EQ(context.type_of(c), submeta.get(context));
        EXPECT_THROW(create_type(context, TypeSpec{"Bad", bases, unrelated}, out), std::invalid_argument);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        create_type(context, TypeSpec{"Other", {}, unrelated}, out);
        auto other = scope.root(out.result().value());
        std::array conflict{a, other};
        EXPECT_THROW(create_type(context, TypeSpec{"Conflict", conflict}, out), std::invalid_argument);
    }

    TEST(FoundationTypes, NativeLayoutIsInheritedWithoutConflatingPythonAndCppInheritance)
    {
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        ReturnSlot out(context);
        create_native_type(context, NativeTypeSpec{{"number"}, LayoutDescriptor::of<NativeNumber>()}, out);
        auto number = scope.root(out.result().value());
        auto child = make_type(context, scope, "Child", {number});
        context.allocate_instance<NativeNumber>(child, out, 42);
        auto instance = scope.root(out.result().value());
        EXPECT_EQ(context.type_of(instance), child.get(context));
        EXPECT_EQ(context.payload<NativeNumber>(instance).value, 42);
        EXPECT_THROW(context.payload<OtherLayout>(instance), std::logic_error);
        EXPECT_THROW(context.allocate_instance<OtherLayout>(child, out, 1.0), std::invalid_argument);
        EXPECT_EQ(context.collect(), 0U);
    }

    TEST(FoundationTypes, UnrelatedSolidBasesConflictEvenIfTheirCppLayoutMatches)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        ReturnSlot out(context);
        create_native_type(context, NativeTypeSpec{{"Left"}, LayoutDescriptor::of<NativeNumber>()}, out);
        auto left = scope.root(out.result().value());
        create_native_type(context, NativeTypeSpec{{"Right"}, LayoutDescriptor::of<NativeNumber>()}, out);
        auto right = scope.root(out.result().value());
        std::array incompatible{left, right};
        EXPECT_THROW(create_type(context, TypeSpec{"Bad", incompatible}, out), std::invalid_argument);
        auto subclass = make_type(context, scope, "Subclass", {left});
        // This host-native mixin has no instance fields. Python heap mixins with
        // a dictionary require the native-tail layout planner, not just C3.
        create_native_type(context, NativeTypeSpec{{"Mixin"}, LayoutDescriptor::of<InstanceObject>()}, out);
        auto mixin = scope.root(out.result().value());
        std::array compatible{mixin, subclass};
        create_type(context, TypeSpec{"Good", compatible}, out);
        auto good = scope.root(out.result().value());
        context.allocate_instance<NativeNumber>(good, out, 7);
        EXPECT_EQ(context.stats().objects, 8U);
    }

    TEST(FoundationTypes, NativeDefinitionsCannotOverrideAnIncompatibleInheritedPrefix)
    {
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap();
        RootScope scope(context);
        ReturnSlot out(context);
        create_native_type(context, NativeTypeSpec{{"Number"}, LayoutDescriptor::of<NativeNumber>()}, out);
        auto base = scope.root(out.result().value());
        NativeTypeSpec invalid{{"Changed", {&base, 1}}, LayoutDescriptor::of<OtherLayout>()};
        EXPECT_THROW(create_native_type(context, invalid, out), std::invalid_argument);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    }

    TEST(FoundationTypes, DynamicTypesAndInstancesAreCollectedTogetherUnderForcedGc)
    {
        Interpreter interpreter({.collect_every_allocation = true});
        Attachment context(interpreter);
        context.bootstrap();
        for (int i = 0; i < 30; ++i)
        {
            {
                RootScope scope(context);
                auto type = make_type(context, scope, "TemporaryClassWithExternalNameStorage");
                ReturnSlot out(context);
                context.allocate_instance<InstanceObject>(type, out);
                auto instance = scope.root(out.result().value());
                const auto expected_type = type.get(context);
                scope.release(type);
                EXPECT_EQ(context.collect(), 0U);
                EXPECT_EQ(context.type_of(instance), expected_type);
                auto retained_type = scope.root(context.type_of(instance));
                EXPECT_EQ(std::get<std::u32string>(context.payload<TypeObject>(retained_type).name),
                          U"TemporaryClassWithExternalNameStorage");
            }
            EXPECT_EQ(context.collect(), 2U);
            EXPECT_EQ(context.stats().objects, 2U);
        }
    }
}
