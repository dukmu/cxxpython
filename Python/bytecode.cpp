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
            for (Object *varname : code->varnames())
            {
                if (varname)
                    visitor(varname);
            }
            for (Object *cellvar : code->cellvars())
            {
                if (cellvar)
                    visitor(cellvar);
            }
            for (Object *freevar : code->freevars())
            {
                if (freevar)
                    visitor(freevar);
            }
        }
    }

    CodeObject::CodeObject(TypeObject *type,
                           std::string name,
                           std::string filename,
                           std::uint32_t arg_count,
                           CodeObject::Flags flags,
                           std::vector<Object *> varnames,
                           std::vector<Object *> constants,
                           std::vector<Object *> names,
                           std::vector<Object *> cellvars,
                           std::vector<Object *> freevars,
                           std::vector<Instruction> instructions,
                           std::vector<std::uint32_t> lines)
        : Object(type),
          name_(std::move(name)),
          filename_(std::move(filename)),
          arg_count_(arg_count),
          flags_(flags),
          varnames_(std::move(varnames)),
          constants_(std::move(constants)),
          names_(std::move(names)),
          cellvars_(std::move(cellvars)),
          freevars_(std::move(freevars)),
          instructions_(std::move(instructions)),
          lines_(std::move(lines))
    {
        if (!lines_.empty() && lines_.size() != instructions_.size())
            throw BytecodeError("code object line table must match instruction count");
    }

    const std::string &CodeObject::name() const noexcept
    {
        return name_;
    }

    const std::string &CodeObject::filename() const noexcept
    {
        return filename_;
    }

    std::uint32_t CodeObject::arg_count() const noexcept
    {
        return arg_count_;
    }

    CodeObject::Flags CodeObject::flags() const noexcept
    {
        return flags_;
    }

    bool CodeObject::has_varargs() const noexcept
    {
        return (flags_ & FlagVarArgs) != 0;
    }

    bool CodeObject::has_varkeywords() const noexcept
    {
        return (flags_ & FlagVarKeywords) != 0;
    }

    std::uint32_t CodeObject::varargs_index() const noexcept
    {
        return arg_count_;
    }

    std::uint32_t CodeObject::varkeywords_index() const noexcept
    {
        return arg_count_ + (has_varargs() ? 1U : 0U);
    }

    const std::vector<Object *> &CodeObject::varnames() const noexcept
    {
        return varnames_;
    }

    const std::vector<Object *> &CodeObject::constants() const noexcept
    {
        return constants_;
    }

    const std::vector<Object *> &CodeObject::names() const noexcept
    {
        return names_;
    }

    const std::vector<Object *> &CodeObject::cellvars() const noexcept
    {
        return cellvars_;
    }

    const std::vector<Object *> &CodeObject::freevars() const noexcept
    {
        return freevars_;
    }

    const std::vector<Instruction> &CodeObject::instructions() const noexcept
    {
        return instructions_;
    }

    const std::vector<std::uint32_t> &CodeObject::lines() const noexcept
    {
        return lines_;
    }

    std::uint32_t CodeObject::line_for_instruction(std::size_t instruction_index) const noexcept
    {
        if (instruction_index >= lines_.size())
            return 0;
        return lines_[instruction_index];
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
        case Opcode::DeleteName:
            return "DELETE_NAME";
        case Opcode::LoadFast:
            return "LOAD_FAST";
        case Opcode::StoreFast:
            return "STORE_FAST";
        case Opcode::DeleteFast:
            return "DELETE_FAST";
        case Opcode::LoadDeref:
            return "LOAD_DEREF";
        case Opcode::StoreDeref:
            return "STORE_DEREF";
        case Opcode::DeleteDeref:
            return "DELETE_DEREF";
        case Opcode::LoadClosure:
            return "LOAD_CLOSURE";
        case Opcode::LoadAttr:
            return "LOAD_ATTR";
        case Opcode::StoreAttr:
            return "STORE_ATTR";
        case Opcode::DeleteAttr:
            return "DELETE_ATTR";
        case Opcode::PopTop:
            return "POP_TOP";
        case Opcode::DupTop:
            return "DUP_TOP";
        case Opcode::RotTwo:
            return "ROT_TWO";
        case Opcode::RotThree:
            return "ROT_THREE";
        case Opcode::UnaryOp:
            return "UNARY_OP";
        case Opcode::BinaryOp:
            return "BINARY_OP";
        case Opcode::BinarySubscr:
            return "BINARY_SUBSCR";
        case Opcode::StoreSubscr:
            return "STORE_SUBSCR";
        case Opcode::DeleteSubscr:
            return "DELETE_SUBSCR";
        case Opcode::BuildList:
            return "BUILD_LIST";
        case Opcode::BuildTuple:
            return "BUILD_TUPLE";
        case Opcode::BuildMap:
            return "BUILD_MAP";
        case Opcode::CompareOp:
            return "COMPARE_OP";
        case Opcode::JumpAbsolute:
            return "JUMP_ABSOLUTE";
        case Opcode::PopJumpIfFalse:
            return "POP_JUMP_IF_FALSE";
        case Opcode::PopJumpIfTrue:
            return "POP_JUMP_IF_TRUE";
        case Opcode::JumpIfFalseOrPop:
            return "JUMP_IF_FALSE_OR_POP";
        case Opcode::MakeFunction:
            return "MAKE_FUNCTION";
        case Opcode::MakeClass:
            return "MAKE_CLASS";
        case Opcode::Call:
            return "CALL";
        case Opcode::CallKw:
            return "CALL_KW";
        case Opcode::SetupExcept:
            return "SETUP_EXCEPT";
        case Opcode::SetupFinally:
            return "SETUP_FINALLY";
        case Opcode::PopExcept:
            return "POP_EXCEPT";
        case Opcode::PopFinally:
            return "POP_FINALLY";
        case Opcode::CheckExceptionMatch:
            return "CHECK_EXCEPTION_MATCH";
        case Opcode::RaiseValue:
            return "RAISE_VALUE";
        case Opcode::EndFinally:
            return "END_FINALLY";
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
        case Opcode::LoadFast:
        case Opcode::LoadDeref:
        case Opcode::LoadClosure:
            return 1;
        case Opcode::LoadAttr:
        case Opcode::DeleteName:
        case Opcode::DeleteFast:
        case Opcode::DeleteDeref:
        case Opcode::JumpAbsolute:
        case Opcode::SetupExcept:
        case Opcode::SetupFinally:
        case Opcode::PopExcept:
        case Opcode::PopFinally:
        case Opcode::CheckExceptionMatch:
        case Opcode::EndFinally:
        case Opcode::RotTwo:
        case Opcode::RotThree:
        case Opcode::UnaryOp:
            return 0;
        case Opcode::MakeFunction:
            return instruction.operand == 0 ? 0 : -1;
        case Opcode::MakeClass:
            return -2;
        case Opcode::DupTop:
            return 1;
        case Opcode::StoreName:
        case Opcode::StoreFast:
        case Opcode::StoreDeref:
        case Opcode::PopTop:
        case Opcode::DeleteAttr:
        case Opcode::PopJumpIfFalse:
        case Opcode::PopJumpIfTrue:
        case Opcode::JumpIfFalseOrPop:
            return -1;
        case Opcode::StoreAttr:
            return -2;
        case Opcode::BinaryOp:
        case Opcode::BinarySubscr:
        case Opcode::CompareOp:
            return -1;
        case Opcode::StoreSubscr:
            return -3;
        case Opcode::DeleteSubscr:
            return -2;
        case Opcode::BuildList:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildTuple:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildMap:
            return 1 - 2 * static_cast<int>(instruction.operand);
        case Opcode::Call:
            return -static_cast<int>(instruction.operand);
        case Opcode::CallKw:
            return -static_cast<int>(instruction.operand) - 1;
        case Opcode::ReturnValue:
        case Opcode::RaiseValue:
            return -1;
        }
        throw BytecodeError("invalid opcode for stack effect");
    }
}
