#include <gtest/gtest.h>

#include <sstream>

#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

TEST(Compile, CompilesSingleIntegerLiteral)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "42", "<test>");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->name(), "<eval>");
    EXPECT_EQ(code->filename(), "<test>");
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
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[2].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[4].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[4].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[5].opcode, cxxpython::Opcode::ReturnValue);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 6);
}

TEST(Compile, CompilesBinaryOperatorPrecedence)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "10 - 2 * 3 + 4");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->instructions().size(), 8U);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[3].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Multiply));
    EXPECT_EQ(code->instructions()[4].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[4].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Subtract));
    EXPECT_EQ(code->instructions()[5].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[6].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[6].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[7].opcode, cxxpython::Opcode::ReturnValue);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 8);
}

TEST(Compile, CompilesUnaryOperators)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "-2 * +3 + ~1");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->instructions().size(), 9U);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::UnaryOp);
    EXPECT_EQ(code->instructions()[1].operand, static_cast<std::uint32_t>(cxxpython::UnaryOp::Negative));
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::UnaryOp);
    EXPECT_EQ(code->instructions()[3].operand, static_cast<std::uint32_t>(cxxpython::UnaryOp::Positive));
    EXPECT_EQ(code->instructions()[4].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[4].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Multiply));
    EXPECT_EQ(code->instructions()[5].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[6].opcode, cxxpython::Opcode::UnaryOp);
    EXPECT_EQ(code->instructions()[6].operand, static_cast<std::uint32_t>(cxxpython::UnaryOp::Invert));
    EXPECT_EQ(code->instructions()[7].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[7].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[8].opcode, cxxpython::Opcode::ReturnValue);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, -8);
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

TEST(Compile, CompilesEqualityAndInequality)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *equal = cxxpython::compile_eval_input(runtime, "1 + 1 == 2", "script.py");
    ASSERT_NE(equal, nullptr);
    ASSERT_EQ(equal->instructions().size(), 6U);
    EXPECT_EQ(equal->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(equal->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(equal->instructions()[2].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(equal->instructions()[2].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(equal->instructions()[3].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(equal->instructions()[4].opcode, cxxpython::Opcode::CompareOp);
    EXPECT_EQ(equal->instructions()[4].operand, static_cast<std::uint32_t>(cxxpython::RichCompareOp::Equal));
    EXPECT_EQ(vm.execute(*equal), runtime.true_value());

    cxxpython::CodeObject *not_equal = cxxpython::compile_eval_input(runtime, "'a' != 'b'", "script.py");
    ASSERT_NE(not_equal, nullptr);
    EXPECT_EQ(not_equal->instructions()[2].opcode, cxxpython::Opcode::CompareOp);
    EXPECT_EQ(not_equal->instructions()[2].operand, static_cast<std::uint32_t>(cxxpython::RichCompareOp::NotEqual));
    EXPECT_EQ(vm.execute(*not_equal), runtime.true_value());
}

TEST(Compile, CompilesOrderingComparisons)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *less = cxxpython::compile_eval_input(runtime, "1 + 1 < 3", "script.py");
    ASSERT_NE(less, nullptr);
    ASSERT_EQ(less->instructions().size(), 6U);
    EXPECT_EQ(less->instructions()[4].opcode, cxxpython::Opcode::CompareOp);
    EXPECT_EQ(less->instructions()[4].operand, static_cast<std::uint32_t>(cxxpython::RichCompareOp::Less));
    EXPECT_EQ(vm.execute(*less), runtime.true_value());

    cxxpython::CodeObject *greater_equal =
        cxxpython::compile_eval_input(runtime, "'bee' >= 'ant'", "script.py");
    ASSERT_NE(greater_equal, nullptr);
    EXPECT_EQ(greater_equal->instructions()[2].opcode, cxxpython::Opcode::CompareOp);
    EXPECT_EQ(greater_equal->instructions()[2].operand,
              static_cast<std::uint32_t>(cxxpython::RichCompareOp::GreaterEqual));
    EXPECT_EQ(vm.execute(*greater_equal), runtime.true_value());
}

TEST(Compile, CompilesIdentityAndMembershipComparisons)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "x = []\n"
        "same = x\n"
        "identity = x is same\n"
        "not_identity = x is []\n"
        "is_not_identity = x is not []\n"
        "contains = 'b' in 'abc'\n"
        "missing = 'z' not in 'abc'\n"
        "list_contains = 2 in [1, 2]\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_is = false;
    bool saw_in = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::CompareOp &&
            instruction.operand == static_cast<std::uint32_t>(cxxpython::ComparisonOp::Is))
            saw_is = true;
        if (instruction.opcode == cxxpython::Opcode::CompareOp &&
            instruction.operand == static_cast<std::uint32_t>(cxxpython::ComparisonOp::In))
            saw_in = true;
    }
    EXPECT_TRUE(saw_is);
    EXPECT_TRUE(saw_in);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    EXPECT_EQ(globals->get(runtime.new_str("identity")), runtime.true_value());
    EXPECT_EQ(globals->get(runtime.new_str("not_identity")), runtime.false_value());
    EXPECT_EQ(globals->get(runtime.new_str("is_not_identity")), runtime.true_value());
    EXPECT_EQ(globals->get(runtime.new_str("contains")), runtime.true_value());
    EXPECT_EQ(globals->get(runtime.new_str("missing")), runtime.true_value());
    EXPECT_EQ(globals->get(runtime.new_str("list_contains")), runtime.true_value());
}

TEST(Compile, CompilesChainedComparisonsWithShortCircuit)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *true_chain = cxxpython::compile_eval_input(runtime, "1 < 2 < 3", "script.py");
    ASSERT_NE(true_chain, nullptr);
    bool saw_dup = false;
    bool saw_rot_three = false;
    bool saw_jump_if_false_or_pop = false;
    bool saw_cleanup_rot_two = false;
    for (const cxxpython::Instruction &instruction : true_chain->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::DupTop)
            saw_dup = true;
        if (instruction.opcode == cxxpython::Opcode::RotThree)
            saw_rot_three = true;
        if (instruction.opcode == cxxpython::Opcode::JumpIfFalseOrPop)
            saw_jump_if_false_or_pop = true;
        if (instruction.opcode == cxxpython::Opcode::RotTwo)
            saw_cleanup_rot_two = true;
    }
    EXPECT_TRUE(saw_dup);
    EXPECT_TRUE(saw_rot_three);
    EXPECT_TRUE(saw_jump_if_false_or_pop);
    EXPECT_TRUE(saw_cleanup_rot_two);
    EXPECT_EQ(vm.execute(*true_chain), runtime.true_value());

    cxxpython::CodeObject *false_chain = cxxpython::compile_eval_input(runtime, "1 < 0 < missing", "script.py");
    ASSERT_NE(false_chain, nullptr);
    EXPECT_EQ(vm.execute(*false_chain), runtime.false_value());
}

TEST(Compile, CompilesIfElseStatements)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "if 1 == 1:\n"
        "    result = 'yes'\n"
        "else:\n"
        "    result = 'no'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_conditional_jump = false;
    bool saw_absolute_jump = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::PopJumpIfFalse)
            saw_conditional_jump = true;
        if (instruction.opcode == cxxpython::Opcode::JumpAbsolute)
            saw_absolute_jump = true;
    }
    EXPECT_TRUE(saw_conditional_jump);
    EXPECT_TRUE(saw_absolute_jump);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "yes");
}

TEST(Compile, CompilesIfWithoutElse)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "x = 'initial'\n"
        "if 0:\n"
        "    x = 'changed'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *x = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("x")));
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->value, "initial");
}

TEST(Compile, RejectsMalformedChainedComparisons)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 < < 3", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:5: expected expression");
    }
}

TEST(Compile, RaisesSyntaxErrorForInvalidIfBlock)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "if 1:\nresult = 'bad'\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:2:1: expected indented block");
    }
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

TEST(Compile, EmitsLineTableAndAttachesRuntimeErrorLocation)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "x = 1\n"
        "missing\n",
        "script.py");

    ASSERT_EQ(code->lines().size(), code->instructions().size());
    ASSERT_GE(code->instructions().size(), 5U);
    EXPECT_EQ(code->line_for_instruction(0), 1U);

    bool found_missing_load = false;
    for (std::size_t index = 0; index < code->instructions().size(); ++index)
    {
        const cxxpython::Instruction instruction = code->instructions()[index];
        if (instruction.opcode != cxxpython::Opcode::LoadName)
            continue;
        auto *name = dynamic_cast<cxxpython::StrObject *>(code->names()[instruction.operand]);
        ASSERT_NE(name, nullptr);
        if (name->value == "missing")
        {
            found_missing_load = true;
            EXPECT_EQ(code->line_for_instruction(index), 2U);
        }
    }
    EXPECT_TRUE(found_missing_load);

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.type_name(), "NameError");
        EXPECT_TRUE(error.value()->has_source_location());
        EXPECT_EQ(error.value()->source_filename(), "script.py");
        EXPECT_EQ(error.value()->source_name(), "<module>");
        EXPECT_EQ(error.value()->source_line(), 2U);
        ASSERT_EQ(error.value()->traceback_entries().size(), 1U);
        EXPECT_EQ(error.value()->traceback_entries()[0].filename, "script.py");
        EXPECT_EQ(error.value()->traceback_entries()[0].name, "<module>");
        EXPECT_EQ(error.value()->traceback_entries()[0].line, 2U);
    }
}

TEST(Compile, KeepsInnerFunctionRuntimeErrorLocation)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def fail():\n"
        "    value = 1\n"
        "    missing\n"
        "fail()\n",
        "script.py");

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.type_name(), "NameError");
        EXPECT_TRUE(error.value()->has_source_location());
        EXPECT_EQ(error.value()->source_filename(), "script.py");
        EXPECT_EQ(error.value()->source_name(), "fail");
        EXPECT_EQ(error.value()->source_line(), 3U);
        ASSERT_EQ(error.value()->traceback_entries().size(), 2U);
        EXPECT_EQ(error.value()->traceback_entries()[0].filename, "script.py");
        EXPECT_EQ(error.value()->traceback_entries()[0].name, "fail");
        EXPECT_EQ(error.value()->traceback_entries()[0].line, 3U);
        EXPECT_EQ(error.value()->traceback_entries()[1].filename, "script.py");
        EXPECT_EQ(error.value()->traceback_entries()[1].name, "<module>");
        EXPECT_EQ(error.value()->traceback_entries()[1].line, 4U);
    }
}

TEST(Compile, RaisesSyntaxErrorForMalformedSource)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 +", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:4: expected expression");
    }
}

TEST(Compile, RaisesSyntaxErrorForUnexpectedToken)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 2", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:3: unexpected token after expression");
    }
}

TEST(Compile, CompilesAssignmentModuleAndStoresGlobals)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "x = 40 + 2\n", "script.py");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->name(), "<module>");
    EXPECT_EQ(code->filename(), "script.py");
    ASSERT_EQ(code->names().size(), 1U);
    auto *name = dynamic_cast<cxxpython::StrObject *>(code->names()[0]);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->value, "x");
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[2].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::StoreName);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *value = dynamic_cast<cxxpython::IntObject *>(globals->get(runtime.new_str("x")));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 42);
}

TEST(Compile, ExecutesModuleStatementsInOrder)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "x = 1\ny = x + 2\n");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());

    auto *x = dynamic_cast<cxxpython::IntObject *>(globals->get(runtime.new_str("x")));
    auto *y = dynamic_cast<cxxpython::IntObject *>(globals->get(runtime.new_str("y")));
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    EXPECT_EQ(x->value, 1);
    EXPECT_EQ(y->value, 3);
}

TEST(Compile, ModuleExpressionStatementsAreDiscarded)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "1 + 2\n");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::BinaryOp);
    EXPECT_EQ(code->instructions()[2].operand, static_cast<std::uint32_t>(cxxpython::BinaryOp::Add));
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::PopTop);
    EXPECT_EQ(vm.execute(*code), runtime.none());
}

TEST(Compile, CompilesAndExecutesCallExpressions)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    std::ostringstream output;
    runtime.set_stdout(output);

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "print('hello', 42)\n", "script.py");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->names().size(), 1U);
    auto *name = dynamic_cast<cxxpython::StrObject *>(code->names()[0]);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->value, "print");
    ASSERT_GE(code->instructions().size(), 5U);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(code->instructions()[3].opcode, cxxpython::Opcode::Call);
    EXPECT_EQ(code->instructions()[3].operand, 2U);

    EXPECT_EQ(vm.execute(*code, *runtime.new_dict()), runtime.none());
    EXPECT_EQ(output.str(), "hello 42\n");
}

TEST(Compile, CallsBuiltinLenThroughObjectProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("items"), runtime.new_list({runtime.new_int(1), runtime.new_int(2)}));

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "len(items)", "script.py");
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(code->names().size(), 2U);
    auto *len_name = dynamic_cast<cxxpython::StrObject *>(code->names()[0]);
    auto *items_name = dynamic_cast<cxxpython::StrObject *>(code->names()[1]);
    ASSERT_NE(len_name, nullptr);
    ASSERT_NE(items_name, nullptr);
    EXPECT_EQ(len_name->value, "len");
    EXPECT_EQ(items_name->value, "items");

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *globals));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 2);
}

TEST(Compile, BuiltinLenRaisesPythonTypeError)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *bad_arity = cxxpython::compile_eval_input(runtime, "len()", "script.py");
    try
    {
        (void)vm.execute(*bad_arity);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "len() takes exactly one argument (0 given)");
    }

    cxxpython::CodeObject *bad_type = cxxpython::compile_eval_input(runtime, "len(1)", "script.py");
    try
    {
        (void)vm.execute(*bad_type);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "object of type 'int' has no len()");
    }
}

TEST(Compile, CompilesAttributeLoadAndStore)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::TypeObject *type = runtime.new_heap_type("Carrier", runtime.object_type());
    cxxpython::Object *object = runtime.allocate<cxxpython::Object>(type);
    globals->set(runtime.new_str("obj"), object);

    cxxpython::CodeObject *store = cxxpython::compile_exec_input(runtime, "obj.field = 'ok'\n", "script.py");
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(store->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(store->instructions()[2].opcode, cxxpython::Opcode::StoreAttr);

    EXPECT_EQ(vm.execute(*store, *globals), runtime.none());
    auto *stored = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(object, "field"));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->value, "ok");

    cxxpython::CodeObject *load = cxxpython::compile_eval_input(runtime, "obj.field", "script.py");
    ASSERT_NE(load, nullptr);
    EXPECT_EQ(load->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(load->instructions()[1].opcode, cxxpython::Opcode::LoadAttr);
    EXPECT_EQ(vm.execute(*load, *globals), stored);
}

TEST(Compile, CompilesAttributeDelete)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::TypeObject *type = runtime.new_heap_type("Carrier", runtime.object_type());
    cxxpython::Object *object = runtime.allocate<cxxpython::Object>(type);
    cxxpython::object_set_attr(object, "field", runtime.new_str("stored"));
    globals->set(runtime.new_str("obj"), object);

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "del obj.field\n", "script.py");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(code->instructions()[1].opcode, cxxpython::Opcode::DeleteAttr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    try
    {
        (void)cxxpython::object_get_attr(object, "field");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'Carrier' object has no attribute 'field'");
    }
}

TEST(Compile, CompilesSubscriptLoadAndStore)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::ListObject *list = runtime.new_list({runtime.new_str("zero"), runtime.new_str("one")});
    globals->set(runtime.new_str("xs"), list);

    cxxpython::CodeObject *load = cxxpython::compile_eval_input(runtime, "xs[1]", "script.py");
    ASSERT_NE(load, nullptr);
    EXPECT_EQ(load->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(load->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(load->instructions()[2].opcode, cxxpython::Opcode::BinarySubscr);

    auto *loaded = dynamic_cast<cxxpython::StrObject *>(vm.execute(*load, *globals));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->value, "one");

    cxxpython::CodeObject *store =
        cxxpython::compile_exec_input(runtime, "xs[1] = 'updated'\n", "script.py");
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->instructions()[0].opcode, cxxpython::Opcode::LoadName);
    EXPECT_EQ(store->instructions()[1].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(store->instructions()[2].opcode, cxxpython::Opcode::LoadConst);
    EXPECT_EQ(store->instructions()[3].opcode, cxxpython::Opcode::StoreSubscr);

    EXPECT_EQ(vm.execute(*store, *globals), runtime.none());
    auto *stored = dynamic_cast<cxxpython::StrObject *>(list->get(1));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->value, "updated");
}

TEST(Compile, CompilesNameAndSubscriptDelete)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "x = 'gone'\n"
        "xs = ['zero', 'one', 'two']\n"
        "d = {'key': 'value'}\n"
        "del x\n"
        "del xs[1]\n"
        "del d['key']\n",
        "script.py");
    ASSERT_NE(code, nullptr);
    bool saw_delete_name = false;
    bool saw_delete_subscr = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::DeleteName)
            saw_delete_name = true;
        if (instruction.opcode == cxxpython::Opcode::DeleteSubscr)
            saw_delete_subscr = true;
    }
    EXPECT_TRUE(saw_delete_name);
    EXPECT_TRUE(saw_delete_subscr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    EXPECT_EQ(globals->get(runtime.new_str("x")), nullptr);
    auto *list = dynamic_cast<cxxpython::ListObject *>(globals->get(runtime.new_str("xs")));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 2U);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(list->get(1))->value, "two");
    auto *dict = dynamic_cast<cxxpython::DictObject *>(globals->get(runtime.new_str("d")));
    ASSERT_NE(dict, nullptr);
    EXPECT_FALSE(dict->contains(runtime.new_str("key")));
}

TEST(Compile, CompilesFunctionLocalDeleteAsFastLocal)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def f():\n"
        "    x = 'first'\n"
        "    del x\n"
        "    x = 'second'\n"
        "    return x\n"
        "result = f()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    cxxpython::CodeObject *function_code = nullptr;
    for (cxxpython::Object *constant : code->constants())
    {
        if (auto *candidate = dynamic_cast<cxxpython::CodeObject *>(constant))
            function_code = candidate;
    }
    ASSERT_NE(function_code, nullptr);
    bool saw_delete_fast = false;
    for (const cxxpython::Instruction &instruction : function_code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::DeleteFast)
            saw_delete_fast = true;
    }
    EXPECT_TRUE(saw_delete_fast);

    cxxpython::DictObject *globals = runtime.new_dict();
    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "second");
}

TEST(Compile, CompilesDictSubscriptThroughObjectProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *dict = runtime.new_dict();
    dict->set(runtime.new_str("answer"), runtime.new_int(42));
    globals->set(runtime.new_str("d"), dict);

    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "d['answer']", "script.py");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->instructions()[2].opcode, cxxpython::Opcode::BinarySubscr);

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *globals));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 42);
}

TEST(Compile, CompilesTupleAndDictLiterals)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "t = (1, 'x', True)\n"
        "single = ('only',)\n"
        "empty = ()\n"
        "d = {'key': 'value', 'n': 2}\n"
        "result = d['key']\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_build_tuple = false;
    bool saw_build_map = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::BuildTuple)
            saw_build_tuple = true;
        if (instruction.opcode == cxxpython::Opcode::BuildMap)
            saw_build_map = true;
    }
    EXPECT_TRUE(saw_build_tuple);
    EXPECT_TRUE(saw_build_map);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());

    auto *tuple = dynamic_cast<cxxpython::TupleObject *>(globals->get(runtime.new_str("t")));
    ASSERT_NE(tuple, nullptr);
    ASSERT_EQ(tuple->size(), 3U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(tuple->items()[0])->value, 1);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(tuple->items()[1])->value, "x");
    EXPECT_EQ(tuple->items()[2], runtime.true_value());

    auto *single = dynamic_cast<cxxpython::TupleObject *>(globals->get(runtime.new_str("single")));
    ASSERT_NE(single, nullptr);
    ASSERT_EQ(single->size(), 1U);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(single->items()[0])->value, "only");

    auto *empty = dynamic_cast<cxxpython::TupleObject *>(globals->get(runtime.new_str("empty")));
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->size(), 0U);

    auto *dict = dynamic_cast<cxxpython::DictObject *>(globals->get(runtime.new_str("d")));
    ASSERT_NE(dict, nullptr);
    EXPECT_EQ(dict->size(), 2U);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(dict->get(runtime.new_str("key")))->value, "value");
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(dict->get(runtime.new_str("n")))->value, 2);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")))->value, "value");
}

TEST(Compile, CompilesListLiterals)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "xs = [1, 'x', False]\n"
        "empty = []\n"
        "picked = xs[1]\n"
        "xs[1] = 'y'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_build_list = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::BuildList)
            saw_build_list = true;
    }
    EXPECT_TRUE(saw_build_list);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());

    auto *list = dynamic_cast<cxxpython::ListObject *>(globals->get(runtime.new_str("xs")));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 3U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(list->items()[0])->value, 1);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(list->items()[1])->value, "y");
    EXPECT_EQ(list->items()[2], runtime.false_value());

    auto *empty = dynamic_cast<cxxpython::ListObject *>(globals->get(runtime.new_str("empty")));
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->size(), 0U);

    auto *picked = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("picked")));
    ASSERT_NE(picked, nullptr);
    EXPECT_EQ(picked->value, "x");
}

TEST(Compile, CompilesSingletonLiteralsWithoutNameLookup)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "truth = True\n"
        "lie = False\n"
        "missing = None\n"
        "result = (truth, lie, missing)\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    std::size_t load_names = 0;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::LoadName)
            ++load_names;
    }
    EXPECT_EQ(load_names, 3U);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    EXPECT_EQ(globals->get(runtime.new_str("truth")), runtime.true_value());
    EXPECT_EQ(globals->get(runtime.new_str("lie")), runtime.false_value());
    EXPECT_EQ(globals->get(runtime.new_str("missing")), runtime.none());

    auto *result = dynamic_cast<cxxpython::TupleObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->size(), 3U);
    EXPECT_EQ(result->items()[0], runtime.true_value());
    EXPECT_EQ(result->items()[1], runtime.false_value());
    EXPECT_EQ(result->items()[2], runtime.none());
}

TEST(Compile, CompilesFunctionDefinitionsAndReturns)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def answer():\n"
        "    return 'ok'\n"
        "def identity(value):\n"
        "    return value\n"
        "first = answer()\n"
        "second = identity('arg')\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_make_function = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::MakeFunction)
            saw_make_function = true;
    }
    EXPECT_TRUE(saw_make_function);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *answer = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("answer")));
    auto *identity = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("identity")));
    ASSERT_NE(answer, nullptr);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(answer->code()->arg_count(), 0U);
    EXPECT_EQ(identity->code()->arg_count(), 1U);
    ASSERT_EQ(identity->code()->varnames().size(), 1U);
    auto *identity_arg = dynamic_cast<cxxpython::StrObject *>(identity->code()->varnames()[0]);
    ASSERT_NE(identity_arg, nullptr);
    EXPECT_EQ(identity_arg->value, "value");

    auto *first = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("first")));
    auto *second = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("second")));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->value, "ok");
    EXPECT_EQ(second->value, "arg");
}

TEST(Compile, CompilesNestedFunctionClosureCells)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def outer():\n"
        "    value = 'before'\n"
        "    def inner():\n"
        "        return value\n"
        "    value = 'after'\n"
        "    return inner\n"
        "result = outer()()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "after");

    auto *outer = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("outer")));
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(outer->code()->cellvars().size(), 1U);
    auto *cellvar = dynamic_cast<cxxpython::StrObject *>(outer->code()->cellvars()[0]);
    ASSERT_NE(cellvar, nullptr);
    EXPECT_EQ(cellvar->value, "value");

    cxxpython::CodeObject *inner_code = nullptr;
    for (cxxpython::Object *constant : outer->code()->constants())
    {
        auto *candidate = dynamic_cast<cxxpython::CodeObject *>(constant);
        if (candidate && candidate->name() == "inner")
            inner_code = candidate;
    }
    ASSERT_NE(inner_code, nullptr);
    ASSERT_EQ(inner_code->freevars().size(), 1U);
    auto *freevar = dynamic_cast<cxxpython::StrObject *>(inner_code->freevars()[0]);
    ASSERT_NE(freevar, nullptr);
    EXPECT_EQ(freevar->value, "value");
}

TEST(Compile, PassesFreeVariablesThroughIntermediateFunctions)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def outer():\n"
        "    value = 'deep'\n"
        "    def middle():\n"
        "        def inner():\n"
        "            return value\n"
        "        return inner\n"
        "    return middle\n"
        "result = outer()()()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "deep");
}

TEST(Compile, CompilesGlobalAndNonlocalDeclarations)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "value = 'old'\n"
        "def update_global():\n"
        "    global value\n"
        "    value = 'new'\n"
        "def outer():\n"
        "    item = 'outer'\n"
        "    def update_nonlocal():\n"
        "        nonlocal item\n"
        "        item = 'inner'\n"
        "    update_nonlocal()\n"
        "    return item\n"
        "update_global()\n"
        "result = value + ':' + outer()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    cxxpython::CodeObject *update_global_code = nullptr;
    cxxpython::CodeObject *update_nonlocal_code = nullptr;
    for (cxxpython::Object *constant : code->constants())
    {
        auto *candidate = dynamic_cast<cxxpython::CodeObject *>(constant);
        if (candidate && candidate->name() == "update_global")
            update_global_code = candidate;
    }
    ASSERT_NE(update_global_code, nullptr);
    bool saw_store_global = false;
    for (const cxxpython::Instruction &instruction : update_global_code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::StoreGlobal)
            saw_store_global = true;
    }
    EXPECT_TRUE(saw_store_global);

    auto *result = dynamic_cast<cxxpython::StrObject *>(vm.execute(*code, *globals));
    EXPECT_EQ(result, nullptr);
    auto *stored = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->value, "new:inner");

    auto *outer = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("outer")));
    ASSERT_NE(outer, nullptr);
    for (cxxpython::Object *constant : outer->code()->constants())
    {
        auto *candidate = dynamic_cast<cxxpython::CodeObject *>(constant);
        if (candidate && candidate->name() == "update_nonlocal")
            update_nonlocal_code = candidate;
    }
    ASSERT_NE(update_nonlocal_code, nullptr);
    ASSERT_EQ(update_nonlocal_code->freevars().size(), 1U);
}

TEST(Compile, RejectsInvalidNonlocalDeclaration)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_exec_input(
            runtime,
            "def broken():\n"
            "    nonlocal missing\n"
            "    return missing\n",
            "script.py");
        FAIL() << "expected SyntaxError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.syntax_error_type());
        EXPECT_STREQ(error.what(), "no binding for nonlocal 'missing' found");
    }
}

TEST(Compile, FunctionsUseFastLocalsForAssignedNames)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "value = 'global'\n"
        "def choose(flag):\n"
        "    if flag:\n"
        "        value = 'local'\n"
        "    return value\n"
        "first = choose(True)\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *function = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("choose")));
    ASSERT_NE(function, nullptr);
    for (const cxxpython::Instruction &instruction : function->code()->instructions())
    {
        EXPECT_NE(instruction.opcode, cxxpython::Opcode::StoreName);
    }
    bool saw_load_fast = false;
    bool saw_store_fast = false;
    for (const cxxpython::Instruction &instruction : function->code()->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::LoadFast)
            saw_load_fast = true;
        if (instruction.opcode == cxxpython::Opcode::StoreFast)
            saw_store_fast = true;
    }
    EXPECT_TRUE(saw_load_fast);
    EXPECT_TRUE(saw_store_fast);

    ASSERT_EQ(function->code()->varnames().size(), 2U);
    auto *param = dynamic_cast<cxxpython::StrObject *>(function->code()->varnames()[0]);
    auto *local = dynamic_cast<cxxpython::StrObject *>(function->code()->varnames()[1]);
    ASSERT_NE(param, nullptr);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(param->value, "flag");
    EXPECT_EQ(local->value, "value");

    auto *first = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("first")));
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->value, "local");

    try
    {
        (void)cxxpython::object_call(function, {runtime.false_value()});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.unbound_local_error_type());
        EXPECT_STREQ(error.what(), "cannot access local variable 'value' where it is not associated with a value");
    }
}

TEST(Compile, FunctionWithoutReturnDefaultsToNone)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def implicit():\n"
        "    value = 'ignored'\n"
        "result = implicit()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    EXPECT_EQ(globals->get(runtime.new_str("result")), runtime.none());
}

TEST(Compile, CompilesVarargsVarkeywordsAndKeywordCalls)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "def collect(first, *args, **kwargs):\n"
        "    return kwargs\n"
        "result = collect('a', 'b', extra='value')\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_call_kw = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::CallKw)
            saw_call_kw = true;
    }
    EXPECT_TRUE(saw_call_kw);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *function = dynamic_cast<cxxpython::PythonFunctionObject *>(globals->get(runtime.new_str("collect")));
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->code()->arg_count(), 1U);
    EXPECT_TRUE(function->code()->has_varargs());
    EXPECT_TRUE(function->code()->has_varkeywords());
    ASSERT_GE(function->code()->varnames().size(), 3U);

    auto *result = dynamic_cast<cxxpython::DictObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    auto *extra = dynamic_cast<cxxpython::StrObject *>(result->get(runtime.new_str("extra")));
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(extra->value, "value");
}

TEST(Compile, CompilesMinimalClassDefinitions)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "class A:\n"
        "    label = 'class-label'\n"
        "    def get_label(self):\n"
        "        return self.label\n"
        "a = A()\n"
        "result = a.get_label()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_make_class = false;
    bool saw_build_class_name = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::MakeClass)
            saw_make_class = true;
        if (instruction.opcode == cxxpython::Opcode::LoadName)
        {
            auto *name = dynamic_cast<cxxpython::StrObject *>(code->names()[instruction.operand]);
            if (name && name->value == "__build_class__")
                saw_build_class_name = true;
        }
    }
    EXPECT_FALSE(saw_make_class);
    EXPECT_TRUE(saw_build_class_name);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *type = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("A")));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name, "A");
    EXPECT_EQ(type->module, "__main__");
    EXPECT_EQ(type->base, runtime.object_type());
    EXPECT_TRUE(cxxpython::is_subclass(type, runtime.object_type()));

    auto *label = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(type, "label"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->value, "class-label");

    cxxpython::Object *instance = globals->get(runtime.new_str("a"));
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->type(), type);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "label"), label);

    auto *method = dynamic_cast<cxxpython::PythonMethodObject *>(cxxpython::object_get_attr(instance, "get_label"));
    ASSERT_NE(method, nullptr);
    EXPECT_EQ(method->self(), instance);

    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "class-label");
}

TEST(Compile, CompilesClassDefinitionsWithBases)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "class Base:\n"
        "    base_attr = 'base'\n"
        "class Right:\n"
        "    right_attr = 'right'\n"
        "class Child(Base, Right):\n"
        "    child_attr = 'child'\n"
        "obj = Child()\n"
        "base_result = obj.base_attr\n"
        "right_result = obj.right_attr\n"
        "child_result = obj.child_attr\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *base = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Base")));
    auto *right = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Right")));
    auto *child = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Child")));
    ASSERT_NE(base, nullptr);
    ASSERT_NE(right, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->bases.size(), 2U);
    EXPECT_EQ(child->bases[0], base);
    EXPECT_EQ(child->bases[1], right);
    ASSERT_GE(child->mro.size(), 4U);
    EXPECT_EQ(child->mro[0], child);
    EXPECT_EQ(child->mro[1], base);
    EXPECT_EQ(child->mro[2], right);
    EXPECT_EQ(child->mro.back(), runtime.object_type());
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("base_result")))->value, "base");
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("right_result")))->value, "right");
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("child_result")))->value, "child");
}

TEST(Compile, ClassInstantiationCallsPythonInit)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "class A:\n"
        "    def __init__(self, value):\n"
        "        self.value = value\n"
        "a = A('ready')\n"
        "result = a.value\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "ready");
}

TEST(Compile, PythonInitMustReturnNone)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "class A:\n"
        "    def __init__(self):\n"
        "        return 'bad'\n"
        "A()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    try
    {
        (void)vm.execute(*code, *globals);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "__init__() should return None");
    }
}

TEST(Compile, CompilesTryExceptAndRaiseStatements)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "try:\n"
        "    raise TypeError('boom')\n"
        "except ValueError:\n"
        "    result = 'wrong'\n"
        "except TypeError as e:\n"
        "    saved = e\n"
        "    result = 'handled'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_setup_except = false;
    bool saw_check_match = false;
    bool saw_raise = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::SetupExcept)
            saw_setup_except = true;
        if (instruction.opcode == cxxpython::Opcode::CheckExceptionMatch)
            saw_check_match = true;
        if (instruction.opcode == cxxpython::Opcode::RaiseValue)
            saw_raise = true;
    }
    EXPECT_TRUE(saw_setup_except);
    EXPECT_TRUE(saw_check_match);
    EXPECT_TRUE(saw_raise);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "handled");
    auto *saved = dynamic_cast<cxxpython::ExceptionObject *>(globals->get(runtime.new_str("saved")));
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->type(), runtime.type_error_type());
    EXPECT_EQ(saved->message(), "boom");
}

TEST(Compile, TryExceptReraisesUnmatchedException)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "try:\n"
        "    raise TypeError('boom')\n"
        "except ValueError:\n"
        "    result = 'wrong'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    try
    {
        (void)vm.execute(*code, *globals);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "boom");
    }
}

TEST(Compile, CompilesAndExecutesTryFinallyControlFlow)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *normal = cxxpython::compile_exec_input(
        runtime,
        "result = 'start'\n"
        "try:\n"
        "    result = result + '-body'\n"
        "finally:\n"
        "    result = result + '-finally'\n",
        "script.py");
    ASSERT_NE(normal, nullptr);

    bool saw_setup_finally = false;
    bool saw_pop_finally = false;
    bool saw_end_finally = false;
    for (const cxxpython::Instruction &instruction : normal->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::SetupFinally)
            saw_setup_finally = true;
        if (instruction.opcode == cxxpython::Opcode::PopFinally)
            saw_pop_finally = true;
        if (instruction.opcode == cxxpython::Opcode::EndFinally)
            saw_end_finally = true;
    }
    EXPECT_TRUE(saw_setup_finally);
    EXPECT_TRUE(saw_pop_finally);
    EXPECT_TRUE(saw_end_finally);

    EXPECT_EQ(vm.execute(*normal, *globals), runtime.none());
    auto *normal_result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(normal_result, nullptr);
    EXPECT_EQ(normal_result->value, "start-body-finally");

    cxxpython::CodeObject *exceptional = cxxpython::compile_exec_input(
        runtime,
        "result = 'body'\n"
        "try:\n"
        "    raise TypeError('boom')\n"
        "finally:\n"
        "    result = result + '-finally'\n",
        "script.py");
    ASSERT_NE(exceptional, nullptr);
    try
    {
        (void)vm.execute(*exceptional, *globals);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "boom");
    }
    auto *exceptional_result = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(exceptional_result, nullptr);
    EXPECT_EQ(exceptional_result->value, "body-finally");

    cxxpython::CodeObject *returning = cxxpython::compile_exec_input(
        runtime,
        "events = []\n"
        "def f():\n"
        "    try:\n"
        "        return 'body'\n"
        "    finally:\n"
        "        events.append('finally')\n"
        "result = f()\n",
        "script.py");
    ASSERT_NE(returning, nullptr);
    EXPECT_EQ(vm.execute(*returning, *globals), runtime.none());
    auto *returned = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("result")));
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(returned->value, "body");
    auto *events = dynamic_cast<cxxpython::ListObject *>(globals->get(runtime.new_str("events")));
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->size(), 1U);
    EXPECT_EQ(dynamic_cast<cxxpython::StrObject *>(events->get(0))->value, "finally");
}

TEST(Compile, TryExceptFinallyUsesNearestBlockDuringUnwind)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();

    cxxpython::CodeObject *handled = cxxpython::compile_exec_input(
        runtime,
        "events = []\n"
        "try:\n"
        "    raise TypeError('boom')\n"
        "except TypeError:\n"
        "    events.append('except')\n"
        "finally:\n"
        "    events.append('finally')\n",
        "script.py");
    ASSERT_NE(handled, nullptr);
    EXPECT_EQ(vm.execute(*handled, *globals), runtime.none());
    auto *events = dynamic_cast<cxxpython::ListObject *>(globals->get(runtime.new_str("events")));
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->size(), 2U);
    EXPECT_EQ(dynamic_cast<cxxpython::StrObject *>(events->get(0))->value, "except");
    EXPECT_EQ(dynamic_cast<cxxpython::StrObject *>(events->get(1))->value, "finally");

    cxxpython::CodeObject *overridden = cxxpython::compile_exec_input(
        runtime,
        "try:\n"
        "    try:\n"
        "        raise TypeError('body')\n"
        "    finally:\n"
        "        raise ValueError('finally')\n"
        "except ValueError as e:\n"
        "    saved = e\n",
        "script.py");
    ASSERT_NE(overridden, nullptr);
    EXPECT_EQ(vm.execute(*overridden, *globals), runtime.none());
    auto *saved = dynamic_cast<cxxpython::ExceptionObject *>(globals->get(runtime.new_str("saved")));
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->type(), runtime.value_error_type());
    EXPECT_EQ(saved->message(), "finally");
}

TEST(Compile, ClassDefinitionsSelectInheritedMetaclass)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "Meta = type('Meta', (type,), {})\n"
        "Base = Meta('Base', (), {})\n"
        "class Child(Base):\n"
        "    label = 'child'\n"
        "result = type(Child)\n"
        "instance = Child()\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *meta = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Meta")));
    auto *base = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Base")));
    auto *child = dynamic_cast<cxxpython::TypeObject *>(globals->get(runtime.new_str("Child")));
    ASSERT_NE(meta, nullptr);
    ASSERT_NE(base, nullptr);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(base->type(), meta);
    EXPECT_EQ(child->type(), meta);
    EXPECT_EQ(globals->get(runtime.new_str("result")), meta);
    EXPECT_EQ(globals->get(runtime.new_str("instance"))->type(), child);
}

TEST(Compile, RaisesSyntaxErrorForInvalidAssignmentTarget)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "1 = 2\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:3: invalid assignment target");
    }

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "True = 2\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:6: invalid assignment target");
    }

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "False = 2\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:7: invalid assignment target");
    }

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "None = 2\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:6: invalid assignment target");
    }
}

TEST(Compile, RaisesSyntaxErrorForReturnOutsideFunction)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "return 1\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:1: 'return' outside function");
    }
}

TEST(Compile, RaisesSyntaxErrorForUnclosedSubscript)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "xs[1", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:5: expected ']' after subscript");
    }
}

TEST(Compile, CompilesAndExecutesBasicFString)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("name"), runtime.new_str("Ada"));

    cxxpython::CodeObject *code = cxxpython::compile_exec_input(
        runtime,
        "value = f'Hello {name}! {1 + 2}'\n"
        "escaped = f'{{{name}}}'\n",
        "script.py");
    ASSERT_NE(code, nullptr);

    bool saw_format_value = false;
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::FormatValue)
        {
            saw_format_value = true;
            break;
        }
    }
    EXPECT_TRUE(saw_format_value);

    EXPECT_EQ(vm.execute(*code, *globals), runtime.none());
    auto *value = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("value")));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, "Hello Ada! 3");
    auto *escaped = dynamic_cast<cxxpython::StrObject *>(globals->get(runtime.new_str("escaped")));
    ASSERT_NE(escaped, nullptr);
    EXPECT_EQ(escaped->value, "{Ada}");
}

TEST(Compile, RaisesSyntaxErrorForUnsupportedFStringConversion)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "f\"{1!r}\"", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:1: f-string conversions and format specs are not supported yet");
    }
}

TEST(Compile, RaisesSyntaxErrorForUnsupportedDottedFromImport)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_exec_input(runtime, "from pkg.mod import value\n", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:9: dotted imports are not supported yet");
    }
}

TEST(Compile, CallsNonCallableThroughNormalTypeErrorPath)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = cxxpython::compile_eval_input(runtime, "1()", "script.py");

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'int' object is not callable");
    }
}
