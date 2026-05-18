#include <gtest/gtest.h>

#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

TEST(Compile, CompilesSingleIntegerLiteral)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "42");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->constants().size(), 1U);
    ASSERT_EQ(code->instructions().size(), 2U);

    auto *constant = dynamic_cast<cxxpython::IntObject *>(code->constants()[0]);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->value, 42);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::ReturnValue);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 42);
}

TEST(Compile, CompilesLeftAssociativeAddition)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "1 + 2 + 3");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->constants().size(), 3U);
    ASSERT_EQ(code->instructions().size(), 6U);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::BinaryAdd);
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[4].opcode, cxxpython::Opcode::BinaryAdd);
    EXPECT_EQ(code->instructions()[5].opcode, cxxpython::Opcode::ReturnValue);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 6);
}

TEST(Compile, CompilesStringLiteralAndAddition)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "'py' + 'thon'");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->constants().size(), 2U);

    auto *result = dynamic_cast<cxxpython::StrObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "python");
}

TEST(Compile, RaisesSyntaxErrorForMalformedSource)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 +");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "expected expression");
    }
}

TEST(Compile, RaisesSyntaxErrorForUnexpectedToken)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 2");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "unexpected token after expression");
    }
}
