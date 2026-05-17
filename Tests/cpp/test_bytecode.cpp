#include <gtest/gtest.h>

#include "cxxpython/vm.hpp"

TEST(Bytecode, MetadataNamesAndStackEffects)
{
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadConst), "LOAD_CONST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinaryAdd), "BINARY_ADD");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ReturnValue), "RETURN_VALUE");

    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadConst, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinaryAdd, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::ReturnValue, 0}), -1);
}

TEST(Bytecode, ExecutesLoadConstAndReturn)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::IntObject *value = runtime.new_int(42);
    cxxpython::CodeObject code({value},
                               {{cxxpython::Opcode::LoadConst, 0},
                                {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(code), value);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
    EXPECT_EQ(vm.thread_state().current_frame(), nullptr);
    EXPECT_EQ(&vm.interpreter_state().runtime(), &runtime);
}

TEST(Bytecode, ExecutesIntegerBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject code({runtime.new_int(2), runtime.new_int(3)},
                               {{cxxpython::Opcode::LoadConst, 0},
                                {cxxpython::Opcode::LoadConst, 1},
                                {cxxpython::Opcode::BinaryAdd, 0},
                                {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 5);
}

TEST(Bytecode, ExecutesStringBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject code({runtime.new_str("py"), runtime.new_str("thon")},
                               {{cxxpython::Opcode::LoadConst, 0},
                                {cxxpython::Opcode::LoadConst, 1},
                                {cxxpython::Opcode::BinaryAdd, 0},
                                {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::StrObject *>(vm.execute(code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "python");
}

TEST(Bytecode, RaisesPythonTypeErrorForUnsupportedBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject code({runtime.new_int(1), runtime.new_str("x")},
                               {{cxxpython::Opcode::LoadConst, 0},
                                {cxxpython::Opcode::LoadConst, 1},
                                {cxxpython::Opcode::BinaryAdd, 0},
                                {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unsupported operand type(s) for +: 'int' and 'str'");
    }
}

TEST(Bytecode, RejectsMalformedBytecode)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    EXPECT_THROW((void)vm.execute(cxxpython::CodeObject({}, {{cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(cxxpython::CodeObject({}, {{cxxpython::Opcode::LoadConst, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)cxxpython::stack_effect({static_cast<cxxpython::Opcode>(255), 0}),
                 cxxpython::BytecodeError);

    EXPECT_THROW((void)vm.execute(cxxpython::CodeObject({}, {{static_cast<cxxpython::Opcode>(255), 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
}
