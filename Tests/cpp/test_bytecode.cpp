#include <gtest/gtest.h>

#include <sstream>

#include "cxxpython/vm.hpp"
#include "test_support.hpp"

TEST(Bytecode, MetadataNamesAndStackEffects)
{
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadConst), "LOAD_CONST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadName), "LOAD_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreName), "STORE_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteName), "DELETE_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadGlobal), "LOAD_GLOBAL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreGlobal), "STORE_GLOBAL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteGlobal), "DELETE_GLOBAL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadFast), "LOAD_FAST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreFast), "STORE_FAST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteFast), "DELETE_FAST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadDeref), "LOAD_DEREF");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreDeref), "STORE_DEREF");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteDeref), "DELETE_DEREF");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadClosure), "LOAD_CLOSURE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadAttr), "LOAD_ATTR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreAttr), "STORE_ATTR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteAttr), "DELETE_ATTR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopTop), "POP_TOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DupTop), "DUP_TOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::RotTwo), "ROT_TWO");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::RotThree), "ROT_THREE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::UnaryOp), "UNARY_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinaryOp), "BINARY_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::FormatValue), "FORMAT_VALUE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinarySubscr), "BINARY_SUBSCR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreSubscr), "STORE_SUBSCR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::DeleteSubscr), "DELETE_SUBSCR");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BuildList), "BUILD_LIST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BuildTuple), "BUILD_TUPLE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BuildMap), "BUILD_MAP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BuildSlice), "BUILD_SLICE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::CompareOp), "COMPARE_OP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::GetIter), "GET_ITER");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ForIter), "FOR_ITER");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BreakLoop), "BREAK_LOOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ContinueLoop), "CONTINUE_LOOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::JumpAbsolute), "JUMP_ABSOLUTE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopJumpIfFalse), "POP_JUMP_IF_FALSE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopJumpIfTrue), "POP_JUMP_IF_TRUE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::JumpIfFalseOrPop), "JUMP_IF_FALSE_OR_POP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::MakeFunction), "MAKE_FUNCTION");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::MakeClass), "MAKE_CLASS");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::Call), "CALL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::CallKw), "CALL_KW");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::SetupExcept), "SETUP_EXCEPT");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::SetupFinally), "SETUP_FINALLY");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopExcept), "POP_EXCEPT");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopFinally), "POP_FINALLY");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::CheckExceptionMatch), "CHECK_EXCEPTION_MATCH");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::RaiseValue), "RAISE_VALUE");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::EndFinally), "END_FINALLY");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ReturnValue), "RETURN_VALUE");

    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadConst, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadName, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreName, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteName, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadGlobal, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreGlobal, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteGlobal, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadFast, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreFast, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteFast, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadDeref, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreDeref, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteDeref, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadClosure, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadAttr, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreAttr, 0}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteAttr, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopTop, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DupTop, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::RotTwo, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::RotThree, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::UnaryOp, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinaryOp, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::FormatValue, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinarySubscr, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreSubscr, 0}), -3);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::DeleteSubscr, 0}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildList, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildList, 3}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildTuple, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildTuple, 3}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildMap, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildMap, 2}), -3);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BuildSlice, 3}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::CompareOp, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::GetIter, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::ForIter, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BreakLoop, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::ContinueLoop, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::JumpAbsolute, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopJumpIfFalse, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopJumpIfTrue, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::JumpIfFalseOrPop, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::MakeFunction, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::MakeFunction, 1}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::MakeClass, 0}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 2}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::CallKw, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::CallKw, 2}), -3);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::SetupExcept, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::SetupFinally, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopExcept, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopFinally, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::CheckExceptionMatch, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::RaiseValue, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::EndFinally, 0}), 0);
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

TEST(Bytecode, DispatchesMakeFunction)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::Object *result = runtime.new_str("made");
    cxxpython::CodeObject *function_code = runtime.new_code(
        "made_function",
        "test.py",
        {result},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::CodeObject *module_code = runtime.new_code(
        {function_code},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::MakeFunction, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *function = dynamic_cast<cxxpython::PythonFunctionObject *>(vm.execute(*module_code, *globals));
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->name(), "made_function");
    EXPECT_EQ(function->globals(), globals);
    EXPECT_EQ(cxxpython::object_call(function, {}), result);
}

TEST(Bytecode, DispatchesMakeFunctionWithClosure)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::Object *captured = runtime.new_str("captured");
    cxxpython::CellObject *cell = runtime.new_cell(captured);
    cxxpython::CodeObject *function_code = runtime.new_code(
        "inner",
        "test.py",
        0,
        0,
        {},
        {},
        {},
        {},
        {runtime.new_str("value")},
        {{cxxpython::Opcode::LoadDeref, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::TupleObject *closure = runtime.new_tuple({cell});
    cxxpython::CodeObject *module_code = runtime.new_code(
        {closure, function_code},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::MakeFunction, 1},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *function = dynamic_cast<cxxpython::PythonFunctionObject *>(vm.execute(*module_code, *globals));
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->closure().size(), 1U);
    EXPECT_EQ(function->closure()[0], cell);
    EXPECT_EQ(cxxpython::object_call(function, {}), captured);
}

TEST(Bytecode, DispatchesMakeClass)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::CodeObject *class_code = runtime.new_code(
        "Made",
        "test.py",
        {runtime.new_str("class-value")},
        {runtime.new_str("value")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::StoreName, 0},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::CodeObject *module_code = runtime.new_code(
        {runtime.new_tuple({runtime.object_type()}), runtime.new_dict(), class_code},
        {runtime.new_str("Made")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::MakeClass, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *type = dynamic_cast<cxxpython::TypeObject *>(vm.execute(*module_code, *globals));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name, "Made");
    EXPECT_EQ(type->module, "__main__");
    EXPECT_EQ(type->base, runtime.object_type());
    EXPECT_TRUE(cxxpython::is_subclass(type, runtime.object_type()));
    auto *value = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(type, "value"));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, "class-value");
}

TEST(Bytecode, MakeClassUsesEvaluatedBases)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::DictObject *base_namespace = runtime.new_dict();
    cxxpython::Object *base_value = runtime.new_str("base-value");
    base_namespace->set(runtime.new_str("base_attr"), base_value);
    cxxpython::TypeObject *base = runtime.new_class("Base", runtime.object_type(), base_namespace, "__main__");
    cxxpython::CodeObject *class_code = runtime.new_code(
        "Child",
        "test.py",
        {runtime.new_str("child-value")},
        {runtime.new_str("child_attr")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::StoreName, 0},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::CodeObject *module_code = runtime.new_code(
        {runtime.new_tuple({base}), runtime.new_dict(), class_code},
        {runtime.new_str("Child")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::MakeClass, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *type = dynamic_cast<cxxpython::TypeObject *>(vm.execute(*module_code, *globals));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->base, base);
    ASSERT_EQ(type->bases.size(), 1U);
    EXPECT_EQ(type->bases[0], base);
    EXPECT_TRUE(cxxpython::is_subclass(type, base));
    EXPECT_EQ(cxxpython::object_get_attr(type, "base_attr"), base_value);
}

TEST(Bytecode, TypeCallClassCreationUsesActiveFrameModule)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("__name__"), runtime.new_str("__main__"));

    cxxpython::DictObject *namespace_dict = runtime.new_dict();
    namespace_dict->set(runtime.new_str("value"), runtime.new_str("from-type"));
    cxxpython::CodeObject *code = runtime.new_code(
        {runtime.new_str("Made"),
         runtime.new_tuple({runtime.object_type()}),
         namespace_dict},
        {runtime.new_str("type"), runtime.new_str("Made")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::Call, 3},
         {cxxpython::Opcode::StoreName, 1},
         {cxxpython::Opcode::LoadName, 1},
         {cxxpython::Opcode::ReturnValue, 0}});

    auto *type = dynamic_cast<cxxpython::TypeObject *>(vm.execute(*code, *globals));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name, "Made");
    EXPECT_EQ(type->module, "__main__");
    auto *module = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(type, "__module__"));
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->value, "__main__");
}

TEST(Bytecode, DispatchesBuildCollectionLiterals)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    auto *list = dynamic_cast<cxxpython::ListObject *>(vm.execute(*runtime.new_code(
        {runtime.new_int(1), runtime.new_str("x"), runtime.false_value()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::BuildList, 3},
         {cxxpython::Opcode::ReturnValue, 0}})));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 3U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(list->items()[0])->value, 1);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(list->items()[1])->value, "x");
    EXPECT_EQ(list->items()[2], runtime.false_value());

    auto *tuple = dynamic_cast<cxxpython::TupleObject *>(vm.execute(*runtime.new_code(
        {runtime.new_int(1), runtime.new_str("x"), runtime.true_value()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::BuildTuple, 3},
         {cxxpython::Opcode::ReturnValue, 0}})));
    ASSERT_NE(tuple, nullptr);
    ASSERT_EQ(tuple->size(), 3U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(tuple->items()[0])->value, 1);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(tuple->items()[1])->value, "x");
    EXPECT_EQ(tuple->items()[2], runtime.true_value());

    auto *dict = dynamic_cast<cxxpython::DictObject *>(vm.execute(*runtime.new_code(
        {runtime.new_str("left"),
         runtime.new_int(1),
         runtime.new_str("right"),
         runtime.new_int(2)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::LoadConst, 3},
         {cxxpython::Opcode::BuildMap, 2},
         {cxxpython::Opcode::ReturnValue, 0}})));
    ASSERT_NE(dict, nullptr);
    EXPECT_EQ(dict->size(), 2U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(dict->get(runtime.new_str("left")))->value, 1);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(dict->get(runtime.new_str("right")))->value, 2);
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

    cxxpython::CodeObject *delete_list = runtime.new_code(
        {list, runtime.new_int(0), runtime.none()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::DeleteSubscr, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*delete_list), runtime.none());
    ASSERT_EQ(list->size(), 1U);
    EXPECT_EQ(list->get(0), replacement);

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

    cxxpython::CodeObject *delete_dict = runtime.new_code(
        {dict, key, runtime.none()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::DeleteSubscr, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*delete_dict), runtime.none());
    EXPECT_FALSE(dict->contains(key));
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
        {tuple, runtime.new_int(0), runtime.new_int(9), runtime.none()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::StoreSubscr, 0},
         {cxxpython::Opcode::LoadConst, 3},
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

TEST(Bytecode, CodeObjectsExposeInstructionLineTable)
{
    cxxpython::Runtime runtime;
    cxxpython::CodeObject *code = runtime.new_code(
        "line_test",
        "test.py",
        {},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}},
        {12, 13});

    ASSERT_EQ(code->lines().size(), 2U);
    EXPECT_EQ(code->line_for_instruction(0), 12U);
    EXPECT_EQ(code->line_for_instruction(1), 13U);
    EXPECT_EQ(code->line_for_instruction(2), 0U);

    EXPECT_THROW((void)runtime.new_code(
                     "bad_lines",
                     "test.py",
                     {},
                     {{cxxpython::Opcode::ReturnValue, 0}},
                     {1, 2}),
                 cxxpython::BytecodeError);
}

TEST(Bytecode, CodeObjectsComputeMaximumStackSize)
{
    cxxpython::Runtime runtime;
    cxxpython::CodeObject *code = runtime.new_code(
        {runtime.new_int(1), runtime.new_int(2), runtime.new_int(3)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::BuildTuple, 3},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->stack_size(), 3U);
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

TEST(Bytecode, ExecutionRequestCarriesExplicitNamespaces)
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
    cxxpython::ModuleObject *module = runtime.new_module("__main__", "<test>");
    locals->set(runtime.new_str("x"), runtime.new_int(42));
    globals->set(runtime.new_str("x"), runtime.new_int(7));
    builtins->set(runtime.new_str("x"), runtime.new_int(1));

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(cxxpython::ExecutionRequest{
        .code = code,
        .locals = locals,
        .globals = globals,
        .builtins = builtins,
        .module = module,
        .closure = {},
        .mode = cxxpython::ExecutionMode::Module,
    }));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 42);
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

    cxxpython::CodeObject *delete_attr = runtime.new_code(
        {object, runtime.none()},
        {name},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::DeleteAttr, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*delete_attr), runtime.none());
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

TEST(Bytecode, CallOpcodeRootsCallableAndArgumentsAcrossDispatch)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    const std::size_t baseline_roots = runtime.root_count();
    bool observed_roots = false;
    cxxpython::Object *result = runtime.new_str("called");
    cxxpython::NativeFunctionObject *callable = runtime.new_native_function(
        "observe_roots",
        [baseline_roots, &observed_roots, result](cxxpython::Runtime &runtime,
                                                  const cxxpython::CallArguments &args)
        {
            EXPECT_EQ(args.positional.size(), 2U);
            EXPECT_GE(runtime.root_count(), baseline_roots + 3U);
            observed_roots = true;
            return result;
        });
    cxxpython::CodeObject *code = runtime.new_code(
        {callable, runtime.new_str("left"), runtime.new_str("right")},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::Call, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*code), result);
    EXPECT_TRUE(observed_roots);
    EXPECT_EQ(runtime.root_count(), baseline_roots);
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

TEST(Bytecode, DispatchesRaiseAndExceptionHandlers)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::Object *handled = runtime.new_str("handled");
    cxxpython::Object *unhandled = runtime.new_str("unhandled");
    cxxpython::ExceptionObject *type_error = runtime.new_exception(runtime.type_error_type(), "boom");

    cxxpython::CodeObject *handled_code = runtime.new_code(
        {type_error, runtime.type_error_type(), unhandled, handled},
        {{cxxpython::Opcode::SetupExcept, 5},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::RaiseValue, 0},
         {cxxpython::Opcode::PopExcept, 0},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::CheckExceptionMatch, 0},
         {cxxpython::Opcode::PopJumpIfFalse, 10},
         {cxxpython::Opcode::PopTop, 0},
         {cxxpython::Opcode::LoadConst, 3},
         {cxxpython::Opcode::ReturnValue, 0},
         {cxxpython::Opcode::RaiseValue, 0}});

    EXPECT_EQ(vm.execute(*handled_code), handled);

    cxxpython::CodeObject *unmatched_code = runtime.new_code(
        {type_error, runtime.value_error_type()},
        {{cxxpython::Opcode::SetupExcept, 4},
         {cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::RaiseValue, 0},
         {cxxpython::Opcode::PopExcept, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::CheckExceptionMatch, 0},
         {cxxpython::Opcode::PopJumpIfFalse, 8},
         {cxxpython::Opcode::PopTop, 0},
         {cxxpython::Opcode::RaiseValue, 0}});

    try
    {
        (void)vm.execute(*unmatched_code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "boom");
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

TEST(Bytecode, EnforcesFrameAndValueStackDepthLimits)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.builtins_dict();
    cxxpython::CodeObject *code = runtime.new_code({runtime.none()},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code, locals, globals, builtins);

    for (std::size_t index = 0; index < cxxpython::ThreadState::MaxFrameDepth; ++index)
        vm.thread_state().push_frame(frame);
    try
    {
        vm.thread_state().push_frame(frame);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.recursion_error_type());
        EXPECT_STREQ(error.what(), "maximum recursion depth exceeded");
    }
    for (std::size_t index = 0; index < cxxpython::ThreadState::MaxFrameDepth; ++index)
        vm.thread_state().pop_frame(frame);

    cxxpython::IntObject *value = runtime.new_int(7);
    for (std::size_t index = 0; index < cxxpython::Frame::MaxValueStackDepth; ++index)
        frame->push(value);
    try
    {
        frame->push(value);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.recursion_error_type());
        EXPECT_STREQ(error.what(), "maximum VM value stack depth exceeded");
    }
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
                     {runtime.new_str("not-code")},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::MakeFunction, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_tuple({runtime.object_type()}), runtime.new_dict(), runtime.new_str("not-code")},
                     {runtime.new_str("Broken")},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::LoadConst, 2},
                      {cxxpython::Opcode::MakeClass, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_tuple({runtime.object_type()}), runtime.new_str("not-keywords"), runtime.new_code({}, {{cxxpython::Opcode::ReturnValue, 0}})},
                     {},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::LoadConst, 2},
                      {cxxpython::Opcode::MakeClass, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code(
                     {runtime.new_str("not-bases"), runtime.new_dict(), runtime.new_code({}, {{cxxpython::Opcode::ReturnValue, 0}})},
                     {runtime.new_str("Broken")},
                     {{cxxpython::Opcode::LoadConst, 0},
                      {cxxpython::Opcode::LoadConst, 1},
                      {cxxpython::Opcode::LoadConst, 2},
                      {cxxpython::Opcode::MakeClass, 0},
                      {cxxpython::Opcode::ReturnValue, 0}})),
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
