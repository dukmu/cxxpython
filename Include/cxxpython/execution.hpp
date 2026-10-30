#pragma once

#include "cxxpython/protocol.hpp"

#include <limits>
#include <string_view>
#include <variant>

namespace cxxpython::core {
enum class Opcode : std::uint8_t {
    LoadConst,
    LoadFast,
    StoreFast,
    DeleteFast,
    PopTop,
    DupTop,
    Binary,
    Compare,
    Jump,
    JumpIfFalse,
    Return,
    RaiseInstance,
    Call,
    EnterRegion,
    LeaveRegion,
    EndHandler,
    BreakLoop,
    ContinueLoop,
    Reraise,
    LoadException,
    ExceptionMatch,
    PrepareWith,
    EnterWith,
    WithExit,
    WithResult,
    LoadGlobal,
    StoreGlobal,
    DeleteGlobal,
    MakeFunction,
    Unary,
    Inplace,
    Swap,
    LoadAttr,
    StoreAttr,
    DeleteAttr,
    BuildTuple,
    BuildNameDict,
    LoadDeref,
    StoreDeref,
    DeleteDeref,
    LoadClosure,
    GetIter,
    ForIter,
    LoadItem,
    StoreItem,
    DeleteItem,
    Copy,
    UnpackSequence,
    Raise,
    LoadName,
    StoreName,
    DeleteName,
    LoadClassDeref,
    LoadBuildClass,
    ImportName,
    ImportFrom,
    ImportStar,
    BuildList,
    BuildMap,
    DictUpdate,
    DictMerge,
    ListAppend,
    ListExtend,
    ListToTuple,
    CallEx,
    UnpackEx,
    LoadAssertionError,
    BuildSlice,
    BuildString,
    FormatValue,
    BuildSet,
    SetAdd,
    SetUpdate,
    Count,
};
enum class OperandDomain {
    None,
    Constant,
    Local,
    Name,
    Binary,
    Compare,
    Target,
    CallSite,
    Region,
    FunctionFlags,
    Unary,
    StackDepth,
    Count,
    Cell,
    RaiseArity,
    UnpackCounts,
    Conversion
};
enum class FunctionFlags : std::uint32_t {
    None = 0,
    Defaults = 1,
    KeywordDefaults = 2,
    Closure = 4
};
enum class FlowKind { Next, Jump, Branch, Stop, Leave, EndHandler, LoopControl, IterBranch };
enum class RegionKind { Loop, Except, Finally, With };
enum class RegionPhase { Protected, Handler, Cleanup };
struct RegionToken {
    std::uint32_t region;
    RegionPhase phase;
    bool operator==(const RegionToken &) const = default;
};
struct RegionLayout {
    RegionKind kind;
    std::optional<RegionToken> parent;
    std::uint32_t enter_pc = 0, leave_pc = 0, handler_pc = 0, end_handler_pc = 0, after_pc = 0;
    // A successful try enters else; a handled exception skips it via after_pc.
    std::uint32_t normal_pc = 0;
    std::uint32_t continue_pc = 0, break_pc = 0;
    std::size_t depth = 1;
};
struct NormalExit {
    std::uint32_t pc;
    std::optional<RegionToken> stop;
};
struct FunctionReturn {
    Value value;
};
struct ExceptionRaise {
    Value exception;
};
struct LoopBreak {
    std::uint32_t region;
};
struct LoopContinue {
    std::uint32_t region;
};
using TracedReason =
    std::variant<NormalExit, FunctionReturn, ExceptionRaise, LoopBreak, LoopContinue>;
struct ActiveRegion {
    RegionToken token;
    std::size_t stack_base;
    std::optional<Value> previous_exception;
};
struct CleanupContinuation {
    std::uint32_t region;
    TracedReason saved_reason;
    std::optional<Value> previous_exception;
};
struct OpcodeDefinition {
    std::string_view name;
    std::size_t inputs;
    std::size_t pops;
    std::size_t pushes;
    OperandDomain operand;
    FlowKind flow;
    bool may_raise;
    bool safepoint;
};
struct Instruction {
    Opcode opcode;
    std::uint32_t operand = 0;
};
struct CallLayout {
    std::uint32_t positional, keywords;
    std::size_t argument_count() const noexcept
    {
        return static_cast<std::size_t>(positional) + keywords;
    }
};
struct StackEffect {
    std::size_t inputs, pops, pushes;
};
struct SourcePosition {
    std::uint32_t line = 0, column = 0, end_line = 0, end_column = 0;
};
enum class CodeErrorKind {
    EmptyCode,
    UnknownOpcode,
    Operand,
    ConstantBounds,
    LocalBounds,
    JumpBounds,
    StackUnderflow,
    StackJoin,
    Fallthrough,
    Signature,
    SourcePosition,
    ForeignLabel,
    UnboundLabel,
    DuplicateLabel,
    SizeLimit,
    CallBounds,
    RegionBounds,
    RegionStructure,
    RegionJump,
    StackBase,
    LoopTarget,
    NameBounds,
    CellBounds,
    CellLayout,
};
class CodeValidationError : public std::invalid_argument {
  public:
    CodeValidationError(CodeErrorKind kind, std::string message, std::optional<std::size_t> pc = {})
        : std::invalid_argument(std::move(message)), kind(kind), pc(pc)
    {}
    CodeErrorKind kind;
    std::optional<std::size_t> pc;
};
enum class CodeKind { Function, Module, ClassBody };
struct CodeSpec {
    std::u32string name = U"<code>";
    std::optional<std::u32string> qualname;
    std::u32string filename = U"<native>";
    std::uint32_t first_line = 1;
    SignatureLayout signature;
    std::vector<std::u32string> locals;
    std::optional<std::u32string> docstring;
    std::vector<std::u32string> cellvars, freevars;
    CodeKind kind = CodeKind::Function;
};
class CodeBuilder;
class VerifiedCode final {
  public:
    VerifiedCode(VerifiedCode &&) noexcept = default;
    VerifiedCode(const VerifiedCode &) = delete;
    std::span<const Instruction> instructions() const noexcept
    {
        return instructions_;
    }
    std::span<const SourcePosition> positions() const noexcept
    {
        return positions_;
    }
    std::span<const CallLayout> calls() const noexcept
    {
        return calls_;
    }
    std::span<const RegionLayout> regions() const noexcept
    {
        return regions_;
    }
    std::size_t max_regions() const noexcept
    {
        return max_regions_;
    }
    const SignatureLayout &signature() const noexcept
    {
        return signature_;
    }
    std::span<const std::u32string> local_names() const noexcept
    {
        return locals_;
    }
    std::span<const std::u32string> cell_names() const noexcept
    {
        return cellvars_;
    }
    std::span<const std::u32string> free_names() const noexcept
    {
        return freevars_;
    }
    std::span<const std::optional<std::uint32_t>> cell_locals() const noexcept
    {
        return cell_locals_;
    }
    CodeKind kind() const noexcept
    {
        return kind_;
    }
    std::size_t max_stack() const noexcept
    {
        return max_stack_;
    }
    std::uint32_t first_line() const noexcept
    {
        return first_line_;
    }
    std::size_t external_bytes() const noexcept;

  private:
    friend class CodeBuilder;
    VerifiedCode(std::vector<Instruction> instructions, std::vector<SourcePosition> positions,
                 std::vector<CallLayout> calls, std::vector<RegionLayout> regions,
                 SignatureLayout signature, std::vector<std::u32string> locals,
                 std::vector<std::u32string> cellvars, std::vector<std::u32string> freevars,
                 std::size_t stack, std::uint32_t first_line, CodeKind kind)
        : instructions_(std::move(instructions)), positions_(std::move(positions)),
          calls_(std::move(calls)), regions_(std::move(regions)), signature_(std::move(signature)),
          locals_(std::move(locals)), cellvars_(std::move(cellvars)),
          freevars_(std::move(freevars)), max_stack_(stack), first_line_(first_line), kind_(kind)
    {
        for (const auto &region : regions_)
            if (region.depth > max_regions_)
                max_regions_ = region.depth;
        for (const auto &name : cellvars_) {
            std::size_t index = 0;
            while (index < locals_.size() && locals_[index] != name)
                ++index;
            cell_locals_.push_back(index < locals_.size()
                                       ? std::optional{static_cast<std::uint32_t>(index)}
                                       : std::nullopt);
        }
    }
    std::vector<Instruction> instructions_;
    std::vector<SourcePosition> positions_;
    std::vector<CallLayout> calls_;
    std::vector<RegionLayout> regions_;
    SignatureLayout signature_;
    std::vector<std::u32string> locals_;
    std::vector<std::u32string> cellvars_, freevars_;
    std::vector<std::optional<std::uint32_t>> cell_locals_;
    std::size_t max_stack_;
    std::uint32_t first_line_;
    std::size_t max_regions_ = 0;
    CodeKind kind_;
};
struct CodeObject {
    ObjectHeader header;
    const VerifiedCode program;
    const Value constants, varnames, name, qualname, filename, call_keywords, names, docstring,
        cellvars, freevars;
    CodeObject(InitialType head, VerifiedCode code, Value consts, Value variables, Value name,
               Value qualname, Value filename, Value call_keywords, Value names, Value docstring,
               Value cellvars, Value freevars)
        : header(head), program(std::move(code)), constants(consts), varnames(variables),
          name(name), qualname(qualname), filename(filename), call_keywords(call_keywords),
          names(names), docstring(docstring), cellvars(cellvars), freevars(freevars)
    {}
    void trace(Tracer &tracer) const noexcept;
    std::size_t external_bytes() const noexcept
    {
        return program.external_bytes();
    }
};
class CodeBuilder final {
  public:
    struct Label {
        const CodeBuilder *owner;
        std::size_t index;
    };
    struct Region {
        const CodeBuilder *owner;
        std::uint32_t index;
    };
    explicit CodeBuilder(Attachment &context, CodeSpec spec = {});
    std::uint32_t constant(Handle value);
    std::uint32_t name(std::u32string value);
    Label label();
    void bind(Label label);
    void emit(Opcode opcode, std::uint32_t operand = 0, SourcePosition source = {});
    void jump(Opcode opcode, Label target, SourcePosition source = {});
    void call(std::uint32_t positional, std::span<const std::u32string> keywords = {},
              SourcePosition source = {});
    void name_dict(std::span<const std::u32string> names, SourcePosition source = {});
    Region region(RegionKind kind);
    Region loop(Label continue_target, Label break_target);
    void enter(Region region, SourcePosition source = {});
    void leave(Region region, SourcePosition source = {});
    void leave(Region region, Label destination, SourcePosition source = {});
    void handler(Region region);
    void end_handler(Region region);
    void control(Opcode opcode, Region loop);
    void freeze(ReturnSlot &out);

  private:
    Attachment &context_;
    RootScope roots_;
    CodeSpec spec_;
    std::vector<Value> constants_;
    std::vector<Value> names_;
    std::vector<Instruction> instructions_;
    std::vector<SourcePosition> positions_;
    std::vector<std::optional<std::size_t>> labels_;
    std::vector<std::pair<std::size_t, Label>> fixups_;
    struct CallSiteSpec {
        std::uint32_t positional;
        std::vector<std::u32string> keywords;
    };
    std::vector<CallSiteSpec> calls_;
    void call_site(Opcode opcode, std::uint32_t positional,
                   std::span<const std::u32string> keywords, SourcePosition source);
    struct RegionSpec {
        RegionLayout layout;
        bool entered = false, closed = false, handler_started = false, complete = false;
        std::optional<Label> continue_target, break_target, normal_target;
    };
    std::vector<RegionSpec> regions_;
    std::vector<std::optional<RegionToken>> scopes_;
    std::optional<RegionToken> scope_;
    RegionSpec &require_region(Region region);
};
struct FunctionObject {
    ObjectHeader header;
    const Value code, globals, builtins;
    const std::optional<Value> defaults, kwdefaults, closure;
    const Value name, qualname;
    FunctionObject(InitialType head, Value code, Value globals, std::optional<Value> defaults,
                   std::optional<Value> kwdefaults, Value name, Value qualname, Value builtins,
                   std::optional<Value> closure)
        : header(head), code(code), globals(globals), builtins(builtins), defaults(defaults),
          kwdefaults(kwdefaults), closure(closure), name(name), qualname(qualname)
    {}
    void trace(Tracer &tracer) const noexcept;
};
struct FunctionSpec {
    Handle code, globals;
    std::optional<Handle> defaults = {}, kwdefaults = {};
    // Host execution environment; source/loader policy selects this mapping.
    std::optional<Handle> builtins = {};
    std::optional<Handle> closure = {};
};
enum class FrameState { Created, Running, Completed };
struct TracebackObject {
    ObjectHeader header;
    AtomicGcEdge next;
    const Value frame;
    const std::int64_t lasti, lineno;
    TracebackObject(InitialType head, std::optional<Value> next, Value frame, std::int64_t lasti,
                    std::int64_t lineno)
        : header(head), next(next), frame(frame), lasti(lasti), lineno(lineno)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(next);
        tracer.visit(frame);
    }
};
struct FrameObject {
    // Execution fields are writer-owned by this invocation's Attachment.
    // Cross-thread observation still requires N3 snapshots/STW, not raw reads.
    ObjectHeader header;
    const Value function, code;
    // Set once before execution can expose this frame to Python callbacks.
    std::optional<Value> back;
    std::vector<std::optional<Value>> locals;
    std::vector<Value> operands;
    const std::vector<Value> cells;
    const std::optional<Value> namespace_mapping;
    // Lazily refreshed reflection snapshot, never the fast-local/Cell authority.
    std::optional<Value> locals_snapshot;
    std::size_t pc = 0, sp = 0;
    std::atomic<std::size_t> last_instruction{std::numeric_limits<std::size_t>::max()};
    std::optional<std::size_t> pending_call;
    std::vector<ActiveRegion> regions;
    std::vector<CleanupContinuation> cleanups;
    std::optional<TracedReason> reason;
    std::optional<Value> handled_exception;
    FrameState state = FrameState::Created;
    FrameObject(InitialType head, Value function, Value code, std::size_t local_count,
                std::size_t stack_count, Value padding, std::size_t max_regions = 0,
                std::vector<Value> cells = {}, std::optional<Value> namespace_mapping = {})
        : header(head), function(function), code(code), locals(local_count),
          operands(stack_count, padding), cells(std::move(cells)),
          namespace_mapping(namespace_mapping)
    {
        regions.reserve(max_regions);
        cleanups.reserve(max_regions);
    }
    void trace(Tracer &tracer) const noexcept;
    void trim_stack(Attachment &context, std::size_t size) noexcept;
    bool unwind(Attachment &context);
    void finish_handler(Attachment &context);
    std::size_t external_bytes() const noexcept
    {
        return locals.capacity() * sizeof(std::optional<Value>) +
               (operands.capacity() + cells.capacity()) * sizeof(Value) +
               regions.capacity() * sizeof(ActiveRegion) +
               cleanups.capacity() * sizeof(CleanupContinuation);
    }
};
const OpcodeDefinition &opcode_definition(Opcode opcode);
StackEffect instruction_effect(Instruction instruction, std::span<const CallLayout> calls = {},
                               bool jump_taken = false);
std::string disassemble(Attachment &context, Handle code);
void make_function(Attachment &context, const FunctionSpec &spec, ReturnSlot &out);
namespace detail {
void exception_match(Attachment &context, Value exception, Value matcher, ReturnSlot &out);
void normalize_exception(Attachment &context, Handle value, ReturnSlot &out);
} // namespace detail
} // namespace cxxpython::core
