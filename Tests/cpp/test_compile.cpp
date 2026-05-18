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
    for (const cxxpython::Instruction &instruction : code->instructions())
    {
        if (instruction.opcode == cxxpython::Opcode::MakeClass)
            saw_make_class = true;
    }
    EXPECT_TRUE(saw_make_class);

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
