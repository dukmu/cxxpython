#include "cxxpython/core/objects.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    Handle text(Attachment &context, RootScope &roots, std::u32string value)
    { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
    Handle attribute(Attachment &context, RootScope &roots, Handle object, std::u32string name)
    {
        ReturnSlot out(context);
        get_attribute(context, object, text(context, roots, std::move(name)), out);
        if (out.result().tag() != ResultTag::Ok) throw std::logic_error("unexpected attribute error");
        return roots.root(out.result().value());
    }
    Handle structural(Attachment &context, RootScope &roots)
    {
        TypeSpec spec{U"Existing"}; spec.module = U"existing_module"; spec.qualname = U"Scope.Existing";
        spec.slots = std::vector<std::u32string>{U"x", U"__dict__"};
        ReturnSlot out(context); create_type(context, spec, out); return roots.root(out.result().value());
    }

    TEST(CoreReadiness, ExistingB2TypesAndInstancesBecomeNormalRuntimeObjects)
    {
        Interpreter interpreter({.collect_every_allocation = true}); Attachment context(interpreter);
        context.bootstrap(); RootScope roots(context); auto type = structural(context, roots);
        const auto identity = type.get(context);
        EXPECT_EQ(context.payload<TypeObject>(type).state, TypeState::Structural);
        ReturnSlot out(context); context.allocate_instance<InstanceObject>(type, out);
        auto self = roots.root(out.result().value());
        context.store(self.get(context), context.instance_slot(self.get(context), 0), context.object_type());
        context.bootstrap_core();
        EXPECT_TRUE(context.core_ready()); EXPECT_EQ(type.get(context), identity);
        EXPECT_EQ(context.payload<TypeObject>(type).state, TypeState::Ready);
        EXPECT_FALSE(context.payload<TypeObject>(type).bootstrap_module.has_value());
        auto name = attribute(context, roots, type, U"__name__");
        EXPECT_EQ(context.payload<StrObject>(name).value, U"Existing");
        EXPECT_EQ(context.payload<StrObject>(attribute(context, roots, type, U"__qualname__")).value, U"Scope.Existing");
        EXPECT_EQ(context.payload<StrObject>(attribute(context, roots, type, U"__module__")).value, U"existing_module");
        EXPECT_EQ(attribute(context, roots, self, U"x").get(context), context.object_type());
        auto value = text(context, roots, U"dynamic dictionary"); auto key = text(context, roots, U"extra");
        set_attribute(context, self, key, value, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(attribute(context, roots, self, U"extra").get(context), value.get(context));
        auto descriptor = attribute(context, roots, type, U"x");
        const auto before = context.stats();
        context.bootstrap_core();
        EXPECT_EQ(context.stats().objects, before.objects);
        EXPECT_EQ(context.stats().collections, before.collections);
        EXPECT_EQ(attribute(context, roots, type, U"x").get(context), descriptor.get(context));
    }

    TEST(CoreReadiness, ExistingMetaclassGraphsArePromotedWithoutReconstructingTypes)
    {
        Interpreter interpreter({.collect_every_allocation = true}); Attachment context(interpreter);
        context.bootstrap(); RootScope roots(context); auto type_type = roots.root(context.type_type());
        ReturnSlot out(context); create_type(context, {"Meta", {&type_type, 1}}, out);
        auto meta = roots.root(out.result().value()); create_type(context, {"Class", {}, meta}, out);
        auto cls = roots.root(out.result().value()); const auto old_meta = meta.get(context); const auto old_cls = cls.get(context);
        context.bootstrap_core();
        EXPECT_EQ(context.payload<TypeObject>(meta).state, TypeState::Ready);
        EXPECT_EQ(context.payload<TypeObject>(cls).state, TypeState::Ready);
        EXPECT_EQ(meta.get(context), old_meta); EXPECT_EQ(cls.get(context), old_cls);
        EXPECT_EQ(attribute(context, roots, cls, U"__class__").get(context), old_meta);
        call(context, cls, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.type_of(out.result().value()), old_cls);
    }

    TEST(CoreReadiness, EveryBootstrapAndPromotionAllocationCanFailAndRetry)
    {
        std::size_t allocations;
        {
            Interpreter reference; Attachment context(reference); context.bootstrap(); RootScope roots(context);
            (void)structural(context, roots); const auto before = context.stats().objects;
            context.bootstrap_core(); allocations = context.stats().objects - before;
        }
        for (std::size_t quota = 0; quota < allocations; ++quota)
        {
            Interpreter interpreter({.collect_every_allocation = true}); Attachment context(interpreter);
            context.bootstrap(); RootScope roots(context); auto type = structural(context, roots);
            const auto value = type.get(context); const auto baseline = context.stats();
            context.set_allocation_limit(quota);
            EXPECT_THROW(context.bootstrap_core(), std::bad_alloc) << quota;
            EXPECT_FALSE(context.core_ready()); EXPECT_EQ(type.get(context), value);
            EXPECT_EQ(context.stats().objects, baseline.objects) << quota;
            EXPECT_EQ(context.stats().roots, baseline.roots) << quota;
            auto &data = context.payload<TypeObject>(type);
            EXPECT_EQ(data.state, TypeState::Structural);
            EXPECT_FALSE(data.dictionary.has_value());
            EXPECT_EQ(std::get<std::u32string>(data.name), U"Existing");
            EXPECT_EQ(data.bootstrap_module, U"existing_module");
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            const auto collections = context.stats().collections;
            { ReturnSlot temporary(context); context.allocate_instance<InstanceObject>(type, temporary); }
            EXPECT_GT(context.stats().collections, collections);
            EXPECT_NO_THROW(context.bootstrap_core()) << quota;
            EXPECT_TRUE(context.core_ready()); EXPECT_EQ(data.state, TypeState::Ready);
            EXPECT_EQ(context.payload<StrObject>(attribute(context, roots, type, U"__module__")).value, U"existing_module");
        }
    }

    TEST(CoreReadiness, ManagedNamespacesCannotBeSilentlyIgnoredBeforeB3)
    {
        Interpreter interpreter; Attachment context(interpreter); context.bootstrap(); RootScope roots(context);
        ReturnSlot dictionary(context); context.allocate<DictObject>(dictionary);
        TypeSpec spec{"EarlyNamespace"}; spec.dictionary = roots.root(dictionary.result().value());
        ReturnSlot out(context); EXPECT_THROW(create_type(context, spec, out), TypeDefinitionError);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        EXPECT_NO_THROW(context.bootstrap_core());
    }

    TEST(CoreReadiness, InvalidNativeTextDoesNotPoisonAFutureBootstrap)
    {
        Interpreter interpreter; Attachment context(interpreter); context.bootstrap(); ReturnSlot out(context);
        TypeSpec spec{std::u32string{char32_t{0x110000}}};
        EXPECT_THROW(create_type(context, spec, out), TypeDefinitionError);
        EXPECT_EQ(context.stats().objects, 2U);
        EXPECT_NO_THROW(context.bootstrap_core()); EXPECT_TRUE(context.core_ready());
    }

    TEST(CoreReadiness, ABuildingTypeCannotEscapeAsANativeReturnValue)
    {
        Interpreter interpreter; Attachment context(interpreter); context.bootstrap_core(); RootScope roots(context);
        ReturnSlot out(context);
        make_native_function(context, "broken builder", {},
            [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
                RootScope roots(c);
                c.allocate_instance<TypeObject>(roots.root(c.type_type()), result, U"Unready", LayoutDescriptor::of<InstanceObject>());
            }, {}, out);
        auto function = roots.root(out.result().value());
        EXPECT_THROW(call(context, function, {}, out), std::logic_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        context.collect();
    }
}
