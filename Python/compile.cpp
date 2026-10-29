#include "internal/compiler.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>

namespace cxxpython::core::detail {
namespace {
class GraphPass final {
  public:
    explicit GraphPass(SourceProgram &program) : program_(program)
    {
        program_.graphs.resize(program_.scopes.size());
    }
    void build(const Suite &body)
    {
        function(0, body);
    }

  private:
    struct Loop {
        std::size_t test, done;
    };
    [[noreturn]] void error(Span span, std::string message) const
    {
        throw SourceError(program_.source, span, std::move(message));
    }
    FlowBlock &current()
    {
        return program_.graphs[scope_].blocks[block_];
    }
    std::size_t block()
    {
        auto &blocks = program_.graphs[scope_].blocks;
        blocks.emplace_back();
        return blocks.size() - 1;
    }
    void emit(Opcode opcode, Span span, std::uint32_t operand = 0, std::u32string name = {})
    {
        current().instructions.push_back({opcode, span, operand, std::move(name), {}, {}, {}});
    }
    void literal(Literal value, Span span)
    {
        emit(Opcode::LoadConst, span);
        current().instructions.back().literal = std::move(value);
    }
    void none(Span span)
    {
        literal({Literal::Kind::None, {}}, span);
    }
    void jump(std::size_t target, Span span)
    {
        current().exit = FlowBlock::Exit::Jump;
        current().target = target;
        current().span = span;
    }
    void branch(std::size_t yes, std::size_t no, Span span)
    {
        current().exit = FlowBlock::Exit::Branch;
        current().target = yes;
        current().alternative = no;
        current().span = span;
    }
    const SymbolInfo &symbol(const std::u32string &name) const
    {
        const auto &symbols = program_.scopes[scope_].symbols;
        auto found = std::find_if(symbols.begin(), symbols.end(),
                                  [&](const auto &entry) { return entry.name == name; });
        if (found == symbols.end())
            throw std::logic_error("scope pass omitted a name");
        return *found;
    }
    void name(const std::u32string &text, Span span, Opcode local, Opcode global)
    {
        const auto &binding = symbol(text);
        if (binding.classification == NameClass::Local)
            emit(local, span, static_cast<std::uint32_t>(*binding.slot));
        else if (binding.classification == NameClass::Cell ||
                 binding.classification == NameClass::Free)
            emit(local == Opcode::LoadFast    ? Opcode::LoadDeref
                 : local == Opcode::StoreFast ? Opcode::StoreDeref
                                              : Opcode::DeleteDeref,
                 span, static_cast<std::uint32_t>(*binding.slot));
        else
            emit(global, span, 0, text);
    }
    void test(const Expr &value, std::size_t yes, std::size_t no, bool preserve = false)
    {
        // Jump-context lowering must not repeat an observable __bool__.
        if (const auto *node = std::get_if<BoolExpr>(&value.node)) {
            const auto right = block();
            test(*node->left, node->conjunction ? right : yes, node->conjunction ? no : right,
                 preserve);
            block_ = right;
            if (preserve)
                emit(Opcode::PopTop, value.span);
            test(*node->right, yes, no, preserve);
            return;
        }
        if (const auto *node = std::get_if<CompareExpr>(&value.node)) {
            expression(*node->values[0]);
            for (std::size_t i = 0; i < node->ops.size(); ++i) {
                expression(*node->values[i + 1]);
                if (i + 1 < node->ops.size()) {
                    emit(Opcode::DupTop, value.span);
                    emit(Opcode::Swap, value.span, 3);
                    emit(Opcode::Swap, value.span, 2);
                }
                emit(Opcode::Compare, value.span, static_cast<std::uint32_t>(node->ops[i]));
                if (preserve)
                    emit(Opcode::DupTop, value.span);
                if (i + 1 == node->ops.size()) {
                    branch(yes, no, value.span);
                    return;
                }
                const auto again = block(), failed = block();
                branch(again, failed, value.span);
                block_ = failed;
                if (preserve)
                    emit(Opcode::Swap, value.span, 2);
                emit(Opcode::PopTop, value.span);
                jump(no, value.span);
                block_ = again;
                if (preserve)
                    emit(Opcode::PopTop, value.span);
            }
        } else {
            expression(value);
            if (preserve)
                emit(Opcode::DupTop, value.span);
            branch(yes, no, value.span);
        }
    }
    void expression(const Expr &value)
    {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Literal>)
                    literal(node, value.span);
                else if constexpr (std::is_same_v<T, NameExpr>) {
                    if (node.name == U"__debug__")
                        literal({Literal::Kind::True, {}}, value.span);
                    else
                        name(node.name, value.span, Opcode::LoadFast, Opcode::LoadGlobal);
                } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                    expression(*node.value);
                    emit(Opcode::Unary, value.span, static_cast<std::uint32_t>(node.op));
                } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                    expression(*node.left);
                    expression(*node.right);
                    emit(Opcode::Binary, value.span, static_cast<std::uint32_t>(node.op));
                } else if constexpr (std::is_same_v<T, AttributeExpr>) {
                    expression(*node.object);
                    emit(Opcode::LoadAttr, value.span, 0, node.name);
                } else if constexpr (std::is_same_v<T, TupleExpr>) {
                    for (const auto &item : node.items)
                        expression(*item);
                    emit(Opcode::BuildTuple, value.span,
                         static_cast<std::uint32_t>(node.items.size()));
                } else if constexpr (std::is_same_v<T, SubscriptExpr>) {
                    expression(*node.object);
                    expression(*node.key);
                    emit(Opcode::LoadItem, value.span);
                } else if constexpr (std::is_same_v<T, BoolExpr>) {
                    const auto right = block(), done = block();
                    test(*node.left, node.conjunction ? right : done,
                         node.conjunction ? done : right, true);
                    block_ = right;
                    emit(Opcode::PopTop, value.span);
                    expression(*node.right);
                    jump(done, value.span);
                    block_ = done;
                } else if constexpr (std::is_same_v<T, CompareExpr>) {
                    expression(*node.values[0]);
                    const auto done = block();
                    for (std::size_t i = 0; i < node.ops.size(); ++i) {
                        expression(*node.values[i + 1]);
                        if (i + 1 < node.ops.size()) {
                            emit(Opcode::DupTop, value.span);
                            emit(Opcode::Swap, value.span, 3);
                            emit(Opcode::Swap, value.span, 2);
                        }
                        emit(Opcode::Compare, value.span, static_cast<std::uint32_t>(node.ops[i]));
                        if (i + 1 < node.ops.size()) {
                            emit(Opcode::DupTop, value.span);
                            const auto again = block(), failed = block();
                            branch(again, failed, value.span);
                            block_ = failed;
                            emit(Opcode::Swap, value.span, 2);
                            emit(Opcode::PopTop, value.span);
                            jump(done, value.span);
                            block_ = again;
                            emit(Opcode::PopTop, value.span);
                        }
                    }
                    jump(done, value.span);
                    block_ = done;
                } else if constexpr (std::is_same_v<T, CallExpr>) {
                    expression(*node.target);
                    for (const auto &item : node.positional)
                        expression(*item);
                    for (const auto &item : node.keywords)
                        expression(*item.second);
                    emit(Opcode::Call, value.span,
                         static_cast<std::uint32_t>(node.positional.size()));
                    for (const auto &item : node.keywords)
                        current().instructions.back().keywords.push_back(item.first);
                } else
                    static_assert(sizeof(T) == 0, "CFG lowering missing expression");
            },
            value.node);
    }
    void store(const Expr &target, bool erase = false)
    {
        if (const auto *tuple = std::get_if<TupleExpr>(&target.node)) {
            if (!erase)
                emit(Opcode::UnpackSequence, target.span,
                     static_cast<std::uint32_t>(tuple->items.size()));
            for (const auto &item : tuple->items)
                store(*item, erase);
        } else if (const auto *entry = std::get_if<NameExpr>(&target.node))
            name(entry->name, target.span, erase ? Opcode::DeleteFast : Opcode::StoreFast,
                 erase ? Opcode::DeleteGlobal : Opcode::StoreGlobal);
        else if (const auto *item = std::get_if<SubscriptExpr>(&target.node)) {
            expression(*item->object);
            expression(*item->key);
            if (!erase) {
                emit(Opcode::Swap, target.span, 2);
                emit(Opcode::Swap, target.span, 3);
            }
            emit(erase ? Opcode::DeleteItem : Opcode::StoreItem, target.span);
        } else {
            const auto &attribute = std::get<AttributeExpr>(target.node);
            expression(*attribute.object);
            if (!erase)
                emit(Opcode::Swap, target.span, 2);
            emit(erase ? Opcode::DeleteAttr : Opcode::StoreAttr, target.span, 0, attribute.name);
        }
    }
    void suite(const Suite &body)
    {
        for (std::size_t index = 0; index < body.size(); ++index) {
            const auto &statement = body[index];
            if (!index && statement.get() == first_statement_) {
                if (scope_ == 0) {
                    literal({Literal::Kind::String, *program_.graphs[scope_].docstring},
                            statement->span);
                    emit(Opcode::StoreGlobal, statement->span, 0, U"__doc__");
                }
                continue;
            }
            if (current().exit != FlowBlock::Exit::Open)
                block_ = block();
            const auto span = statement->span;
            std::visit(
                [&](const auto &node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, ExprStmt>) {
                        expression(*node.value);
                        emit(Opcode::PopTop, span);
                    } else if constexpr (std::is_same_v<T, AssignStmt>) {
                        expression(*node.value);
                        for (std::size_t i = 0; i < node.targets.size(); ++i) {
                            if (i + 1 < node.targets.size())
                                emit(Opcode::DupTop, span);
                            store(*node.targets[i]);
                        }
                    } else if constexpr (std::is_same_v<T, DeleteStmt>) {
                        for (const auto &item : node.targets)
                            store(*item, true);
                    } else if constexpr (std::is_same_v<T, AugAssignStmt>) {
                        if (const auto *attribute =
                                std::get_if<AttributeExpr>(&node.target->node)) {
                            expression(*attribute->object);
                            emit(Opcode::DupTop, span);
                            emit(Opcode::LoadAttr, span, 0, attribute->name);
                        } else if (const auto *item =
                                       std::get_if<SubscriptExpr>(&node.target->node)) {
                            expression(*item->object);
                            expression(*item->key);
                            emit(Opcode::Copy, span, 2);
                            emit(Opcode::Copy, span, 2);
                            emit(Opcode::LoadItem, span);
                        } else
                            expression(*node.target);
                        expression(*node.value);
                        emit(Opcode::Inplace, span, static_cast<std::uint32_t>(node.op));
                        if (const auto *attribute = std::get_if<AttributeExpr>(&node.target->node))
                            emit(Opcode::StoreAttr, span, 0, attribute->name);
                        else if (std::holds_alternative<SubscriptExpr>(node.target->node))
                            emit(Opcode::StoreItem, span);
                        else
                            store(*node.target);
                    } else if constexpr (std::is_same_v<T, IfStmt>) {
                        const auto yes = block(), no = block(), done = block();
                        test(*node.test, yes, no);
                        block_ = yes;
                        suite(node.body);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        block_ = no;
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        block_ = done;
                    } else if constexpr (std::is_same_v<T, WhileStmt>) {
                        const auto test = block(), body = block(), exhausted = block(),
                                   done = block();
                        jump(test, span);
                        block_ = test;
                        this->test(*node.test, body, exhausted);
                        block_ = body;
                        loops_.push_back({test, done});
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        block_ = exhausted;
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        block_ = done;
                    } else if constexpr (std::is_same_v<T, ForStmt>) {
                        expression(*node.iterable);
                        emit(Opcode::GetIter, span);
                        const auto test = block(), body = block(), exhausted = block(),
                                   broken = block(), done = block();
                        jump(test, span);
                        block_ = test;
                        current().exit = FlowBlock::Exit::IterNext;
                        current().target = body;
                        current().alternative = exhausted;
                        current().span = span;
                        block_ = body;
                        store(*node.target);
                        loops_.push_back({test, broken});
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        block_ = broken;
                        emit(Opcode::PopTop, span);
                        jump(done, span);
                        block_ = exhausted;
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        block_ = done;
                    } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                        if (!scope_ && program_.mode != InputMode::Eval)
                            error(span, "'return' outside function");
                        if (node.value)
                            expression(*node.value);
                        else
                            none(span);
                        current().exit = FlowBlock::Exit::Return;
                        current().span = span;
                    } else if constexpr (std::is_same_v<T, ControlStmt>) {
                        if (node == ControlStmt::Pass)
                            return;
                        if (loops_.empty())
                            error(span, "'break'/'continue' outside loop");
                        jump(node == ControlStmt::Break ? loops_.back().done : loops_.back().test,
                             span);
                    } else if constexpr (std::is_same_v<T, FunctionStmt>) {
                        const auto parent_scope = scope_, parent_block = block_;
                        auto parent_loops = std::move(loops_);
                        const auto *parent_first = first_statement_;
                        function(node.scope, node.body, &node);
                        scope_ = parent_scope;
                        block_ = parent_block;
                        loops_ = std::move(parent_loops);
                        first_statement_ = parent_first;
                        std::uint32_t defaults = 0;
                        std::vector<std::u32string> keywords;
                        for (const auto &parameter : node.parameters)
                            if (parameter.default_value &&
                                parameter.kind != ParameterKind::KeywordOnly) {
                                expression(*parameter.default_value);
                                ++defaults;
                            }
                        if (defaults)
                            emit(Opcode::BuildTuple, span, defaults);
                        for (const auto &parameter : node.parameters)
                            if (parameter.default_value &&
                                parameter.kind == ParameterKind::KeywordOnly) {
                                expression(*parameter.default_value);
                                keywords.push_back(parameter.name);
                            }
                        if (!keywords.empty()) {
                            emit(Opcode::BuildNameDict, span);
                            current().instructions.back().keywords = keywords;
                        }
                        const auto &freevars = program_.scopes[node.scope].freevars;
                        for (const auto &name : freevars) {
                            const auto &binding = symbol(name);
                            if (binding.classification != NameClass::Cell &&
                                binding.classification != NameClass::Free)
                                throw std::logic_error("child free variable has no enclosing cell");
                            emit(Opcode::LoadClosure, span,
                                 static_cast<std::uint32_t>(*binding.slot));
                        }
                        if (!freevars.empty())
                            emit(Opcode::BuildTuple, span,
                                 static_cast<std::uint32_t>(freevars.size()));
                        emit(Opcode::LoadConst, span);
                        current().instructions.back().function = node.scope;
                        emit(Opcode::MakeFunction, span,
                             (defaults ? 1 : 0) | (!keywords.empty() ? 2 : 0) |
                                 (!freevars.empty() ? 4 : 0));
                        name(node.name, span, Opcode::StoreFast, Opcode::StoreGlobal);
                    } else
                        static_assert(std::is_same_v<T, DeclareStmt>,
                                      "CFG lowering missing statement");
                },
                statement->node);
        }
    }
    void function(std::size_t scope, const Suite &body, const FunctionStmt *definition = nullptr)
    {
        scope_ = scope;
        block_ = block();
        loops_.clear();
        first_statement_ = nullptr;
        if (!body.empty())
            if (const auto *expression = std::get_if<ExprStmt>(&body.front()->node))
                if (const auto *literal = std::get_if<Literal>(&expression->value->node);
                    literal && literal->kind == Literal::Kind::String) {
                    program_.graphs[scope].docstring = literal->text;
                    first_statement_ = body.front().get();
                }
        if (definition) {
            auto &signature = program_.graphs[scope].signature;
            for (const auto &parameter : definition->parameters)
                signature.parameters.push_back({parameter.name, parameter.kind});
            signature.varargs = definition->varargs;
            signature.varkwargs = definition->varkwargs;
        }
        suite(body);
        if (current().exit == FlowBlock::Exit::Open) {
            none(program_.scopes[scope].span);
            current().exit = FlowBlock::Exit::Return;
            current().span = program_.scopes[scope].span;
        }
    }
    SourceProgram &program_;
    std::size_t scope_ = 0, block_ = 0;
    std::vector<Loop> loops_;
    const Stmt *first_statement_ = nullptr;
};

void literal(Attachment &context, const Literal &value, ReturnSlot &out)
{
    if (value.kind == Literal::Kind::String) {
        make_str(context, value.text, out);
        return;
    }
    if (value.kind != Literal::Kind::Integer) {
        out.ok(context.builtin(value.kind == Literal::Kind::None   ? CoreObject::None
                               : value.kind == Literal::Kind::True ? CoreObject::True
                                                                   : CoreObject::False));
        return;
    }
    boost::multiprecision::cpp_int number = 0;
    unsigned base = 10;
    std::size_t begin = 0;
    if (value.text.size() > 1 && value.text[0] == U'0') {
        const auto ch = value.text[1];
        base = ch == U'x' || ch == U'X'   ? 16
               : ch == U'o' || ch == U'O' ? 8
               : ch == U'b' || ch == U'B' ? 2
                                          : 10;
        if (base != 10)
            begin = 2;
    }
    for (auto i = begin; i < value.text.size(); ++i) {
        const auto ch = value.text[i];
        if (ch == U'_')
            continue;
        const auto digit = ch <= U'9' ? ch - U'0' : ch <= U'F' ? ch - U'A' + 10 : ch - U'a' + 10;
        number *= base;
        number += digit;
        if (i % 64 == 63)
            context.poll();
    }
    make_int_decimal(context, number.str(), out);
}
} // namespace
} // namespace cxxpython::core::detail

namespace cxxpython::core {
SourceUnit::SourceUnit(SourceBuffer source, InputMode mode)
{
    auto program = std::make_unique<detail::SourceProgram>(
        detail::SourceProgram{std::move(source), mode, {}, {}});
    auto body = detail::parse_source(program->source, mode);
    detail::analyze_scopes(*program, body);
    detail::GraphPass(*program).build(body);
    program_ = std::move(program);
}
SourceUnit::~SourceUnit() = default;
SourceUnit::SourceUnit(SourceUnit &&) noexcept = default;
SourceUnit &SourceUnit::operator=(SourceUnit &&) noexcept = default;
const SourceBuffer &SourceUnit::source() const noexcept
{
    return program_->source;
}
std::span<const ScopeInfo> SourceUnit::scopes() const noexcept
{
    return program_->scopes;
}
void SourceUnit::freeze(Attachment &context, ReturnSlot &out) const
{
    out.reset(context);
    try {
        RootScope roots(context);
        ReturnSlot temporary(context);
        std::vector<Handle> codes(program_->graphs.size());
        const auto filename_text = detail::decode_filename(source());
        for (std::size_t id = codes.size(); id-- > 0;) {
            const auto &graph = program_->graphs[id];
            const auto &scope = program_->scopes[id];
            CodeSpec spec;
            spec.name = scope.name;
            spec.qualname = scope.qualname;
            spec.filename = filename_text;
            spec.first_line = source().position(scope.span).line;
            spec.locals = scope.locals;
            spec.signature = graph.signature;
            spec.docstring = graph.docstring;
            spec.cellvars = scope.cellvars;
            spec.freevars = scope.freevars;
            CodeBuilder builder(context, std::move(spec));
            std::vector<CodeBuilder::Label> labels;
            for (std::size_t i = 0; i < graph.blocks.size(); ++i)
                labels.push_back(builder.label());
            for (std::size_t i = 0; i < graph.blocks.size(); ++i) {
                builder.bind(labels[i]);
                const auto &block = graph.blocks[i];
                for (const auto &instruction : block.instructions) {
                    const auto position = source().position(instruction.span);
                    auto operand = instruction.operand;
                    if (instruction.literal) {
                        detail::literal(context, *instruction.literal, temporary);
                        operand = builder.constant(roots.root(temporary.value()));
                    } else if (instruction.function)
                        operand = builder.constant(codes[*instruction.function]);
                    else if (!instruction.name.empty())
                        operand = builder.name(instruction.name);
                    if (instruction.opcode == Opcode::Call)
                        builder.call(operand, instruction.keywords, position);
                    else if (instruction.opcode == Opcode::BuildNameDict)
                        builder.name_dict(instruction.keywords, position);
                    else
                        builder.emit(instruction.opcode, operand, position);
                }
                const auto position = source().position(block.span);
                if (block.exit == detail::FlowBlock::Exit::Return)
                    builder.emit(Opcode::Return, 0, position);
                else if (block.exit == detail::FlowBlock::Exit::Jump)
                    builder.jump(Opcode::Jump, labels[block.target], position);
                else if (block.exit == detail::FlowBlock::Exit::IterNext) {
                    builder.jump(Opcode::ForIter, labels[block.alternative], position);
                    builder.jump(Opcode::Jump, labels[block.target], position);
                } else if (block.exit == detail::FlowBlock::Exit::Branch) {
                    builder.jump(Opcode::JumpIfFalse, labels[block.alternative], position);
                    builder.jump(Opcode::Jump, labels[block.target], position);
                } else
                    throw std::logic_error("compiler left a CFG block unterminated");
            }
            builder.freeze(temporary);
            codes[id] = roots.root(temporary.value());
        }
        out.ok(codes[0].get(context));
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
}
ResultView run(Attachment &context, const RunRequest &request, ReturnSlot &out)
{
    out.check_caller(context);
    RootScope roots(context);
    ReturnSlot compiled(context), environment(context), temporary(context);
    std::optional<Handle> input_code, input_globals, input_builtins;
    if (const auto *code = std::get_if<Handle>(&request.program))
        input_code = roots.root(code->get(context));
    if (request.globals)
        input_globals = roots.root(request.globals->get(context));
    if (request.builtins)
        input_builtins = roots.root(request.builtins->get(context));
    out.reset(context);
    try {
        if (const auto *source = std::get_if<const SourceUnit *>(&request.program)) {
            if (!*source)
                throw std::invalid_argument("RunRequest source is null");
            (*source)->freeze(context, compiled);
            if (compiled.tag() == ResultTag::Raised) {
                out.raised(compiled.exception());
                return out.result();
            }
        } else
            compiled.ok(input_code->get(context));
        if (input_globals)
            environment.ok(input_globals->get(context));
        else
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                                  environment);
        const auto globals = roots.root(environment.value());
        auto dictionary = [&](Value value) {
            return context.has_layout<DictObject>(value) &&
                   is_subtype(context, context.type_of(value),
                              context.builtin(CoreObject::DictType));
        };
        if (!dictionary(globals.get(context))) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"RunRequest globals must be a name dictionary", out);
            return out.result();
        }
        auto builtins = roots.root(input_builtins ? input_builtins->get(context)
                                                  : context.builtin(CoreObject::Builtins));
        if (!input_builtins)
            if (auto supplied = detail::name_lookup(context, globals.get(context), U"__builtins__"))
                builtins = roots.root(*supplied);
        if (!dictionary(builtins.get(context))) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"RunRequest requires globals/builtins name dictionaries", out);
            return out.result();
        }
        if (!detail::name_lookup(context, globals.get(context), U"__builtins__")) {
            make_str(context, U"__builtins__", temporary);
            detail::name_store(context, globals.get(context), temporary.value(),
                               builtins.get(context));
        }
        if (!input_globals) {
            make_str(context, U"__name__", temporary);
            const auto name = roots.root(temporary.value());
            make_str(context, U"__main__", temporary);
            detail::name_store(context, globals.get(context), name.get(context), temporary.value());
        }
        make_function(context, {roots.root(compiled.value()), globals, {}, {}, builtins},
                      temporary);
        if (temporary.tag() == ResultTag::Raised)
            out.raised(temporary.exception());
        else
            call(context, roots.root(temporary.value()), {}, out);
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
} // namespace cxxpython::core
