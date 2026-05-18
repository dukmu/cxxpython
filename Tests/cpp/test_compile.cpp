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

TEST(Compile, CompilesNameLoadsThroughSymbolTable)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("x"), runtime.new_int(40));

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "x + 2");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->names().size(), 1U);
    auto *first_name = dynamic_cast<cxxpython::StrObject *>(code->names()[0]);
    ASSERT_NE(first_name, nullptr);
    EXPECT_EQ(first_name->value, "x");
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(code->instructions()[0].operand, 0U);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *globals));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 42);
}

TEST(Compile, ReusesSymbolTableEntries)
{
    cxxpython::Runtime runtime;
    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "x + x");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->names().size(), 1U);
    auto *first_name = dynamic_cast<cxxpython::StrObject *>(code->names()[0]);
    ASSERT_NE(first_name, nullptr);
    EXPECT_EQ(first_name->value, "x");
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(code->instructions()[0].operand, 0U);
    EXPECT_EQ(code->instructions()[1].operand, 0U);
}

TEST(Compile, RaisesNameErrorForMissingName)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "missing");

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "NameError");
        EXPECT_STREQ(error.what(), "name 'missing' is not defined");
    }
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
