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
    bool forwarded = false, annotated = false, type_parameter = false;
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
                const auto &current = program_.scopes[scope];
                if (current.sees_class_namespace && entry.name != U"__classdict__") {
                    auto *class_entry = bindings_[*class_namespace(scope)].find(entry.name);
                    if (class_entry &&
                        (class_entry->global || (class_entry->bound && !class_entry->nonlocal))) {
                        bindings_[scope].entries[i].classification =
                            class_entry->global ? NameClass::GlobalExplicit
                                                : NameClass::GlobalImplicit;
                        continue;
                    }
                }
                auto parent = program_.scopes[scope].parent;
                std::optional<std::size_t> owner;
                while (parent && program_.scopes[*parent].kind != ScopeKind::Module) {
                    if (program_.scopes[*parent].kind == ScopeKind::Class) {
                        if (entry.name == U"__class__" ||
                            (entry.name == U"__classdict__" && current.sees_class_namespace)) {
                            owner = parent;
                            auto &cells = program_.scopes[*parent].cellvars;
                            if (std::find(cells.begin(), cells.end(), entry.name) == cells.end())
                                cells.push_back(entry.name);
                            break;
                        }
                        parent = program_.scopes[*parent].parent;
                        continue;
                    }
                    const auto *candidate = bindings_[*parent].find(entry.name);
                    if (candidate && candidate->global)
                        break;
                    if (candidate && candidate->bound && !candidate->nonlocal) {
                        if (entry.nonlocal && candidate->type_parameter)
                            error(entry.first, "nonlocal binding not allowed for type parameter");
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
            for (const auto &entry : bindings_[scope].entries)
                if (entry.parameter && (entry.classification == NameClass::Local ||
                                        entry.classification == NameClass::Cell))
                    info.locals.push_back(entry.name);
            for (const auto &entry : bindings_[scope].entries) {
                if (!entry.parameter && (entry.classification == NameClass::Local ||
                                         entry.classification == NameClass::Cell))
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
        if (parent.annotation_role == AnnotationRole::TypeParameters)
            return qualified(name, *parent.parent);
        return parent.kind == ScopeKind::Module
                   ? name
                   : parent.qualname + (parent.kind == ScopeKind::Function ? U".<locals>." : U".") +
                         name;
    }
    std::optional<std::size_t> class_namespace(std::size_t scope) const
    {
        for (auto parent = program_.scopes[scope].parent; parent;
             parent = program_.scopes[*parent].parent) {
            if (program_.scopes[*parent].kind == ScopeKind::Class)
                return parent;
            if (program_.scopes[*parent].annotation_role != AnnotationRole::TypeParameters)
                break;
        }
        return {};
    }
    void annotation_restriction(std::size_t scope, Span span, const char *expression)
    {
        switch (program_.scopes[scope].annotation_role) {
        case AnnotationRole::None:
            return;
        case AnnotationRole::TypeAlias:
            error(span, std::string(expression) + " expression cannot be used within a type alias");
        case AnnotationRole::TypeVarBound:
            error(span,
                  std::string(expression) + " expression cannot be used within a TypeVar bound");
        case AnnotationRole::TypeVarConstraints:
            error(span, std::string(expression) +
                            " expression cannot be used within a TypeVar constraint");
        case AnnotationRole::TypeParameters:
            error(span, std::string(expression) +
                            " expression cannot be used within a type parameter scope");
        }
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
                } else if constexpr (std::is_same_v<T, YieldExpr>) {
                    annotation_restriction(scope, value.span, "yield");
                    if (program_.scopes[scope].kind == ScopeKind::Annotation)
                        error(value.span, "yield expression cannot be used within an annotation");
                    if (program_.scopes[scope].kind != ScopeKind::Function)
                        error(value.span, "'yield' outside function");
                    if (comprehensions_[scope] && !node.implicit)
                        error(value.span, "'yield' inside comprehension");
                    if (program_.scopes[scope].coroutine && node.delegated)
                        error(value.span, "'yield from' inside async function");
                    program_.scopes[scope].generator = true;
                    if (node.value)
                        expression(*node.value, scope);
                } else if constexpr (std::is_same_v<T, AwaitExpr>) {
                    annotation_restriction(scope, value.span, "await");
                    if (program_.scopes[scope].kind == ScopeKind::Annotation)
                        error(value.span, "await expression cannot be used within an annotation");
                    async_scope(scope, value.span, "'await' outside async function");
                    expression(*node.value, scope);
                } else if constexpr (std::is_same_v<T, UnaryExpr> || std::is_same_v<T, StarredExpr>)
                    expression(*node.value, scope);
                else if constexpr (std::is_same_v<T, ConditionalExpr>) {
                    expression(*node.test, scope);
                    expression(*node.body, scope);
                    expression(*node.alternative, scope);
                } else if constexpr (std::is_same_v<T, NamedExpr>) {
                    annotation_restriction(scope, value.span, "named");
                    if (program_.scopes[scope].kind == ScopeKind::Annotation)
                        error(value.span, "named expression cannot be used within an annotation");
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
                    if (program_.scopes[scope].sees_class_namespace)
                        error(value.span,
                              "Cannot use comprehension in annotation scope within class scope");
                    ++comprehension_iterable_;
                    expression(*node.iterable, scope);
                    --comprehension_iterable_;
                    function(*node.function, scope, value.span);
                    const auto &child = program_.scopes[node.function->scope];
                    if (child.coroutine && !child.generator)
                        async_scope(scope, value.span,
                                    "asynchronous comprehension outside of an "
                                    "asynchronous function");
                } else if constexpr (std::is_same_v<T, BoolExpr>) {
                    for (const auto &item : node.values)
                        expression(*item, scope);
                } else if constexpr (std::is_same_v<T, BinaryExpr>) {
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
                } else if constexpr (std::is_same_v<T, TupleExpr> || std::is_same_v<T, ListExpr> ||
                                     std::is_same_v<T, SetExpr>) {
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
    void async_scope(std::size_t scope, Span span, const char *message)
    {
        if (comprehensions_[scope] || annotation_symbol_only_)
            program_.scopes[scope].coroutine = true;
        else if (!program_.scopes[scope].coroutine)
            error(span, message);
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
    std::size_t type_parameters(TypeParameters &parameters, const std::u32string &name,
                                std::size_t parent, Span span, bool class_definition = false)
    {
        if (parameters.values.empty())
            return parent;
        const auto scope_name = U"<generic parameters of " + name + U">";
        parameters.scope =
            add(ScopeKind::Function, scope_name, qualified(scope_name, parent), parent, span);
        const auto scope = parameters.scope;
        if (class_definition)
            private_names_[scope] = name;
        auto &info = program_.scopes[scope];
        info.annotation_role = AnnotationRole::TypeParameters;
        info.sees_class_namespace = class_namespace(scope).has_value();
        if (info.sees_class_namespace)
            bindings_[scope].get(U"__classdict__", span).used = true;
        bindings_[scope].get(U".type_params", span).bound = true;
        for (auto &parameter : parameters.values) {
            parameter.binding = mangle(parameter.name, scope);
            auto &entry = bindings_[scope].get(parameter.binding, parameter.span);
            if (entry.bound)
                error(parameter.span, "duplicate type parameter");
            entry.bound = entry.type_parameter = true;
        }
        for (auto &parameter : parameters.values)
            if (parameter.evaluator)
                function(*parameter.evaluator, scope, parameter.span);
        return scope;
    }
    void function(FunctionStmt &node, std::size_t scope, Span span)
    {
        for (const auto &decorator : node.decorators)
            expression(*decorator, scope);
        for (const auto &parameter : node.parameters)
            if (parameter.default_value)
                expression(*parameter.default_value, scope);
        const auto parent = type_parameters(node.type_parameters, node.name, scope, span);
        if (parent != scope) {
            bool defaults = false, kwdefaults = false;
            for (const auto &parameter : node.parameters)
                if (parameter.default_value)
                    (parameter.kind == ParameterKind::KeywordOnly ? kwdefaults : defaults) = true;
            for (auto [name, present] :
                 {std::pair{U".defaults", defaults}, {U".kwdefaults", kwdefaults}})
                if (present) {
                    auto &entry = bindings_[parent].get(name, span);
                    entry.bound = entry.parameter = true;
                }
        }
        for (auto &item : node.annotations)
            annotation(item.second, parent);
        node.scope =
            add(ScopeKind::Function, node.name, qualified(node.name, parent), parent, span);
        for (auto &annotation : node.annotations)
            annotation.first = mangle(std::move(annotation.first), node.scope);
        program_.scopes[node.scope].coroutine = node.coroutine;
        program_.scopes[node.scope].annotation_role = node.annotation_role;
        if (node.annotation_role != AnnotationRole::None && class_namespace(node.scope)) {
            program_.scopes[node.scope].sees_class_namespace = true;
            bindings_[node.scope].get(U"__classdict__", span).used = true;
        }
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
    void annotation(ExprPtr &value, std::size_t scope, bool evaluated = true)
    {
        const bool previous = annotation_symbol_only_;
        annotation_symbol_only_ |= !evaluated || bool(program_.future_flags & future_annotations);
        if (!(program_.future_flags & future_annotations)) {
            expression(*value, scope);
            annotation_symbol_only_ = previous;
            return;
        }
        auto text = unparse_expression(*value, program_.source);
        const auto size = bindings_.size();
        const auto temporary =
            add(ScopeKind::Annotation, U"<annotation>", U"<annotation>", {}, value->span);
        private_names_[temporary] = private_names_[scope];
        // CPython's AnnotationBlock does not contribute runtime closures.
        expression(*value, temporary);
        bindings_.resize(size);
        comprehensions_.resize(size);
        iteration_names_.resize(size);
        private_names_.resize(size);
        program_.scopes.resize(size);
        value->node = Literal{Literal::Kind::String, std::move(text)};
        annotation_symbol_only_ = previous;
    }
    struct PatternInfo {
        std::vector<std::u32string> names;
        bool irrefutable = false;
    };
    PatternInfo pattern(Pattern &value, std::size_t scope, bool allow_star = false)
    {
        PatternInfo info;
        auto bind = [&](PatternCapture &capture) {
            if (!capture.binding)
                return;
            target(*capture.binding, scope);
            const auto &name = capture.name;
            if (std::find(info.names.begin(), info.names.end(), name) != info.names.end())
                error(capture.binding->span, "multiple assignments to name in pattern");
            info.names.push_back(name);
        };
        auto merge = [&](Pattern &child, bool star = false) {
            auto nested = pattern(child, scope, star);
            for (const auto &name : nested.names) {
                if (std::find(info.names.begin(), info.names.end(), name) != info.names.end())
                    error(child.span, "multiple assignments to name in pattern");
                info.names.push_back(name);
            }
            return nested.irrefutable;
        };
        std::visit(
            [&](auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ValuePattern>) {
                    if (!std::holds_alternative<Literal>(node.value->node) &&
                        !std::holds_alternative<AttributeExpr>(node.value->node) &&
                        !std::holds_alternative<UnaryExpr>(node.value->node) &&
                        !std::holds_alternative<BinaryExpr>(node.value->node))
                        error(value.span, "patterns may only match literals and attribute lookups");
                    expression(*node.value, scope);
                } else if constexpr (std::is_same_v<T, CapturePattern>) {
                    bind(node.target);
                    info.irrefutable = true;
                } else if constexpr (std::is_same_v<T, StarPattern>) {
                    if (!allow_star)
                        error(value.span, "star pattern must be in a sequence pattern");
                    bind(node.target);
                } else if constexpr (std::is_same_v<T, AsPattern>) {
                    info.irrefutable = merge(*node.pattern);
                    bind(node.target);
                } else if constexpr (std::is_same_v<T, SequencePattern>) {
                    bool starred = false;
                    for (auto &item : node.items) {
                        if (std::holds_alternative<StarPattern>(item->node)) {
                            if (starred)
                                error(item->span, "multiple starred names in sequence pattern");
                            starred = true;
                        }
                        merge(*item, true);
                    }
                } else if constexpr (std::is_same_v<T, MappingPattern>) {
                    for (auto &[key, child] : node.items) {
                        if (!std::holds_alternative<Literal>(key->node) &&
                            !std::holds_alternative<AttributeExpr>(key->node) &&
                            !std::holds_alternative<UnaryExpr>(key->node) &&
                            !std::holds_alternative<BinaryExpr>(key->node))
                            error(key->span, "mapping pattern keys may only match literals and "
                                             "attribute lookups");
                        expression(*key, scope);
                        merge(*child);
                    }
                    bind(node.rest);
                } else if constexpr (std::is_same_v<T, ClassPattern>) {
                    expression(*node.type, scope);
                    for (auto &child : node.positional)
                        merge(*child);
                    std::vector<std::u32string> attributes;
                    for (auto &[name, child] : node.keywords) {
                        if (std::find(attributes.begin(), attributes.end(), name) !=
                            attributes.end())
                            error(child->span, "attribute name repeated in class pattern");
                        attributes.push_back(name);
                        merge(*child);
                    }
                } else if constexpr (std::is_same_v<T, OrPattern>) {
                    std::vector<std::u32string> expected;
                    for (std::size_t i = 0; i < node.items.size(); ++i) {
                        auto alternative = pattern(*node.items[i], scope);
                        if (alternative.irrefutable && i + 1 < node.items.size())
                            error(node.items[i]->span,
                                  "irrefutable pattern makes remaining patterns unreachable");
                        auto names = alternative.names;
                        std::sort(names.begin(), names.end());
                        if (!i) {
                            expected = names;
                            info.names = std::move(alternative.names);
                        } else if (names != expected)
                            error(node.items[i]->span, "alternative patterns bind different names");
                        info.irrefutable |= alternative.irrefutable;
                    }
                }
            },
            value.node);
        return info;
    }
    void collect(Suite &body, std::size_t scope)
    {
        for (auto &statement : body)
            std::visit(
                [&](auto &node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, ExprStmt>)
                        expression(*node.value, scope);
                    else if constexpr (std::is_same_v<T, MatchStmt>) {
                        expression(*node.subject, scope);
                        for (std::size_t i = 0; i < node.cases.size(); ++i) {
                            auto &item = node.cases[i];
                            const auto info = pattern(*item.pattern, scope);
                            if (info.irrefutable && !item.guard && i + 1 < node.cases.size())
                                error(item.span,
                                      "irrefutable pattern makes remaining patterns unreachable");
                            if (item.guard)
                                expression(*item.guard, scope);
                            collect(item.body, scope);
                        }
                    } else if constexpr (std::is_same_v<T, TypeAliasStmt>) {
                        if (node.name == U"__debug__")
                            error(statement->span, "cannot assign __debug__");
                        node.binding = mangle(node.name, scope);
                        bindings_[scope].get(node.binding, statement->span).bound = true;
                        const auto parent = type_parameters(node.type_parameters, node.name, scope,
                                                            statement->span);
                        function(*node.evaluator, parent, statement->span);
                    } else if constexpr (std::is_same_v<T, AnnAssignStmt>) {
                        if (program_.scopes[scope].kind != ScopeKind::Function)
                            program_.scopes[scope].annotations = true;
                        if (auto *name = std::get_if<NameExpr>(&node.target->node)) {
                            if (node.simple) {
                                auto &entry = bindings_[scope].get(mangle(name->name, scope),
                                                                   node.target->span);
                                if (scope && (entry.global || entry.nonlocal))
                                    error(statement->span,
                                          "annotated name '" + source_utf8(name->name) +
                                              "' can't be " +
                                              (entry.global ? "global" : "nonlocal"));
                                entry.annotated = true;
                            }
                            if (node.simple || node.value)
                                target(*node.target, scope);
                        } else
                            target(*node.target, scope);
                        annotation(node.annotation, scope,
                                   program_.scopes[scope].kind != ScopeKind::Function);
                        if (node.value)
                            expression(*node.value, scope);
                    } else if constexpr (std::is_same_v<T, AssignStmt>) {
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
                        if (node.asynchronous)
                            async_scope(scope, statement->span,
                                        "'async for' outside async function");
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
                        if (node.asynchronous && !program_.scopes[scope].coroutine)
                            error(statement->span, "'async with' outside async function");
                        for (const auto &item : node.items) {
                            expression(*item.context, scope);
                            if (item.target)
                                target(*item.target, scope);
                        }
                        collect(node.body, scope);
                    } else if constexpr (std::is_same_v<T, ImportStmt>) {
                        if (node.module == U"__future__" && !node.level && !node.future)
                            error(
                                statement->span,
                                "from __future__ imports must occur at the beginning of the file");
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
                            if (entry.annotated)
                                error(span, "annotated name '" + source_utf8(name) + "' can't be " +
                                                (node.nonlocal ? "nonlocal" : "global"));
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
                        const auto parent = type_parameters(node.type_parameters, node.name, scope,
                                                            statement->span, true);
                        for (const auto &base : node.bases)
                            expression(*base, parent);
                        for (auto &item : node.keywords) {
                            expression(*item.second, parent);
                        }
                        node.scope = add(ScopeKind::Class, node.name, qualified(node.name, scope),
                                         parent, statement->span);
                        if (parent != scope)
                            bindings_[node.scope].get(U".type_params", statement->span).used = true;
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
    bool annotation_symbol_only_ = false;
};

} // namespace
void analyze_scopes(SourceProgram &program, Suite &body)
{
    ScopePass(program, body).analyze();
}
} // namespace cxxpython::core::detail
