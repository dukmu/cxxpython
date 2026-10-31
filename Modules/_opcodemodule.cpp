// Inspection of cxxpython's private verified bytecode, not CPython wordcode.
#include "internal/opcodemodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/moduleobject.hpp"
#include "internal/errors.hpp"

namespace cxxpython::core::detail {
namespace {
std::string_view domain_name(OperandDomain domain)
{
    switch (domain) {
    case OperandDomain::None:
        return "None";
    case OperandDomain::Constant:
        return "Constant";
    case OperandDomain::Local:
        return "Local";
    case OperandDomain::Name:
        return "Name";
    case OperandDomain::Binary:
        return "Binary";
    case OperandDomain::Compare:
        return "Compare";
    case OperandDomain::Target:
        return "Target";
    case OperandDomain::CallSite:
        return "CallSite";
    case OperandDomain::Region:
        return "Region";
    case OperandDomain::FunctionFlags:
        return "FunctionFlags";
    case OperandDomain::Unary:
        return "Unary";
    case OperandDomain::StackDepth:
        return "StackDepth";
    case OperandDomain::Count:
        return "Count";
    case OperandDomain::Cell:
        return "Cell";
    case OperandDomain::RaiseArity:
        return "RaiseArity";
    case OperandDomain::UnpackCounts:
        return "UnpackCounts";
    case OperandDomain::Conversion:
        return "Conversion";
    }
    return "Unknown";
}

std::string_view flow_name(FlowKind flow)
{
    switch (flow) {
    case FlowKind::Next:
        return "Next";
    case FlowKind::Jump:
        return "Jump";
    case FlowKind::Branch:
        return "Branch";
    case FlowKind::Stop:
        return "Stop";
    case FlowKind::Leave:
        return "Leave";
    case FlowKind::EndHandler:
        return "EndHandler";
    case FlowKind::LoopControl:
        return "LoopControl";
    case FlowKind::IterBranch:
        return "IterBranch";
    }
    return "Unknown";
}

// Every intermediate is rooted until the containing tuple has been published.
struct TupleBuilder {
    Attachment &c;
    RootScope roots;
    std::vector<Handle> fields;
    explicit TupleBuilder(Attachment &context) : c(context), roots(context) {}
    void number(std::int64_t n)
    {
        ReturnSlot value(c);
        make_int(c, n, value);
        fields.push_back(roots.root(value.value()));
    }
    void text(std::string_view s)
    {
        ReturnSlot value(c);
        make_str(c, std::u32string(s.begin(), s.end()), value);
        fields.push_back(roots.root(value.value()));
    }
    void finish(ReturnSlot &out)
    {
        std::vector<Value> values;
        for (auto field : fields)
            values.push_back(field.get(c));
        make_tuple(c, std::span<const Value>(values), out);
    }
};

void metadata(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> rows;
    for (unsigned i = 0; i < static_cast<unsigned>(Opcode::Count); ++i) {
        const auto &d = opcode_definition(static_cast<Opcode>(i));
        TupleBuilder row(c);
        row.number(i);
        row.text(d.name);
        row.text(domain_name(d.operand));
        row.text(flow_name(d.flow));
        ReturnSlot result(c);
        row.finish(result);
        rows.push_back(roots.root(result.value()));
    }
    std::vector<Value> values;
    for (auto row : rows)
        values.push_back(row.get(c));
    make_tuple(c, std::span<const Value>(values), out);
}

void code_info(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto code = roots.root(args[0]);
    if (c.type_of(code) != c.builtin(CoreObject::CodeType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected a code object", out);
        return;
    }
    const auto &program = c.payload<CodeObject>(code).program;
    const auto &signature = program.signature();
    std::size_t positional = 0, positional_only = 0, keyword_only = 0;
    for (const auto &parameter : signature.parameters) {
        if (parameter.kind == ParameterKind::KeywordOnly)
            ++keyword_only;
        else {
            ++positional;
            if (parameter.kind == ParameterKind::PositionalOnly)
                ++positional_only;
        }
    }
    TupleBuilder result(c);
    result.number(positional);
    result.number(positional_only);
    result.number(keyword_only);
    result.number(signature.varargs.has_value());
    result.number(signature.varkwargs.has_value());
    result.number(program.is_generator());
    result.number(program.is_coroutine());
    result.finish(out);
}

void local_names(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto code = roots.root(args[0]);
    if (c.type_of(code) != c.builtin(CoreObject::CodeType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected a code object", out);
        return;
    }
    std::vector<Value> names;
    ReturnSlot name(c);
    for (const auto &text : c.payload<CodeObject>(code).program.local_names()) {
        make_str(c, text, name);
        names.push_back(roots.root(name.value()).get(c));
    }
    make_tuple(c, std::span<const Value>(names), out);
}

void instructions(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto code = roots.root(args[0]);
    if (c.type_of(code) != c.builtin(CoreObject::CodeType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected a code object", out);
        return;
    }
    const auto &program = c.payload<CodeObject>(code).program;
    std::vector<Handle> rows;
    for (std::size_t pc = 0; pc < program.instructions().size(); ++pc) {
        const auto instruction = program.instructions()[pc];
        const auto position = program.positions()[pc];
        const auto fall = instruction_effect(instruction, program.calls(), false);
        const auto jump = instruction_effect(instruction, program.calls(), true);
        TupleBuilder row(c);
        row.number(static_cast<std::int64_t>(pc));
        row.number(static_cast<int>(instruction.opcode));
        row.number(instruction.operand);
        row.number(position.line);
        row.number(position.end_line);
        row.number(position.column);
        row.number(position.end_column);
        row.number(static_cast<std::int64_t>(fall.pushes) - static_cast<std::int64_t>(fall.pops));
        row.number(static_cast<std::int64_t>(jump.pushes) - static_cast<std::int64_t>(jump.pops));
        ReturnSlot result(c);
        row.finish(result);
        rows.push_back(roots.root(result.value()));
    }
    std::vector<Value> values;
    for (auto row : rows)
        values.push_back(row.get(c));
    make_tuple(c, std::span<const Value>(values), out);
}

void effect(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    for (auto arg : args) {
        if (c.type_of(arg) != c.builtin(CoreObject::IntType)) {
            raise_error(c, CoreObject::TypeErrorType, U"opcode and operand must be integers", out);
            return;
        }
    }
    std::int64_t op, operand, taken;
    if (!int_fits_signed(c, args[0], op) || !int_fits_signed(c, args[1], operand) ||
        !int_fits_signed(c, args[2], taken) || op < 0 ||
        op >= static_cast<unsigned>(Opcode::Count) || operand < 0 || operand > UINT32_MAX) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid opcode or operand", out);
        return;
    }
    const auto opcode = static_cast<Opcode>(op);
    if (opcode_definition(opcode).operand == OperandDomain::CallSite) {
        raise_error(c, CoreObject::ValueErrorType, U"call stack effect requires a code object",
                    out);
        return;
    }
    try {
        const auto result =
            instruction_effect({opcode, static_cast<std::uint32_t>(operand)}, {}, taken != 0);
        make_int(c,
                 static_cast<std::int64_t>(result.pushes) - static_cast<std::int64_t>(result.pops),
                 out);
    } catch (const CodeValidationError &) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid opcode operand", out);
    }
}
} // namespace

void initialize_opcode(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dict = roots.root(module_dict(c, module.get(c)));
    struct Entry {
        const char *name;
        void (*function)(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
        unsigned arity;
    };
    for (const auto &entry :
         {Entry{"metadata", metadata, 0}, Entry{"instructions", instructions, 1},
          Entry{"code_info", code_info, 1}, Entry{"local_names", local_names, 1},
          Entry{"stack_effect", effect, 3}}) {
        SignatureLayout signature;
        for (unsigned i = 0; i < entry.arity; ++i)
            signature.parameters.push_back(
                {std::u32string(1, U'a' + i), ParameterKind::PositionalOnly});
        ReturnSlot function(c), key(c);
        make_native_function(c, entry.name, signature, entry.function, {}, function,
                             NativeFunctionBinding::Function);
        if (function.tag() == ResultTag::Raised) {
            out.raised(function.exception());
            return;
        }
        auto rooted = roots.root(function.value());
        make_str(
            c, std::u32string(entry.name, entry.name + std::char_traits<char>::length(entry.name)),
            key);
        dict_store(c, dict, roots.root(key.value()), rooted, out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
