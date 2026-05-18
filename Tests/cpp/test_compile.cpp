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

TEST(Compile, RejectsChainedComparisonsUntilJumpSemanticsExist)
{
    cxxpython::Runtime runtime;

    try
    {
        (void)cxxpython::compile_eval_input(runtime, "1 == 1 == 1", "broken.py");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type_name(), "SyntaxError");
        EXPECT_STREQ(error.what(), "broken.py:1:8: chained comparisons are not implemented");
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
