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
        DeleteName,
        LoadGlobal,
        StoreGlobal,
        DeleteGlobal,
        LoadFast,
        StoreFast,
        DeleteFast,
        LoadDeref,
        StoreDeref,
        DeleteDeref,
        LoadClosure,
        LoadAttr,
        StoreAttr,
        DeleteAttr,
        PopTop,
        DupTop,
        RotTwo,
        RotThree,
        UnaryOp,
        BinaryOp,
        FormatValue,
        BinarySubscr,
        StoreSubscr,
        DeleteSubscr,
        BuildList,
        BuildTuple,
        BuildMap,
        BuildSlice,
        CompareOp,
        GetIter,
        ForIter,
        JumpAbsolute,
        PopJumpIfFalse,
        PopJumpIfTrue,
        JumpIfFalseOrPop,
        MakeFunction,
        MakeClass,
        Call,
        CallKw,
        SetupExcept,
        SetupFinally,
        PopExcept,
        PopFinally,
        CheckExceptionMatch,
        RaiseValue,
        EndFinally,
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
        using Flags = std::uint32_t;

        static constexpr Flags FlagVarArgs = 1U << 0;
        static constexpr Flags FlagVarKeywords = 1U << 1;

        CodeObject(TypeObject *type,
                   std::string name,
                   std::string filename,
                   std::uint32_t arg_count,
                   Flags flags,
                   std::vector<Object *> varnames,
                   std::vector<Object *> constants,
                   std::vector<Object *> names,
                   std::vector<Object *> cellvars,
                   std::vector<Object *> freevars,
                   std::vector<Instruction> instructions,
                   std::vector<std::uint32_t> lines = {});

        const std::string &name() const noexcept;
        const std::string &filename() const noexcept;
        std::uint32_t arg_count() const noexcept;
        Flags flags() const noexcept;
        bool has_varargs() const noexcept;
        bool has_varkeywords() const noexcept;
        std::uint32_t varargs_index() const noexcept;
        std::uint32_t varkeywords_index() const noexcept;
        const std::vector<Object *> &varnames() const noexcept;
        const std::vector<Object *> &constants() const noexcept;
        const std::vector<Object *> &names() const noexcept;
        const std::vector<Object *> &cellvars() const noexcept;
        const std::vector<Object *> &freevars() const noexcept;
        const std::vector<Instruction> &instructions() const noexcept;
        const std::vector<std::uint32_t> &lines() const noexcept;
        std::uint32_t line_for_instruction(std::size_t instruction_index) const noexcept;

    private:
        std::string name_;
        std::string filename_;
        std::uint32_t arg_count_ = 0;
        Flags flags_ = 0;
        std::vector<Object *> varnames_;
        std::vector<Object *> constants_;
        std::vector<Object *> names_;
        std::vector<Object *> cellvars_;
        std::vector<Object *> freevars_;
        std::vector<Instruction> instructions_;
        std::vector<std::uint32_t> lines_;
    };

    const char *opcode_name(Opcode opcode);
    int stack_effect(Instruction instruction);
}
