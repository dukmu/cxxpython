#include "cxxpython/core/protocol.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    class CoreNumbers : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle integer(std::string_view text)
        {
            ReturnSlot out(context); make_int_decimal(context, text, out);
            return roots.root(out.result().value());
        }
        Handle builtin(CoreObject id) { return roots.root(context.builtin(id)); }
        void expect_integer(ReturnSlot &out, std::string_view expected)
        {
            ASSERT_EQ(out.result().tag(), ResultTag::Ok);
            EXPECT_EQ(context.type_of(out.result().value()), context.builtin(CoreObject::IntType));
            EXPECT_EQ(int_decimal(context, out.result().value()), expected);
            context.collect(); EXPECT_EQ(int_decimal(context, out.result().value()), expected);
        }
    };

    TEST_F(CoreNumbers, BoxedIdentityAndBoolAreNotConfusedWithUnboundOrInt)
    {
        auto zero = integer("0"); auto one = integer("1"); auto another = integer("+0001");
        EXPECT_EQ(one.get(context), another.get(context));
        EXPECT_NE(one.get(context), context.builtin(CoreObject::True));
        EXPECT_NE(zero.get(context), context.builtin(CoreObject::False));
        EXPECT_TRUE(is_int(context, context.builtin(CoreObject::True)));
        EXPECT_TRUE(is_subtype(context, context.builtin(CoreObject::BoolType), context.builtin(CoreObject::IntType)));
        ReturnSlot out(context);
        truth(context, zero, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
        truth(context, one, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto bool_type = builtin(CoreObject::BoolType);
        EXPECT_THROW(create_type(context, {"BoolSubclass", {&bool_type, 1}}, out), TypeDefinitionError);
    }

    TEST_F(CoreNumbers, SignedOverflowPromotesAndResultsNormalizeWithoutLosingPrecision)
    {
        auto maximum = integer("9223372036854775807"); auto one = integer("1"); ReturnSlot out(context);
        binary(context, maximum, one, BinaryOp::Add, out); expect_integer(out, "9223372036854775808");
        auto large = roots.root(out.result().value());
        binary(context, large, one, BinaryOp::Subtract, out); expect_integer(out, "9223372036854775807");
        auto minimum = integer("-9223372036854775808"); auto negative = integer("-1");
        binary(context, minimum, negative, BinaryOp::FloorDivide, out); expect_integer(out, "9223372036854775808");
        binary(context, maximum, maximum, BinaryOp::Multiply, out);
        expect_integer(out, "85070591730234615847396907784232501249");
        binary(context, minimum, one, BinaryOp::Subtract, out); expect_integer(out, "-9223372036854775809");
    }

    TEST_F(CoreNumbers, FloorDivisionAndModuloHaveDivisorSignForSmallAndLargeOperands)
    {
        struct Sample { const char *left; const char *right; const char *quotient; const char *remainder; };
        for (auto sample : {Sample{"-7", "3", "-3", "2"}, {"7", "-3", "-3", "-2"},
                            {"-7", "-3", "2", "-1"},
                            {"-18446744073709551617", "3", "-6148914691236517206", "1"},
                            {"18446744073709551617", "-3", "-6148914691236517206", "-1"}})
        {
            auto a = integer(sample.left); auto b = integer(sample.right); ReturnSlot out(context);
            binary(context, a, b, BinaryOp::FloorDivide, out); expect_integer(out, sample.quotient);
            binary(context, a, b, BinaryOp::Modulo, out); expect_integer(out, sample.remainder);
        }
    }

    TEST_F(CoreNumbers, DivisionByZeroAndInvalidOperandsProduceManagedExceptions)
    {
        auto large = integer("123456789012345678901234567890"); auto zero = integer("0"); ReturnSlot out(context);
        for (auto op : {BinaryOp::FloorDivide, BinaryOp::Modulo})
        {
            binary(context, large, zero, op, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
            EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::ZeroDivisionErrorType));
        }
        binary(context, large, builtin(CoreObject::None), BinaryOp::Add, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::TypeErrorType));
        EXPECT_THROW(make_int_decimal(context, "1_0", out), std::invalid_argument);
    }

    TEST_F(CoreNumbers, NumericWrappersAndImplicitOperationsShareImplementations)
    {
        auto a = integer("18446744073709551616"); auto b = integer("3"); ReturnSlot out(context);
        make_str(context, U"__add__", out); auto name = roots.root(out.result().value());
        get_attribute(context, a, name, out); auto method = roots.root(out.result().value());
        std::array args{b.get(context)}; call(context, method, {args, {}}, out);
        expect_integer(out, "18446744073709551619");
        inplace(context, a, b, BinaryOp::Add, out); expect_integer(out, "18446744073709551619");
        binary(context, builtin(CoreObject::True), b, BinaryOp::Add, out); expect_integer(out, "4");
        rich_compare(context, a, b, CompareOp::Gt, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        index(context, builtin(CoreObject::True), out); expect_integer(out, "1");
    }

    TEST_F(CoreNumbers, ConstructorsPreserveIntSubclassLayoutAndReturnExactConversions)
    {
        ReturnSlot out(context); auto int_type = builtin(CoreObject::IntType);
        create_type(context, {"MyInt", {&int_type, 1}}, out); auto subtype = roots.root(out.result().value());
        auto big = integer("18446744073709551616"); std::array args{big.get(context)};
        call(context, subtype, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto instance = roots.root(out.result().value());
        EXPECT_EQ(context.type_of(instance), subtype.get(context));
        EXPECT_NE(instance.get(context), big.get(context));
        EXPECT_NE(context.instance_dictionary(instance.get(context)), nullptr);
        index(context, instance, out); expect_integer(out, "18446744073709551616");
        call(context, int_type, {args, {}}, out); EXPECT_EQ(out.result().value(), big.get(context));
        call(context, builtin(CoreObject::BoolType), {args, {}}, out);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        call(context, builtin(CoreObject::BoolType), {}, out);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    }

    TEST_F(CoreNumbers, NativeLayoutAloneDoesNotConferIntegerSemantics)
    {
        ReturnSlot out(context); create_native_type(context, {{"FakeInt"}, LayoutDescriptor::of<IntObject>()}, out);
        auto type = roots.root(out.result().value()); context.allocate_instance<IntObject>(type, out, std::int64_t{4});
        auto fake = roots.root(out.result().value()); EXPECT_FALSE(is_int(context, fake.get(context)));
        index(context, fake, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::TypeErrorType));
    }

    TEST_F(CoreNumbers, IntConversionHonorsSubclassIntOverrideButIndexDoesNot)
    {
        auto int_type = builtin(CoreObject::IntType); ReturnSlot out(context);
        create_type(context, {"OverridesInt", {&int_type, 1}}, out); auto subtype = roots.root(out.result().value());
        context.allocate_instance<IntObject>(subtype, out, std::int64_t{4}); auto value = roots.root(out.result().value());
        auto converted = integer("9"); SignatureLayout signature; signature.parameters = {{U"self"}};
        std::array captures{converted.get(context)};
        make_native_function(context, "__int__", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
            }, captures, out);
        auto function = roots.root(out.result().value()); make_str(context, U"__int__", out);
        set_attribute(context, subtype, roots.root(out.result().value()), function, out);
        std::array args{value.get(context)}; call(context, int_type, {args, {}}, out); expect_integer(out, "9");
        index(context, value, out); expect_integer(out, "4");
        make_str(context, U"__new__", out); get_attribute(context, int_type, roots.root(out.result().value()), out);
        auto constructor = roots.root(out.result().value());
        std::array unsafe{context.builtin(CoreObject::BoolType), value.get(context)};
        call(context, constructor, {unsafe, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::TypeErrorType));
    }

    TEST_F(CoreNumbers, BigStorageIsAccountedAndAllocationFailureDoesNotLoseOperands)
    {
        context.collect(); auto before = context.stats().live_bytes;
        auto big = integer("12345678901234567890123456789012345678901234567890123456789012345678901234567890");
        EXPECT_GT(context.stats().live_bytes - before, sizeof(IntObject));
        ReturnSlot out(context); context.set_allocation_limit(0);
        binary(context, big, big, BinaryOp::Multiply, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        binary(context, big, integer("0"), BinaryOp::Add, out);
        expect_integer(out, "12345678901234567890123456789012345678901234567890123456789012345678901234567890");
    }
}
