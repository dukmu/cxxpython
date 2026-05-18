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
            auto *code = static_cast<const CodeObject *>(&object);
            std::ostringstream out;
            out << "<code object " << code->name()
                << " at " << &object
                << ", file '" << code->filename() << "'>";
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
            for (Object *name : code->names())
            {
                if (name)
                    visitor(name);
            }
        }
    }

    CodeObject::CodeObject(TypeObject *type,
                           std::string name,
                           std::string filename,
                           std::vector<Object *> constants,
                           std::vector<Object *> names,
                           std::vector<Instruction> instructions)
        : Object(type),
          name_(std::move(name)),
          filename_(std::move(filename)),
          constants_(std::move(constants)),
          names_(std::move(names)),
          instructions_(std::move(instructions))
    {
    }

    const std::string &CodeObject::name() const noexcept
    {
        return name_;
    }

    const std::string &CodeObject::filename() const noexcept
    {
        return filename_;
    }

    const std::vector<Object *> &CodeObject::constants() const noexcept
    {
        return constants_;
    }

    const std::vector<Object *> &CodeObject::names() const noexcept
    {
        return names_;
    }

    const std::vector<Instruction> &CodeObject::instructions() const noexcept
    {
        return instructions_;
    }

    TypeSlots make_code_slots()
    {
        return TypeSlots{
            .repr = code_repr,
            .hash = code_hash,
            .equal = code_equal,
            .truth = code_truth,
            .traverse = code_traverse,
        };
    }

    const char *opcode_name(Opcode opcode)
    {
        switch (opcode)
        {
        case Opcode::LoadConst:
            return "LOAD_CONST";
        case Opcode::LoadName:
            return "LOAD_NAME";
        case Opcode::StoreName:
            return "STORE_NAME";
        case Opcode::LoadAttr:
            return "LOAD_ATTR";
        case Opcode::StoreAttr:
            return "STORE_ATTR";
        case Opcode::PopTop:
            return "POP_TOP";
        case Opcode::BinaryAdd:
            return "BINARY_ADD";
        case Opcode::Call:
            return "CALL";
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
        case Opcode::LoadName:
            return 1;
        case Opcode::LoadAttr:
            return 0;
        case Opcode::StoreName:
        case Opcode::PopTop:
            return -1;
        case Opcode::StoreAttr:
            return -2;
        case Opcode::BinaryAdd:
            return -1;
        case Opcode::Call:
            return -static_cast<int>(instruction.operand);
        case Opcode::ReturnValue:
            return -1;
        }
        throw BytecodeError("invalid opcode for stack effect");
    }
}
