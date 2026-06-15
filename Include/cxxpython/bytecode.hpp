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
        DupTwo,
        RotTwo,
        RotThree,
        UnaryOp,
        BinaryOp,
        InplaceBinaryOp,
        FormatValue,
        BinarySubscr,
        StoreSubscr,
        DeleteSubscr,
        BuildList,
        BuildTuple,
        BuildTupleUnpack,
        BuildMap,
        BuildKwMapUnpack,
        BuildSlice,
        CompareOp,
        GetIter,
        ForIter,
        BreakLoop,
        ContinueLoop,
        JumpAbsolute,
        PopJumpIfFalse,
        PopJumpIfTrue,
        JumpIfFalseOrPop,
        MakeFunction,
        MakeClass,
        Call,
        CallKw,
        CallEx,
        ImportStar,
        SetupExcept,
        SetupFinally,
        PopExcept,
        PopFinally,
        CheckExceptionMatch,
        RaiseValue,
        WithExit,
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

    constexpr std::uint32_t MakeFunctionHasDefaults = 1U << 31;
    constexpr std::uint32_t MakeFunctionHasKwDefaults = 1U << 30;
    constexpr std::uint32_t MakeFunctionHasAnnotations = 1U << 29;
    constexpr std::uint32_t MakeFunctionClosureCountMask =
        ~(MakeFunctionHasDefaults | MakeFunctionHasKwDefaults | MakeFunctionHasAnnotations);

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
                   std::uint32_t posonly_arg_count,
                   std::uint32_t kwonly_arg_count,
                   Flags flags,
                   std::vector<StrObject *> varnames,
                   std::vector<Object *> constants,
                   std::vector<StrObject *> names,
                   std::vector<StrObject *> cellvars,
                   std::vector<StrObject *> freevars,
                   std::vector<Instruction> instructions,
                   std::vector<std::uint32_t> lines = {});

        const std::string &name() const noexcept;
        const std::string &filename() const noexcept;
        std::uint32_t arg_count() const noexcept;
        std::uint32_t posonly_arg_count() const noexcept;
        std::uint32_t kwonly_arg_count() const noexcept;
        Flags flags() const noexcept;
        bool has_varargs() const noexcept;
        bool has_varkeywords() const noexcept;
        std::uint32_t varargs_index() const noexcept;
        std::uint32_t varkeywords_index() const noexcept;
        const std::vector<StrObject *> &varnames() const noexcept;
        const std::vector<Object *> &constants() const noexcept;
        const std::vector<StrObject *> &names() const noexcept;
        const std::vector<StrObject *> &cellvars() const noexcept;
        const std::vector<StrObject *> &freevars() const noexcept;
        const std::vector<Instruction> &instructions() const noexcept;
        const std::vector<std::uint32_t> &lines() const noexcept;
        std::uint32_t stack_size() const noexcept;
        std::uint32_t line_for_instruction(std::size_t instruction_index) const noexcept;

    private:
        std::string name_;
        std::string filename_;
        std::uint32_t arg_count_ = 0;
        std::uint32_t posonly_arg_count_ = 0;
        std::uint32_t kwonly_arg_count_ = 0;
        Flags flags_ = 0;
        std::vector<StrObject *> varnames_;
        std::vector<Object *> constants_;
        std::vector<StrObject *> names_;
        std::vector<StrObject *> cellvars_;
        std::vector<StrObject *> freevars_;
        std::vector<Instruction> instructions_;
        std::vector<std::uint32_t> lines_;
        std::uint32_t stack_size_ = 0;
    };

    const char *opcode_name(Opcode opcode);
    int stack_effect(Instruction instruction);
}
