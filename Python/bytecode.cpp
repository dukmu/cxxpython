#include "cxxpython/bytecode.hpp"

#include <utility>

namespace cxxpython
{
    BytecodeError::BytecodeError(std::string message)
        : message_(std::move(message))
    {
    }

    const char *BytecodeError::what() const noexcept
    {
        return message_.c_str();
    }

    CodeObject::CodeObject(std::vector<Object *> constants, std::vector<Instruction> instructions)
        : constants_(std::move(constants)), instructions_(std::move(instructions))
    {
    }

    const std::vector<Object *> &CodeObject::constants() const noexcept
    {
        return constants_;
    }

    const std::vector<Instruction> &CodeObject::instructions() const noexcept
    {
        return instructions_;
    }

    const char *opcode_name(Opcode opcode)
    {
        switch (opcode)
        {
        case Opcode::LoadConst:
            return "LOAD_CONST";
        case Opcode::BinaryAdd:
            return "BINARY_ADD";
        case Opcode::ReturnValue:
            return "RETURN_VALUE";
        }
        return "<invalid opcode>";
    }

    int stack_effect(Instruction instruction)
    {
        switch (instruction.opcode)
        {
        case Opcode::LoadConst:
            return 1;
        case Opcode::BinaryAdd:
            return -1;
        case Opcode::ReturnValue:
            return -1;
        }
        throw BytecodeError("invalid opcode for stack effect");
    }
}
