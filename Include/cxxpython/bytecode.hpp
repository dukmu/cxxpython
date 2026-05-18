#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include "cxxpython/object.hpp"

namespace cxxpython
{
    enum class Opcode : std::uint8_t
    {
        LoadConst,
        LoadName,
        StoreName,
        LoadAttr,
        StoreAttr,
        PopTop,
        DupTop,
        RotTwo,
        RotThree,
        UnaryOp,
        BinaryOp,
        BinarySubscr,
        StoreSubscr,
        CompareOp,
        JumpAbsolute,
        PopJumpIfFalse,
        PopJumpIfTrue,
        JumpIfFalseOrPop,
        MakeFunction,
        MakeClass,
        Call,
        ReturnValue,
    };

    struct Instruction
    {
        Opcode opcode;
        std::uint32_t operand = 0;
    };

    class BytecodeError final : public std::exception
    {
    public:
        explicit BytecodeError(std::string message);

        const char *what() const noexcept override;

    private:
        std::string message_;
    };

    class CodeObject final : public Object
    {
    public:
        CodeObject(TypeObject *type,
                   std::string name,
                   std::string filename,
                   std::uint32_t arg_count,
                   std::vector<Object *> varnames,
                   std::vector<Object *> constants,
                   std::vector<Object *> names,
                   std::vector<Instruction> instructions);

        const std::string &name() const noexcept;
        const std::string &filename() const noexcept;
        std::uint32_t arg_count() const noexcept;
        const std::vector<Object *> &varnames() const noexcept;
        const std::vector<Object *> &constants() const noexcept;
        const std::vector<Object *> &names() const noexcept;
        const std::vector<Instruction> &instructions() const noexcept;

    private:
        std::string name_;
        std::string filename_;
        std::uint32_t arg_count_ = 0;
        std::vector<Object *> varnames_;
        std::vector<Object *> constants_;
        std::vector<Object *> names_;
        std::vector<Instruction> instructions_;
    };

    const char *opcode_name(Opcode opcode);
    int stack_effect(Instruction instruction);
}
