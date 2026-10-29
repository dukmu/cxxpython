#pragma once

#include "cxxpython/core/protocol.hpp"

#include <string_view>

namespace cxxpython::core
{
    enum class Opcode : std::uint8_t
    {
        LoadConst, LoadFast, StoreFast, DeleteFast, PopTop, DupTop,
        Binary, Compare, Jump, JumpIfFalse, Return, RaiseInstance, Call, Count,
    };
    enum class OperandDomain { None, Constant, Local, Binary, Compare, Target, CallSite };
    enum class FlowKind { Next, Jump, Branch, Stop };
    struct OpcodeDefinition
    {
        std::string_view name;
        std::size_t inputs;
        std::size_t pops;
        std::size_t pushes;
        OperandDomain operand;
        FlowKind flow;
        bool may_raise;
        bool safepoint;
    };
    struct Instruction
    {
        Opcode opcode;
        std::uint32_t operand = 0;
    };
    struct CallLayout
    {
        std::uint32_t positional, keywords;
        std::size_t argument_count() const noexcept { return static_cast<std::size_t>(positional) + keywords; }
    };
    struct StackEffect { std::size_t inputs, pops, pushes; };
    struct SourcePosition
    {
        std::uint32_t line = 0, column = 0, end_line = 0, end_column = 0;
    };
    enum class CodeErrorKind
    {
        EmptyCode, UnknownOpcode, Operand, ConstantBounds, LocalBounds, JumpBounds,
        StackUnderflow, StackJoin, Fallthrough, Signature, SourcePosition,
        ForeignLabel, UnboundLabel, DuplicateLabel, SizeLimit, CallBounds,
    };
    class CodeValidationError : public std::invalid_argument
    {
    public:
        CodeValidationError(CodeErrorKind kind, std::string message, std::optional<std::size_t> pc = {})
            : std::invalid_argument(std::move(message)), kind(kind), pc(pc) {}
        CodeErrorKind kind;
        std::optional<std::size_t> pc;
    };
    struct CodeSpec
    {
        std::u32string name = U"<code>";
        std::optional<std::u32string> qualname;
        std::u32string filename = U"<native>";
        std::uint32_t first_line = 1;
        SignatureLayout signature;
        std::vector<std::u32string> locals;
    };
    class CodeBuilder;
    class VerifiedCode final
    {
    public:
        VerifiedCode(VerifiedCode &&) noexcept = default;
        VerifiedCode(const VerifiedCode &) = delete;
        std::span<const Instruction> instructions() const noexcept { return instructions_; }
        std::span<const SourcePosition> positions() const noexcept { return positions_; }
        std::span<const CallLayout> calls() const noexcept { return calls_; }
        const SignatureLayout &signature() const noexcept { return signature_; }
        std::span<const std::u32string> local_names() const noexcept { return locals_; }
        std::size_t max_stack() const noexcept { return max_stack_; }
        std::uint32_t first_line() const noexcept { return first_line_; }
        std::size_t external_bytes() const noexcept;
    private:
        friend class CodeBuilder;
        VerifiedCode(std::vector<Instruction> instructions, std::vector<SourcePosition> positions,
                     std::vector<CallLayout> calls, SignatureLayout signature, std::vector<std::u32string> locals,
                     std::size_t stack, std::uint32_t first_line)
            : instructions_(std::move(instructions)), positions_(std::move(positions)), calls_(std::move(calls)), signature_(std::move(signature)),
              locals_(std::move(locals)), max_stack_(stack), first_line_(first_line) {}
        std::vector<Instruction> instructions_;
        std::vector<SourcePosition> positions_;
        std::vector<CallLayout> calls_;
        SignatureLayout signature_;
        std::vector<std::u32string> locals_;
        std::size_t max_stack_;
        std::uint32_t first_line_;
    };
    struct CodeObject
    {
        ObjectHeader header;
        const VerifiedCode program;
        const Value constants, varnames, name, qualname, filename, call_keywords;
        CodeObject(ObjectHeader head, VerifiedCode code, Value consts, Value variables,
                   Value name, Value qualname, Value filename, Value call_keywords)
            : header(head), program(std::move(code)), constants(consts), varnames(variables),
              name(name), qualname(qualname), filename(filename), call_keywords(call_keywords) {}
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept { return program.external_bytes(); }
    };
    class CodeBuilder final
    {
    public:
        struct Label { const CodeBuilder *owner; std::size_t index; };
        explicit CodeBuilder(Attachment &context, CodeSpec spec = {});
        std::uint32_t constant(Handle value);
        Label label();
        void bind(Label label);
        void emit(Opcode opcode, std::uint32_t operand = 0, SourcePosition source = {});
        void jump(Opcode opcode, Label target, SourcePosition source = {});
        void call(std::uint32_t positional, std::span<const std::u32string> keywords = {}, SourcePosition source = {});
        void freeze(ReturnSlot &out);
    private:
        Attachment &context_;
        RootScope roots_;
        CodeSpec spec_;
        std::vector<Value> constants_;
        std::vector<Instruction> instructions_;
        std::vector<SourcePosition> positions_;
        std::vector<std::optional<std::size_t>> labels_;
        std::vector<std::pair<std::size_t, Label>> fixups_;
        struct CallSiteSpec { std::uint32_t positional; std::vector<std::u32string> keywords; };
        std::vector<CallSiteSpec> calls_;
    };
    struct FunctionObject
    {
        ObjectHeader header;
        Value code, globals;
        std::optional<Value> defaults, kwdefaults;
        Value name, qualname;
        FunctionObject(ObjectHeader head, Value code, Value globals, std::optional<Value> defaults,
                       std::optional<Value> kwdefaults, Value name, Value qualname)
            : header(head), code(code), globals(globals), defaults(defaults), kwdefaults(kwdefaults), name(name), qualname(qualname) {}
        void trace(Tracer &tracer) const noexcept;
    };
    struct FunctionSpec
    {
        Handle code, globals;
        std::optional<Handle> defaults = {}, kwdefaults = {};
    };
    enum class FrameState { Created, Running, Completed };
    struct FrameObject
    {
        ObjectHeader header;
        const Value function, code;
        std::optional<Value> back;
        std::vector<std::optional<Value>> locals;
        std::vector<Value> operands;
        std::size_t pc = 0, sp = 0;
        std::optional<std::size_t> last_instruction;
        std::optional<std::size_t> pending_call;
        FrameState state = FrameState::Created;
        FrameObject(ObjectHeader head, Value function, Value code, std::size_t local_count, std::size_t stack_count, Value padding)
            : header(head), function(function), code(code), locals(local_count), operands(stack_count, padding) {}
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept
        { return locals.capacity() * sizeof(std::optional<Value>) + operands.capacity() * sizeof(Value); }
    };
    const OpcodeDefinition &opcode_definition(Opcode opcode);
    StackEffect instruction_effect(Instruction instruction, std::span<const CallLayout> calls = {});
    std::string disassemble(Attachment &context, Handle code);
    void make_function(Attachment &context, const FunctionSpec &spec, ReturnSlot &out);
}
