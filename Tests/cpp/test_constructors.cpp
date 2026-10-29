#include "cxxpython/compiler.hpp"
#include "cxxpython/floatobject.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

class Constructors : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};

    void SetUp() override
    {
        context.bootstrap_core();
    }

    void evaluate(const char *expression, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(expression), InputMode::Eval);
        run(context, {&unit}, out);
    }
};

TEST_F(Constructors, IntegerTextAndBaseSurviveCollection)
{
    ReturnSlot out(context);
    for (const auto &[expression, expected] : {
             std::pair{"int(' -123_456 ')", "-123456"},
             {"int('0xff', 0)", "255"},
             {"int('-0B_101', base=0)", "-5"},
             {"int('z', 36)", "35"},
             {"int('123456789012345678901234567890')", "123456789012345678901234567890"},
             {"int('\\u0661\\u0662\\u0663')", "123"},
         }) {
        SCOPED_TRACE(expression);
        evaluate(expression, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.value()), expected);
    }
}

TEST_F(Constructors, IntegerInvalidInputsRaisePythonExceptions)
{
    ReturnSlot out(context);
    for (const auto *expression :
         {"int('01', 0)", "int('1__2')", "int('0x', 0)", "int('1', 1)", "int('1', 37)"}) {
        SCOPED_TRACE(expression);
        evaluate(expression, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    }
    for (const auto *expression :
         {"int(12, 10)", "int(base=10)", "int(x='12')", "int('12', 10, base=10)"}) {
        SCOPED_TRACE(expression);
        evaluate(expression, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
    }
}

TEST_F(Constructors, FloatPayloadIdentityAndIntegerConversionSurviveCollection)
{
    RootScope roots(context);
    ReturnSlot out(context);
    evaluate("float('12.5')", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto value = roots.root(out.value());
    EXPECT_TRUE(is_float(context, value.get(context)));
    EXPECT_EQ(context.payload<FloatObject>(value).value, 12.5);
    context.collect();
    EXPECT_EQ(context.payload<FloatObject>(value).value, 12.5);
    evaluate("int(float('-123.75'))", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "-123");
    evaluate("float('nan')", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto nan = roots.root(out.value());
    rich_compare(context, nan, nan, CompareOp::Eq, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::False));
    evaluate("int(float('inf'))", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::OverflowErrorType));
    for (const auto *expression : {"float('+-1')", "float('+-inf')"}) {
        SCOPED_TRACE(expression);
        evaluate(expression, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    }
}

TEST_F(Constructors, FloatSubclassTailFieldsRemainTraced)
{
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(class Slotted(float):
    __slots__ = ('item',)
value = Slotted('1.5')
value.item = (777, 888)
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    SourceUnit expression(SourceBuffer("value.item[1]"), InputMode::Eval);
    run(context, {&expression, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "888");
}

TEST(ParallelConstructors, SharedFloatProtocolsRemainRootedDuringCollection)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot result(owner);
    make_float(owner, 42.5, result);
    auto value = roots.root(result.value());
    std::array tokens{owner.make_transfer(value), owner.make_transfer(value)};
    std::latch ready(2), start(1);
    std::array<std::future<void>, 2> workers;
    {
        NativeWait wait(owner);
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i] =
                std::async(std::launch::async, [&, token = std::move(tokens[i])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope worker_roots(context);
                        ReturnSlot input(context), output(context);
                        context.receive(token, input);
                        auto shared = worker_roots.root(input.value());
                        announced = true;
                        ready.count_down();
                        start.wait();
                        for (int iteration = 0; iteration < 100; ++iteration) {
                            hash(context, shared, output);
                            if (output.tag() != ResultTag::Ok ||
                                context.payload<FloatObject>(shared).value != 42.5)
                                throw std::runtime_error("shared float hash failed");
                            if (iteration % 20 == 0)
                                context.collect();
                        }
                    } catch (...) {
                        if (!announced)
                            ready.count_down();
                        throw;
                    }
                });
        }
        ready.wait();
        start.count_down();
        for (auto &worker : workers)
            EXPECT_NO_THROW(worker.get());
    }
}
} // namespace
