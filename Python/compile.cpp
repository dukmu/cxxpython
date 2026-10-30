#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
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
    void select(std::size_t id)
    {
        block_ = id;
        program_.graphs[scope_].order.push_back(id);
    }
    std::size_t region(RegionKind kind, Span span, std::optional<std::size_t> continued = {},
                       std::optional<std::size_t> broken = {})
    {
        auto &regions = program_.graphs[scope_].regions;
        const auto id = regions.size();
        regions.push_back({kind, continued, broken, {}});
        emit(Opcode::EnterRegion, span, static_cast<std::uint32_t>(id));
        return id;
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
        if (binding.classification == NameClass::Namespace ||
            (program_.scopes[scope_].kind == ScopeKind::Class &&
             binding.classification == NameClass::GlobalImplicit))
            emit(local == Opcode::LoadFast    ? Opcode::LoadName
                 : local == Opcode::StoreFast ? Opcode::StoreName
                                              : Opcode::DeleteName,
                 span, 0, text);
        else if (binding.classification == NameClass::Local)
            emit(local, span, static_cast<std::uint32_t>(*binding.slot));
        else if (binding.classification == NameClass::Cell ||
                 binding.classification == NameClass::Free)
            emit(local == Opcode::LoadFast
                     ? (program_.scopes[scope_].kind == ScopeKind::Class ? Opcode::LoadClassDeref
                                                                         : Opcode::LoadDeref)
                 : local == Opcode::StoreFast ? Opcode::StoreDeref
                                              : Opcode::DeleteDeref,
                 span, static_cast<std::uint32_t>(*binding.slot));
        else
            emit(global, span, 0, text);
    }
    void nested(std::size_t scope, const Suite &body, const FunctionStmt *definition = nullptr)
    {
        const auto parent_scope = scope_, parent_block = block_;
        auto parent_loops = std::move(loops_);
        const auto *parent_first = first_statement_;
        function(scope, body, definition);
        scope_ = parent_scope;
        block_ = parent_block;
        loops_ = std::move(parent_loops);
        first_statement_ = parent_first;
    }
    void make_function(std::size_t child, Span span, std::uint32_t flags = 0)
    {
        const auto &parent = program_.scopes[scope_];
        const auto &freevars = program_.scopes[child].freevars;
        for (const auto &name : freevars) {
            auto cell = std::find(parent.cellvars.begin(), parent.cellvars.end(), name);
            std::size_t slot = cell - parent.cellvars.begin();
            if (cell == parent.cellvars.end()) {
                const auto free = std::find(parent.freevars.begin(), parent.freevars.end(), name);
                if (free == parent.freevars.end())
                    throw std::logic_error("child free variable has no enclosing cell");
                slot += free - parent.freevars.begin();
            }
            emit(Opcode::LoadClosure, span, static_cast<std::uint32_t>(slot));
        }
        if (!freevars.empty())
            emit(Opcode::BuildTuple, span, static_cast<std::uint32_t>(freevars.size()));
        emit(Opcode::LoadConst, span);
        current().instructions.back().function = child;
        emit(Opcode::MakeFunction, span, flags | (!freevars.empty() ? 4 : 0));
    }
    void test(const Expr &value, std::size_t yes, std::size_t no, bool preserve = false)
    {
        // Jump-context lowering must not repeat an observable __bool__.
        if (const auto *node = std::get_if<BoolExpr>(&value.node)) {
            const auto right = block();
            test(*node->left, node->conjunction ? right : yes, node->conjunction ? no : right,
                 preserve);
            select(right);
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
                select(failed);
                if (preserve)
                    emit(Opcode::Swap, value.span, 2);
                emit(Opcode::PopTop, value.span);
                jump(no, value.span);
                select(again);
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
                } else if constexpr (std::is_same_v<T, TupleExpr> || std::is_same_v<T, ListExpr>) {
                    for (const auto &item : node.items)
                        expression(*item);
                    emit(std::is_same_v<T, TupleExpr> ? Opcode::BuildTuple : Opcode::BuildList,
                         value.span, static_cast<std::uint32_t>(node.items.size()));
                } else if constexpr (std::is_same_v<T, SubscriptExpr>) {
                    expression(*node.object);
                    expression(*node.key);
                    emit(Opcode::LoadItem, value.span);
                } else if constexpr (std::is_same_v<T, BoolExpr>) {
                    const auto right = block(), done = block();
                    test(*node.left, node.conjunction ? right : done,
                         node.conjunction ? done : right, true);
                    select(right);
                    emit(Opcode::PopTop, value.span);
                    expression(*node.right);
                    jump(done, value.span);
                    select(done);
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
                            select(failed);
                            emit(Opcode::Swap, value.span, 2);
                            emit(Opcode::PopTop, value.span);
                            jump(done, value.span);
                            select(again);
                            emit(Opcode::PopTop, value.span);
                        }
                    }
                    jump(done, value.span);
                    select(done);
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
        } else if (const auto *list = std::get_if<ListExpr>(&target.node)) {
            if (!erase)
                emit(Opcode::UnpackSequence, target.span,
                     static_cast<std::uint32_t>(list->items.size()));
            for (const auto &item : list->items)
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
    void close_protected(std::size_t id, Span span)
    {
        const auto leave = block();
        if (current().exit == FlowBlock::Exit::Open)
            jump(leave, span);
        select(leave);
        current().exit = FlowBlock::Exit::Leave;
        current().target = id;
        current().span = span;
    }
    void close_handler(std::size_t id, Span span)
    {
        const auto end = block();
        if (current().exit == FlowBlock::Exit::Open)
            jump(end, span);
        select(end);
        current().exit = FlowBlock::Exit::EndHandler;
        current().target = id;
        current().span = span;
    }
    void finally_handler(std::size_t id, const Suite &cleanup, Span span)
    {
        close_protected(id, span);
        select(block());
        current().handler = id;
        suite(cleanup);
        close_handler(id, span);
        select(block());
    }
    void try_except(const TryStmt &node, Span span)
    {
        if (node.handlers.empty()) {
            suite(node.body);
            return;
        }
        const auto id = region(RegionKind::Except, span);
        const auto normal = block(), done = block(), finished = block();
        program_.graphs[scope_].regions[id].normal_target = normal;
        suite(node.body);
        close_protected(id, span);
        select(block());
        current().handler = id;
        for (const auto &handler : node.handlers) {
            std::optional<std::size_t> next;
            if (handler.type) {
                emit(Opcode::DupTop, handler.span);
                expression(*handler.type);
                emit(Opcode::ExceptionMatch, handler.span);
                const auto matched = block();
                next = block();
                branch(matched, *next, handler.span);
                select(matched);
            }
            if (handler.name) {
                const auto &[text, location] = *handler.name;
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
                const auto cleanup = region(RegionKind::Finally, handler.span);
                suite(handler.body);
                close_protected(cleanup, handler.span);
                select(block());
                current().handler = cleanup;
                none(location);
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
                name(text, location, Opcode::DeleteFast, Opcode::DeleteGlobal);
                close_handler(cleanup, handler.span);
                select(block());
            } else {
                emit(Opcode::PopTop, handler.span);
                suite(handler.body);
            }
            if (current().exit == FlowBlock::Exit::Open)
                jump(finished, handler.span);
            if (next)
                select(*next);
            else
                break;
        }
        if (node.handlers.back().type) {
            emit(Opcode::Reraise, span);
            current().exit = FlowBlock::Exit::Stop;
        }
        select(finished);
        current().exit = FlowBlock::Exit::EndHandler;
        current().target = id;
        current().span = span;
        select(block());
        jump(done, span);
        select(normal);
        suite(node.alternative);
        if (current().exit == FlowBlock::Exit::Open)
            jump(done, span);
        select(done);
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
                select(block());
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
                        select(yes);
                        suite(node.body);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(no);
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, WhileStmt>) {
                        const auto test = block(), body = block(), exhausted = block(),
                                   done = block();
                        const auto loop = region(RegionKind::Loop, span, test, done);
                        jump(test, span);
                        select(test);
                        this->test(*node.test, body, exhausted);
                        select(body);
                        loops_.push_back(loop);
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        select(exhausted);
                        current().exit = FlowBlock::Exit::Leave;
                        current().target = loop;
                        current().span = span;
                        select(block());
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, ForStmt>) {
                        expression(*node.iterable);
                        emit(Opcode::GetIter, span);
                        const auto test = block(), body = block(), exhausted = block(),
                                   broken = block(), done = block();
                        const auto loop = region(RegionKind::Loop, span, test, broken);
                        jump(test, span);
                        select(test);
                        current().exit = FlowBlock::Exit::IterNext;
                        current().target = body;
                        current().alternative = exhausted;
                        current().span = span;
                        select(body);
                        store(*node.target);
                        loops_.push_back(loop);
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        select(exhausted);
                        current().exit = FlowBlock::Exit::Leave;
                        current().target = loop;
                        current().span = span;
                        select(block());
                        emit(Opcode::PopTop, span);
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(broken);
                        emit(Opcode::PopTop, span);
                        jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                        if (program_.scopes[scope_].kind != ScopeKind::Function &&
                            program_.mode != InputMode::Eval)
                            error(span, "'return' outside function");
                        if (node.value)
                            expression(*node.value);
                        else
                            none(span);
                        current().exit = FlowBlock::Exit::Return;
                        current().span = span;
                    } else if constexpr (std::is_same_v<T, RaiseStmt>) {
                        if (node.exception) {
                            expression(*node.exception);
                            if (node.cause)
                                expression(*node.cause);
                            emit(Opcode::Raise, span, node.cause ? 2 : 1);
                        } else
                            emit(Opcode::Reraise, span);
                        current().exit = FlowBlock::Exit::Stop;
                    } else if constexpr (std::is_same_v<T, TryStmt>) {
                        if (node.cleanup.empty())
                            try_except(node, span);
                        else {
                            const auto id = region(RegionKind::Finally, span);
                            try_except(node, span);
                            finally_handler(id, node.cleanup, span);
                        }
                    } else if constexpr (std::is_same_v<T, WithStmt>) {
                        std::vector<std::size_t> managers;
                        for (const auto &item : node.items) {
                            expression(*item.context);
                            managers.push_back(region(RegionKind::With, item.context->span));
                            if (item.target)
                                store(*item.target);
                            else
                                emit(Opcode::PopTop, span);
                        }
                        suite(node.body);
                        for (auto i = managers.size(); i-- > 0;) {
                            close_protected(managers[i], span);
                            select(block());
                        }
                    } else if constexpr (std::is_same_v<T, ControlStmt>) {
                        if (node == ControlStmt::Pass)
                            return;
                        if (loops_.empty())
                            error(span, "'break'/'continue' outside loop");
                        emit(node == ControlStmt::Break ? Opcode::BreakLoop : Opcode::ContinueLoop,
                             span, static_cast<std::uint32_t>(loops_.back()));
                        current().exit = FlowBlock::Exit::Stop;
                    } else if constexpr (std::is_same_v<T, FunctionStmt>) {
                        nested(node.scope, node.body, &node);
                        for (const auto &decorator : node.decorators)
                            expression(*decorator);
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
                        make_function(node.scope, span,
                                      (defaults ? 1 : 0) | (!keywords.empty() ? 2 : 0));
                        for (std::size_t i = 0; i < node.decorators.size(); ++i)
                            emit(Opcode::Call, span, 1);
                        name(node.binding, span, Opcode::StoreFast, Opcode::StoreGlobal);
                    } else if constexpr (std::is_same_v<T, ImportStmt>) {
                        auto level = std::to_string(node.level);
                        if (node.module) {
                            literal({Literal::Kind::Integer,
                                     std::u32string(level.begin(), level.end())},
                                    span);
                            for (const auto &alias : node.names)
                                literal({Literal::Kind::String, alias.name}, alias.span);
                            emit(Opcode::BuildTuple, span,
                                 static_cast<std::uint32_t>(node.names.size()));
                            emit(Opcode::ImportName, span, 0,
                                 mangle_name(*node.module, node.private_name));
                            for (const auto &alias : node.names) {
                                if (alias.name == U"*") {
                                    emit(Opcode::ImportStar, alias.span);
                                    return;
                                }
                                emit(Opcode::ImportFrom, alias.span, 0,
                                     mangle_name(alias.name, node.private_name));
                                name(alias.binding, alias.span, Opcode::StoreFast,
                                     Opcode::StoreGlobal);
                            }
                            emit(Opcode::PopTop, span);
                        } else {
                            for (const auto &alias : node.names) {
                                literal({Literal::Kind::Integer, U"0"}, alias.span);
                                none(alias.span);
                                emit(Opcode::ImportName, alias.span, 0,
                                     mangle_name(alias.name, node.private_name));
                                auto dot = alias.name.find(U'.');
                                const bool traversed = alias.asname && dot != alias.name.npos;
                                if (traversed) {
                                    do {
                                        const auto begin = dot + 1;
                                        dot = alias.name.find(U'.', begin);
                                        emit(Opcode::ImportFrom, alias.span, 0,
                                             mangle_name(
                                                 alias.name.substr(begin, dot == alias.name.npos
                                                                              ? dot
                                                                              : dot - begin),
                                                 node.private_name));
                                        if (dot != alias.name.npos) {
                                            emit(Opcode::Swap, alias.span, 2);
                                            emit(Opcode::PopTop, alias.span);
                                        }
                                    } while (dot != alias.name.npos);
                                }
                                name(alias.binding, alias.span, Opcode::StoreFast,
                                     Opcode::StoreGlobal);
                                if (traversed)
                                    emit(Opcode::PopTop, alias.span);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, ClassStmt>) {
                        nested(node.scope, node.body);
                        for (const auto &decorator : node.decorators)
                            expression(*decorator);
                        emit(Opcode::LoadBuildClass, span);
                        make_function(node.scope, span);
                        literal({Literal::Kind::String, node.name}, span);
                        for (const auto &base : node.bases)
                            expression(*base);
                        for (const auto &keyword : node.keywords)
                            expression(*keyword.second);
                        emit(Opcode::Call, span, static_cast<std::uint32_t>(node.bases.size() + 2));
                        for (const auto &keyword : node.keywords)
                            current().instructions.back().keywords.push_back(keyword.first);
                        for (std::size_t i = 0; i < node.decorators.size(); ++i)
                            emit(Opcode::Call, span, 1);
                        name(node.binding, span, Opcode::StoreFast, Opcode::StoreGlobal);
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
        select(block());
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
        const auto &info = program_.scopes[scope];
        if (info.kind == ScopeKind::Class) {
            emit(Opcode::LoadName, info.span, 0, U"__name__");
            emit(Opcode::StoreName, info.span, 0, U"__module__");
            literal({Literal::Kind::String, info.qualname}, info.span);
            emit(Opcode::StoreName, info.span, 0, U"__qualname__");
            if (const auto &docstring = program_.graphs[scope].docstring) {
                literal({Literal::Kind::String, *docstring}, info.span);
                emit(Opcode::StoreName, info.span, 0, U"__doc__");
            }
        }
        suite(body);
        if (current().exit == FlowBlock::Exit::Open) {
            if (info.kind == ScopeKind::Class && !info.cellvars.empty()) {
                emit(Opcode::LoadClosure, info.span, 0);
                emit(Opcode::DupTop, info.span);
                emit(Opcode::StoreName, info.span, 0, U"__classcell__");
            } else
                none(info.span);
            current().exit = FlowBlock::Exit::Return;
            current().span = program_.scopes[scope].span;
        }
    }
    SourceProgram &program_;
    std::size_t scope_ = 0, block_ = 0;
    std::vector<std::size_t> loops_;
    const Stmt *first_statement_ = nullptr;
};

void literal(Attachment &context, const Literal &value, ReturnSlot &out)
{
    if (value.kind == Literal::Kind::Float) {
        make_float(context, value.number, out);
        return;
    }
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
            spec.kind = scope.kind == ScopeKind::Class    ? CodeKind::ClassBody
                        : scope.kind == ScopeKind::Module ? CodeKind::Module
                                                          : CodeKind::Function;
            CodeBuilder builder(context, std::move(spec));
            std::vector<CodeBuilder::Label> labels;
            for (std::size_t i = 0; i < graph.blocks.size(); ++i)
                labels.push_back(builder.label());
            std::vector<CodeBuilder::Region> regions;
            for (const auto &region : graph.regions)
                regions.push_back(region.kind == RegionKind::Loop
                                      ? builder.loop(labels[*region.continue_target],
                                                     labels[*region.break_target])
                                      : builder.region(region.kind));
            for (auto i : graph.order) {
                const auto &block = graph.blocks[i];
                if (block.handler)
                    builder.handler(regions[*block.handler]);
                builder.bind(labels[i]);
                for (const auto &instruction : block.instructions) {
                    const auto position = source().position(instruction.span);
                    auto operand = instruction.operand;
                    if (instruction.literal) {
                        detail::literal(context, *instruction.literal, temporary);
                        operand = builder.constant(roots.root(temporary.value()));
                    } else if (instruction.function)
                        operand = builder.constant(codes[*instruction.function]);
                    else if (opcode_definition(instruction.opcode).operand == OperandDomain::Name)
                        operand = builder.name(instruction.name);
                    if (instruction.opcode == Opcode::EnterRegion)
                        builder.enter(regions[operand], position);
                    else if (instruction.opcode == Opcode::BreakLoop ||
                             instruction.opcode == Opcode::ContinueLoop)
                        builder.control(instruction.opcode, regions[operand]);
                    else if (instruction.opcode == Opcode::Call)
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
                } else if (block.exit == detail::FlowBlock::Exit::Leave) {
                    if (const auto normal = graph.regions[block.target].normal_target)
                        builder.leave(regions[block.target], labels[*normal], position);
                    else
                        builder.leave(regions[block.target], position);
                } else if (block.exit == detail::FlowBlock::Exit::EndHandler)
                    builder.end_handler(regions[block.target]);
                else if (block.exit != detail::FlowBlock::Exit::Stop)
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
        ReturnSlot builtin_key(context), supplied(context);
        make_str(context, U"__builtins__", builtin_key);
        const auto key = roots.root(builtin_key.value());
        dict_lookup(context, globals, key, supplied);
        if (supplied.tag() == ResultTag::Raised) {
            out.raised(supplied.exception());
            return out.result();
        }
        if (!input_builtins && supplied.tag() == ResultTag::Ok)
            builtins = roots.root(supplied.value());
        if (is_module(context, builtins.get(context)))
            builtins = roots.root(module_dict(context, builtins.get(context)));
        if (!dictionary(builtins.get(context))) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"RunRequest requires globals/builtins name dictionaries", out);
            return out.result();
        }
        if (supplied.tag() == ResultTag::Empty) {
            dict_store(context, globals, key, builtins, out);
            if (out.tag() == ResultTag::Raised)
                return out.result();
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
