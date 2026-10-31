#include "internal/ast_nodes.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core::ast {
namespace {
using namespace detail;

class Lowerer {
    const SourceBuffer &source_;
    std::size_t depth_ = 0;
    struct DepthGuard {
        std::size_t &depth;
        ~DepthGuard()
        {
            --depth;
        }
    };
    DepthGuard enter(const Node &node)
    {
        if (depth_ >= 256)
            syntax_error(node, "AST nesting exceeds the current compiler limit");
        ++depth_;
        return DepthGuard{depth_};
    }

    [[noreturn]] void invalid(const Node &node, std::string message) const
    {
        throw SourceError(source_, node.span, std::move(message), SourceErrorKind::AstValidation);
    }
    [[noreturn]] void syntax_error(const Node &node, std::string message) const
    {
        throw SourceError(source_, node.span, std::move(message));
    }
    const Field &field(const Node &node, std::size_t i) const
    {
        if (i >= node.fields.size())
            invalid(node, "missing AST field");
        return node.fields[i];
    }
    const Node *optional(const Node &node, std::size_t i) const
    {
        const auto &f = field(node, i);
        if (std::holds_alternative<std::monostate>(f))
            return nullptr;
        auto p = std::get_if<NodePtr>(&f);
        if (!p)
            invalid(node, "invalid AST node field");
        return p->get();
    }
    const Node &child(const Node &node, std::size_t i) const
    {
        auto p = optional(node, i);
        if (!p)
            invalid(node, "missing AST node");
        return *p;
    }
    const Nodes &nodes(const Node &node, std::size_t i) const
    {
        auto p = std::get_if<Nodes>(&field(node, i));
        if (!p)
            invalid(node, "invalid AST node list");
        return *p;
    }
    const Strings &strings(const Node &node, std::size_t i) const
    {
        auto p = std::get_if<Strings>(&field(node, i));
        if (!p)
            invalid(node, "invalid AST identifier list");
        return *p;
    }
    const std::u32string &string(const Node &node, std::size_t i) const
    {
        auto p = std::get_if<std::u32string>(&field(node, i));
        if (!p)
            invalid(node, "invalid AST identifier");
        return *p;
    }
    std::optional<std::u32string> optional_string(const Node &node, std::size_t i) const
    {
        const auto &f = field(node, i);
        if (std::holds_alternative<std::monostate>(f))
            return std::nullopt;
        if (auto p = std::get_if<std::u32string>(&f))
            return *p;
        invalid(node, "invalid optional AST identifier");
    }
    std::int64_t integer(const Node &node, std::size_t i) const
    {
        auto p = std::get_if<std::int64_t>(&field(node, i));
        if (!p)
            invalid(node, "invalid AST integer");
        return *p;
    }
    template <class T> ExprPtr expr(const Node &node, T value, std::size_t depth = 1) const
    {
        if (depth > 128)
            syntax_error(node, "expression nesting exceeds the current compiler limit");
        return std::make_unique<Expr>(Expr{node.span, depth, std::move(value)});
    }
    template <class T> std::unique_ptr<Stmt> stmt(const Node &node, T value) const
    {
        return std::make_unique<Stmt>(Stmt{node.span, std::move(value)});
    }
    ExprPtr maybe_expr(const Node &node, std::size_t i)
    {
        auto p = optional(node, i);
        return p ? expression(*p) : nullptr;
    }
    BinaryOp binary(const Node &n) const
    {
        switch (n.kind) {
        case Kind::Add:
            return BinaryOp::Add;
        case Kind::Sub:
            return BinaryOp::Subtract;
        case Kind::Mult:
            return BinaryOp::Multiply;
        case Kind::MatMult:
            return BinaryOp::MatrixMultiply;
        case Kind::Div:
            return BinaryOp::TrueDivide;
        case Kind::FloorDiv:
            return BinaryOp::FloorDivide;
        case Kind::Mod:
            return BinaryOp::Modulo;
        case Kind::Pow:
            return BinaryOp::Power;
        case Kind::LShift:
            return BinaryOp::LeftShift;
        case Kind::RShift:
            return BinaryOp::RightShift;
        case Kind::BitOr:
            return BinaryOp::BitOr;
        case Kind::BitXor:
            return BinaryOp::BitXor;
        case Kind::BitAnd:
            return BinaryOp::BitAnd;
        default:
            invalid(n, "invalid AST binary operator");
        }
    }
    CompareOp comparison(const Node &n) const
    {
        switch (n.kind) {
        case Kind::Eq:
            return CompareOp::Eq;
        case Kind::NotEq:
            return CompareOp::Ne;
        case Kind::Lt:
            return CompareOp::Lt;
        case Kind::LtE:
            return CompareOp::Le;
        case Kind::Gt:
            return CompareOp::Gt;
        case Kind::GtE:
            return CompareOp::Ge;
        case Kind::Is:
            return CompareOp::Is;
        case Kind::IsNot:
            return CompareOp::IsNot;
        case Kind::In:
            return CompareOp::In;
        case Kind::NotIn:
            return CompareOp::NotIn;
        default:
            invalid(n, "invalid AST comparison operator");
        }
    }
    void require_context(const Node &n, std::size_t field_index, Kind expected) const
    {
        if (child(n, field_index).kind != expected)
            invalid(n, "invalid expression context");
    }
    ExprPtr target(const Node &n, Kind expected, bool nested = false)
    {
        auto guard = enter(n);
        switch (n.kind) {
        case Kind::Name:
            require_context(n, 1, expected);
            break;
        case Kind::Attribute:
        case Kind::Subscript:
            require_context(n, 2, expected);
            break;
        case Kind::Starred:
            require_context(n, 1, expected);
            if (expected == Kind::Del || !nested)
                syntax_error(n, "invalid starred assignment target");
            return expr(n, StarredExpr{target(child(n, 0), expected)});
        case Kind::Tuple:
        case Kind::List: {
            require_context(n, 1, expected);
            std::vector<ExprPtr> items;
            unsigned stars = 0;
            for (const auto &item : nodes(n, 0)) {
                if (!item)
                    invalid(n, "null assignment target");
                if (item->kind == Kind::Starred)
                    ++stars;
                items.push_back(target(*item, expected, true));
            }
            if (stars > 1)
                syntax_error(n, "multiple starred expressions in assignment");
            if (n.kind == Kind::Tuple)
                return expr(n, TupleExpr{std::move(items)});
            return expr(n, ListExpr{std::move(items)});
        }
        default:
            invalid(n, "invalid assignment target");
        }
        if (n.kind == Kind::Name) {
            if (string(n, 0) == U"__debug__")
                syntax_error(n, "cannot assign to __debug__");
            return expr(n, NameExpr{string(n, 0)});
        }
        if (n.kind == Kind::Attribute)
            return expr(n, AttributeExpr{expression(child(n, 0)), string(n, 1)});
        return expr(n, SubscriptExpr{expression(child(n, 0)), expression(child(n, 1))});
    }
    ExprPtr expression(const Node &n, bool allow_star = false)
    {
        auto guard = enter(n);
        switch (n.kind) {
        case Kind::Constant: {
            auto p = std::get_if<Literal>(&field(n, 0));
            if (!p)
                invalid(n, "invalid AST constant");
            return expr(n, *p);
        }
        case Kind::Name:
            require_context(n, 1, Kind::Load);
            return expr(n, NameExpr{string(n, 0)});
        case Kind::Attribute:
            require_context(n, 2, Kind::Load);
            return expr(n, AttributeExpr{expression(child(n, 0)), string(n, 1)});
        case Kind::Subscript:
            require_context(n, 2, Kind::Load);
            return expr(n, SubscriptExpr{expression(child(n, 0)), expression(child(n, 1))});
        case Kind::Starred:
            if (!allow_star)
                invalid(n, "starred expression is not allowed here");
            require_context(n, 1, Kind::Load);
            return expr(n, StarredExpr{expression(child(n, 0))});
        case Kind::Tuple:
        case Kind::List:
        case Kind::Set: {
            if (n.kind != Kind::Set)
                require_context(n, 1, Kind::Load);
            std::vector<ExprPtr> items;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null AST expression");
                items.push_back(expression(*p, true));
            }
            if (n.kind == Kind::Tuple)
                return expr(n, TupleExpr{std::move(items)});
            if (n.kind == Kind::List)
                return expr(n, ListExpr{std::move(items)});
            return expr(n, SetExpr{std::move(items)});
        }
        case Kind::Dict: {
            auto &keys = nodes(n, 0);
            auto &values = nodes(n, 1);
            if (keys.size() != values.size())
                invalid(n, "Dict keys and values differ in length");
            DictExpr result;
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (!values[i])
                    invalid(n, "null dictionary value");
                result.items.emplace_back(keys[i] ? expression(*keys[i]) : nullptr,
                                          expression(*values[i]));
            }
            return expr(n, std::move(result));
        }
        case Kind::BinOp:
            return expr(n, BinaryExpr{binary(child(n, 1)), expression(child(n, 0)),
                                      expression(child(n, 2))});
        case Kind::UnaryOp: {
            auto &op = child(n, 0);
            auto kind = op.kind == Kind::Not    ? UnaryOp::Not
                        : op.kind == Kind::UAdd ? UnaryOp::Positive
                        : op.kind == Kind::USub ? UnaryOp::Negative
                                                : UnaryOp::Invert;
            if (op.kind != Kind::Not && op.kind != Kind::UAdd && op.kind != Kind::USub &&
                op.kind != Kind::Invert)
                invalid(op, "invalid AST unary operator");
            return expr(n, UnaryExpr{kind, expression(child(n, 1))});
        }
        case Kind::BoolOp: {
            auto &op = child(n, 0);
            if (op.kind != Kind::And && op.kind != Kind::Or)
                invalid(op, "invalid AST boolean operator");
            BoolExpr result{op.kind == Kind::And, {}};
            for (const auto &p : nodes(n, 1)) {
                if (!p)
                    invalid(n, "null boolean operand");
                result.values.push_back(expression(*p));
            }
            if (result.values.size() < 2)
                invalid(n, "BoolOp requires at least two values");
            return expr(n, std::move(result));
        }
        case Kind::Compare: {
            auto &ops = nodes(n, 1);
            auto &values = nodes(n, 2);
            if (ops.empty() || ops.size() != values.size())
                invalid(n, "invalid Compare operands");
            CompareExpr result;
            result.values.push_back(expression(child(n, 0)));
            for (std::size_t i = 0; i < ops.size(); ++i) {
                if (!ops[i] || !values[i])
                    invalid(n, "null comparison operand");
                result.ops.push_back(comparison(*ops[i]));
                result.values.push_back(expression(*values[i]));
            }
            return expr(n, std::move(result));
        }
        case Kind::IfExp:
            return expr(n, ConditionalExpr{expression(child(n, 0)), expression(child(n, 1)),
                                           expression(child(n, 2))});
        case Kind::NamedExpr:
            if (child(n, 0).kind != Kind::Name)
                throw SourceError(source_, n.span, "named expression target must be a Name",
                                  SourceErrorKind::AstTypeValidation);
            return expr(n, NamedExpr{target(child(n, 0), Kind::Store), expression(child(n, 1))});
        case Kind::Await:
            return expr(n, AwaitExpr{expression(child(n, 0))});
        case Kind::Yield:
            return expr(n, YieldExpr{maybe_expr(n, 0), false});
        case Kind::YieldFrom:
            return expr(n, YieldExpr{expression(child(n, 0)), true});
        case Kind::Slice:
            return expr(n, SliceExpr{maybe_expr(n, 0), maybe_expr(n, 1), maybe_expr(n, 2)});
        case Kind::Call: {
            CallExpr result;
            result.target = expression(child(n, 0));
            for (const auto &p : nodes(n, 1)) {
                if (!p)
                    invalid(n, "null call argument");
                result.positional.push_back(expression(*p, true));
            }
            std::unordered_set<std::u32string> seen;
            for (const auto &p : nodes(n, 2)) {
                if (!p || p->kind != Kind::keyword)
                    invalid(n, "invalid keyword");
                auto name = optional_string(*p, 0);
                if (name && !seen.insert(*name).second)
                    syntax_error(*p, "keyword argument repeated");
                result.keywords.emplace_back(name.value_or(U""), expression(child(*p, 1)));
            }
            return expr(n, std::move(result));
        }
        case Kind::JoinedStr: {
            JoinedStrExpr result;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null f-string value");
                result.items.push_back(expression(*p));
            }
            return expr(n, std::move(result));
        }
        case Kind::FormattedValue: {
            auto conversion = integer(n, 1);
            if (conversion != -1 && conversion != 's' && conversion != 'r' && conversion != 'a')
                invalid(n, "invalid f-string conversion");
            auto spec = maybe_expr(n, 2);
            bool has_spec = bool(spec);
            if (!spec)
                spec = expr(n, Literal{Literal::Kind::String, U""});
            return expr(n, FormattedExpr{expression(child(n, 0)), std::move(spec),
                                         static_cast<char>(conversion == -1 ? 0 : conversion),
                                         has_spec});
        }
        case Kind::Lambda: {
            auto fn = std::make_unique<FunctionStmt>();
            fn->name = U"<lambda>";
            parameters(child(n, 0), *fn);
            fn->body.push_back(stmt(child(n, 1), ReturnStmt{expression(child(n, 1))}));
            return expr(n, LambdaExpr{std::move(fn)});
        }
        case Kind::ListComp:
        case Kind::SetComp:
        case Kind::DictComp:
        case Kind::GeneratorExp:
            return comprehension(n);
        default:
            invalid(n, "invalid AST expression kind");
        }
    }
    void parameters(const Node &n, FunctionStmt &fn)
    {
        if (n.kind != Kind::arguments)
            invalid(n, "expected arguments");
        const auto &posonly = nodes(n, 0), &pos = nodes(n, 1), &kwonly = nodes(n, 3);
        const auto &kwdefaults = nodes(n, 4), &defaults = nodes(n, 6);
        if (defaults.size() > posonly.size() + pos.size() || kwdefaults.size() != kwonly.size())
            invalid(n, "invalid argument defaults");
        std::unordered_set<std::u32string> names;
        auto read_arg = [&](const Node &arg, bool starred_annotation = false) {
            if (arg.kind != Kind::arg)
                invalid(arg, "expected arg");
            auto name = string(arg, 0);
            if (!names.insert(name).second)
                syntax_error(arg, "duplicate argument in function definition");
            if (name == U"__debug__")
                syntax_error(arg, "cannot assign to __debug__");
            if (auto annotation = optional(arg, 1))
                fn.annotations.emplace_back(name, expression(*annotation, starred_annotation));
            return name;
        };
        std::size_t index = 0, first_default = posonly.size() + pos.size() - defaults.size();
        auto append_pos = [&](const Nodes &items, ParameterKind kind) {
            for (const auto &p : items) {
                if (!p)
                    invalid(n, "null argument");
                ExprPtr value;
                if (index >= first_default) {
                    auto &def = defaults[index - first_default];
                    if (!def)
                        invalid(n, "null positional default");
                    value = expression(*def);
                }
                fn.parameters.push_back(
                    ParameterSpec{read_arg(*p), kind, std::move(value), p->span});
                ++index;
            }
        };
        append_pos(posonly, ParameterKind::PositionalOnly);
        append_pos(pos, ParameterKind::PositionalOrKeyword);
        if (auto p = optional(n, 2))
            fn.varargs = read_arg(*p, true);
        for (std::size_t i = 0; i < kwonly.size(); ++i) {
            if (!kwonly[i])
                invalid(n, "null keyword-only argument");
            fn.parameters.push_back(ParameterSpec{
                read_arg(*kwonly[i]), ParameterKind::KeywordOnly,
                kwdefaults[i] ? expression(*kwdefaults[i]) : nullptr, kwonly[i]->span});
        }
        if (auto p = optional(n, 5))
            fn.varkwargs = read_arg(*p);
    }
    TypeParameters type_parameters(const Node &n, std::size_t index)
    {
        TypeParameters result;
        std::unordered_set<std::u32string> names;
        for (const auto &p : nodes(n, index)) {
            if (!p || (p->kind != Kind::TypeVar && p->kind != Kind::TypeVarTuple &&
                       p->kind != Kind::ParamSpec))
                invalid(n, "invalid type parameter");
            TypeParameter item;
            item.span = p->span;
            item.name = string(*p, 0);
            if (item.name == U"__debug__")
                syntax_error(*p, "cannot assign to __debug__");
            if (!names.insert(item.name).second)
                syntax_error(*p, "duplicate type parameter");
            item.kind = p->kind == Kind::TypeVar        ? TypeParameter::Kind::TypeVar
                        : p->kind == Kind::TypeVarTuple ? TypeParameter::Kind::TypeVarTuple
                                                        : TypeParameter::Kind::ParamSpec;
            if (p->kind == Kind::TypeVar)
                if (auto bound = optional(*p, 1)) {
                    auto value = expression(*bound);
                    item.constraints = std::holds_alternative<TupleExpr>(value->node);
                    item.evaluator = std::make_unique<FunctionStmt>();
                    item.evaluator->name = item.name;
                    item.evaluator->annotation_role = item.constraints
                                                          ? AnnotationRole::TypeVarConstraints
                                                          : AnnotationRole::TypeVarBound;
                    item.evaluator->body.push_back(stmt(*bound, ReturnStmt{std::move(value)}));
                }
            result.values.push_back(std::move(item));
        }
        return result;
    }
    ExprPtr comprehension(const Node &n)
    {
        const bool dict = n.kind == Kind::DictComp, generator = n.kind == Kind::GeneratorExp;
        const auto &clauses = nodes(n, dict ? 2 : 1);
        if (clauses.empty() || !clauses[0] || clauses[0]->kind != Kind::comprehension)
            invalid(n, "comprehension requires a generator");
        auto iterable = expression(child(*clauses[0], 1));
        auto fn = std::make_unique<FunctionStmt>();
        fn->name = generator                  ? U"<genexpr>"
                   : n.kind == Kind::ListComp ? U"<listcomp>"
                   : n.kind == Kind::SetComp  ? U"<setcomp>"
                                              : U"<dictcomp>";
        fn->comprehension = true;
        fn->parameters.push_back(
            ParameterSpec{U".0", ParameterKind::PositionalOnly, nullptr, n.span});
        auto name = [&]() { return expr(n, NameExpr{U".result"}); };
        if (!generator) {
            AssignStmt init;
            init.targets.push_back(name());
            if (n.kind == Kind::ListComp)
                init.value = expr(n, ListExpr{});
            else if (n.kind == Kind::SetComp)
                init.value = expr(n, SetExpr{});
            else
                init.value = expr(n, DictExpr{});
            fn->body.push_back(stmt(n, std::move(init)));
        }
        Suite body;
        if (generator)
            body.push_back(
                stmt(n, ExprStmt{expr(n, YieldExpr{expression(child(n, 0)), false, true})}));
        else {
            CollectionAddStmt add;
            add.collection = name();
            add.kind = n.kind == Kind::ListComp  ? detail::CollectionKind::List
                       : n.kind == Kind::SetComp ? detail::CollectionKind::Set
                                                 : detail::CollectionKind::Dict;
            if (dict) {
                add.key = expression(child(n, 0));
                add.value = expression(child(n, 1));
            } else
                add.value = expression(child(n, 0));
            body.push_back(stmt(n, std::move(add)));
        }
        for (std::size_t i = clauses.size(); i-- > 0;) {
            if (!clauses[i] || clauses[i]->kind != Kind::comprehension)
                invalid(n, "invalid comprehension clause");
            const auto &c = *clauses[i];
            const auto &filters = nodes(c, 2);
            for (std::size_t j = filters.size(); j-- > 0;) {
                if (!filters[j])
                    invalid(c, "null comprehension filter");
                IfStmt condition;
                condition.test = expression(*filters[j]);
                condition.body = std::move(body);
                body.clear();
                body.push_back(stmt(*filters[j], std::move(condition)));
            }
            auto async = integer(c, 3);
            ForStmt loop;
            loop.asynchronous = async != 0;
            loop.target = target(child(c, 0), Kind::Store);
            loop.iterator_ready = i == 0;
            loop.iterable = i == 0 ? expr(c, NameExpr{U".0"}) : expression(child(c, 1));
            loop.body = std::move(body);
            body.clear();
            body.push_back(stmt(c, std::move(loop)));
        }
        for (auto &item : body)
            fn->body.push_back(std::move(item));
        if (!generator)
            fn->body.push_back(stmt(n, ReturnStmt{name()}));
        return expr(
            n, ComprehensionExpr{std::move(fn), std::move(iterable), integer(*clauses[0], 3) != 0});
    }
    PatternCapture capture(const Node &n, std::size_t index)
    {
        auto name = optional_string(n, index);
        if (!name)
            return {};
        if (*name == U"_")
            invalid(n, "cannot capture '_' in patterns");
        if (*name == U"__debug__")
            syntax_error(n, "cannot assign to __debug__");
        return {*name, expr(n, NameExpr{*name})};
    }
    bool pattern_number(const Node &n, bool real, bool imaginary) const
    {
        if (n.kind != Kind::Constant)
            return false;
        auto value = std::get_if<Literal>(&field(n, 0));
        return value && ((real && (value->kind == Literal::Kind::Integer ||
                                   value->kind == Literal::Kind::Float)) ||
                         (imaginary && (value->kind == Literal::Kind::Imaginary ||
                                        value->kind == Literal::Kind::Complex)));
    }
    bool negative_number(const Node &n, bool real, bool imaginary) const
    {
        return n.kind == Kind::UnaryOp && child(n, 0).kind == Kind::USub &&
               pattern_number(child(n, 1), real, imaginary);
    }
    void pattern_value(const Node &n, bool mapping_key = false)
    {
        switch (n.kind) {
        case Kind::Constant: {
            auto value = std::get_if<Literal>(&field(n, 0));
            if (!value || value->kind == Literal::Kind::Ellipsis ||
                value->kind == Literal::Kind::Tuple || value->kind == Literal::Kind::FrozenSet ||
                (!mapping_key &&
                 (value->kind == Literal::Kind::None || value->kind == Literal::Kind::True ||
                  value->kind == Literal::Kind::False)))
                invalid(n, "invalid literal pattern");
            return;
        }
        case Kind::UnaryOp:
            if (!negative_number(n, true, true))
                invalid(n, "invalid negative literal pattern");
            return;
        case Kind::BinOp: {
            auto &op = child(n, 1), &left = child(n, 0), &right = child(n, 2);
            if ((op.kind != Kind::Add && op.kind != Kind::Sub) ||
                !(pattern_number(left, true, false) || negative_number(left, true, false)) ||
                !pattern_number(right, false, true))
                invalid(n, "invalid complex literal pattern");
            return;
        }
        case Kind::Attribute:
        case Kind::JoinedStr:
            return;
        default:
            invalid(n, "pattern must be a literal or attribute lookup");
        }
    }
    PatternPtr pattern(const Node &n, bool star_ok = false)
    {
        auto guard = enter(n);
        auto build = [&](auto value) {
            return std::make_unique<Pattern>(Pattern{n.span, std::move(value)});
        };
        switch (n.kind) {
        case Kind::MatchValue:
            pattern_value(child(n, 0));
            return build(ValuePattern{expression(child(n, 0))});
        case Kind::MatchSingleton: {
            auto literal = std::get_if<Literal>(&field(n, 0));
            if (!literal ||
                (literal->kind != Literal::Kind::None && literal->kind != Literal::Kind::True &&
                 literal->kind != Literal::Kind::False))
                invalid(n, "invalid singleton pattern");
            return build(ValuePattern{expr(n, *literal), true});
        }
        case Kind::MatchAs:
            if (auto p = optional(n, 0)) {
                auto name = capture(n, 1);
                if (!name.binding)
                    invalid(n, "MatchAs requires a name when a pattern is present");
                return build(AsPattern{pattern(*p), std::move(name)});
            }
            return build(CapturePattern{capture(n, 1)});
        case Kind::MatchStar:
            if (!star_ok)
                invalid(n, "MatchStar outside sequence pattern");
            return build(StarPattern{capture(n, 0)});
        case Kind::MatchSequence: {
            SequencePattern result;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null pattern");
                result.items.push_back(pattern(*p, true));
            }
            return build(std::move(result));
        }
        case Kind::MatchOr: {
            OrPattern result;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null pattern");
                result.items.push_back(pattern(*p));
            }
            if (result.items.size() < 2)
                invalid(n, "MatchOr requires at least two patterns");
            return build(std::move(result));
        }
        case Kind::MatchMapping: {
            MappingPattern result;
            auto &keys = nodes(n, 0), &values = nodes(n, 1);
            if (keys.size() != values.size())
                invalid(n, "mapping pattern keys and values differ in length");
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (!keys[i] || !values[i])
                    invalid(n, "null mapping pattern");
                pattern_value(*keys[i], true);
                result.items.emplace_back(expression(*keys[i]), pattern(*values[i]));
            }
            result.rest = capture(n, 2);
            return build(std::move(result));
        }
        case Kind::MatchClass: {
            auto cls = &child(n, 0);
            while (cls->kind == Kind::Attribute)
                cls = &child(*cls, 0);
            if (cls->kind != Kind::Name)
                invalid(n, "class pattern requires a name or attribute");
            ClassPattern result;
            result.type = expression(child(n, 0));
            for (const auto &p : nodes(n, 1)) {
                if (!p)
                    invalid(n, "null class pattern");
                result.positional.push_back(pattern(*p));
            }
            auto &names = strings(n, 2);
            auto &values = nodes(n, 3);
            if (names.size() != values.size())
                invalid(n, "class pattern names and values differ in length");
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (!values[i])
                    invalid(n, "null class pattern");
                result.keywords.emplace_back(names[i], pattern(*values[i]));
            }
            return build(std::move(result));
        }
        default:
            invalid(n, "invalid AST pattern kind");
        }
    }
    Suite suite(const Node &n, std::size_t index, bool required = false)
    {
        Suite result;
        for (const auto &p : nodes(n, index)) {
            if (!p)
                invalid(n, "null statement");
            result.push_back(statement(*p));
        }
        if (required && result.empty())
            invalid(n, "empty statement body");
        return result;
    }
    void call_parts(const Node &n, std::size_t base_index, std::size_t keyword_index,
                    std::vector<ExprPtr> &bases,
                    std::vector<std::pair<std::u32string, ExprPtr>> &keywords)
    {
        for (const auto &p : nodes(n, base_index)) {
            if (!p)
                invalid(n, "null argument");
            bases.push_back(expression(*p, true));
        }
        std::unordered_set<std::u32string> seen;
        for (const auto &p : nodes(n, keyword_index)) {
            if (!p || p->kind != Kind::keyword)
                invalid(n, "invalid keyword");
            auto name = optional_string(*p, 0);
            if (name && !seen.insert(*name).second)
                syntax_error(*p, "keyword argument repeated");
            keywords.emplace_back(name.value_or(U""), expression(child(*p, 1)));
        }
    }
    std::unique_ptr<Stmt> statement(const Node &n)
    {
        auto guard = enter(n);
        switch (n.kind) {
        case Kind::Expr:
            return stmt(n, ExprStmt{expression(child(n, 0))});
        case Kind::Pass:
            return stmt(n, ControlStmt::Pass);
        case Kind::Break:
            return stmt(n, ControlStmt::Break);
        case Kind::Continue:
            return stmt(n, ControlStmt::Continue);
        case Kind::Return:
            return stmt(n, ReturnStmt{maybe_expr(n, 0)});
        case Kind::Raise:
            return stmt(n, RaiseStmt{maybe_expr(n, 0), maybe_expr(n, 1)});
        case Kind::Assert:
            return stmt(n, AssertStmt{expression(child(n, 0)), maybe_expr(n, 1)});
        case Kind::Assign: {
            AssignStmt result;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null assignment target");
                result.targets.push_back(target(*p, Kind::Store));
            }
            if (result.targets.empty())
                invalid(n, "Assign requires targets");
            result.value = expression(child(n, 1));
            return stmt(n, std::move(result));
        }
        case Kind::AnnAssign: {
            auto &t = child(n, 0);
            if (t.kind != Kind::Name && t.kind != Kind::Attribute && t.kind != Kind::Subscript)
                invalid(t, "invalid annotated assignment target");
            auto simple = integer(n, 3);
            if (simple && t.kind != Kind::Name)
                invalid(n, "invalid AnnAssign simple flag");
            return stmt(n, AnnAssignStmt{target(t, Kind::Store), expression(child(n, 1)),
                                         maybe_expr(n, 2), simple != 0});
        }
        case Kind::AugAssign: {
            auto &t = child(n, 0);
            if (t.kind != Kind::Name && t.kind != Kind::Attribute && t.kind != Kind::Subscript)
                invalid(t, "invalid augmented assignment target");
            return stmt(n, AugAssignStmt{target(t, Kind::Store), binary(child(n, 1)),
                                         expression(child(n, 2))});
        }
        case Kind::Delete: {
            DeleteStmt result;
            for (const auto &p : nodes(n, 0)) {
                if (!p)
                    invalid(n, "null deletion target");
                result.targets.push_back(target(*p, Kind::Del));
            }
            if (result.targets.empty())
                invalid(n, "Delete requires targets");
            return stmt(n, std::move(result));
        }
        case Kind::If:
            return stmt(n, IfStmt{expression(child(n, 0)), suite(n, 1, true), suite(n, 2)});
        case Kind::While:
            return stmt(n, WhileStmt{expression(child(n, 0)), suite(n, 1, true), suite(n, 2)});
        case Kind::For:
        case Kind::AsyncFor: {
            ForStmt result;
            result.target = target(child(n, 0), Kind::Store);
            result.iterable = expression(child(n, 1));
            result.body = suite(n, 2, true);
            result.alternative = suite(n, 3);
            result.asynchronous = n.kind == Kind::AsyncFor;
            return stmt(n, std::move(result));
        }
        case Kind::With:
        case Kind::AsyncWith: {
            WithStmt result;
            result.asynchronous = n.kind == Kind::AsyncWith;
            for (const auto &p : nodes(n, 0)) {
                if (!p || p->kind != Kind::withitem)
                    invalid(n, "invalid with item");
                auto binding = optional(*p, 1);
                result.items.push_back(WithItem{expression(child(*p, 0)),
                                                binding ? target(*binding, Kind::Store) : nullptr});
            }
            if (result.items.empty())
                invalid(n, "With requires items");
            result.body = suite(n, 1, true);
            return stmt(n, std::move(result));
        }
        case Kind::Try:
        case Kind::TryStar: {
            TryStmt result;
            result.starred = n.kind == Kind::TryStar;
            result.body = suite(n, 0, true);
            for (const auto &p : nodes(n, 1)) {
                if (!p || p->kind != Kind::ExceptHandler)
                    invalid(n, "invalid exception handler");
                ExceptHandler handler;
                handler.span = p->span;
                handler.type = maybe_expr(*p, 0);
                if (auto name = optional_string(*p, 1))
                    handler.name = std::pair{*name, p->span};
                handler.body = suite(*p, 2, true);
                result.handlers.push_back(std::move(handler));
            }
            result.alternative = suite(n, 2);
            result.cleanup = suite(n, 3);
            if (result.handlers.empty() && result.cleanup.empty())
                invalid(n, "Try requires handlers or finalbody");
            if (result.handlers.empty() && !result.alternative.empty())
                invalid(n, "Try has else without handlers");
            if (result.starred && result.handlers.empty())
                invalid(n, "TryStar requires handlers");
            for (std::size_t i = 0; i < result.handlers.size(); ++i)
                if (!result.handlers[i].type && (result.starred || i + 1 != result.handlers.size()))
                    invalid(n, "invalid default except handler");
            return stmt(n, std::move(result));
        }
        case Kind::FunctionDef:
        case Kind::AsyncFunctionDef: {
            FunctionStmt result;
            result.name = string(n, 0);
            result.coroutine = n.kind == Kind::AsyncFunctionDef;
            parameters(child(n, 1), result);
            result.body = suite(n, 2, true);
            for (const auto &p : nodes(n, 3)) {
                if (!p)
                    invalid(n, "null decorator");
                result.decorators.push_back(expression(*p));
            }
            if (auto ret = optional(n, 4))
                result.annotations.emplace_back(U"return", expression(*ret));
            result.type_parameters = type_parameters(n, 6);
            return stmt(n, std::move(result));
        }
        case Kind::ClassDef: {
            ClassStmt result;
            result.name = string(n, 0);
            call_parts(n, 1, 2, result.bases, result.keywords);
            result.body = suite(n, 3, true);
            for (const auto &p : nodes(n, 4)) {
                if (!p)
                    invalid(n, "null decorator");
                result.decorators.push_back(expression(*p));
            }
            result.type_parameters = type_parameters(n, 5);
            return stmt(n, std::move(result));
        }
        case Kind::TypeAlias: {
            const auto &name = child(n, 0);
            if (name.kind != Kind::Name)
                invalid(name, "type alias name must be Name");
            require_context(name, 1, Kind::Store);
            TypeAliasStmt result;
            result.name = string(name, 0);
            result.type_parameters = type_parameters(n, 1);
            result.evaluator = std::make_unique<FunctionStmt>();
            result.evaluator->name = result.name;
            result.evaluator->annotation_role = AnnotationRole::TypeAlias;
            result.evaluator->body.push_back(stmt(n, ReturnStmt{expression(child(n, 2))}));
            return stmt(n, std::move(result));
        }
        case Kind::Global:
        case Kind::Nonlocal: {
            DeclareStmt result{n.kind == Kind::Nonlocal, {}};
            for (const auto &name : strings(n, 0))
                result.names.emplace_back(name, n.span);
            if (result.names.empty())
                invalid(n, "declaration requires names");
            return stmt(n, std::move(result));
        }
        case Kind::Import:
        case Kind::ImportFrom: {
            ImportStmt result;
            std::size_t index = 0;
            if (n.kind == Kind::ImportFrom) {
                result.module = optional_string(n, 0).value_or(U"");
                index = 1;
                auto level =
                    std::holds_alternative<std::monostate>(field(n, 2)) ? 0 : integer(n, 2);
                if (level < 0 || static_cast<std::uint64_t>(level) > UINT32_MAX)
                    invalid(n, "invalid import level");
                result.level = static_cast<std::uint32_t>(level);
            }
            for (const auto &p : nodes(n, index)) {
                if (!p || p->kind != Kind::alias)
                    invalid(n, "invalid import alias");
                result.names.push_back(
                    ImportAlias{string(*p, 0), optional_string(*p, 1), {}, p->span});
            }
            if (result.names.empty())
                invalid(n, "import requires names");
            return stmt(n, std::move(result));
        }
        case Kind::Match: {
            MatchStmt result;
            result.subject = expression(child(n, 0));
            for (const auto &p : nodes(n, 1)) {
                if (!p || p->kind != Kind::match_case)
                    invalid(n, "invalid match case");
                result.cases.push_back(MatchCase{pattern(child(*p, 0)), maybe_expr(*p, 1),
                                                 suite(*p, 2, true), p->span});
            }
            if (result.cases.empty())
                invalid(n, "Match requires cases");
            return stmt(n, std::move(result));
        }
        default:
            invalid(n, "invalid AST statement kind");
        }
    }

  public:
    explicit Lowerer(const SourceBuffer &source) : source_(source) {}
    Suite run(const Node &node, InputMode mode)
    {
        if (mode == InputMode::Eval) {
            if (node.kind != Kind::Expression)
                invalid(node, "expected Expression AST for eval mode");
            Suite result;
            result.push_back(stmt(child(node, 0), ReturnStmt{expression(child(node, 0))}));
            return result;
        }
        if (mode == InputMode::Single) {
            if (node.kind != Kind::Interactive)
                invalid(node, "expected Interactive AST for single mode");
            return suite(node, 0);
        }
        if (mode != InputMode::Exec || node.kind != Kind::Module)
            invalid(node, "expected Module AST for exec mode");
        for (const auto &item : nodes(node, 1))
            if (!item || item->kind != Kind::TypeIgnore)
                invalid(node, "invalid type ignore");
        return suite(node, 0);
    }
};
} // namespace

detail::Suite lower(const Node &node, const SourceBuffer &source, InputMode mode)
{
    return Lowerer(source).run(node, mode);
}
} // namespace cxxpython::core::ast
