#include "cxxpython/core/protocol.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    class CoreProtocols : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle builtin(CoreObject id) { return roots.root(context.builtin(id)); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle integer(std::int64_t value)
        { ReturnSlot out(context); make_int(context, value, out); return roots.root(out.result().value()); }
        Handle type(std::span<const Handle> bases = {})
        { ReturnSlot out(context); create_type(context, {"Custom", bases}, out); return roots.root(out.result().value()); }
        Handle instance(Handle type)
        { ReturnSlot out(context); context.allocate_instance<InstanceObject>(type, out); return roots.root(out.result().value()); }
        void put(Handle type, std::u32string name, Handle value)
        { ReturnSlot out(context); set_attribute(context, type, text(std::move(name)), value, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); }
        Handle function(Handle result, std::size_t arity)
        {
            SignatureLayout signature; signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
            if (arity) signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
            std::array captures{result.get(context)}; ReturnSlot out(context);
            make_native_function(context, "protocol", signature,
                [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                    c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
                }, captures, out);
            return roots.root(out.result().value());
        }
        void method(Handle type, std::u32string name, Handle result, std::size_t arity = 0)
        { put(type, std::move(name), function(result, arity)); }
        void error(ReturnSlot &out, CoreObject type)
        { ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type)); }
    };

    TEST_F(CoreProtocols, TruthUsesTypeMethodsAndChecksBooleanBeforeLength)
    {
        auto cls = type(); auto obj = instance(cls); ReturnSlot out(context);
        truth(context, obj, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        method(cls, U"__len__", integer(0)); truth(context, obj, out);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        method(cls, U"__bool__", builtin(CoreObject::True)); truth(context, obj, out);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        method(cls, U"__bool__", integer(1)); truth(context, obj, out); error(out, CoreObject::TypeErrorType);
        auto empty = text(U""); truth(context, empty, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        truth(context, builtin(CoreObject::None), out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    }

    TEST_F(CoreProtocols, LengthAcceptsIndexButRejectsNegativeAndUnboundedResults)
    {
        auto cls = type(); auto obj = instance(cls); ReturnSlot out(context);
        length(context, obj, out); error(out, CoreObject::TypeErrorType);
        method(cls, U"__len__", integer(-1)); length(context, obj, out); error(out, CoreObject::ValueErrorType);
        make_int_decimal(context, "9223372036854775808", out); auto big = roots.root(out.result().value());
        method(cls, U"__len__", big); length(context, obj, out); error(out, CoreObject::OverflowErrorType);
        auto index_type = type(); method(index_type, U"__index__", integer(3)); auto indexed = instance(index_type);
        method(cls, U"__len__", indexed); length(context, obj, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(context, out.result().value()), "3");
        method(index_type, U"__index__", builtin(CoreObject::None)); index(context, indexed, out); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreProtocols, RichComparisonKeepsArbitraryResultsAndContainerIdentityIsSeparate)
    {
        auto cls = type(); auto obj = instance(cls); ReturnSlot out(context);
        method(cls, U"__eq__", builtin(CoreObject::False), 1);
        rich_compare(context, obj, obj, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        container_equal(context, obj, obj, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto marker = text(U"result"); method(cls, U"__eq__", marker, 1);
        auto other = instance(cls); rich_compare(context, obj, other, CompareOp::Eq, out);
        EXPECT_EQ(out.result().value(), marker.get(context));
        container_equal(context, obj, other, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        rich_compare(context, obj, other, CompareOp::Ne, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    }

    TEST_F(CoreProtocols, StrictSubtypeReflectedComparisonPrecedesLeftEvenForSameDescriptor)
    {
        auto base = type(); auto subtype = type({&base, 1}); auto left = instance(base); auto right = instance(subtype);
        auto marker = text(U"right"); method(base, U"__lt__", builtin(CoreObject::False), 1);
        method(base, U"__gt__", marker, 1); ReturnSlot out(context);
        rich_compare(context, left, right, CompareOp::Lt, out); EXPECT_EQ(out.result().value(), marker.get(context));
    }

    TEST_F(CoreProtocols, BinaryReflectedPriorityDependsOnDescriptorIdentityNotThunk)
    {
        auto base = type(); auto subtype = type({&base, 1}); auto left = instance(base); auto right = instance(subtype);
        auto l = text(U"left"); auto r = text(U"right"); auto inherited = text(U"inherited");
        method(base, U"__add__", l, 1); method(base, U"__radd__", inherited, 1); ReturnSlot out(context);
        binary(context, left, right, BinaryOp::Add, out); EXPECT_EQ(out.result().value(), l.get(context));
        method(subtype, U"__radd__", r, 1); binary(context, left, right, BinaryOp::Add, out);
        EXPECT_EQ(out.result().value(), r.get(context));
        method(subtype, U"__radd__", builtin(CoreObject::NotImplemented), 1);
        binary(context, left, right, BinaryOp::Add, out); EXPECT_EQ(out.result().value(), l.get(context));
        method(base, U"__iadd__", inherited, 1); inplace(context, left, right, BinaryOp::Add, out);
        EXPECT_EQ(out.result().value(), inherited.get(context));
        method(base, U"__iadd__", builtin(CoreObject::NotImplemented), 1);
        inplace(context, left, right, BinaryOp::Add, out); EXPECT_EQ(out.result().value(), l.get(context));
    }

    TEST_F(CoreProtocols, NotImplementedAndNoneAndMissingHaveDistinctDispatchMeaning)
    {
        auto cls = type(); auto a = instance(cls); auto b = instance(cls); ReturnSlot out(context);
        method(cls, U"__eq__", builtin(CoreObject::NotImplemented), 1);
        rich_compare(context, a, b, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        rich_compare(context, a, a, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        rich_compare(context, a, b, CompareOp::Lt, out); error(out, CoreObject::TypeErrorType);
        put(cls, U"__add__", builtin(CoreObject::None));
        binary(context, a, b, BinaryOp::Add, out); error(out, CoreObject::TypeErrorType);
        put(cls, U"__hash__", builtin(CoreObject::None));
        auto slot = resolve_protocol(context, cls, Protocol::Hash, roots); EXPECT_EQ(slot.state, SlotState::Disabled);
        auto missing = resolve_protocol(context, cls, Protocol::Len, roots); EXPECT_EQ(missing.state, SlotState::Missing);
    }

    TEST_F(CoreProtocols, ReflectedOverrideUsesClassDescriptorBindingAndRichInequality)
    {
        auto base = type(); auto subtype = type({&base, 1}); auto a = instance(base); auto b = instance(subtype);
        auto marker = text(U"left wins"); method(base, U"__add__", marker, 1);
        method(base, U"__radd__", builtin(CoreObject::NotImplemented), 1);
        auto equality = type(); method(equality, U"__ne__", builtin(CoreObject::False), 1);
        auto compared = instance(equality); auto descriptor_type = type();
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
        std::array captures{compared.get(context)}; ReturnSlot out(context);
        make_native_function(context, "__get__", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
            }, captures, out);
        put(descriptor_type, U"__get__", roots.root(out.result().value()));
        put(subtype, U"__radd__", instance(descriptor_type));
        binary(context, a, b, BinaryOp::Add, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), marker.get(context));
    }

    TEST_F(CoreProtocols, ReflectedFallbackResolvesAgainAfterLeftCallbackMutatesRightType)
    {
        auto left_type = type(); auto right_type = type(); auto a = instance(left_type); auto b = instance(right_type);
        auto replacement = function(text(U"fresh reflected result"), 1); auto name = text(U"__radd__");
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"other"}};
        std::array captures{right_type.get(context), name.get(context), replacement.get(context)}; ReturnSlot out(context);
        make_native_function(context, "mutate", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                RootScope roots(c); auto captures = c.payload<NativeFunctionObject>(fn).captures;
                set_attribute(c, roots.root(captures[0]), roots.root(captures[1]), roots.root(captures[2]), out);
                c.collect(); out.ok(c.builtin(CoreObject::NotImplemented));
            }, captures, out);
        put(left_type, U"__add__", roots.root(out.result().value()));
        binary(context, a, b, BinaryOp::Add, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"fresh reflected result");
    }

    TEST_F(CoreProtocols, ClassDefinitionHashRuleDoesNotRerunOnLaterMutation)
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out); auto namespace_ = roots.root(out.result().value());
        auto equality = function(builtin(CoreObject::True), 1);
        detail::name_store(context, namespace_.get(context), text(U"__eq__").get(context), equality.get(context));
        TypeSpec spec{"DefinesEquality"}; spec.dictionary = namespace_; create_type(context, spec, out);
        auto cls = roots.root(out.result().value());
        EXPECT_EQ(resolve_protocol(context, cls, Protocol::Hash, roots).state, SlotState::Disabled);
        auto late = type(); put(late, U"__eq__", equality);
        EXPECT_FALSE(detail::name_lookup(context, *context.payload<TypeObject>(late).dictionary, U"__hash__"));
        EXPECT_FALSE(detail::name_lookup(context, namespace_.get(context), U"__hash__"));
    }

    TEST_F(CoreProtocols, SameTypeArithmeticDoesNotTryReflectedButEqualityDoes)
    {
        auto cls = type(); auto a = instance(cls); auto b = instance(cls); ReturnSlot out(context);
        method(cls, U"__add__", builtin(CoreObject::NotImplemented), 1);
        method(cls, U"__radd__", text(U"must not run"), 1);
        binary(context, a, b, BinaryOp::Add, out); error(out, CoreObject::TypeErrorType);
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"other"}};
        std::array captures{b.get(context)};
        make_native_function(context, "eq", signature,
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
                out.ok(c.builtin(args[0] == c.payload<NativeFunctionObject>(fn).captures[0] ? CoreObject::True : CoreObject::NotImplemented));
            }, captures, out);
        put(cls, U"__eq__", roots.root(out.result().value()));
        rich_compare(context, a, b, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    }

    TEST_F(CoreProtocols, FullMroAndEpochRevalidationUseCurrentClassDefinitions)
    {
        auto first = type(); auto second = type(); auto marker = integer(4); method(second, U"__len__", marker);
        std::array bases{first, second}; auto cls = type(bases); auto obj = instance(cls);
        auto slot = resolve_protocol(context, cls, Protocol::Len, roots);
        EXPECT_EQ(slot.owner.get(context), second.get(context));
        auto replacement = integer(5); method(second, U"__len__", replacement); ReturnSlot out(context);
        invoke_protocol(context, obj, slot, {}, out); EXPECT_EQ(out.result().value(), replacement.get(context));
    }

    TEST_F(CoreProtocols, OperandsSurviveAliasedOutputAndMemoryErrorIsRooted)
    {
        ReturnSlot out(context); make_int(context, 42, out);
        RootScope temporary(context); auto only = temporary.root(out.result().value());
        truth(context, only, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto cls = type(); auto obj = instance(cls); context.set_allocation_limit(0);
        length(context, obj, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
    }

    TEST_F(CoreProtocols, ImplicitLookupIgnoresInstanceMethodsAndAttributeHooks)
    {
        auto cls = type(); method(cls, U"__len__", integer(0)); auto obj = instance(cls); ReturnSlot out(context);
        auto shadow = function(integer(8), 0);
        set_attribute(context, obj, text(U"__len__"), shadow, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        method(cls, U"__getattribute__", integer(9), 1);
        truth(context, obj, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        length(context, obj, out); EXPECT_EQ(int_decimal(context, out.result().value()), "0");
    }

    TEST_F(CoreProtocols, RecursiveProtocolReentryUsesTheExistingCallDepthLimit)
    {
        auto cls = type(); auto obj = instance(cls); SignatureLayout signature; signature.parameters = {{U"self"}};
        ReturnSlot out(context);
        make_native_function(context, "recursive bool", signature,
            [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out) {
                RootScope roots(c); truth(c, roots.root(args[0]), out);
            }, {}, out);
        put(cls, U"__bool__", roots.root(out.result().value()));
        truth(context, obj, out); error(out, CoreObject::RecursionErrorType);
        del_attribute(context, cls, text(U"__bool__"), out);
        truth(context, obj, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    }

    TEST_F(CoreProtocols, ForeignOperandsResultsAndEvenStaleResolutionAreRejected)
    {
        Interpreter other; Attachment foreign(other); foreign.bootstrap_core(); RootScope foreign_roots(foreign);
        auto foreign_type = foreign_roots.root(foreign.builtin(CoreObject::IntType));
        auto foreign_value = foreign_roots.root(foreign.builtin(CoreObject::IntOne));
        auto slot = resolve_protocol(foreign, foreign_type, Protocol::Bool, foreign_roots);
        context.invalidate_types(); auto value = integer(3); ReturnSlot out(context); ReturnSlot foreign_out(foreign);
        EXPECT_THROW(truth(context, foreign_value, out), std::logic_error);
        EXPECT_THROW(invoke_protocol(context, value, slot, {}, out), std::logic_error);
        foreign_out.ok(foreign.builtin(CoreObject::None));
        EXPECT_THROW(truth(context, value, foreign_out), std::logic_error);
        EXPECT_EQ(foreign_out.result().value(), foreign.builtin(CoreObject::None));
        EXPECT_NE(context.builtin(CoreObject::IntOne), foreign.builtin(CoreObject::IntOne));
    }
}
