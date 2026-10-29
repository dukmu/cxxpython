#include "cxxpython/core/objects.hpp"

#include <gtest/gtest.h>

#include <future>

namespace
{
    using namespace cxxpython::core;
    struct alignas(128) NativePayload
    {
        ObjectHeader header;
        std::u32string text;
        int *destroyed;
        NativePayload(InitialType head, int *count = nullptr) : header(head), text(U"native prefix"), destroyed(count) {}
        ~NativePayload() noexcept { if (destroyed) ++*destroyed; }
        void trace(Tracer &) const noexcept {}
        std::size_t external_bytes() const noexcept { return text.capacity() * sizeof(char32_t); }
    };
    struct FailingPayload
    {
        ObjectHeader header;
        explicit FailingPayload(InitialType head) : header(head) { throw std::bad_alloc(); }
        void trace(Tracer &) const noexcept {}
    };

    class CoreLayout : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle type(std::u32string name, std::optional<std::vector<std::u32string>> slots,
                    std::initializer_list<Handle> bases = {})
        {
            TypeSpec spec{std::move(name), bases}; spec.slots = std::move(slots);
            ReturnSlot out(context); create_type(context, spec, out); return roots.root(out.result().value());
        }
        Handle instance(Handle cls)
        { ReturnSlot out(context); call(context, cls, {}, out); return roots.root(out.result().value()); }
        Handle get(Handle self, std::u32string name)
        {
            ReturnSlot out(context); get_attribute(context, self, text(std::move(name)), out);
            if (out.result().tag() != ResultTag::Ok) throw std::logic_error("unexpected attribute error");
            return roots.root(out.result().value());
        }
        void set(Handle self, std::u32string name, Handle value)
        {
            ReturnSlot out(context);
            ASSERT_EQ(set_attribute(context, self, text(std::move(name)), value, out).tag(), ResultTag::Ok);
        }
        void expect_error(ReturnSlot &out, CoreObject type)
        {
            ASSERT_EQ(out.result().tag(), ResultTag::Raised);
            EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
            context.collect();
        }
        Handle space()
        {
            ReturnSlot out(context);
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
            return roots.root(out.result().value());
        }
        void define(Handle slots, ReturnSlot &out, Handle dictionary)
        {
            detail::name_store(context, dictionary.get(context), text(U"__slots__").get(context), slots.get(context));
            auto name = text(U"PublicSlots"); ReturnSlot tuple(context);
            cxxpython::core::make_tuple(context, {}, tuple);
            auto bases = roots.root(tuple.result().value());
            std::array args{name.get(context), bases.get(context), dictionary.get(context)};
            call(context, roots.root(context.type_type()), {args, {}}, out);
        }
    };

    TEST_F(CoreLayout, EmptySlotsUseOnlyTheNativeHeaderAndSuppressDictionary)
    {
        auto cls = type(U"Empty", std::vector<std::u32string>{}); context.collect();
        const auto bytes = context.stats().live_bytes;
        ReturnSlot out(context); context.allocate_instance<InstanceObject>(cls, out);
        auto self = roots.root(out.result().value());
        EXPECT_EQ(context.stats().live_bytes - bytes, sizeof(ObjectHeader));
        EXPECT_EQ(context.instance_dictionary(self.get(context)), nullptr);
        get_attribute(context, self, text(U"__dict__"), out); expect_error(out, CoreObject::AttributeErrorType);
        set_attribute(context, self, text(U"extra"), text(U"value"), out); expect_error(out, CoreObject::AttributeErrorType);
    }

    TEST_F(CoreLayout, MemberDescriptorsDistinguishUnboundFromNoneAndCheckOwners)
    {
        auto cls = type(U"Slots", std::vector<std::u32string>{U"value"}); auto self = instance(cls);
        auto name = text(U"value"); ReturnSlot out(context);
        get_attribute(context, self, name, out); expect_error(out, CoreObject::AttributeErrorType);
        auto descriptor = get(cls, U"value");
        EXPECT_EQ(context.type_of(descriptor), context.builtin(CoreObject::MemberDescriptorType));
        auto none = roots.root(context.builtin(CoreObject::None)); set(self, U"value", none);
        EXPECT_EQ(get(self, U"value").get(context), none.get(context));
        del_attribute(context, self, name, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        del_attribute(context, self, name, out); expect_error(out, CoreObject::AttributeErrorType);
        auto getter = get(descriptor, U"__get__"); auto wrong = text(U"not an instance");
        std::array args{wrong.get(context), cls.get(context)};
        call(context, getter, {args, {}}, out); expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreLayout, DictionaryTailDoesNotShadowOrReplaceNamedSlots)
    {
        auto cls = type(U"SlotsAndDict", std::vector<std::u32string>{U"x", U"__dict__"}); auto self = instance(cls);
        auto slot_value = text(U"slot"); auto dict_value = text(U"dict"); set(self, U"x", slot_value);
        auto dictionary = get(self, U"__dict__");
        detail::name_store(context, dictionary.get(context), text(U"x").get(context), dict_value.get(context));
        EXPECT_EQ(get(self, U"x").get(context), slot_value.get(context));
        set(self, U"__dict__", space());
        EXPECT_EQ(get(self, U"x").get(context), slot_value.get(context));
        ReturnSlot out(context); del_attribute(context, self, text(U"x"), out);
        get_attribute(context, self, text(U"x"), out); expect_error(out, CoreObject::AttributeErrorType);
    }

    TEST_F(CoreLayout, SubclassWithoutSlotsAddsDictionaryAndKeepsInheritedMembers)
    {
        auto base = type(U"Base", std::vector<std::u32string>{U"x"});
        auto cls = type(U"Child", {}, {base}); auto self = instance(cls);
        auto value = text(U"value"); set(self, U"x", value); set(self, U"extra", value);
        EXPECT_EQ(get(self, U"x").get(context), value.get(context));
        EXPECT_EQ(detail::name_lookup(context, get(self, U"__dict__").get(context), U"extra"), value.get(context));
        EXPECT_EQ(context.payload<TypeObject>(cls).slot_names, (std::vector<std::u32string>{U"x"}));
    }

    TEST_F(CoreLayout, InheritedDictionaryDescriptorUsesTheActualInstancesTailOffset)
    {
        auto base = type(U"Base", {});
        auto cls = type(U"Child", std::vector<std::u32string>{U"x"}, {base}); auto self = instance(cls);
        auto slot = text(U"slot"); auto extra = text(U"extra");
        set(self, U"x", slot); set(self, U"extra", extra);
        EXPECT_EQ(get(self, U"x").get(context), slot.get(context));
        EXPECT_EQ(detail::name_lookup(context, get(self, U"__dict__").get(context), U"extra"), extra.get(context));
        EXPECT_NE(context.instance_dictionary(self.get(context)), &context.instance_slot(self.get(context), 0));
    }

    TEST_F(CoreLayout, RedeclaredMemberKeepsTheBaseDescriptorStorageDistinct)
    {
        auto base = type(U"Base", std::vector<std::u32string>{U"x"});
        auto cls = type(U"Child", std::vector<std::u32string>{U"x"}, {base}); auto self = instance(cls);
        auto base_descriptor = get(base, U"x"); auto setter = get(base_descriptor, U"__set__");
        auto base_value = text(U"base"); auto child_value = text(U"child");
        ReturnSlot out(context); std::array args{self.get(context), base_value.get(context)};
        call(context, setter, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        set(self, U"x", child_value); EXPECT_EQ(get(self, U"x").get(context), child_value.get(context));
        auto getter = get(base_descriptor, U"__get__"); std::array receiver{self.get(context), cls.get(context)};
        call(context, getter, {receiver, {}}, out);
        EXPECT_EQ(out.result().value(), base_value.get(context));
    }

    TEST_F(CoreLayout, MultipleInheritanceChecksStorageLineageNotJustCellCount)
    {
        auto a = type(U"A", std::vector<std::u32string>{U"x"});
        auto b = type(U"B", std::vector<std::u32string>{U"x"});
        std::array bases{a, b}; ReturnSlot out(context);
        EXPECT_THROW(create_type(context, {U"Conflict", bases}, out), TypeDefinitionError);
        auto mixin = type(U"Mixin", {});
        auto cls = type(U"Mixed", std::vector<std::u32string>{}, {mixin, a}); auto self = instance(cls);
        auto value = text(U"mixed"); set(self, U"x", value); set(self, U"extra", value);
        EXPECT_EQ(get(self, U"x").get(context), value.get(context));
        EXPECT_EQ(get(self, U"extra").get(context), value.get(context));
    }

    TEST_F(CoreLayout, OverAlignedNativePrefixAndTailHaveIndependentLifetimes)
    {
        int destroyed = 0; ReturnSlot out(context);
        create_native_type(context, {{"Native"}, LayoutDescriptor::of<NativePayload>()}, out);
        auto native = roots.root(out.result().value());
        auto cls = type(U"NativeChild", std::vector<std::u32string>{U"x", U"__dict__"}, {native});
        context.allocate_instance<NativePayload>(cls, out, &destroyed);
        auto self = roots.root(out.result().value());
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(self.get(context).object()) % alignof(NativePayload), 0U);
        auto value = text(U"tail"); set(self, U"x", value); set(self, U"extra", value);
        EXPECT_EQ(context.payload<NativePayload>(self).text, U"native prefix");
        EXPECT_EQ(get(self, U"x").get(context), value.get(context));
        context.collect(); EXPECT_EQ(destroyed, 0);
        roots.release(self); out.reset(); context.collect(); EXPECT_EQ(destroyed, 1);
    }

    TEST_F(CoreLayout, ConstructorFailureReleasesTheWholeAlignedStorageBeforePublication)
    {
        ReturnSlot out(context);
        create_native_type(context, {{"Failing"}, LayoutDescriptor::of<FailingPayload>()}, out);
        auto native = roots.root(out.result().value());
        auto cls = type(U"Child", std::vector<std::u32string>{U"x", U"y"}, {native});
        context.collect(); const auto baseline = context.stats().objects;
        EXPECT_THROW(context.allocate_instance<FailingPayload>(cls, out), std::bad_alloc);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        EXPECT_EQ(context.stats().objects, baseline);
    }

    TEST_F(CoreLayout, NativeTypesAndCyclicTailInstancesCanBeSweptInTheSameBatch)
    {
        int destroyed = 0; context.collect(); const auto baseline = context.stats().objects;
        for (int i = 0; i < 12; ++i)
        {
            {
                RootScope local(context); ReturnSlot temporary(context);
                create_native_type(context, {{"Native"}, LayoutDescriptor::of<NativePayload>()}, temporary);
                auto native = local.root(temporary.result().value());
                TypeSpec spec{U"Child", {&native, 1}}; spec.slots = std::vector<std::u32string>{U"cycle"};
                create_type(context, spec, temporary);
                auto cls = local.root(temporary.result().value());
                context.allocate_instance<NativePayload>(cls, temporary, &destroyed);
                context.store(temporary.result().value(), context.instance_slot(temporary.result().value(), 0), temporary.result().value());
            }
            context.collect();
            EXPECT_EQ(destroyed, i + 1);
            EXPECT_EQ(context.stats().objects, baseline);
        }
    }

    TEST_F(CoreLayout, MemberEdgesSurviveRemovingTheDescriptorAndReclaimCycles)
    {
        auto cls = type(U"Slots", std::vector<std::u32string>{U"x"}); auto self = instance(cls);
        ReturnSlot out(context); make_str(context, U"owned by the slot only", out);
        auto name = text(U"x");
        {
            RootScope local(context);
            set_attribute(context, self, name, local.root(out.result().value()), out);
        }
        del_attribute(context, cls, name, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        context.collect();
        ReturnSlot value(context); context.load(self.get(context), context.instance_slot(self.get(context), 0), value);
        ASSERT_EQ(value.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(value.result().value()).value, U"owned by the slot only");
        value.reset(); context.store(self.get(context), context.instance_slot(self.get(context), 0), self.get(context));
        const auto before = context.stats().objects;
        roots.release(self); out.reset(); context.collect();
        EXPECT_LT(context.stats().objects, before);
    }

    TEST_F(CoreLayout, ClassAssignmentChecksSortedNamesAndStorageAncestry)
    {
        auto a = type(U"A", std::vector<std::u32string>{U"y", U"x"});
        auto b = type(U"B", std::vector<std::u32string>{U"x", U"y"});
        auto c = type(U"C", std::vector<std::u32string>{U"x", U"z"});
        auto self = instance(a); auto value = text(U"survives class switch"); set(self, U"x", value);
        set(self, U"__class__", b); EXPECT_EQ(get(self, U"x").get(context), value.get(context));
        ReturnSlot out(context); set_attribute(context, self, text(U"__class__"), c, out);
        expect_error(out, CoreObject::TypeErrorType);
        auto base_a = type(U"BaseA", std::vector<std::u32string>{U"a"});
        auto base_b = type(U"BaseB", std::vector<std::u32string>{U"a"});
        auto child_a = type(U"ChildA", std::vector<std::u32string>{U"b"}, {base_a});
        auto child_b = type(U"ChildB", std::vector<std::u32string>{U"b"}, {base_b});
        auto child = instance(child_a);
        set_attribute(context, child, text(U"__class__"), child_b, out); expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreLayout, MatchingCppPrefixesDoNotMakeIndependentNativeBasesInterchangeable)
    {
        ReturnSlot out(context);
        create_native_type(context, {{"NativeA"}, LayoutDescriptor::of<NativePayload>()}, out);
        auto a = roots.root(out.result().value());
        create_native_type(context, {{"NativeB"}, LayoutDescriptor::of<NativePayload>()}, out);
        auto b = roots.root(out.result().value());
        context.allocate_instance<NativePayload>(a, out); auto self = roots.root(out.result().value());
        set_attribute(context, self, text(U"__class__"), b, out); expect_error(out, CoreObject::TypeErrorType);
        auto child_a = type(U"ChildA", std::vector<std::u32string>{U"x"}, {a});
        auto child_b = type(U"ChildB", std::vector<std::u32string>{U"x"}, {a});
        context.allocate_instance<NativePayload>(child_a, out); auto child = roots.root(out.result().value());
        auto value = text(U"tail"); set(child, U"x", value); set(child, U"__class__", child_b);
        EXPECT_EQ(get(child, U"x").get(context), value.get(context));
        EXPECT_EQ(context.payload<NativePayload>(child).text, U"native prefix");
    }

    TEST_F(CoreLayout, ChangingTheSlotsAttributeDoesNotRewritePublishedLayouts)
    {
        auto cls = type(U"Slots", std::vector<std::u32string>{U"x"}); auto before = context.payload<TypeObject>(cls).instance_layout;
        auto first = instance(cls); auto value = text(U"value"); set(first, U"x", value);
        set(cls, U"__slots__", text(U"different"));
        auto second = instance(cls); set(second, U"x", value);
        EXPECT_EQ(get(first, U"x").get(context), value.get(context));
        EXPECT_EQ(get(second, U"x").get(context), value.get(context));
        EXPECT_EQ(context.payload<TypeObject>(cls).instance_layout, before);
        EXPECT_EQ(context.instance_dictionary(second.get(context)), nullptr);
    }

    TEST_F(CoreLayout, FrozenRootsPermitTailMutationAndCollectionFromAnotherAttachment)
    {
        auto cls = type(U"Slots", std::vector<std::u32string>{U"x"}); auto self = instance(cls); auto name = text(U"x");
        const auto shared_self = self.get(context), shared_name = name.get(context);
        {
            NativeWait wait(context);
            auto finished = std::async(std::launch::async, [&] {
                Attachment other(interpreter); RootScope scope(other); ReturnSlot out(other);
                auto receiver = scope.root(shared_self); auto attribute = scope.root(shared_name);
                make_str(other, U"from another attachment", out);
                auto value = scope.root(out.result().value());
                set_attribute(other, receiver, attribute, value, out);
                EXPECT_EQ(out.result().tag(), ResultTag::Ok);
                other.collect();
            });
            finished.get();
        }
        context.collect();
        EXPECT_EQ(context.payload<StrObject>(get(self, U"x")).value, U"from another attachment");
    }

    TEST_F(CoreLayout, SlotDefinitionAllocationFailuresLeaveNoPartiallyPublishedTypes)
    {
        context.collect(); const auto baseline = context.stats(); bool completed = false;
        TypeSpec spec{U"Slots"}; spec.slots = std::vector<std::u32string>{U"x", U"y", U"__dict__"};
        for (std::size_t quota = 0; quota < 100; ++quota)
        {
            {
                ReturnSlot out(context); context.set_allocation_limit(quota);
                try { create_type(context, spec, out); completed = true; }
                catch (const std::bad_alloc &) { EXPECT_EQ(out.result().tag(), ResultTag::Empty); }
                context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            }
            context.collect();
            EXPECT_EQ(context.stats().objects, baseline.objects) << quota;
            EXPECT_EQ(context.stats().roots, baseline.roots) << quota;
            if (completed) break;
        }
        EXPECT_TRUE(completed);
    }

    TEST_F(CoreLayout, PublicTypeNewAcceptsStringTupleAndMappingSlotNames)
    {
        ReturnSlot out(context); define(text(U"x"), out, space());
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto cls = roots.root(out.result().value()); auto self = instance(cls); auto value = text(U"value");
        set(self, U"x", value); EXPECT_EQ(get(self, U"x").get(context), value.get(context));
        auto names = space(); detail::name_store(context, names.get(context), text(U"y").get(context), value.get(context));
        define(names, out, space()); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        cls = roots.root(out.result().value()); self = instance(cls); set(self, U"y", value);
        EXPECT_EQ(get(self, U"y").get(context), value.get(context));
        ReturnSlot tuple(context); std::array entries{text(U"z").get(context)};
        cxxpython::core::make_tuple(context, entries, tuple);
        define(roots.root(tuple.result().value()), out, space()); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    }

    TEST_F(CoreLayout, InvalidAndConflictingSlotDefinitionsUsePythonErrors)
    {
        ReturnSlot out(context); define(text(U"not-valid"), out, space()); expect_error(out, CoreObject::TypeErrorType);
        define(roots.root(context.builtin(CoreObject::None)), out, space()); expect_error(out, CoreObject::TypeErrorType);
        auto dictionary = space(); auto value = text(U"value");
        detail::name_store(context, dictionary.get(context), text(U"x").get(context), value.get(context));
        define(text(U"x"), out, dictionary); expect_error(out, CoreObject::ValueErrorType);
        auto tuple_type = roots.root(context.builtin(CoreObject::TupleType));
        TypeSpec spec{U"TupleChild", {&tuple_type, 1}}; spec.slots = std::vector<std::u32string>{U"__dict__"};
        EXPECT_THROW(create_type(context, spec, out), TypeDefinitionError);
    }

    TEST_F(CoreLayout, UnicodeIdentifiersUseThePinnedUnicode15PropertiesAndPrivateMangling)
    {
        EXPECT_TRUE(is_identifier(U"\u4e2d\u6587"));
        EXPECT_TRUE(is_identifier(U"a\u0301"));
        EXPECT_TRUE(is_identifier(U"\U00011f02")); // Kawi letter added in Unicode 15.
        EXPECT_FALSE(is_identifier(U"\u0301a"));
        EXPECT_FALSE(is_identifier(U"\U0001f600"));
        EXPECT_FALSE(is_identifier(std::u32string{char32_t{0xd800}}));
        auto cls = type(U"__Owner", std::vector<std::u32string>{U"__private", U"\u4e2d\u6587"});
        auto self = instance(cls); auto value = text(U"value"); set(self, U"_Owner__private", value);
        set(self, U"\u4e2d\u6587", value);
        EXPECT_EQ(get(self, U"_Owner__private").get(context), value.get(context));
        EXPECT_EQ(get(self, U"\u4e2d\u6587").get(context), value.get(context));
    }
}
