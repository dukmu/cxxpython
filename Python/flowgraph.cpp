#include "cxxpython/execution.hpp"

#include <algorithm>
#include <bit>
#include <unordered_set>

namespace cxxpython::core {
namespace {
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
    OpcodeDefinition{"ENTER_REGION", 0, 0, 0, OperandDomain::Region, FlowKind::Next, false, false},
    OpcodeDefinition{"LEAVE_REGION", 0, 0, 0, OperandDomain::Region, FlowKind::Leave, false, false},
    OpcodeDefinition{"END_HANDLER", 0, 0, 0, OperandDomain::Region, FlowKind::EndHandler, false,
                     false},
    OpcodeDefinition{"BREAK_LOOP", 0, 0, 0, OperandDomain::Region, FlowKind::LoopControl, false,
                     false},
    OpcodeDefinition{"CONTINUE_LOOP", 0, 0, 0, OperandDomain::Region, FlowKind::LoopControl, false,
                     false},
    OpcodeDefinition{"RERAISE", 0, 0, 0, OperandDomain::None, FlowKind::Stop, true, true},
    OpcodeDefinition{"LOAD_EXCEPTION", 0, 0, 1, OperandDomain::None, FlowKind::Next, false, false},
    OpcodeDefinition{"EXCEPTION_MATCH", 2, 2, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"PREPARE_WITH", 1, 1, 2, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"ENTER_WITH", 2, 0, 0, OperandDomain::Region, FlowKind::Next, false, false},
    OpcodeDefinition{"WITH_EXIT", 0, 0, 4, OperandDomain::Region, FlowKind::Next, false, false},
    OpcodeDefinition{"WITH_RESULT", 1, 1, 0, OperandDomain::Region, FlowKind::Next, true, true},
    OpcodeDefinition{"LOAD_GLOBAL", 0, 0, 1, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"STORE_GLOBAL", 1, 1, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"DELETE_GLOBAL", 0, 0, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"MAKE_FUNCTION", 1, 1, 1, OperandDomain::FunctionFlags, FlowKind::Next, true,
                     true},
    OpcodeDefinition{"UNARY", 1, 1, 1, OperandDomain::Unary, FlowKind::Next, true, true},
    OpcodeDefinition{"INPLACE", 2, 2, 1, OperandDomain::Binary, FlowKind::Next, true, true},
    OpcodeDefinition{"SWAP", 0, 0, 0, OperandDomain::StackDepth, FlowKind::Next, false, false},
    OpcodeDefinition{"LOAD_ATTR", 1, 1, 1, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"STORE_ATTR", 2, 2, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"DELETE_ATTR", 1, 1, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"BUILD_TUPLE", 0, 0, 1, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"BUILD_NAME_DICT", 0, 0, 1, OperandDomain::CallSite, FlowKind::Next, true,
                     true},
    OpcodeDefinition{"LOAD_DEREF", 0, 0, 1, OperandDomain::Cell, FlowKind::Next, true, true},
    OpcodeDefinition{"STORE_DEREF", 1, 1, 0, OperandDomain::Cell, FlowKind::Next, false, false},
    OpcodeDefinition{"DELETE_DEREF", 0, 0, 0, OperandDomain::Cell, FlowKind::Next, true, true},
    OpcodeDefinition{"LOAD_CLOSURE", 0, 0, 1, OperandDomain::Cell, FlowKind::Next, false, false},
    OpcodeDefinition{"GET_ITER", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"FOR_ITER", 1, 0, 1, OperandDomain::Target, FlowKind::IterBranch, true, true},
    OpcodeDefinition{"LOAD_ITEM", 2, 2, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"STORE_ITEM", 3, 3, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"DELETE_ITEM", 2, 2, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"COPY", 0, 0, 1, OperandDomain::StackDepth, FlowKind::Next, false, false},
    OpcodeDefinition{"UNPACK_SEQUENCE", 1, 1, 0, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"RAISE", 0, 0, 0, OperandDomain::RaiseArity, FlowKind::Stop, true, true},
    OpcodeDefinition{"LOAD_NAME", 0, 0, 1, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"STORE_NAME", 1, 1, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"DELETE_NAME", 0, 0, 0, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"LOAD_CLASS_DEREF", 0, 0, 1, OperandDomain::Cell, FlowKind::Next, true, true},
    OpcodeDefinition{"LOAD_BUILD_CLASS", 0, 0, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"IMPORT_NAME", 2, 2, 1, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"IMPORT_FROM", 1, 0, 1, OperandDomain::Name, FlowKind::Next, true, true},
    OpcodeDefinition{"IMPORT_STAR", 1, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"BUILD_LIST", 0, 0, 1, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"BUILD_MAP", 0, 0, 1, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"DICT_UPDATE", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"DICT_MERGE", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"LIST_APPEND", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"LIST_EXTEND", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"LIST_TO_TUPLE", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"CALL_EX", 3, 3, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"UNPACK_EX", 1, 1, 0, OperandDomain::UnpackCounts, FlowKind::Next, true, true},
    OpcodeDefinition{"LOAD_ASSERTION_ERROR", 0, 0, 1, OperandDomain::None, FlowKind::Next, false,
                     false},
    OpcodeDefinition{"BUILD_SLICE", 3, 3, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"BUILD_STRING", 0, 0, 1, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"FORMAT_VALUE", 2, 2, 1, OperandDomain::Conversion, FlowKind::Next, true,
                     true},
    OpcodeDefinition{"BUILD_SET", 0, 0, 1, OperandDomain::Count, FlowKind::Next, true, true},
    OpcodeDefinition{"SET_ADD", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"SET_UPDATE", 2, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"PRINT_EXPR", 1, 1, 0, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"GET_AWAITABLE", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"YIELD_VALUE", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"YIELD_FROM", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
    OpcodeDefinition{"ASYNC_YIELD_VALUE", 1, 1, 1, OperandDomain::None, FlowKind::Next, true, true},
};
static_assert(definitions.size() == static_cast<std::size_t>(Opcode::Count));
[[noreturn]] void invalid(CodeErrorKind kind, const char *message,
                          std::optional<std::size_t> pc = {})
{
    throw CodeValidationError(kind, message, pc);
}
std::size_t string_bytes(const std::u32string &text) noexcept
{
    const auto data = reinterpret_cast<std::uintptr_t>(text.data());
    const auto start = reinterpret_cast<std::uintptr_t>(&text);
    return data >= start && data - start < sizeof(text) ? 0
                                                        : (text.capacity() + 1) * sizeof(char32_t);
}
enum class ReasonKind { Normal, Return, Raise, Break, Continue };
struct VerifyReason {
    ReasonKind kind;
    std::uint32_t target = 0;
    std::optional<RegionToken> stop = {};
    bool operator==(const VerifyReason &) const = default;
};
std::size_t verify(const CodeSpec &spec, std::span<const Instruction> instructions,
                   std::span<const SourcePosition> positions, std::span<const CallLayout> calls,
                   std::span<const RegionLayout> regions,
                   std::span<const std::optional<RegionToken>> scopes, std::size_t constants,
                   std::size_t name_count)
{
    if (instructions.empty())
        invalid(CodeErrorKind::EmptyCode, "empty instruction sequence");
    if (!spec.first_line)
        invalid(CodeErrorKind::SourcePosition, "first line must be positive");
    if ((spec.generator || spec.coroutine) && spec.kind != CodeKind::Function)
        invalid(CodeErrorKind::Signature, "only function code can be a generator");
    if (spec.kind < CodeKind::Function || spec.kind > CodeKind::ClassBody ||
        (spec.kind != CodeKind::Function &&
         (!spec.locals.empty() || !spec.signature.parameters.empty() || spec.signature.varargs ||
          spec.signature.varkwargs)))
        invalid(CodeErrorKind::Signature, "mapping code cannot declare fast locals or parameters");
    try {
        spec.signature.validate();
    } catch (const std::invalid_argument &error) {
        throw CodeValidationError(CodeErrorKind::Signature, error.what());
    }
    std::vector<std::u32string> parameters;
    for (const auto &parameter : spec.signature.parameters) {
        if (parameter.default_value)
            invalid(CodeErrorKind::Signature, "defaults belong to Function, not Code");
        parameters.push_back(parameter.name);
    }
    if (spec.signature.varargs)
        parameters.push_back(*spec.signature.varargs);
    if (spec.signature.varkwargs)
        parameters.push_back(*spec.signature.varkwargs);
    if (spec.locals.size() < parameters.size() ||
        !std::equal(parameters.begin(), parameters.end(), spec.locals.begin()))
        invalid(CodeErrorKind::Signature, "parameter slots must prefix fast locals");
    std::unordered_set<std::u32string> names;
    for (const auto &name : spec.locals)
        if (name.empty() || !names.insert(name).second)
            invalid(CodeErrorKind::Signature, "empty or duplicate local name");
    const auto limit = std::numeric_limits<std::uint32_t>::max();
    if (spec.locals.size() > limit || spec.cellvars.size() > limit ||
        spec.freevars.size() > limit - spec.cellvars.size())
        invalid(CodeErrorKind::SizeLimit, "too many local or closure slots");
    std::unordered_set<std::u32string> closure_names;
    for (const auto &name : spec.cellvars)
        if ((spec.kind != CodeKind::ClassBody && !names.contains(name)) || name.empty() ||
            !closure_names.insert(name).second)
            invalid(CodeErrorKind::CellLayout,
                    "cell variable must identify one distinct local slot");
    if (spec.kind == CodeKind::ClassBody)
        closure_names.clear();
    for (const auto &name : spec.freevars)
        if (name.empty() || names.contains(name) || !closure_names.insert(name).second)
            invalid(CodeErrorKind::CellLayout,
                    "free variable must be distinct from locals and other cells");
    for (std::size_t pc = 0; pc < instructions.size(); ++pc) {
        const auto [opcode, operand] = instructions[pc];
        if ((opcode == Opcode::YieldValue && (!spec.generator || spec.coroutine)) ||
            (opcode == Opcode::YieldFrom && !spec.generator && !spec.coroutine))
            invalid(CodeErrorKind::Operand, "yield requires generator code", pc);
        if (opcode == Opcode::GetAwaitable && !spec.coroutine)
            invalid(CodeErrorKind::Operand, "await requires coroutine code", pc);
        if (opcode == Opcode::AsyncYieldValue && (!spec.generator || !spec.coroutine))
            invalid(CodeErrorKind::Operand, "async yield requires async generator code", pc);
        if (static_cast<std::size_t>(opcode) >= definitions.size())
            invalid(CodeErrorKind::UnknownOpcode, "unknown opcode", pc);
        const auto &definition = opcode_definition(opcode);
        if (opcode == Opcode::ImportStar && spec.kind != CodeKind::Module)
            invalid(CodeErrorKind::Operand, "IMPORT_STAR requires module code", pc);
        if ((opcode == Opcode::LoadName || opcode == Opcode::StoreName ||
             opcode == Opcode::DeleteName || opcode == Opcode::LoadClassDeref) &&
            spec.kind == CodeKind::Function)
            invalid(CodeErrorKind::Operand, "namespace opcode requires mapping code", pc);
        if (opcode == Opcode::LoadClassDeref &&
            (spec.kind != CodeKind::ClassBody || operand < spec.cellvars.size()))
            invalid(CodeErrorKind::CellBounds, "LOAD_CLASS_DEREF requires a class free cell", pc);
        switch (definition.operand) {
        case OperandDomain::None:
            if (operand)
                invalid(CodeErrorKind::Operand, "opcode has no operand", pc);
            break;
        case OperandDomain::Constant:
            if (operand >= constants)
                invalid(CodeErrorKind::ConstantBounds, "constant index out of bounds", pc);
            break;
        case OperandDomain::Local:
            if (operand >= spec.locals.size())
                invalid(CodeErrorKind::LocalBounds, "local index out of bounds", pc);
            if (std::find(spec.cellvars.begin(), spec.cellvars.end(), spec.locals[operand]) !=
                spec.cellvars.end())
                invalid(CodeErrorKind::CellLayout, "captured locals must use cell operations", pc);
            break;
        case OperandDomain::Cell:
            if (operand >= spec.cellvars.size() + spec.freevars.size())
                invalid(CodeErrorKind::CellBounds, "closure index out of bounds", pc);
            break;
        case OperandDomain::Name:
            if (operand >= name_count)
                invalid(CodeErrorKind::NameBounds, "name index out of bounds", pc);
            break;
        case OperandDomain::FunctionFlags:
            if (operand & ~15U)
                invalid(CodeErrorKind::Operand, "invalid function creation flags", pc);
            break;
        case OperandDomain::Target:
            if (operand >= instructions.size())
                invalid(CodeErrorKind::JumpBounds, "jump target out of bounds", pc);
            break;
        case OperandDomain::Binary:
            if (operand > static_cast<std::uint32_t>(BinaryOp::MatrixMultiply))
                invalid(CodeErrorKind::Operand, "invalid binary operator", pc);
            break;
        case OperandDomain::Compare:
            if (operand > static_cast<std::uint32_t>(CompareOp::NotIn))
                invalid(CodeErrorKind::Operand, "invalid comparison operator", pc);
            break;
        case OperandDomain::Unary:
            if (operand > static_cast<std::uint32_t>(UnaryOp::Not))
                invalid(CodeErrorKind::Operand, "invalid unary operator", pc);
            break;
        case OperandDomain::StackDepth:
            if (operand < (opcode == Opcode::Copy ? 1U : 2U))
                invalid(CodeErrorKind::Operand, "swap depth must be at least two", pc);
            break;
        case OperandDomain::Count:
            break;
        case OperandDomain::RaiseArity:
            if (operand != 1 && operand != 2)
                invalid(CodeErrorKind::Operand, "raise expects an exception and optional cause",
                        pc);
            break;
        case OperandDomain::UnpackCounts:
            break;
        case OperandDomain::Conversion:
            if (operand > 3)
                invalid(CodeErrorKind::Operand, "invalid format conversion", pc);
            break;
        case OperandDomain::CallSite:
            if (operand >= calls.size())
                invalid(CodeErrorKind::CallBounds, "call-site index out of bounds", pc);
            if (opcode == Opcode::BuildNameDict && calls[operand].positional)
                invalid(CodeErrorKind::Operand, "name dictionary has positional keys", pc);
            break;
        case OperandDomain::Region: {
            if (operand >= regions.size())
                invalid(CodeErrorKind::RegionBounds, "region index out of bounds", pc);
            const auto &region = regions[operand];
            if (((opcode == Opcode::EnterRegion || opcode == Opcode::EnterWith) &&
                 (region.enter_pc != pc || scopes[pc] != region.parent ||
                  (opcode == Opcode::EnterWith) != (region.kind == RegionKind::With))) ||
                (opcode == Opcode::LeaveRegion &&
                 (region.leave_pc != pc ||
                  scopes[pc] != RegionToken{operand, RegionPhase::Protected})) ||
                (opcode == Opcode::EndHandler &&
                 (region.kind == RegionKind::Loop || region.end_handler_pc != pc ||
                  scopes[pc] != RegionToken{operand, region.kind == RegionKind::Except
                                                         ? RegionPhase::Handler
                                                         : RegionPhase::Cleanup})))
                invalid(CodeErrorKind::RegionStructure,
                        "region opcode is outside its structured boundary", pc);
            if ((opcode == Opcode::WithExit || opcode == Opcode::WithResult) &&
                (region.kind != RegionKind::With ||
                 scopes[pc] != RegionToken{operand, RegionPhase::Cleanup} ||
                 (opcode == Opcode::WithExit ? region.handler_pc != pc
                                             : region.end_handler_pc != pc + 1)))
                invalid(CodeErrorKind::RegionStructure,
                        "with opcode is outside its cleanup template", pc);
            if (opcode == Opcode::BreakLoop || opcode == Opcode::ContinueLoop) {
                auto cursor = scopes[pc];
                while (cursor && *cursor != RegionToken{operand, RegionPhase::Protected})
                    cursor = regions[cursor->region].parent;
                if (!cursor || region.kind != RegionKind::Loop)
                    invalid(CodeErrorKind::LoopTarget, "target loop is not active", pc);
            }
            break;
        }
        }
        if (definition.operand == OperandDomain::Target && scopes[operand] != scopes[pc])
            invalid(CodeErrorKind::RegionJump, "ordinary branch crosses a region boundary", pc);
        const auto &position = positions[pc];
        if ((!position.line && (position.column || position.end_line || position.end_column)) ||
            (position.line &&
             (position.end_line < position.line ||
              (position.end_line == position.line && position.end_column < position.column))))
            invalid(CodeErrorKind::SourcePosition, "invalid source position", pc);
    }
    for (std::uint32_t id = 0; id < regions.size(); ++id) {
        const auto &region = regions[id];
        if (region.normal_pc < instructions.size() && scopes[region.normal_pc] != region.parent)
            invalid(CodeErrorKind::RegionJump, "normal exit must target the region's parent",
                    region.enter_pc);
        if (region.kind == RegionKind::Loop &&
            (region.continue_pc >= instructions.size() || region.break_pc >= instructions.size() ||
             scopes[region.continue_pc] != RegionToken{id, RegionPhase::Protected} ||
             scopes[region.break_pc] != region.parent))
            invalid(CodeErrorKind::LoopTarget, "invalid loop continuation or break destination",
                    region.enter_pc);
        if (region.kind == RegionKind::With) {
            const auto start = static_cast<std::size_t>(region.handler_pc);
            if (start + 3 >= instructions.size() || region.end_handler_pc != start + 3 ||
                instructions[start].opcode != Opcode::WithExit ||
                instructions[start].operand != id ||
                instructions[start + 1].opcode != Opcode::Call ||
                instructions[start + 2].opcode != Opcode::WithResult ||
                instructions[start + 2].operand != id ||
                instructions[start + 3].opcode != Opcode::EndHandler ||
                calls[instructions[start + 1].operand].positional != 3 ||
                calls[instructions[start + 1].operand].keywords)
                invalid(CodeErrorKind::RegionStructure, "invalid with cleanup template",
                        region.enter_pc);
        }
    }
    std::vector<std::optional<std::size_t>> heights(instructions.size());
    std::vector<std::vector<VerifyReason>> continuations(regions.size());
    std::vector<std::size_t> pending;
    std::size_t maximum = 0;
    auto stack_base = [&](std::uint32_t id) {
        return *heights[regions[id].enter_pc] - (regions[id].kind == RegionKind::With ? 1 : 0);
    };
    auto join = [&](std::size_t target, std::size_t height, std::optional<RegionToken> scope) {
        if (target == instructions.size())
            invalid(CodeErrorKind::Fallthrough, "reachable code falls off its end", target - 1);
        if (scopes[target] != scope)
            invalid(CodeErrorKind::RegionJump, "control flow crosses a region boundary", target);
        maximum = std::max(maximum, height);
        if (!heights[target]) {
            heights[target] = height;
            pending.push_back(target);
        } else if (*heights[target] != height)
            invalid(CodeErrorKind::StackJoin, "inconsistent stack height at merge", target);
    };
    auto route = [&](VerifyReason reason, std::optional<RegionToken> scope, std::size_t height) {
        for (;;) {
            if (reason.kind == ReasonKind::Normal && scope == reason.stop) {
                join(reason.target, height, scope);
                return;
            }
            if (!scope)
                return;
            const auto id = scope->region;
            const auto &region = regions[id];
            const auto base = stack_base(id);
            if (scope->phase == RegionPhase::Protected) {
                if (region.kind == RegionKind::Finally || region.kind == RegionKind::With) {
                    auto &saved = continuations[id];
                    if (std::find(saved.begin(), saved.end(), reason) == saved.end()) {
                        saved.push_back(reason);
                        if (heights[region.end_handler_pc])
                            pending.push_back(region.end_handler_pc);
                    }
                    join(region.handler_pc, base, RegionToken{id, RegionPhase::Cleanup});
                    return;
                }
                if (region.kind == RegionKind::Except && reason.kind == ReasonKind::Raise) {
                    join(region.handler_pc, base + 1, RegionToken{id, RegionPhase::Handler});
                    return;
                }
                if (region.kind == RegionKind::Loop && reason.target == id) {
                    if (reason.kind == ReasonKind::Break) {
                        join(region.break_pc, base, region.parent);
                        return;
                    }
                    if (reason.kind == ReasonKind::Continue) {
                        join(region.continue_pc, base, scope);
                        return;
                    }
                }
            }
            height = base - (region.kind == RegionKind::With ? 1 : 0);
            scope = region.parent;
        }
    };
    join(0, 0, {});
    // Heights merge once; a cleanup end is revisited only for a new
    // finite (reason, destination) pair, never for growing stack states.
    for (std::size_t next = 0; next < pending.size(); ++next) {
        const auto pc = pending[next];
        const auto &instruction = instructions[pc];
        const auto &definition = opcode_definition(instruction.opcode);
        const auto height = *heights[pc];
        const auto effect = instruction_effect(instruction, calls);
        if (height < effect.inputs)
            invalid(CodeErrorKind::StackUnderflow, "instruction requires more stack inputs", pc);
        if (scopes[pc] && height - effect.pops < stack_base(scopes[pc]->region))
            invalid(CodeErrorKind::StackBase, "instruction consumes a protected stack prefix", pc);
        const auto after = height - effect.pops + effect.pushes;
        maximum = std::max(maximum, after);
        if (definition.may_raise)
            route({ReasonKind::Raise}, scopes[pc], height);
        if (instruction.opcode == Opcode::Return)
            route({ReasonKind::Return}, scopes[pc], after);
        if (definition.flow == FlowKind::Jump || definition.flow == FlowKind::Branch)
            join(instruction.operand, after, scopes[pc]);
        if (definition.flow == FlowKind::IterBranch) {
            const auto taken = instruction_effect(instruction, calls, true);
            const auto target_height = height - taken.pops + taken.pushes;
            if (scopes[pc] && target_height < stack_base(scopes[pc]->region))
                invalid(CodeErrorKind::StackBase, "iterator exit consumes a protected stack prefix",
                        pc);
            join(instruction.operand, target_height, scopes[pc]);
        }
        if (definition.flow == FlowKind::Next || definition.flow == FlowKind::Branch ||
            definition.flow == FlowKind::IterBranch) {
            auto scope =
                instruction.opcode == Opcode::EnterRegion || instruction.opcode == Opcode::EnterWith
                    ? std::optional<RegionToken>{{instruction.operand, RegionPhase::Protected}}
                    : scopes[pc];
            join(pc + 1, after, scope);
        }
        if (definition.flow == FlowKind::Leave || definition.flow == FlowKind::EndHandler) {
            const auto &region = regions[instruction.operand];
            if (height != stack_base(instruction.operand))
                invalid(CodeErrorKind::StackBase, "region exit must restore its stack base", pc);
            if (definition.flow == FlowKind::Leave)
                route({ReasonKind::Normal, region.normal_pc, region.parent}, scopes[pc], height);
            else if (region.kind == RegionKind::Except)
                join(region.after_pc, height, region.parent);
            else
                for (const auto &reason : continuations[instruction.operand]) {
                    const auto restored = height - (region.kind == RegionKind::With ? 1 : 0);
                    route(reason, region.parent, restored);
                    if (region.kind == RegionKind::With && reason.kind == ReasonKind::Raise)
                        route({ReasonKind::Normal, region.after_pc, region.parent}, region.parent,
                              restored);
                }
        }
        if (definition.flow == FlowKind::LoopControl)
            route(
                {instruction.opcode == Opcode::BreakLoop ? ReasonKind::Break : ReasonKind::Continue,
                 instruction.operand},
                scopes[pc], height);
    }
    return maximum;
}
} // namespace
const OpcodeDefinition &opcode_definition(Opcode opcode)
{
    return definitions.at(static_cast<std::size_t>(opcode));
}
StackEffect instruction_effect(Instruction instruction, std::span<const CallLayout> calls,
                               bool jump_taken)
{
    const auto &definition = opcode_definition(instruction.opcode);
    if (instruction.opcode == Opcode::ForIter && jump_taken)
        return {1, 0, 0};
    if (instruction.opcode == Opcode::Raise)
        return {instruction.operand, instruction.operand, 0};
    if (instruction.opcode == Opcode::UnpackSequence)
        return {1, 1, instruction.operand};
    if (instruction.opcode == Opcode::UnpackEx)
        return {1, 1, (instruction.operand & 65535U) + (instruction.operand >> 16) + 1};
    if (instruction.opcode == Opcode::BuildMap) {
        const auto count = static_cast<std::size_t>(instruction.operand) * 2;
        return {count, count, 1};
    }
    if (definition.operand == OperandDomain::CallSite && instruction.operand >= calls.size())
        invalid(CodeErrorKind::CallBounds, "call-site index out of bounds");
    const auto arguments = definition.operand == OperandDomain::CallSite
                               ? calls[instruction.operand].argument_count()
                           : definition.operand == OperandDomain::Count ? instruction.operand
                                                                        : 0;
    if (definition.operand == OperandDomain::FunctionFlags && (instruction.operand & ~15U))
        invalid(CodeErrorKind::Operand, "invalid function creation flags");
    const auto defaults =
        definition.operand == OperandDomain::FunctionFlags ? std::popcount(instruction.operand) : 0;
    const auto inputs =
        definition.operand == OperandDomain::StackDepth ? instruction.operand : definition.inputs;
    return {inputs + arguments + defaults, definition.pops + arguments + defaults,
            definition.pushes};
}
std::size_t VerifiedCode::external_bytes() const noexcept
{
    auto bytes = instructions_.capacity() * sizeof(Instruction) +
                 positions_.capacity() * sizeof(SourcePosition) +
                 calls_.capacity() * sizeof(CallLayout) +
                 regions_.capacity() * sizeof(RegionLayout) +
                 (locals_.capacity() + cellvars_.capacity() + freevars_.capacity()) *
                     sizeof(std::u32string) +
                 cell_locals_.capacity() * sizeof(std::optional<std::uint32_t>) +
                 signature_.external_bytes();
    for (const auto &local : locals_)
        bytes += string_bytes(local);
    for (const auto &name : cellvars_)
        bytes += string_bytes(name);
    for (const auto &name : freevars_)
        bytes += string_bytes(name);
    return bytes;
}
void CodeObject::trace(Tracer &tracer) const noexcept
{
    for (auto value : {constants, varnames, name, qualname, filename, call_keywords, names,
                       docstring, cellvars, freevars})
        tracer.visit(value);
}
CodeBuilder::CodeBuilder(Attachment &context, CodeSpec spec)
    : context_(context), roots_(context), spec_(std::move(spec))
{
    if (!context.core_ready())
        throw std::logic_error("CodeBuilder requires an initialized core runtime");
    for (const auto &parameter : spec_.signature.parameters)
        if (parameter.default_value)
            roots_.root(*parameter.default_value);
}
std::uint32_t CodeBuilder::constant(Handle value)
{
    if (constants_.size() >= std::numeric_limits<std::uint32_t>::max())
        invalid(CodeErrorKind::SizeLimit, "too many constants");
    auto rooted = roots_.root(value.get(context_));
    constants_.push_back(rooted.get(context_));
    return static_cast<std::uint32_t>(constants_.size() - 1);
}
std::uint32_t CodeBuilder::name(std::u32string value)
{
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (context_.payload<StrObject>(names_[i]).value == value)
            return static_cast<std::uint32_t>(i);
    if (names_.size() >= std::numeric_limits<std::uint32_t>::max())
        invalid(CodeErrorKind::SizeLimit, "too many names");
    ReturnSlot text(context_);
    make_str(context_, std::move(value), text);
    const auto rooted = roots_.root(text.value());
    names_.push_back(rooted.get(context_));
    return static_cast<std::uint32_t>(names_.size() - 1);
}
CodeBuilder::Label CodeBuilder::label()
{
    labels_.push_back({});
    return {this, labels_.size() - 1};
}
void CodeBuilder::bind(Label label)
{
    if (label.owner != this || label.index >= labels_.size())
        invalid(CodeErrorKind::ForeignLabel, "label belongs to a different builder");
    if (labels_[label.index])
        invalid(CodeErrorKind::DuplicateLabel, "label is already bound");
    labels_[label.index] = instructions_.size();
}
void CodeBuilder::emit(Opcode opcode, std::uint32_t operand, SourcePosition source)
{
    context_.require_attached();
    if (instructions_.size() >= std::numeric_limits<std::uint32_t>::max())
        invalid(CodeErrorKind::SizeLimit, "too many instructions");
    instructions_.push_back({opcode, operand});
    try {
        positions_.push_back(source);
        try {
            scopes_.push_back(scope_);
        } catch (...) {
            positions_.pop_back();
            throw;
        }
    } catch (...) {
        instructions_.pop_back();
        throw;
    }
}
void CodeBuilder::jump(Opcode opcode, Label target, SourcePosition source)
{
    if (target.owner != this || target.index >= labels_.size())
        invalid(CodeErrorKind::ForeignLabel, "label belongs to a different builder");
    if (opcode != Opcode::Jump && opcode != Opcode::JumpIfFalse && opcode != Opcode::ForIter)
        invalid(CodeErrorKind::Operand, "not a branch opcode");
    fixups_.push_back({instructions_.size(), target});
    try {
        emit(opcode, 0, source);
    } catch (...) {
        fixups_.pop_back();
        throw;
    }
}
void CodeBuilder::freeze(ReturnSlot &out)
{
    out.reset(context_);
    auto instructions = instructions_;
    for (const auto &[pc, label] : fixups_) {
        if (!labels_[label.index])
            invalid(CodeErrorKind::UnboundLabel, "unbound branch label", pc);
        instructions[pc].operand = static_cast<std::uint32_t>(*labels_[label.index]);
    }
    std::vector<CallLayout> calls;
    for (const auto &site : calls_) {
        if (site.keywords.size() >= std::numeric_limits<std::uint32_t>::max() ||
            site.positional >= std::numeric_limits<std::uint32_t>::max() - site.keywords.size())
            invalid(CodeErrorKind::SizeLimit, "too many call arguments");
        for (const auto &name : site.keywords)
            for (auto point : name)
                if (point > 0x10ffff)
                    invalid(CodeErrorKind::Operand, "invalid keyword codepoint");
        calls.push_back({site.positional, static_cast<std::uint32_t>(site.keywords.size())});
    }
    if (scope_)
        invalid(CodeErrorKind::RegionStructure, "unclosed region");
    std::vector<RegionLayout> regions;
    for (const auto &region : regions_) {
        if (!region.complete)
            invalid(CodeErrorKind::RegionStructure, "incomplete region");
        auto layout = region.layout;
        layout.normal_pc = layout.after_pc;
        if (region.normal_target) {
            if (!labels_[region.normal_target->index])
                invalid(CodeErrorKind::UnboundLabel, "unbound normal exit target");
            layout.normal_pc = static_cast<std::uint32_t>(*labels_[region.normal_target->index]);
        }
        if (layout.kind == RegionKind::Loop) {
            if (!region.continue_target || !region.break_target)
                invalid(CodeErrorKind::LoopTarget, "loop requires explicit targets");
            if (!labels_[region.continue_target->index] || !labels_[region.break_target->index])
                invalid(CodeErrorKind::UnboundLabel, "unbound loop target");
            layout.continue_pc =
                static_cast<std::uint32_t>(*labels_[region.continue_target->index]);
            layout.break_pc = static_cast<std::uint32_t>(*labels_[region.break_target->index]);
        }
        regions.push_back(layout);
    }
    const auto maximum = verify(spec_, instructions, positions_, calls, regions, scopes_,
                                constants_.size(), names_.size());
    VerifiedCode verified(std::move(instructions), positions_, std::move(calls), std::move(regions),
                          spec_.signature, spec_.locals, spec_.cellvars, spec_.freevars, maximum,
                          spec_.first_line, spec_.kind, spec_.generator, spec_.coroutine);
    RootScope roots(context_);
    ReturnSlot temporary(context_);
    cxxpython::core::make_tuple(context_, constants_, temporary);
    auto constants = roots.root(temporary.value());
    cxxpython::core::make_tuple(context_, names_, temporary);
    auto global_names = roots.root(temporary.value());
    std::vector<Value> names;
    const auto parameters = spec_.signature.parameters.size() + bool(spec_.signature.varargs) +
                            bool(spec_.signature.varkwargs);
    for (std::size_t i = 0; i < spec_.locals.size(); ++i)
        if (i < parameters || std::find(spec_.cellvars.begin(), spec_.cellvars.end(),
                                        spec_.locals[i]) == spec_.cellvars.end()) {
            make_str(context_, spec_.locals[i], temporary);
            names.push_back(roots.root(temporary.value()).get(context_));
        }
    cxxpython::core::make_tuple(context_, names, temporary);
    auto varnames = roots.root(temporary.value());
    auto closure_names = [&](const std::vector<std::u32string> &entries) {
        if (entries.empty())
            return roots.root(context_.builtin(CoreObject::EmptyTuple));
        std::vector<Value> values;
        for (const auto &entry : entries) {
            make_str(context_, entry, temporary);
            values.push_back(roots.root(temporary.value()).get(context_));
        }
        cxxpython::core::make_tuple(context_, values, temporary);
        return roots.root(temporary.value());
    };
    const auto cellvars = closure_names(spec_.cellvars), freevars = closure_names(spec_.freevars);
    make_str(context_, spec_.name, temporary);
    auto name = roots.root(temporary.value());
    make_str(context_, spec_.qualname.value_or(spec_.name), temporary);
    auto qualname = roots.root(temporary.value());
    make_str(context_, spec_.filename, temporary);
    auto filename = roots.root(temporary.value());
    auto docstring = roots.root(context_.builtin(CoreObject::None));
    if (spec_.docstring) {
        make_str(context_, *spec_.docstring, temporary);
        docstring = roots.root(temporary.value());
    }
    std::vector<Value> keyword_tables;
    for (const auto &site : calls_) {
        std::vector<Value> keys;
        for (const auto &key : site.keywords) {
            make_str(context_, key, temporary);
            keys.push_back(roots.root(temporary.value()).get(context_));
        }
        cxxpython::core::make_tuple(context_, keys, temporary);
        keyword_tables.push_back(roots.root(temporary.value()).get(context_));
    }
    auto call_keywords = roots.root(context_.builtin(CoreObject::EmptyTuple));
    if (!keyword_tables.empty()) {
        cxxpython::core::make_tuple(context_, keyword_tables, temporary);
        call_keywords = roots.root(temporary.value());
    }
    auto type = roots.root(context_.builtin(CoreObject::CodeType));
    context_.allocate_instance<CodeObject>(
        type, out, std::move(verified), constants.get(context_), varnames.get(context_),
        name.get(context_), qualname.get(context_), filename.get(context_),
        call_keywords.get(context_), global_names.get(context_), docstring.get(context_),
        cellvars.get(context_), freevars.get(context_));
}
void CodeBuilder::call(std::uint32_t positional, std::span<const std::u32string> keywords,
                       SourcePosition source)
{
    call_site(Opcode::Call, positional, keywords, source);
}
void CodeBuilder::name_dict(std::span<const std::u32string> names, SourcePosition source)
{
    call_site(Opcode::BuildNameDict, 0, names, source);
}
void CodeBuilder::call_site(Opcode opcode, std::uint32_t positional,
                            std::span<const std::u32string> keywords, SourcePosition source)
{
    context_.require_attached();
    if (calls_.size() >= std::numeric_limits<std::uint32_t>::max())
        invalid(CodeErrorKind::SizeLimit, "too many call sites");
    calls_.push_back({positional, {keywords.begin(), keywords.end()}});
    try {
        emit(opcode, static_cast<std::uint32_t>(calls_.size() - 1), source);
    } catch (...) {
        calls_.pop_back();
        throw;
    }
}
CodeBuilder::RegionSpec &CodeBuilder::require_region(Region region)
{
    if (region.owner != this || region.index >= regions_.size())
        invalid(CodeErrorKind::RegionBounds, "foreign or unknown region");
    return regions_[region.index];
}
CodeBuilder::Region CodeBuilder::region(RegionKind kind)
{
    if (kind < RegionKind::Loop || kind > RegionKind::With)
        invalid(CodeErrorKind::RegionStructure, "unknown region kind");
    if (regions_.size() >= std::numeric_limits<std::uint32_t>::max())
        invalid(CodeErrorKind::SizeLimit, "too many regions");
    RegionSpec spec;
    spec.layout.kind = kind;
    regions_.push_back(spec);
    return {this, static_cast<std::uint32_t>(regions_.size() - 1)};
}
CodeBuilder::Region CodeBuilder::loop(Label continued, Label broken)
{
    for (auto label : {continued, broken})
        if (label.owner != this || label.index >= labels_.size())
            invalid(CodeErrorKind::ForeignLabel, "foreign loop label");
    auto result = region(RegionKind::Loop);
    regions_.back().continue_target = continued;
    regions_.back().break_target = broken;
    return result;
}
void CodeBuilder::enter(Region id, SourcePosition source)
{
    auto &region = require_region(id);
    if (region.entered)
        invalid(CodeErrorKind::RegionStructure, "region already entered");
    region.layout.parent = scope_;
    region.layout.depth = scope_ ? regions_[scope_->region].layout.depth + 1 : 1;
    const auto count = instructions_.size(), sites = calls_.size();
    try {
        if (region.layout.kind == RegionKind::With) {
            emit(Opcode::PrepareWith, 0, source);
            call(0, {}, source);
        }
        region.layout.enter_pc = static_cast<std::uint32_t>(instructions_.size());
        emit(region.layout.kind == RegionKind::With ? Opcode::EnterWith : Opcode::EnterRegion,
             id.index, source);
    } catch (...) {
        instructions_.resize(count);
        positions_.resize(count);
        scopes_.resize(count);
        calls_.resize(sites);
        throw;
    }
    region.entered = true;
    scope_ = RegionToken{id.index, RegionPhase::Protected};
}
void CodeBuilder::leave(Region id, SourcePosition source)
{
    auto &region = require_region(id);
    if (scope_ != RegionToken{id.index, RegionPhase::Protected})
        invalid(CodeErrorKind::RegionStructure, "not in the protected region");
    const auto saved = region;
    const auto scope = scope_;
    const auto count = instructions_.size(), sites = calls_.size();
    try {
        region.layout.leave_pc = static_cast<std::uint32_t>(instructions_.size());
        emit(Opcode::LeaveRegion, id.index, source);
        region.closed = true;
        scope_ = region.layout.parent;
        if (region.layout.kind == RegionKind::Loop) {
            region.complete = true;
            region.layout.after_pc = static_cast<std::uint32_t>(instructions_.size());
        }
        if (region.layout.kind == RegionKind::With) {
            handler(id);
            emit(Opcode::WithExit, id.index, source);
            call(3, {}, source);
            emit(Opcode::WithResult, id.index, source);
            end_handler(id);
        }
    } catch (...) {
        region = saved;
        scope_ = scope;
        instructions_.resize(count);
        positions_.resize(count);
        scopes_.resize(count);
        calls_.resize(sites);
        throw;
    }
}
void CodeBuilder::leave(Region id, Label target, SourcePosition source)
{
    if (target.owner != this || target.index >= labels_.size())
        invalid(CodeErrorKind::ForeignLabel, "normal exit label belongs to a different builder");
    auto &region = require_region(id);
    const auto previous = region.normal_target;
    region.normal_target = target;
    try {
        leave(id, source);
    } catch (...) {
        region.normal_target = previous;
        throw;
    }
}
void CodeBuilder::handler(Region id)
{
    auto &region = require_region(id);
    if (!region.closed || region.handler_started || region.layout.kind == RegionKind::Loop ||
        scope_ != region.layout.parent)
        invalid(CodeErrorKind::RegionStructure, "invalid handler boundary");
    region.layout.handler_pc = static_cast<std::uint32_t>(instructions_.size());
    region.handler_started = true;
    scope_ = RegionToken{id.index, region.layout.kind == RegionKind::Except ? RegionPhase::Handler
                                                                            : RegionPhase::Cleanup};
}
void CodeBuilder::end_handler(Region id)
{
    auto &region = require_region(id);
    const auto phase =
        region.layout.kind == RegionKind::Except ? RegionPhase::Handler : RegionPhase::Cleanup;
    if (!region.handler_started || scope_ != RegionToken{id.index, phase})
        invalid(CodeErrorKind::RegionStructure, "not in this handler");
    region.layout.end_handler_pc = static_cast<std::uint32_t>(instructions_.size());
    emit(Opcode::EndHandler, id.index);
    region.layout.after_pc = static_cast<std::uint32_t>(instructions_.size());
    region.complete = true;
    scope_ = region.layout.parent;
}
void CodeBuilder::control(Opcode opcode, Region id)
{
    const auto &region = require_region(id);
    if ((opcode != Opcode::BreakLoop && opcode != Opcode::ContinueLoop) ||
        region.layout.kind != RegionKind::Loop)
        invalid(CodeErrorKind::LoopTarget, "control requires a loop and break/continue opcode");
    emit(opcode, id.index);
}
std::string disassemble(Attachment &context, Handle code)
{
    if (context.type_of(code) != context.builtin(CoreObject::CodeType))
        throw std::invalid_argument("disassembly requires code");
    const auto &program = context.payload<CodeObject>(code).program;
    std::string result;
    for (std::size_t pc = 0; pc < program.instructions().size(); ++pc) {
        const auto &instruction = program.instructions()[pc];
        const auto &definition = opcode_definition(instruction.opcode);
        result += std::to_string(pc) + " " + std::string(definition.name);
        if (definition.operand != OperandDomain::None)
            result += " " + std::to_string(instruction.operand);
        result += '\n';
    }
    return result;
}
} // namespace cxxpython::core
