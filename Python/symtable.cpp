#include "internal/compiler.hpp"

#include <algorithm>

namespace cxxpython::core::detail {
std::u32string mangle_name(std::u32string name, const std::u32string &owner)
{
    if (owner.empty() || !name.starts_with(U"__") || name.ends_with(U"__") ||
        name.find(U'.') != name.npos)
        return name;
    const auto begin = owner.find_first_not_of(U'_');
    return begin == owner.npos ? name : U"_" + owner.substr(begin) + name;
}
namespace {
struct Binding {
    std::u32string name;
    Span first;
    bool bound = false, used = false, parameter = false, global = false, nonlocal = false;
    NameClass classification = NameClass::GlobalImplicit;
    bool forwarded = false;
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
                                       : entry.bound && !entry.nonlocal &&
                                               program_.scopes[scope].kind == ScopeKind::Function
                                           ? NameClass::Local
                                       : entry.bound && !entry.nonlocal &&
                                               program_.scopes[scope].kind == ScopeKind::Class
                                           ? NameClass::Namespace
                                           : NameClass::GlobalImplicit;
        for (std::size_t scope = 1; scope < bindings_.size(); ++scope) {
            const auto count = bindings_[scope].entries.size();
            for (std::size_t i = 0; i < count; ++i) {
                const auto entry = bindings_[scope].entries[i];
                if (entry.global || (entry.bound && !entry.nonlocal))
                    continue;
                auto parent = program_.scopes[scope].parent;
                std::optional<std::size_t> owner;
                while (parent && program_.scopes[*parent].kind != ScopeKind::Module) {
                    if (program_.scopes[*parent].kind == ScopeKind::Class) {
                        if (entry.name == U"__class__") {
                            owner = parent;
                            auto &cells = program_.scopes[*parent].cellvars;
                            if (cells.empty())
                                cells.push_back(U"__class__");
                            break;
                        }
                        parent = program_.scopes[*parent].parent;
                        continue;
                    }
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
                if (program_.scopes[*owner].kind == ScopeKind::Function)
                    bindings_[*owner].find(entry.name)->classification = NameClass::Cell;
                for (auto cursor = std::optional<std::size_t>{scope}; cursor != owner;
                     cursor = program_.scopes[*cursor].parent) {
                    auto &forwarded = bindings_[*cursor].get(entry.name, entry.first);
                    forwarded.forwarded = true;
                    if (program_.scopes[*cursor].kind != ScopeKind::Class ||
                        (!forwarded.bound && !forwarded.global) || forwarded.nonlocal)
                        forwarded.classification = NameClass::Free;
                }
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
                if (entry.classification == NameClass::Free || entry.forwarded)
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
        comprehensions_.push_back(false);
        iteration_names_.emplace_back();
        private_names_.push_back(parent ? private_names_[*parent] : std::u32string{});
        program_.scopes.push_back(
            {kind, std::move(name), std::move(qualname), parent, span, {}, {}, {}, {}});
        return id;
    }
    std::u32string mangle(std::u32string name, std::size_t scope) const
    {
        return mangle_name(std::move(name), private_names_[scope]);
    }
    std::u32string qualified(const std::u32string &name, std::size_t scope) const
    {
        const auto &parent = program_.scopes[scope];
        return parent.kind == ScopeKind::Module
                   ? name
                   : parent.qualname + (parent.kind == ScopeKind::Function ? U".<locals>." : U".") +
                         name;
    }
    void expression(Expr &value, std::size_t scope)
    {
        std::visit(
            [&](auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, NameExpr>) {
                    node.name = mangle(std::move(node.name), scope);
                    bindings_[scope].get(node.name, value.span).used = true;
                    if (node.name == U"super" && program_.scopes[scope].kind == ScopeKind::Function)
                        bindings_[scope].get(U"__class__", value.span).used = true;
                } else if constexpr (std::is_same_v<T, UnaryExpr> || std::is_same_v<T, StarredExpr>)
                    expression(*node.value, scope);
                else if constexpr (std::is_same_v<T, ConditionalExpr>) {
                    expression(*node.test, scope);
                    expression(*node.body, scope);
                    expression(*node.alternative, scope);
                } else if constexpr (std::is_same_v<T, NamedExpr>) {
                    if (comprehension_iterable_)
                        error(value.span, "assignment expression cannot be used in a comprehension "
                                          "iterable expression");
                    expression(*node.value, scope);
                    auto owner = scope;
                    const auto &name = std::get<NameExpr>(node.target->node).name;
                    while (comprehensions_[owner]) {
                        if (std::find(iteration_names_[owner].begin(),
                                      iteration_names_[owner].end(),
                                      mangle(name, owner)) != iteration_names_[owner].end())
                            error(value.span, "assignment expression cannot rebind comprehension "
                                              "iteration variable");
                        owner = *program_.scopes[owner].parent;
                    }
                    if (owner != scope) {
                        if (program_.scopes[owner].kind == ScopeKind::Class)
                            error(value.span, "assignment expression within a comprehension cannot "
                                              "be used in a class body");
                        auto &entry = bindings_[scope].get(mangle(name, scope), value.span);
                        entry.global = program_.scopes[owner].kind == ScopeKind::Module;
                        entry.nonlocal = !entry.global;
                        auto &outer = bindings_[owner].get(mangle(name, owner), value.span);
                        if (outer.global || outer.nonlocal) {
                            entry.global = outer.global;
                            entry.nonlocal = outer.nonlocal;
                        } else
                            outer.bound = true;
                        std::get<NameExpr>(node.target->node).name = mangle(name, scope);
                    } else
                        target(*node.target, scope);
                } else if constexpr (std::is_same_v<T, LambdaExpr>)
                    function(*node.function, scope, value.span);
                else if constexpr (std::is_same_v<T, ComprehensionExpr>) {
                    ++comprehension_iterable_;
                    expression(*node.iterable, scope);
                    --comprehension_iterable_;
                    function(*node.function, scope, value.span);
                } else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, BoolExpr>) {
                    expression(*node.left, scope);
                    expression(*node.right, scope);
                } else if constexpr (std::is_same_v<T, CompareExpr>) {
                    for (const auto &item : node.values)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, CallExpr>) {
                    expression(*node.target, scope);
                    for (const auto &item : node.positional)
                        expression(*item, scope);
                    for (auto &item : node.keywords) {
                        expression(*item.second, scope);
                    }
                } else if constexpr (std::is_same_v<T, AttributeExpr>) {
                    expression(*node.object, scope);
                    node.name = mangle(std::move(node.name), scope);
                } else if constexpr (std::is_same_v<T, TupleExpr> || std::is_same_v<T, ListExpr>) {
                    for (const auto &item : node.items)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, DictExpr>) {
                    for (const auto &[key, item] : node.items) {
                        if (key)
                            expression(*key, scope);
                        expression(*item, scope);
                    }
                } else if constexpr (std::is_same_v<T, SubscriptExpr>) {
                    expression(*node.object, scope);
                    expression(*node.key, scope);
                } else if constexpr (std::is_same_v<T, SliceExpr>) {
                    if (node.start)
                        expression(*node.start, scope);
                    if (node.stop)
                        expression(*node.stop, scope);
                    if (node.step)
                        expression(*node.step, scope);
                } else if constexpr (std::is_same_v<T, JoinedStrExpr>) {
                    for (const auto &item : node.items)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, FormattedExpr>) {
                    expression(*node.value, scope);
                    expression(*node.spec, scope);
                } else
                    static_assert(std::is_same_v<T, Literal>, "scope traversal missing expression");
            },
            value.node);
    }
    void target(Expr &value, std::size_t scope, bool read = false)
    {
        if (const auto *star = std::get_if<StarredExpr>(&value.node)) {
            target(*star->value, scope, read);
        } else if (const auto *tuple = std::get_if<TupleExpr>(&value.node)) {
            for (const auto &item : tuple->items)
                target(*item, scope, read);
        } else if (const auto *list = std::get_if<ListExpr>(&value.node)) {
            for (const auto &item : list->items)
                target(*item, scope, read);
        } else if (auto *name = std::get_if<NameExpr>(&value.node)) {
            name->name = mangle(std::move(name->name), scope);
            if (name->name == U"__debug__")
                error(value.span, "cannot assign/delete __debug__");
            auto &entry = bindings_[scope].get(name->name, value.span);
            entry.bound = true;
            entry.used |= read;
        } else if (const auto *item = std::get_if<SubscriptExpr>(&value.node)) {
            expression(*item->object, scope);
            expression(*item->key, scope);
        } else {
            auto &attribute = std::get<AttributeExpr>(value.node);
            expression(*attribute.object, scope);
            attribute.name = mangle(std::move(attribute.name), scope);
        }
    }
    void function(FunctionStmt &node, std::size_t scope, Span span)
    {
        for (const auto &decorator : node.decorators)
            expression(*decorator, scope);
        for (const auto &parameter : node.parameters)
            if (parameter.default_value)
                expression(*parameter.default_value, scope);
        node.scope = add(ScopeKind::Function, node.name, qualified(node.name, scope), scope, span);
        comprehensions_[node.scope] = node.comprehension;
        if (node.comprehension)
            iteration_targets(node.body, node.scope);
        for (auto &parameter : node.parameters) {
            parameter.name = mangle(std::move(parameter.name), node.scope);
            auto &entry = bindings_[node.scope].get(parameter.name, parameter.span);
            if (entry.parameter)
                error(parameter.span, "duplicate parameter name");
            entry.bound = entry.parameter = true;
        }
        for (auto *name : {&node.varargs, &node.varkwargs})
            if (*name) {
                **name = mangle(std::move(**name), node.scope);
                auto &entry = bindings_[node.scope].get(**name, span);
                if (entry.parameter)
                    error(span, "duplicate parameter name");
                entry.bound = entry.parameter = true;
            }
        collect(node.body, node.scope);
    }
    void iteration_target(const Expr &item, std::size_t scope)
    {
        if (const auto *name = std::get_if<NameExpr>(&item.node))
            iteration_names_[scope].push_back(mangle(name->name, scope));
        else if (const auto *star = std::get_if<StarredExpr>(&item.node))
            iteration_target(*star->value, scope);
        else if (const auto *tuple = std::get_if<TupleExpr>(&item.node))
            for (const auto &child : tuple->items)
                iteration_target(*child, scope);
        else if (const auto *list = std::get_if<ListExpr>(&item.node))
            for (const auto &child : list->items)
                iteration_target(*child, scope);
    }
    void iteration_targets(const Suite &body, std::size_t scope)
    {
        for (const auto &statement : body) {
            if (const auto *loop = std::get_if<ForStmt>(&statement->node)) {
                iteration_target(*loop->target, scope);
                iteration_targets(loop->body, scope);
            } else if (const auto *filter = std::get_if<IfStmt>(&statement->node))
                iteration_targets(filter->body, scope);
        }
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
                        if (comprehensions_[scope]) {
                            ++comprehension_iterable_;
                        }
                        expression(*node.iterable, scope);
                        if (comprehensions_[scope])
                            --comprehension_iterable_;
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
                    } else if constexpr (std::is_same_v<T, AssertStmt>) {
                        expression(*node.test, scope);
                        if (node.message)
                            expression(*node.message, scope);
                    } else if constexpr (std::is_same_v<T, CollectionAddStmt>) {
                        expression(*node.collection, scope);
                        if (node.key)
                            expression(*node.key, scope);
                        expression(*node.value, scope);
                    } else if constexpr (std::is_same_v<T, TryStmt>) {
                        collect(node.body, scope);
                        for (auto &handler : node.handlers) {
                            if (handler.type)
                                expression(*handler.type, scope);
                            if (handler.name) {
                                auto &[name, span] = *handler.name;
                                name = mangle(std::move(name), scope);
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
                    } else if constexpr (std::is_same_v<T, ImportStmt>) {
                        node.private_name = private_names_[scope];
                        for (auto &alias : node.names) {
                            if (alias.name == U"*") {
                                if (program_.scopes[scope].kind != ScopeKind::Module)
                                    error(alias.span, "import * only allowed at module level");
                                continue;
                            }
                            auto binding = alias.asname ? *alias.asname
                                           : node.module
                                               ? alias.name
                                               : alias.name.substr(0, alias.name.find(U'.'));
                            if (binding == U"__debug__")
                                error(alias.span, "cannot assign __debug__");
                            alias.binding = mangle(std::move(binding), scope);
                            bindings_[scope].get(alias.binding, alias.span).bound = true;
                        }
                    } else if constexpr (std::is_same_v<T, DeclareStmt>) {
                        if (node.nonlocal && program_.scopes[scope].kind == ScopeKind::Module)
                            error(statement->span,
                                  "nonlocal declaration not allowed at module level");
                        for (auto &[name, span] : node.names) {
                            name = mangle(std::move(name), scope);
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
                        node.binding = mangle(node.name, scope);
                        bindings_[scope].get(node.binding, statement->span).bound = true;
                        function(node, scope, statement->span);
                    } else if constexpr (std::is_same_v<T, ClassStmt>) {
                        if (node.name == U"__debug__")
                            error(statement->span, "cannot assign __debug__");
                        node.binding = mangle(node.name, scope);
                        bindings_[scope].get(node.binding, statement->span).bound = true;
                        for (const auto &decorator : node.decorators)
                            expression(*decorator, scope);
                        for (const auto &base : node.bases)
                            expression(*base, scope);
                        for (auto &item : node.keywords) {
                            expression(*item.second, scope);
                        }
                        node.scope = add(ScopeKind::Class, node.name, qualified(node.name, scope),
                                         scope, statement->span);
                        private_names_[node.scope] = node.name;
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
    std::vector<std::u32string> private_names_;
    std::vector<bool> comprehensions_;
    std::vector<std::vector<std::u32string>> iteration_names_;
    std::size_t comprehension_iterable_ = 0;
};

} // namespace
void analyze_scopes(SourceProgram &program, Suite &body)
{
    ScopePass(program, body).analyze();
}
} // namespace cxxpython::core::detail
