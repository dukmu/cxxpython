#include <gtest/gtest.h>

#include <sstream>

#include "cxxpython/vm.hpp"
#include "test_support.hpp"

TEST(Bytecode, MetadataNamesAndStackEffects)
{
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadConst), "LOAD_CONST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadName), "LOAD_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreName), "STORE_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadAttr), "LOAD_ATTR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreAttr), "STORE_ATTR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopTop), "POP_TOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DupTop), "DUP_TOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::RotTwo), "ROT_TWO");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::RotThree), "ROT_THREE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::UnaryOp), "UNARY_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinaryOp), "BINARY_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinarySubscr), "BINARY_SUBSCR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreSubscr), "STORE_SUBSCR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::CompareOp), "COMPARE_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::JumpAbsolute), "JUMP_ABSOLUTE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopJumpIfFalse), "POP_JUMP_IF_FALSE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopJumpIfTrue), "POP_JUMP_IF_TRUE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::JumpIfFalseOrPop), "JUMP_IF_FALSE_OR_POP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::Call), "CALL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ReturnValue), "RETURN_VALUE");

    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadConst, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadName, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreName, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadAttr, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreAttr, 0}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopTop, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DupTop, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::RotTwo, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::RotThree, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::UnaryOp, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinaryOp, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinarySubscr, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreSubscr, 0}), -3);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::CompareOp, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::JumpAbsolute, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopJumpIfFalse, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopJumpIfTrue, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::JumpIfFalseOrPop, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 2}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::ReturnValue, 0}), -1);
}

TEST(Bytecode, DispatchesCompareOpThroughRichCompareProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::CodeObject *equal = runtime.new_code(
        {runtime.new_int(4), runtime.new_int(4)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::CompareOp, static_cast<std::uint32_t>(cxxpython::RichCompareOp::Equal)},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*equal), runtime.true_value());

    cxxpython::CodeObject *not_equal = runtime.new_code(
        {runtime.new_str("left"), runtime.new_str("right")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::CompareOp, static_cast<std::uint32_t>(cxxpython::RichCompareOp::NotEqual)},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*not_equal), runtime.true_value());
}

TEST(Bytecode, DispatchesJumpOpcodesThroughTruthProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::TypeSlots false_slots;
    false_slots.truth = test_support::custom_false;
    cxxpython::TypeObject *false_type = runtime.new_heap_type("FalseLike", runtime.object_type(), false_slots);
    cxxpython::Object *false_like = runtime.allocate<cxxpython::Object>(false_type);

    cxxpython::Object *taken = runtime.new_str("taken");
    cxxpython::Object *skipped = runtime.new_str("skipped");
    cxxpython::CodeObject *false_jump = runtime.new_code(
        {false_like, skipped, taken},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::PopJumpIfFalse, 4},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*false_jump), taken);

    cxxpython::CodeObject *true_jump = runtime.new_code(
        {runtime.true_value(), skipped, taken},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::PopJumpIfTrue, 4},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*true_jump), taken);

    cxxpython::CodeObject *absolute = runtime.new_code(
        {skipped, taken},
        {{cxxpython::Opcode::JumpAbsolute, 2},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*absolute), taken);
}

TEST(Bytecode, DispatchesStackManipulationOpcodes)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *second = runtime.new_str("second");
    cxxpython::Object *third = runtime.new_str("third");

    cxxpython::CodeObject *rot_three = runtime.new_code(
        {first, second, third},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::RotThree, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*rot_three), second);

    cxxpython::CodeObject *rot_two = runtime.new_code(
        {first, second},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::RotTwo, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*rot_two), first);

    cxxpython::CodeObject *duplicate = runtime.new_code(
        {first},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::DupTop, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*duplicate), first);
}

TEST(Bytecode, DispatchesJumpIfFalseOrPop)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    cxxpython::Object *taken = runtime.new_str("taken");
    cxxpython::Object *skipped = runtime.new_str("skipped");

    cxxpython::CodeObject *false_jump = runtime.new_code(
        {runtime.false_value(), skipped, taken},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::JumpIfFalseOrPop, 4},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*false_jump), taken);

    cxxpython::CodeObject *true_pop = runtime.new_code(
        {runtime.true_value(), taken},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::JumpIfFalseOrPop, 4},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    EXPECT_EQ(vm.execute(*true_pop), taken);
}

TEST(Bytecode, DispatchesSubscriptOpcodesThroughObjectProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::ListObject *list = runtime.new_list({runtime.new_str("zero"), runtime.new_str("one")});
    cxxpython::Object *replacement = runtime.new_str("updated");
    cxxpython::CodeObject *load_list = runtime.new_code(
        {list, runtime.new_int(1)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::BinarySubscr, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*load_list), list->get(1));

    cxxpython::CodeObject *store_list = runtime.new_code(
        {list, runtime.new_int(1), replacement, runtime.none()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::StoreSubscr, 0},
         {cxxpython::Opcode::LoadConst, 3},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*store_list), runtime.none());
    EXPECT_EQ(list->get(1), replacement);

    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::Object *key = runtime.new_str("name");
    cxxpython::Object *value = runtime.new_str("dict-value");
    dict->set(key, value);
    cxxpython::CodeObject *load_dict = runtime.new_code(
        {dict, key},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::BinarySubscr, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*load_dict), value);
}

TEST(Bytecode, SubscriptOpcodesRaisePythonErrorsThroughObjectProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::Object *missing = runtime.new_str("missing");
    cxxpython::CodeObject *missing_key = runtime.new_code(
        {dict, missing},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::BinarySubscr, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(*missing_key);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.key_error_type());
        EXPECT_STREQ(error.what(), "'missing'");
    }

    cxxpython::TupleObject *tuple = runtime.new_tuple({runtime.new_int(1)});
    cxxpython::CodeObject *store_tuple = runtime.new_code(
        {tuple, runtime.new_int(0), runtime.new_int(9)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::StoreSubscr, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(*store_tuple);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'tuple' object does not support item assignment");
    }
}

TEST(Bytecode, ExecutesLoadConstAndReturn)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::IntObject *value = runtime.new_int(42);
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->type(), runtime.code_type());
    EXPECT_EQ(vm.execute(*code), value);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
    EXPECT_EQ(vm.thread_state().current_frame(), nullptr);
    EXPECT_EQ(&vm.interpreter_state().runtime(), &runtime);
}

TEST(Bytecode, ExecutesIntegerBinaryOpAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_int(2), runtime.new_int(3)},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryOp,
                                                     static_cast<std::uint32_t>(cxxpython::BinaryOp::Add)},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 5);
}

TEST(Bytecode, ExecutesStringBinaryOpAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_str("py"), runtime.new_str("thon")},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryOp,
                                                     static_cast<std::uint32_t>(cxxpython::BinaryOp::Add)},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::StrObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "python");
}

TEST(Bytecode, ExecutesGenericUnaryOp)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *negative = runtime.new_code(
        {runtime.new_int(5)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::UnaryOp, static_cast<std::uint32_t>(cxxpython::UnaryOp::Negative)},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *negative_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*negative));
    ASSERT_NE(negative_result, nullptr);
    EXPECT_EQ(negative_result->value, -5);

    cxxpython::CodeObject *invert = runtime.new_code(
        {runtime.new_int(1)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::UnaryOp, static_cast<std::uint32_t>(cxxpython::UnaryOp::Invert)},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *invert_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*invert));
    ASSERT_NE(invert_result, nullptr);
    EXPECT_EQ(invert_result->value, -2);
}

TEST(Bytecode, ExecutesGenericBinaryOp)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *subtract = runtime.new_code(
        {runtime.new_int(9), runtime.new_int(4)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::BinaryOp, static_cast<std::uint32_t>(cxxpython::BinaryOp::Subtract)},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *difference = dynamic_cast<cxxpython::IntObject *>(vm.execute(*subtract));
    ASSERT_NE(difference, nullptr);
    EXPECT_EQ(difference->value, 5);

    cxxpython::CodeObject *multiply = runtime.new_code(
        {runtime.new_int(6), runtime.new_int(7)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::BinaryOp, static_cast<std::uint32_t>(cxxpython::BinaryOp::Multiply)},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *product = dynamic_cast<cxxpython::IntObject *>(vm.execute(*multiply));
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(product->value, 42);
}

TEST(Bytecode, RaisesPythonTypeErrorForUnsupportedBinaryOp)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_int(1), runtime.new_str("x")},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryOp,
                                                     static_cast<std::uint32_t>(cxxpython::BinaryOp::Add)},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unsupported operand type(s) for +: 'int' and 'str'");
    }
}

TEST(Bytecode, DispatchesBinaryOpThroughTypeSlots)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    auto add_repr = [](const cxxpython::Object &object) -> std::string
    {
        return "<adder " + std::to_string(static_cast<const cxxpython::IntObject &>(object).value) + ">";
    };
    auto add_binary = [](const cxxpython::Object &left,
                         const cxxpython::Object &right,
                         cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        if (op != cxxpython::BinaryOp::Add)
            cxxpython::raise_type_error(left.runtime(), "adder only supports add");
        auto &left_value = static_cast<const cxxpython::IntObject &>(left);
        auto *right_value = dynamic_cast<const cxxpython::IntObject *>(&right);
        if (!right_value)
            cxxpython::raise_type_error(left.runtime(), "adder expects int right operand");
        return left.runtime()->new_int(left_value.value * 10 + right_value->value);
    };
    auto add_traverse = [](const cxxpython::Object &object, const cxxpython::VisitChildren &visitor)
    {
        (void)object;
        (void)visitor;
    };

    cxxpython::TypeObject *adder_type = runtime.new_heap_type(
        "Adder",
        runtime.int_type(),
        cxxpython::TypeSlots{
            .repr = add_repr,
            .hash = cxxpython::make_int_slots().hash,
            .equal = cxxpython::make_int_slots().equal,
            .truth = cxxpython::make_int_slots().truth,
            .traverse = add_traverse,
            .binary_op = add_binary,
        });

    cxxpython::IntObject *left = runtime.allocate<cxxpython::IntObject>(adder_type, 7);
    cxxpython::CodeObject *code = runtime.new_code({left, runtime.new_int(3)},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryOp,
                                                     static_cast<std::uint32_t>(cxxpython::BinaryOp::Add)},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 73);
}

TEST(Bytecode, CodeObjectsTraverseReferencedConstants)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *first = runtime.new_int(1);
    cxxpython::StrObject *second = runtime.new_str("x");
    cxxpython::CodeObject *code = runtime.new_code({first, second},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->name(), "<module>");
    EXPECT_EQ(code->filename(), "<string>");

    std::vector<cxxpython::Object *> visited;
    code->visit_children([&visited](cxxpython::Object *object)
                         { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(visited[0], first);
    EXPECT_EQ(visited[1], second);
    EXPECT_EQ(code->repr().find("<code object <module> at "), 0U);
    EXPECT_NE(code->repr().find("file '<string>'"), std::string::npos);
}

TEST(Bytecode, CodeObjectsExposeArgumentMetadata)
{
    cxxpython::Runtime runtime;
    cxxpython::StrObject *self_name = runtime.new_str("self");
    cxxpython::StrObject *value_name = runtime.new_str("value");
    cxxpython::Object *constant = runtime.new_int(1);
    cxxpython::StrObject *global_name = runtime.new_str("global_name");
    cxxpython::CodeObject *code = runtime.new_code(
        "method",
        "test.py",
        2,
        {self_name, value_name},
        {constant},
        {global_name},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->arg_count(), 2U);
    ASSERT_EQ(code->varnames().size(), 2U);
    EXPECT_EQ(code->varnames()[0], self_name);
    EXPECT_EQ(code->varnames()[1], value_name);

    std::vector<cxxpython::Object *> visited;
    code->visit_children([&visited](cxxpython::Object *object)
                         { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 4U);
    EXPECT_EQ(visited[0], constant);
    EXPECT_EQ(visited[1], global_name);
    EXPECT_EQ(visited[2], self_name);
    EXPECT_EQ(visited[3], value_name);
}

TEST(Bytecode, FrameObjectsTrackCodeAndStackReferences)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *value = runtime.new_int(9);
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.new_dict();
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code, locals, globals, builtins);
    frame->push(value);

    EXPECT_EQ(frame->type(), runtime.frame_type());
    EXPECT_EQ(frame->code(), code);
    EXPECT_EQ(frame->locals(), locals);
    EXPECT_EQ(frame->globals(), globals);
    EXPECT_EQ(frame->builtins(), builtins);
    EXPECT_EQ(frame->stack_size(), 1U);
    EXPECT_EQ(frame->repr().find("<frame at "), 0U);

    std::vector<cxxpython::Object *> visited;
    frame->visit_children([&visited](cxxpython::Object *object)
                          { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 5U);
    EXPECT_EQ(visited[0], code);
    EXPECT_EQ(visited[1], locals);
    EXPECT_EQ(visited[2], globals);
    EXPECT_EQ(visited[3], builtins);
    EXPECT_EQ(visited[4], value);
}

TEST(Bytecode, LoadNameResolvesLocalsGlobalsThenBuiltins)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::StrObject *name = runtime.new_str("x");
    cxxpython::CodeObject *code = runtime.new_code({},
                                                   {name},
                                                   {{cxxpython::Opcode::LoadName, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.new_dict();
    builtins->set(runtime.new_str("x"), runtime.new_int(1));
    globals->set(runtime.new_str("x"), runtime.new_int(2));
    locals->set(runtime.new_str("x"), runtime.new_int(3));

    auto *local_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(local_result, nullptr);
    EXPECT_EQ(local_result->value, 3);

    locals = runtime.new_dict();
    auto *global_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(global_result, nullptr);
    EXPECT_EQ(global_result->value, 2);

    globals = runtime.new_dict();
    auto *builtin_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(builtin_result, nullptr);
    EXPECT_EQ(builtin_result->value, 1);
}

TEST(Bytecode, DispatchesLoadAttrAndStoreAttrThroughObjectApi)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::TypeObject *type = runtime.new_heap_type("Carrier", runtime.object_type());
    cxxpython::Object *object = runtime.allocate<cxxpython::Object>(type);
    cxxpython::Object *value = runtime.new_str("stored");
    cxxpython::StrObject *name = runtime.new_str("field");
    cxxpython::CodeObject *store = runtime.new_code(
        {object, value, runtime.none()},
        {name},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::StoreAttr, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*store), runtime.none());
    EXPECT_EQ(cxxpython::object_get_attr(object, "field"), value);

    cxxpython::CodeObject *load = runtime.new_code(
        {object},
        {name},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadAttr, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*load), value);
}

TEST(Bytecode, CallsCallableObjectsThroughTypeSlot)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::Object *callable = runtime.builtins_dict()->get(runtime.new_str("print"));
    ASSERT_NE(callable, nullptr);

    std::ostringstream output;
    runtime.set_stdout(output);
    cxxpython::CodeObject *code = runtime.new_code(
        {callable, runtime.new_str("answer"), runtime.new_int(42)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::Call, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*code), runtime.none());
    EXPECT_EQ(output.str(), "answer 42\n");
}

TEST(Bytecode, RaisesPythonTypeErrorForNonCallable)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code(
        {runtime.new_int(1)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::Call, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

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

TEST(Bytecode, ThreadStateVisitsActiveFramesAsRoots)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::IntObject *value = runtime.new_int(11);
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.builtins_dict();
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code, locals, globals, builtins);
    frame->push(value);

    vm.thread_state().push_frame(frame);

    std::vector<cxxpython::Object *> active_roots;
    vm.thread_state().visit_frames([&active_roots](cxxpython::Object *object)
                                   { active_roots.push_back(object); });

    ASSERT_EQ(active_roots.size(), 1U);
    EXPECT_EQ(active_roots[0], frame);

    std::vector<cxxpython::Object *> frame_children;
    active_roots[0]->visit_children([&frame_children](cxxpython::Object *object)
                                    { frame_children.push_back(object); });
    ASSERT_EQ(frame_children.size(), 5U);
    EXPECT_EQ(frame_children[0], code);
    EXPECT_EQ(frame_children[1], locals);
    EXPECT_EQ(frame_children[2], globals);
    EXPECT_EQ(frame_children[3], builtins);
    EXPECT_EQ(frame_children[4], value);

    vm.thread_state().pop_frame(frame);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
}

TEST(Bytecode, RejectsMalformedBytecode)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{cxxpython::Opcode::LoadConst, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)cxxpython::stack_effect({static_cast<cxxpython::Opcode>(255), 0}),
                 cxxpython::BytecodeError);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_int(1)},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::UnaryOp, 255},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_int(1), runtime.new_int(1)},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::BinaryOp, 255},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_int(1), runtime.new_int(1)},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::CompareOp, 255},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.true_value()},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::PopJumpIfFalse, 99},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.false_value()},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::JumpIfFalseOrPop, 99},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{cxxpython::Opcode::DupTop, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_int(1)},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::RotTwo, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_int(1), runtime.new_int(2)},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::RotThree, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{static_cast<cxxpython::Opcode>(255), 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
}
