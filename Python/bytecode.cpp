#include "cxxpython/bytecode.hpp"

#include <sstream>
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

    namespace
    {
        std::string code_repr(const Object &object)
        {
            std::ostringstream out;
            out << "<code object at " << &object << ">";
            return out.str();
        }

        std::int64_t code_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool code_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool code_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void code_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *code = static_cast<const CodeObject *>(&object);
            for (Object *constant : code->constants())
            {
                if (constant)
                    visitor(constant);
            }
        }
    }

    CodeObject::CodeObject(TypeObject *type, std::vector<Object *> constants, std::vector<Instruction> instructions)
        : Object(type), constants_(std::move(constants)), instructions_(std::move(instructions))
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

    TypeSlots make_code_slots()
    {
        return TypeSlots{code_repr, code_hash, code_equal, code_truth, code_traverse, nullptr};
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
