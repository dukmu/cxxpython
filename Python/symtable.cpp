#include "internal/compiler.hpp"

#include <algorithm>

namespace cxxpython::core::detail {
namespace {
struct Binding {
    std::u32string name;
    Span first;
    bool bound = false, used = false, parameter = false, global = false, nonlocal = false;
    NameClass classification = NameClass::GlobalImplicit;
};
struct ScopeBindings {
    std::vector<Binding> entries;
    Binding &get(const std::u32string &name, Span span)
    {
        auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry) { return entry.name == name; });
        if (found != entries.end())
            return *found;
        entries.push_back({name, span});
        return entries.back();
    }
    Binding *find(const std::u32string &name)
    {
        auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry) { return entry.name == name; });
        return found == entries.end() ? nullptr : &*found;
    }
};
class ScopePass final {
  public:
    ScopePass(SourceProgram &program, Suite &body) : program_(program), body_(body) {}
    void analyze()
    {
        add(ScopeKind::Module, U"<module>", U"<module>", {}, {0, program_.source.text().size()});
        collect(body_, 0);
        for (std::size_t scope = 0; scope < bindings_.size(); ++scope)
            for (auto &entry : bindings_[scope].entries)
                entry.classification = entry.global ? NameClass::GlobalExplicit
                                       : program_.scopes[scope].kind == ScopeKind::Function &&
                                               entry.bound && !entry.nonlocal
                                           ? NameClass::Local
                                           : NameClass::GlobalImplicit;
        for (std::size_t scope = 1; scope < bindings_.size(); ++scope) {
            const auto count = bindings_[scope].entries.size();
            for (std::size_t i = 0; i < count; ++i) {
                const auto entry = bindings_[scope].entries[i];
                if (entry.global || (entry.bound && !entry.nonlocal))
                    continue;
                auto parent = program_.scopes[scope].parent;
                std::optional<std::size_t> owner;
                while (parent && program_.scopes[*parent].kind == ScopeKind::Function) {
                    const auto *candidate = bindings_[*parent].find(entry.name);
                    if (candidate && candidate->global)
                        break;
                    if (candidate && candidate->bound && !candidate->nonlocal) {
                        owner = parent;
                        break;
                    }
                    parent = program_.scopes[*parent].parent;
                }
                if (!owner) {
                    if (entry.nonlocal)
                        error(entry.first, "no binding for nonlocal name found");
                    continue;
                }
                bindings_[*owner].find(entry.name)->classification = NameClass::Cell;
                for (auto cursor = std::optional<std::size_t>{scope}; cursor != owner;
                     cursor = program_.scopes[*cursor].parent)
                    bindings_[*cursor].get(entry.name, entry.first).classification =
                        NameClass::Free;
            }
        }
        for (std::size_t scope = 0; scope < bindings_.size(); ++scope) {
            auto &info = program_.scopes[scope];
            for (const auto &entry : bindings_[scope].entries) {
                if (entry.classification == NameClass::Local ||
                    entry.classification == NameClass::Cell)
                    info.locals.push_back(entry.name);
                if (entry.classification == NameClass::Cell && entry.parameter)
                    info.cellvars.push_back(entry.name);
                if (entry.classification == NameClass::Free)
                    info.freevars.push_back(entry.name);
            }
            const auto cell_parameters = info.cellvars.size();
            for (const auto &entry : bindings_[scope].entries)
                if (entry.classification == NameClass::Cell && !entry.parameter)
                    info.cellvars.push_back(entry.name);
            std::sort(info.cellvars.begin() + cell_parameters, info.cellvars.end());
            std::sort(info.freevars.begin(), info.freevars.end());
            for (const auto &entry : bindings_[scope].entries) {
                std::optional<std::size_t> slot;
                if (entry.classification == NameClass::Local)
                    slot = std::find(info.locals.begin(), info.locals.end(), entry.name) -
                           info.locals.begin();
                else if (entry.classification == NameClass::Cell)
                    slot = std::find(info.cellvars.begin(), info.cellvars.end(), entry.name) -
                           info.cellvars.begin();
                else if (entry.classification == NameClass::Free)
                    slot = info.cellvars.size() +
                           (std::find(info.freevars.begin(), info.freevars.end(), entry.name) -
                            info.freevars.begin());
                info.symbols.push_back({entry.name, entry.classification, entry.first, slot});
            }
        }
    }

  private:
    [[noreturn]] void error(Span span, std::string message) const
    {
        throw SourceError(program_.source, span, std::move(message));
    }
    std::size_t add(ScopeKind kind, std::u32string name, std::u32string qualname,
                    std::optional<std::size_t> parent, Span span)
    {
        const auto id = bindings_.size();
        bindings_.emplace_back();
        program_.scopes.push_back(
            {kind, std::move(name), std::move(qualname), parent, span, {}, {}, {}, {}});
        return id;
    }
    void expression(const Expr &value, std::size_t scope)
    {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, NameExpr>)
                    bindings_[scope].get(node.name, value.span).used = true;
                else if constexpr (std::is_same_v<T, UnaryExpr>)
                    expression(*node.value, scope);
                else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, BoolExpr>) {
                    expression(*node.left, scope);
                    expression(*node.right, scope);
                } else if constexpr (std::is_same_v<T, CompareExpr>) {
                    for (const auto &item : node.values)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, CallExpr>) {
                    expression(*node.target, scope);
                    for (const auto &item : node.positional)
                        expression(*item, scope);
                    for (const auto &item : node.keywords)
                        expression(*item.second, scope);
                } else if constexpr (std::is_same_v<T, AttributeExpr>)
                    expression(*node.object, scope);
                else if constexpr (std::is_same_v<T, TupleExpr>) {
                    for (const auto &item : node.items)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, SubscriptExpr>) {
                    expression(*node.object, scope);
                    expression(*node.key, scope);
                } else
                    static_assert(std::is_same_v<T, Literal>, "scope traversal missing expression");
            },
            value.node);
    }
    void target(const Expr &value, std::size_t scope, bool read = false)
    {
        if (const auto *tuple = std::get_if<TupleExpr>(&value.node)) {
            for (const auto &item : tuple->items)
                target(*item, scope, read);
        } else if (const auto *name = std::get_if<NameExpr>(&value.node)) {
            if (name->name == U"__debug__")
                error(value.span, "cannot assign/delete __debug__");
            auto &entry = bindings_[scope].get(name->name, value.span);
            entry.bound = true;
            entry.used |= read;
        } else if (const auto *item = std::get_if<SubscriptExpr>(&value.node)) {
            expression(*item->object, scope);
            expression(*item->key, scope);
        } else
            expression(*std::get<AttributeExpr>(value.node).object, scope);
    }
    void collect(Suite &body, std::size_t scope)
    {
        for (auto &statement : body)
            std::visit(
                [&](auto &node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, ExprStmt>)
                        expression(*node.value, scope);
                    else if constexpr (std::is_same_v<T, AssignStmt>) {
                        for (const auto &item : node.targets)
                            target(*item, scope);
                        expression(*node.value, scope);
                    } else if constexpr (std::is_same_v<T, AugAssignStmt>) {
                        target(*node.target, scope, true);
                        expression(*node.value, scope);
                    } else if constexpr (std::is_same_v<T, DeleteStmt>) {
                        for (const auto &item : node.targets)
                            target(*item, scope);
                    } else if constexpr (std::is_same_v<T, IfStmt> ||
                                         std::is_same_v<T, WhileStmt>) {
                        expression(*node.test, scope);
                        collect(node.body, scope);
                        collect(node.alternative, scope);
                    } else if constexpr (std::is_same_v<T, ForStmt>) {
                        target(*node.target, scope);
                        expression(*node.iterable, scope);
                        collect(node.body, scope);
                        collect(node.alternative, scope);
                    } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                        if (node.value)
                            expression(*node.value, scope);
                    } else if constexpr (std::is_same_v<T, RaiseStmt>) {
                        if (node.exception)
                            expression(*node.exception, scope);
                        if (node.cause)
                            expression(*node.cause, scope);
                    } else if constexpr (std::is_same_v<T, TryStmt>) {
                        collect(node.body, scope);
                        for (auto &handler : node.handlers) {
                            if (handler.type)
                                expression(*handler.type, scope);
                            if (handler.name) {
                                const auto &[name, span] = *handler.name;
                                if (name == U"__debug__")
                                    error(span, "cannot assign __debug__");
                                bindings_[scope].get(name, span).bound = true;
                            }
                            collect(handler.body, scope);
                        }
                        collect(node.alternative, scope);
                        collect(node.cleanup, scope);
                    } else if constexpr (std::is_same_v<T, WithStmt>) {
                        for (const auto &item : node.items) {
                            expression(*item.context, scope);
                            if (item.target)
                                target(*item.target, scope);
                        }
                        collect(node.body, scope);
                    } else if constexpr (std::is_same_v<T, DeclareStmt>) {
                        if (node.nonlocal && program_.scopes[scope].kind == ScopeKind::Module)
                            error(statement->span,
                                  "nonlocal declaration not allowed at module level");
                        for (const auto &[name, span] : node.names) {
                            auto &entry = bindings_[scope].get(name, span);
                            if (entry.parameter || entry.bound || entry.used)
                                error(span, "name used, assigned or parameter before "
                                            "global/nonlocal declaration");
                            if ((node.nonlocal && entry.global) ||
                                (!node.nonlocal && entry.nonlocal))
                                error(span, "name is both nonlocal and global");
                            entry.nonlocal |= node.nonlocal;
                            entry.global |= !node.nonlocal;
                        }
                    } else if constexpr (std::is_same_v<T, FunctionStmt>) {
                        if (node.name == U"__debug__")
                            error(statement->span, "cannot assign __debug__");
                        bindings_[scope].get(node.name, statement->span).bound = true;
                        for (const auto &parameter : node.parameters)
                            if (parameter.default_value)
                                expression(*parameter.default_value, scope);
                        const auto qualname =
                            program_.scopes[scope].kind == ScopeKind::Module
                                ? node.name
                                : program_.scopes[scope].qualname + U".<locals>." + node.name;
                        node.scope =
                            add(ScopeKind::Function, node.name, qualname, scope, statement->span);
                        for (const auto &parameter : node.parameters) {
                            auto &entry = bindings_[node.scope].get(parameter.name, parameter.span);
                            entry.bound = entry.parameter = true;
                        }
                        for (const auto *name : {&node.varargs, &node.varkwargs})
                            if (*name) {
                                auto &entry = bindings_[node.scope].get(**name, statement->span);
                                entry.bound = entry.parameter = true;
                            }
                        collect(node.body, node.scope);
                    } else
                        static_assert(std::is_same_v<T, ControlStmt>,
                                      "scope traversal missing statement");
                },
                statement->node);
    }
    SourceProgram &program_;
    Suite &body_;
    std::vector<ScopeBindings> bindings_;
};

} // namespace
void analyze_scopes(SourceProgram &program, Suite &body)
{
    ScopePass(program, body).analyze();
}
} // namespace cxxpython::core::detail
