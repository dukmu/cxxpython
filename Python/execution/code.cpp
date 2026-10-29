#include "cxxpython/core/execution.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core
{
    namespace
    {
        constexpr std::array definitions{
            OpcodeDefinition{"LOAD_CONST", 0, 0, 1, OperandDomain::Constant, FlowKind::Next, false, false},
            OpcodeDefinition{"LOAD_FAST", 0, 0, 1, OperandDomain::Local, FlowKind::Next, true, true},
            OpcodeDefinition{"STORE_FAST", 1, 1, 0, OperandDomain::Local, FlowKind::Next, false, false},
            OpcodeDefinition{"DELETE_FAST", 0, 0, 0, OperandDomain::Local, FlowKind::Next, true, true},
            OpcodeDefinition{"POP_TOP", 1, 1, 0, OperandDomain::None, FlowKind::Next, false, false},
            OpcodeDefinition{"DUP_TOP", 1, 0, 1, OperandDomain::None, FlowKind::Next, false, false},
            OpcodeDefinition{"BINARY", 2, 2, 1, OperandDomain::Binary, FlowKind::Next, true, true},
            OpcodeDefinition{"COMPARE", 2, 2, 1, OperandDomain::Compare, FlowKind::Next, true, true},
            OpcodeDefinition{"JUMP", 0, 0, 0, OperandDomain::Target, FlowKind::Jump, false, false},
            OpcodeDefinition{"JUMP_IF_FALSE", 1, 1, 0, OperandDomain::Target, FlowKind::Branch, true, true},
            OpcodeDefinition{"RETURN", 1, 1, 0, OperandDomain::None, FlowKind::Stop, false, false},
            OpcodeDefinition{"RAISE_INSTANCE", 1, 1, 0, OperandDomain::None, FlowKind::Stop, true, true},
            OpcodeDefinition{"CALL", 1, 1, 1, OperandDomain::CallSite, FlowKind::Next, true, true},
        };
        static_assert(definitions.size() == static_cast<std::size_t>(Opcode::Count));
        [[noreturn]] void invalid(CodeErrorKind kind, const char *message, std::optional<std::size_t> pc = {})
        { throw CodeValidationError(kind, message, pc); }
        std::size_t string_bytes(const std::u32string &text) noexcept
        {
            const auto data = reinterpret_cast<std::uintptr_t>(text.data());
            const auto start = reinterpret_cast<std::uintptr_t>(&text);
            return data >= start && data - start < sizeof(text) ? 0 : (text.capacity() + 1) * sizeof(char32_t);
        }
        std::size_t verify(const CodeSpec &spec, std::span<const Instruction> instructions,
                           std::span<const SourcePosition> positions, std::span<const CallLayout> calls, std::size_t constants)
        {
            if (instructions.empty()) invalid(CodeErrorKind::EmptyCode, "empty instruction sequence");
            if (!spec.first_line) invalid(CodeErrorKind::SourcePosition, "first line must be positive");
            try { spec.signature.validate(); }
            catch (const std::invalid_argument &error) { throw CodeValidationError(CodeErrorKind::Signature, error.what()); }
            std::vector<std::u32string> parameters;
            for (const auto &parameter : spec.signature.parameters)
            {
                if (parameter.default_value) invalid(CodeErrorKind::Signature, "defaults belong to Function, not Code");
                parameters.push_back(parameter.name);
            }
            if (spec.signature.varargs) parameters.push_back(*spec.signature.varargs);
            if (spec.signature.varkwargs) parameters.push_back(*spec.signature.varkwargs);
            if (spec.locals.size() < parameters.size() || !std::equal(parameters.begin(), parameters.end(), spec.locals.begin()))
                invalid(CodeErrorKind::Signature, "parameter slots must prefix fast locals");
            std::unordered_set<std::u32string> names;
            for (const auto &name : spec.locals)
                if (name.empty() || !names.insert(name).second) invalid(CodeErrorKind::Signature, "empty or duplicate local name");
            for (std::size_t pc = 0; pc < instructions.size(); ++pc)
            {
                const auto [opcode, operand] = instructions[pc];
                if (static_cast<std::size_t>(opcode) >= definitions.size()) invalid(CodeErrorKind::UnknownOpcode, "unknown opcode", pc);
                const auto &definition = opcode_definition(opcode);
                switch (definition.operand)
                {
                    case OperandDomain::None:
                        if (operand) invalid(CodeErrorKind::Operand, "opcode has no operand", pc);
                        break;
                    case OperandDomain::Constant:
                        if (operand >= constants) invalid(CodeErrorKind::ConstantBounds, "constant index out of bounds", pc);
                        break;
                    case OperandDomain::Local:
                        if (operand >= spec.locals.size()) invalid(CodeErrorKind::LocalBounds, "local index out of bounds", pc);
                        break;
                    case OperandDomain::Target:
                        if (operand >= instructions.size()) invalid(CodeErrorKind::JumpBounds, "jump target out of bounds", pc);
                        break;
                    case OperandDomain::Binary:
                        if (operand > static_cast<std::uint32_t>(BinaryOp::Modulo)) invalid(CodeErrorKind::Operand, "invalid binary operator", pc);
                        break;
                    case OperandDomain::Compare:
                        if (operand > static_cast<std::uint32_t>(CompareOp::Ge)) invalid(CodeErrorKind::Operand, "invalid comparison operator", pc);
                        break;
                    case OperandDomain::CallSite:
                        if (operand >= calls.size()) invalid(CodeErrorKind::CallBounds, "call-site index out of bounds", pc);
                        break;
                }
                const auto &position = positions[pc];
                if ((!position.line && (position.column || position.end_line || position.end_column)) ||
                    (position.line && (position.end_line < position.line ||
                        (position.end_line == position.line && position.end_column < position.column))))
                    invalid(CodeErrorKind::SourcePosition, "invalid source position", pc);
            }
            std::vector<std::optional<std::size_t>> heights(instructions.size());
            std::vector<std::size_t> pending{0}; heights[0] = 0; std::size_t maximum = 0;
            auto join = [&](std::size_t target, std::size_t height) {
                if (target == instructions.size()) invalid(CodeErrorKind::Fallthrough, "reachable code falls off its end", target - 1);
                if (!heights[target]) { heights[target] = height; pending.push_back(target); }
                else if (*heights[target] != height) invalid(CodeErrorKind::StackJoin, "inconsistent stack height at merge", target);
            };
            // Each PC is queued once. A growing back edge is a failed merge,
            // not an invitation to enumerate ever larger verifier states.
            for (std::size_t next = 0; next < pending.size(); ++next)
            {
                const auto pc = pending[next]; const auto &instruction = instructions[pc];
                const auto &definition = opcode_definition(instruction.opcode); const auto height = *heights[pc];
                const auto effect = instruction_effect(instruction, calls);
                if (height < effect.inputs) invalid(CodeErrorKind::StackUnderflow, "instruction requires more stack inputs", pc);
                const auto after = height - effect.pops + effect.pushes; maximum = std::max(maximum, after);
                if (definition.flow == FlowKind::Jump || definition.flow == FlowKind::Branch) join(instruction.operand, after);
                if (definition.flow == FlowKind::Next || definition.flow == FlowKind::Branch) join(pc + 1, after);
            }
            return maximum;
        }
    }
    const OpcodeDefinition &opcode_definition(Opcode opcode)
    { return definitions.at(static_cast<std::size_t>(opcode)); }
    StackEffect instruction_effect(Instruction instruction, std::span<const CallLayout> calls)
    {
        const auto &definition = opcode_definition(instruction.opcode);
        if (definition.operand == OperandDomain::CallSite && instruction.operand >= calls.size())
            invalid(CodeErrorKind::CallBounds, "call-site index out of bounds");
        const auto arguments = definition.operand == OperandDomain::CallSite ? calls[instruction.operand].argument_count() : 0;
        return {definition.inputs + arguments, definition.pops + arguments, definition.pushes};
    }
    std::size_t VerifiedCode::external_bytes() const noexcept
    {
        auto bytes = instructions_.capacity() * sizeof(Instruction) + positions_.capacity() * sizeof(SourcePosition) +
                     calls_.capacity() * sizeof(CallLayout) + locals_.capacity() * sizeof(std::u32string) + signature_.external_bytes();
        for (const auto &local : locals_) bytes += string_bytes(local);
        return bytes;
    }
    void CodeObject::trace(Tracer &tracer) const noexcept
    { for (auto value : {constants, varnames, name, qualname, filename, call_keywords}) tracer.visit(value); }
    CodeBuilder::CodeBuilder(Attachment &context, CodeSpec spec) : context_(context), roots_(context), spec_(std::move(spec))
    {
        if (!context.core_ready()) throw std::logic_error("CodeBuilder requires the B3 image");
        for (const auto &parameter : spec_.signature.parameters)
            if (parameter.default_value) roots_.root(*parameter.default_value);
    }
    std::uint32_t CodeBuilder::constant(Handle value)
    {
        if (constants_.size() >= std::numeric_limits<std::uint32_t>::max()) invalid(CodeErrorKind::SizeLimit, "too many constants");
        auto rooted = roots_.root(value.get(context_)); constants_.push_back(rooted.get(context_));
        return static_cast<std::uint32_t>(constants_.size() - 1);
    }
    CodeBuilder::Label CodeBuilder::label()
    { labels_.push_back({}); return {this, labels_.size() - 1}; }
    void CodeBuilder::bind(Label label)
    {
        if (label.owner != this || label.index >= labels_.size()) invalid(CodeErrorKind::ForeignLabel, "label belongs to a different builder");
        if (labels_[label.index]) invalid(CodeErrorKind::DuplicateLabel, "label is already bound");
        labels_[label.index] = instructions_.size();
    }
    void CodeBuilder::emit(Opcode opcode, std::uint32_t operand, SourcePosition source)
    {
        context_.require_attached();
        if (instructions_.size() >= std::numeric_limits<std::uint32_t>::max()) invalid(CodeErrorKind::SizeLimit, "too many instructions");
        instructions_.push_back({opcode, operand});
        try { positions_.push_back(source); }
        catch (...) { instructions_.pop_back(); throw; }
    }
    void CodeBuilder::jump(Opcode opcode, Label target, SourcePosition source)
    {
        if (target.owner != this || target.index >= labels_.size()) invalid(CodeErrorKind::ForeignLabel, "label belongs to a different builder");
        if (opcode != Opcode::Jump && opcode != Opcode::JumpIfFalse) invalid(CodeErrorKind::Operand, "not a branch opcode");
        fixups_.push_back({instructions_.size(), target});
        try { emit(opcode, 0, source); }
        catch (...) { fixups_.pop_back(); throw; }
    }
    void CodeBuilder::freeze(ReturnSlot &out)
    {
        out.reset(context_); auto instructions = instructions_;
        for (const auto &[pc, label] : fixups_)
        {
            if (!labels_[label.index]) invalid(CodeErrorKind::UnboundLabel, "unbound branch label", pc);
            instructions[pc].operand = static_cast<std::uint32_t>(*labels_[label.index]);
        }
        std::vector<CallLayout> calls;
        for (const auto &site : calls_)
        {
            if (site.keywords.size() >= std::numeric_limits<std::uint32_t>::max() ||
                site.positional >= std::numeric_limits<std::uint32_t>::max() - site.keywords.size())
                invalid(CodeErrorKind::SizeLimit, "too many call arguments");
            for (const auto &name : site.keywords)
                for (auto point : name)
                    if (point > 0x10ffff) invalid(CodeErrorKind::Operand, "invalid keyword codepoint");
            calls.push_back({site.positional, static_cast<std::uint32_t>(site.keywords.size())});
        }
        const auto maximum = verify(spec_, instructions, positions_, calls, constants_.size());
        VerifiedCode verified(std::move(instructions), positions_, std::move(calls), spec_.signature, spec_.locals, maximum, spec_.first_line);
        RootScope roots(context_); ReturnSlot temporary(context_);
        cxxpython::core::make_tuple(context_, constants_, temporary); auto constants = roots.root(temporary.result().value());
        std::vector<Value> names;
        for (const auto &name : spec_.locals)
        { make_str(context_, name, temporary); names.push_back(roots.root(temporary.result().value()).get(context_)); }
        cxxpython::core::make_tuple(context_, names, temporary); auto varnames = roots.root(temporary.result().value());
        make_str(context_, spec_.name, temporary); auto name = roots.root(temporary.result().value());
        make_str(context_, spec_.qualname.value_or(spec_.name), temporary); auto qualname = roots.root(temporary.result().value());
        make_str(context_, spec_.filename, temporary); auto filename = roots.root(temporary.result().value());
        std::vector<Value> keyword_tables;
        for (const auto &site : calls_)
        {
            std::vector<Value> keys;
            for (const auto &key : site.keywords)
            { make_str(context_, key, temporary); keys.push_back(roots.root(temporary.result().value()).get(context_)); }
            cxxpython::core::make_tuple(context_, keys, temporary);
            keyword_tables.push_back(roots.root(temporary.result().value()).get(context_));
        }
        auto call_keywords = roots.root(context_.builtin(CoreObject::EmptyTuple));
        if (!keyword_tables.empty())
        { cxxpython::core::make_tuple(context_, keyword_tables, temporary); call_keywords = roots.root(temporary.result().value()); }
        auto type = roots.root(context_.builtin(CoreObject::CodeType));
        context_.allocate_instance<CodeObject>(type, out, std::move(verified), constants.get(context_), varnames.get(context_),
                                             name.get(context_), qualname.get(context_), filename.get(context_), call_keywords.get(context_));
    }
    void CodeBuilder::call(std::uint32_t positional, std::span<const std::u32string> keywords, SourcePosition source)
    {
        context_.require_attached();
        if (calls_.size() >= std::numeric_limits<std::uint32_t>::max()) invalid(CodeErrorKind::SizeLimit, "too many call sites");
        calls_.push_back({positional, {keywords.begin(), keywords.end()}});
        try { emit(Opcode::Call, static_cast<std::uint32_t>(calls_.size() - 1), source); }
        catch (...) { calls_.pop_back(); throw; }
    }
    std::string disassemble(Attachment &context, Handle code)
    {
        if (context.type_of(code) != context.builtin(CoreObject::CodeType)) throw std::invalid_argument("disassembly requires code");
        const auto &program = context.payload<CodeObject>(code).program; std::string result;
        for (std::size_t pc = 0; pc < program.instructions().size(); ++pc)
        {
            const auto &instruction = program.instructions()[pc]; const auto &definition = opcode_definition(instruction.opcode);
            result += std::to_string(pc) + " " + std::string(definition.name);
            if (definition.operand != OperandDomain::None) result += " " + std::to_string(instruction.operand);
            result += '\n';
        }
        return result;
    }
}
