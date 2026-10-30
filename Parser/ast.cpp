#include "cxxpython/floatobject.hpp"
#include "cxxpython/parser.hpp"
#include "internal/compiler.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core::detail {
namespace {
using syntax::Node;
using syntax::Rule;
using syntax::TokenKind;

class Adapter {
    const SourceBuffer &source_;
    syntax::Tree tree_;

    Span span(const Node &n) const
    {
        auto offset = [&](std::size_t i) {
            return i < tree_.tokens.size() ? tree_.tokens[i].span.begin : source_.text().size();
        };
        return {offset(n.begin), n.end > n.begin && n.end <= tree_.tokens.size()
                                     ? tree_.tokens[n.end - 1].span.end
                                     : offset(n.begin)};
    }
    [[noreturn]] void unsupported(const Node &n) const
    {
        throw SourceError(source_, span(n),
                          "unsupported syntax capability: " +
                              std::string(syntax::rule_name(n.rule)),
                          SourceErrorKind::Capability);
    }
    void flatten(const Node &n, std::vector<const Node *> &out) const
    {
        for (const auto &child : n.children) {
            if (child->rule == Rule::Group)
                flatten(*child, out);
            else
                out.push_back(child.get());
        }
    }
    std::vector<const Node *> children(const Node &n) const
    {
        std::vector<const Node *> out;
        flatten(n, out);
        return out;
    }
    const syntax::Token &token(const Node &n) const
    {
        return tree_.tokens.at(n.begin);
    }
    std::u32string identifier(const Node &n) const
    {
        return syntax::normalize_identifier(token(n).text);
    }
    bool is(const Node &n, std::u32string_view text) const
    {
        return n.rule == Rule::Token && token(n).text == text;
    }
    template <class T> ExprPtr make(const Node &n, T value, std::size_t depth = 1) const
    {
        if (depth > 128)
            throw SourceError(source_, span(n),
                              "expression nesting exceeds the current compiler limit");
        return std::make_unique<Expr>(Expr{span(n), depth, std::move(value)});
    }
    template <class T> std::unique_ptr<Stmt> stmt(const Node &n, T value) const
    {
        return std::make_unique<Stmt>(Stmt{span(n), std::move(value)});
    }
    std::vector<const Node *> rules(const Node &n) const
    {
        auto c = children(n);
        c.erase(std::remove_if(c.begin(), c.end(), [](auto p) { return p->rule == Rule::Token; }),
                c.end());
        return c;
    }
    const Node *find(const Node &n, Rule rule) const
    {
        for (auto p : children(n))
            if (p->rule == rule)
                return p;
        return nullptr;
    }
    bool has(const Node &n, std::u32string_view text) const
    {
        for (auto p : children(n))
            if (is(*p, text))
                return true;
        return false;
    }
    BinaryOp binary(std::u32string_view op, const Node &n) const
    {
        if (op == U"+" || op == U"+=")
            return BinaryOp::Add;
        if (op == U"-" || op == U"-=")
            return BinaryOp::Subtract;
        if (op == U"*" || op == U"*=")
            return BinaryOp::Multiply;
        if (op == U"/" || op == U"/=")
            return BinaryOp::TrueDivide;
        if (op == U"//" || op == U"//=")
            return BinaryOp::FloorDivide;
        if (op == U"%" || op == U"%=")
            return BinaryOp::Modulo;
        if (op == U"**" || op == U"**=")
            return BinaryOp::Power;
        if (op == U"<<" || op == U"<<=")
            return BinaryOp::LeftShift;
        if (op == U">>" || op == U">>=")
            return BinaryOp::RightShift;
        if (op == U"&" || op == U"&=")
            return BinaryOp::BitAnd;
        if (op == U"|" || op == U"|=")
            return BinaryOp::BitOr;
        if (op == U"^" || op == U"^=")
            return BinaryOp::BitXor;
        if (op == U"@" || op == U"@=")
            return BinaryOp::MatrixMultiply;
        unsupported(n);
    }
    std::u32string string_value(const Node &n, const std::u32string &text)
    {
        std::size_t quote = text.find_first_of(U"'\"");
        if (quote == std::u32string::npos)
            unsupported(n);
        bool raw = false, bytes = false;
        for (std::size_t i = 0; i < quote; ++i) {
            if (text[i] == U'f' || text[i] == U'F')
                unsupported(n);
            if (text[i] == U'b' || text[i] == U'B')
                bytes = true;
            if (text[i] == U'r' || text[i] == U'R')
                raw = true;
        }
        std::size_t q = quote + 2 < text.size() && text[quote + 1] == text[quote] &&
                                text[quote + 2] == text[quote]
                            ? 3
                            : 1;
        std::u32string out;
        for (std::size_t i = quote + q; i + q < text.size(); ++i) {
            char32_t ch = text[i];
            if (ch != U'\\' || raw || i + 1 + q >= text.size()) {
                out.push_back(ch);
                continue;
            }
            ch = text[++i];
            if (bytes && (ch == U'u' || ch == U'U' || ch == U'N')) {
                out.push_back(U'\\');
                out.push_back(ch);
                continue;
            }
            switch (ch) {
            case U'\n':
                break;
            case U'n':
                out.push_back(U'\n');
                break;
            case U'r':
                out.push_back(U'\r');
                break;
            case U't':
                out.push_back(U'\t');
                break;
            case U'b':
                out.push_back(U'\b');
                break;
            case U'f':
                out.push_back(U'\f');
                break;
            case U'v':
                out.push_back(U'\v');
                break;
            case U'a':
                out.push_back(U'\a');
                break;
            case U'\\':
            case U'\'':
            case U'"':
                out.push_back(ch);
                break;
            case U'N': {
                if (i + 1 + q >= text.size() || text[++i] != U'{')
                    throw SourceError(source_, span(n), "malformed named Unicode escape");
                const auto begin = ++i;
                while (i + q < text.size() && text[i] != U'}')
                    ++i;
                const auto value =
                    syntax::unicode_name(std::u32string_view(text).substr(begin, i - begin));
                if (!value)
                    throw SourceError(source_, span(n), "unknown Unicode character name");
                out.push_back(*value);
                break;
            }
            case U'x':
            case U'u':
            case U'U': {
                int count = ch == U'x' ? 2 : ch == U'u' ? 4 : 8;
                char32_t value = 0;
                for (int j = 0; j < count; ++j) {
                    if (++i + q >= text.size())
                        throw SourceError(source_, span(n), "truncated Unicode escape");
                    auto h = text[i];
                    int digit = h >= U'0' && h <= U'9'   ? h - U'0'
                                : h >= U'a' && h <= U'f' ? h - U'a' + 10
                                : h >= U'A' && h <= U'F' ? h - U'A' + 10
                                                         : -1;
                    if (digit < 0)
                        throw SourceError(source_, span(n), "invalid Unicode escape");
                    value = value * 16 + digit;
                }
                if (value > 0x10ffff)
                    throw SourceError(source_, span(n), "invalid Unicode escape");
                out.push_back(value);
                break;
            }
            default:
                if (ch >= U'0' && ch <= U'7') {
                    char32_t value = ch - U'0';
                    for (int j = 0; j < 2 && i + 1 + q < text.size() && text[i + 1] >= U'0' &&
                                    text[i + 1] <= U'7';
                         ++j)
                        value = value * 8 + text[++i] - U'0';
                    out.push_back(bytes ? value & 255 : value);
                } else {
                    out.push_back(U'\\');
                    out.push_back(ch);
                }
            }
        }
        return out;
    }
    ExprPtr terminal(const Node &n)
    {
        const auto &t = token(n);
        if (t.kind == TokenKind::NAME) {
            if (t.text == U"None")
                return make(n, Literal{Literal::Kind::None, {}});
            if (t.text == U"True")
                return make(n, Literal{Literal::Kind::True, {}});
            if (t.text == U"False")
                return make(n, Literal{Literal::Kind::False, {}});
            return make(n, NameExpr{identifier(n)});
        }
        if (t.kind == TokenKind::ELLIPSIS)
            return make(n, Literal{Literal::Kind::Ellipsis, {}});
        if (t.kind == TokenKind::STRING) {
            const auto prefix = t.text.substr(0, t.text.find_first_of(U"'\""));
            const auto kind = prefix.find_first_of(U"bB") == std::u32string::npos
                                  ? Literal::Kind::String
                                  : Literal::Kind::Bytes;
            return make(n, Literal{kind, string_value(n, t.text)});
        }
        if (t.kind == TokenKind::NUMBER) {
            auto text = t.text;
            const bool imaginary = !text.empty() && (text.back() == U'j' || text.back() == U'J');
            if (imaginary)
                text.pop_back();
            text.erase(std::remove(text.begin(), text.end(), U'_'), text.end());
            if ((imaginary || text.find_first_of(U".eE") != std::u32string::npos) &&
                !(text.size() > 1 && text[0] == U'0' && (text[1] == U'x' || text[1] == U'X'))) {
                double value = 0;
                if (!parse_float(text, value))
                    throw SourceError(source_, span(n), "invalid float literal");
                return make(n, Literal{imaginary ? Literal::Kind::Imaginary : Literal::Kind::Float,
                                       text, value});
            }
            return make(n, Literal{Literal::Kind::Integer, text});
        }
        unsupported(n);
    }
    ExprPtr expression(const Node &n)
    {
        if (n.rule == Rule::Token)
            return terminal(n);
        auto c = children(n);
        auto r = rules(n);
        switch (n.rule) {
        case Rule::await_primary:
            if (has(n, U"await")) {
                auto value = expression(*r.at(0));
                const auto depth = value->depth + 1;
                return make(n, AwaitExpr{std::move(value)}, depth);
            }
            break;
        case Rule::yield_expr: {
            auto value = r.empty() ? nullptr : expression(*r[0]);
            auto depth = value ? value->depth + 1 : 1;
            return make(n, YieldExpr{std::move(value), has(n, U"from")}, depth);
        }
        case Rule::lambdef: {
            auto fn = std::make_unique<FunctionStmt>();
            fn->name = U"<lambda>";
            if (auto p = find(n, Rule::lambda_params))
                parameters(*p, *fn);
            validate_parameters(n, *fn);
            auto body = find(n, Rule::expression);
            if (!body)
                unsupported(n);
            auto value = expression(*body);
            auto depth = value->depth + 1;
            fn->body.push_back(stmt(*body, ReturnStmt{std::move(value)}));
            return make(n, LambdaExpr{std::move(fn)}, depth);
        }
        case Rule::listcomp:
        case Rule::dictcomp:
        case Rule::setcomp:
        case Rule::genexp:
            return comprehension(n);
        case Rule::fstring:
            return fstring(n);
        case Rule::strings: {
            JoinedStrExpr joined;
            bool formatted = false;
            for (auto p : r) {
                auto e = expression(*p);
                if (auto nested = std::get_if<JoinedStrExpr>(&e->node)) {
                    formatted = true;
                    for (auto &item : nested->items)
                        joined.items.push_back(std::move(item));
                } else {
                    auto lit = std::get_if<Literal>(&e->node);
                    if (!lit ||
                        (lit->kind != Literal::Kind::String && lit->kind != Literal::Kind::Bytes))
                        unsupported(*p);
                    joined.items.push_back(std::move(e));
                }
            }
            if (formatted)
                return make(n, std::move(joined), 2);
            std::u32string value;
            for (auto &item : joined.items)
                value += std::get<Literal>(item->node).text;
            const auto kind = joined.items.empty() ? Literal::Kind::String
                                                   : std::get<Literal>(joined.items[0]->node).kind;
            return make(n, Literal{kind, std::move(value)});
        }
        case Rule::string:
            for (auto p : c)
                if (p->rule == Rule::Token && token(*p).kind == TokenKind::STRING)
                    return terminal(*p);
            unsupported(n);
        case Rule::atom:
            for (auto p : c)
                if (p->rule != Rule::Token || token(*p).kind == TokenKind::NAME ||
                    token(*p).kind == TokenKind::NUMBER || token(*p).kind == TokenKind::ELLIPSIS)
                    return expression(*p);
            unsupported(n);
        case Rule::expressions:
        case Rule::star_expressions:
        case Rule::star_targets:
        case Rule::star_targets_list_seq:
        case Rule::star_targets_tuple_seq:
        case Rule::del_targets:
        case Rule::star_named_expressions: {
            if (r.size() == 1 && !has(n, U","))
                return expression(*r[0]);
            TupleExpr tuple;
            std::size_t depth = 1;
            for (auto p : r) {
                auto e = expression(*p);
                depth = std::max(depth, e->depth + 1);
                tuple.items.push_back(std::move(e));
            }
            return make(n, std::move(tuple), depth);
        }
        case Rule::star_expression:
        case Rule::star_named_expression:
        case Rule::star_target:
        case Rule::starred_expression:
            if (has(n, U"*")) {
                auto e = expression(*r.at(0));
                auto d = e->depth + 1;
                return make(n, StarredExpr{std::move(e)}, d);
            }
            break;
        case Rule::single_target:
        case Rule::del_t_atom:
            if (n.rule == Rule::del_t_atom && (has(n, U"[") || has(n, U"("))) {
                if (n.alternative == 1)
                    return expression(*r.at(0));
                std::vector<ExprPtr> items;
                for (auto p : r)
                    for (auto item : rules(*p))
                        items.push_back(expression(*item));
                if (has(n, U"["))
                    return make(n, ListExpr{std::move(items)}, 2);
                return make(n, TupleExpr{std::move(items)}, 2);
            }
            if (r.empty())
                for (auto p : c)
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                        return terminal(*p);
            break;
        case Rule::assignment_expression: {
            const Node *name = nullptr;
            for (auto p : c)
                if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                    name = p;
                    break;
                }
            auto target = terminal(*name), value = expression(*r.at(0));
            auto d = std::max(target->depth, value->depth) + 1;
            return make(n, NamedExpr{std::move(target), std::move(value)}, d);
        }
        case Rule::expression:
            if (r.size() == 3) {
                auto body = expression(*r[0]), test = expression(*r[1]), alt = expression(*r[2]);
                auto d = std::max({body->depth, test->depth, alt->depth}) + 1;
                return make(n, ConditionalExpr{std::move(test), std::move(body), std::move(alt)},
                            d);
            }
            break;
        case Rule::disjunction:
        case Rule::conjunction:
            if (r.size() > 1) {
                auto left = expression(*r[0]);
                for (std::size_t i = 1; i < r.size(); ++i) {
                    auto right = expression(*r[i]);
                    auto d = std::max(left->depth, right->depth) + 1;
                    left = make(
                        n, BoolExpr{n.rule == Rule::conjunction, std::move(left), std::move(right)},
                        d);
                }
                return left;
            }
            break;
        case Rule::inversion:
        case Rule::factor:
            if (!c.empty() && c[0]->rule == Rule::Token) {
                auto op = token(*c[0]).text;
                auto e = expression(*r.at(0));
                auto d = e->depth + 1;
                return make(n,
                            UnaryExpr{op == U"not" ? UnaryOp::Not
                                      : op == U"+" ? UnaryOp::Positive
                                      : op == U"-" ? UnaryOp::Negative
                                                   : UnaryOp::Invert,
                                      std::move(e)},
                            d);
            }
            break;
        case Rule::bitwise_or:
        case Rule::bitwise_xor:
        case Rule::bitwise_and:
        case Rule::shift_expr:
        case Rule::sum:
        case Rule::term:
        case Rule::power:
            if (r.size() == 2) {
                auto a = expression(*r[0]), b = expression(*r[1]);
                auto d = std::max(a->depth, b->depth) + 1;
                for (auto p : c)
                    if (p->rule == Rule::Token)
                        return make(
                            n, BinaryExpr{binary(token(*p).text, n), std::move(a), std::move(b)},
                            d);
            }
            break;
        case Rule::comparison:
            if (r.size() > 1) {
                CompareExpr cmp;
                cmp.values.push_back(expression(*r[0]));
                std::size_t d = cmp.values[0]->depth;
                for (std::size_t i = 1; i < r.size(); ++i) {
                    auto pair = rules(*r[i]);
                    if (pair.size() != 1)
                        unsupported(*r[i]);
                    const auto &p = *pair[0];
                    auto rhs = rules(p);
                    if (rhs.size() != 1)
                        unsupported(p);
                    CompareOp op;
                    switch (p.rule) {
                    case Rule::eq_bitwise_or:
                        op = CompareOp::Eq;
                        break;
                    case Rule::noteq_bitwise_or:
                        op = CompareOp::Ne;
                        break;
                    case Rule::lt_bitwise_or:
                        op = CompareOp::Lt;
                        break;
                    case Rule::lte_bitwise_or:
                        op = CompareOp::Le;
                        break;
                    case Rule::gt_bitwise_or:
                        op = CompareOp::Gt;
                        break;
                    case Rule::gte_bitwise_or:
                        op = CompareOp::Ge;
                        break;
                    case Rule::is_bitwise_or:
                        op = CompareOp::Is;
                        break;
                    case Rule::isnot_bitwise_or:
                        op = CompareOp::IsNot;
                        break;
                    case Rule::in_bitwise_or:
                        op = CompareOp::In;
                        break;
                    case Rule::notin_bitwise_or:
                        op = CompareOp::NotIn;
                        break;
                    default:
                        unsupported(p);
                    }
                    cmp.ops.push_back(op);
                    auto e = expression(*rhs[0]);
                    d = std::max(d, e->depth);
                    cmp.values.push_back(std::move(e));
                }
                return make(n, std::move(cmp), d + 1);
            }
            break;
        case Rule::primary:
        case Rule::t_primary:
        case Rule::single_subscript_attribute_target:
        case Rule::target_with_star_atom:
        case Rule::del_target:
            if (has(n, U".")) {
                auto e = expression(*r.at(0));
                auto d = e->depth + 1;
                for (auto p : c)
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                        return make(n, AttributeExpr{std::move(e), identifier(*p)}, d);
            }
            if (has(n, U"[") && r.size() >= 2) {
                auto e = expression(*r[0]), key = expression(*r[1]);
                auto d = std::max(e->depth, key->depth) + 1;
                return make(n, SubscriptExpr{std::move(e), std::move(key)}, d);
            }
            if (r.size() == 2 && r[1]->rule == Rule::genexp) {
                CallExpr call;
                call.target = expression(*r[0]);
                call.positional.push_back(comprehension(*r[1]));
                const auto depth = std::max(call.target->depth, call.positional[0]->depth) + 1;
                return make(n, std::move(call), depth);
            }
            if (has(n, U"(")) {
                CallExpr call;
                call.target = expression(*r.at(0));
                if (r.size() > 1)
                    arguments(*r[1], call.positional, call.keywords);
                return make(n, std::move(call), 2);
            }
            break;
        case Rule::list:
        case Rule::tuple:
        case Rule::set:
        case Rule::star_atom: {
            if (n.rule == Rule::star_atom && r.empty())
                for (auto p : c)
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                        return terminal(*p);
            if (n.rule == Rule::star_atom && n.alternative == 1 && r.size() == 1)
                return expression(*r[0]);
            bool list = has(n, U"[");
            if (!list && !has(n, U"(") && n.rule != Rule::set)
                break;
            std::vector<ExprPtr> items;
            for (auto p : r) {
                if (p->rule == Rule::star_named_expressions ||
                    p->rule == Rule::star_targets_list_seq ||
                    p->rule == Rule::star_targets_tuple_seq) {
                    for (auto item : rules(*p))
                        items.push_back(expression(*item));
                } else
                    items.push_back(expression(*p));
            }
            if (list)
                return make(n, ListExpr{std::move(items)}, 2);
            if (n.rule == Rule::set)
                return make(n, SetExpr{std::move(items)}, 2);
            return make(n, TupleExpr{std::move(items)}, 2);
        }
        case Rule::slices:
            if (r.size() == 1 && !has(n, U","))
                return expression(*r[0]);
            {
                TupleExpr t;
                for (auto p : r)
                    t.items.push_back(expression(*p));
                return make(n, std::move(t), 2);
            }
        case Rule::slice:
            if (has(n, U":")) {
                SliceExpr s;
                int section = 0;
                for (auto p : c) {
                    if (is(*p, U":"))
                        ++section;
                    else if (p->rule != Rule::Token) {
                        if (section == 0)
                            s.start = expression(*p);
                        else if (section == 1)
                            s.stop = expression(*p);
                        else
                            s.step = expression(*p);
                    }
                }
                return make(n, std::move(s), 2);
            }
            break;
        case Rule::dict: {
            DictExpr dict;
            if (!r.empty())
                dict_items(*r[0], dict);
            return make(n, std::move(dict), 2);
        }
        default:
            break;
        }
        if (r.size() == 1)
            return expression(*r[0]);
        unsupported(n);
    }
    ExprPtr comprehension(const Node &n)
    {
        auto clauses = find(n, Rule::for_if_clauses);
        if (!clauses)
            unsupported(n);
        auto loops = rules(*clauses);
        if (loops.empty())
            unsupported(n);
        auto first_iter = find(*loops[0], Rule::disjunction);
        if (!first_iter)
            unsupported(*loops[0]);
        auto iterable = expression(*first_iter);
        auto fn = std::make_unique<FunctionStmt>();
        const bool generator = n.rule == Rule::genexp;
        fn->name = generator                  ? U"<genexpr>"
                   : n.rule == Rule::listcomp ? U"<listcomp>"
                   : n.rule == Rule::setcomp  ? U"<setcomp>"
                                              : U"<dictcomp>";
        fn->comprehension = true;
        fn->parameters.push_back(
            ParameterSpec{U".0", ParameterKind::PositionalOnly, nullptr, span(n)});
        auto name = [&]() { return make(n, NameExpr{U".result"}); };
        AssignStmt init;
        init.targets.push_back(name());
        if (n.rule == Rule::listcomp)
            init.value = make(n, ListExpr{});
        else if (n.rule == Rule::setcomp)
            init.value = make(n, SetExpr{});
        else
            init.value = make(n, DictExpr{});
        if (!generator)
            fn->body.push_back(stmt(n, std::move(init)));
        CollectionAddStmt add;
        add.collection = name();
        add.kind = n.rule == Rule::listcomp  ? CollectionKind::List
                   : n.rule == Rule::setcomp ? CollectionKind::Set
                                             : CollectionKind::Dict;
        if (n.rule == Rule::dictcomp) {
            auto pair = find(n, Rule::kvpair);
            if (!pair)
                unsupported(n);
            auto values = rules(*pair);
            if (values.size() != 2)
                unsupported(*pair);
            add.key = expression(*values[0]);
            add.value = expression(*values[1]);
        } else {
            auto parts = rules(n);
            auto value =
                generator ? (parts.empty() ? nullptr : parts[0]) : find(n, Rule::named_expression);
            if (!value)
                unsupported(n);
            add.value = expression(*value);
        }
        Suite body;
        if (generator)
            body.push_back(
                stmt(n, ExprStmt{make(n, YieldExpr{std::move(add.value), false, true})}));
        else
            body.push_back(stmt(n, std::move(add)));
        for (std::size_t index = loops.size(); index-- > 0;) {
            const auto &clause = *loops[index];
            if (has(clause, U"async"))
                unsupported(clause);
            auto target = find(clause, Rule::star_targets);
            auto parts = rules(clause);
            if (!target)
                unsupported(clause);
            std::vector<const Node *> tests;
            for (auto p : parts)
                if (p->rule == Rule::disjunction)
                    tests.push_back(p);
            if (tests.empty())
                unsupported(clause);
            for (std::size_t j = tests.size(); j-- > 1;) {
                IfStmt condition;
                condition.test = expression(*tests[j]);
                condition.body = std::move(body);
                body.clear();
                body.push_back(stmt(*tests[j], std::move(condition)));
            }
            ForStmt loop;
            loop.target = expression(*target);
            loop.iterator_ready = index == 0;
            validate_target(*loop.target);
            loop.iterable = index == 0 ? make(clause, NameExpr{U".0"}) : expression(*tests[0]);
            loop.body = std::move(body);
            body.clear();
            body.push_back(stmt(clause, std::move(loop)));
        }
        for (auto &item : body)
            fn->body.push_back(std::move(item));
        if (!generator)
            fn->body.push_back(stmt(n, ReturnStmt{name()}));
        return make(n, ComprehensionExpr{std::move(fn), std::move(iterable)}, 2);
    }
    std::u32string fstring_text(const Node &n, bool raw)
    {
        auto text = token(n).text;
        std::u32string collapsed;
        for (std::size_t i = 0; i < text.size(); ++i) {
            collapsed.push_back(text[i]);
            if ((text[i] == U'{' || text[i] == U'}') && i + 1 < text.size() &&
                text[i + 1] == text[i])
                ++i;
        }
        return string_value(n, (raw ? U"r'" : U"'") + collapsed + U"'");
    }
    std::u32string debug_text(std::size_t begin, std::size_t end, std::size_t first_token) const
    {
        std::u32string result;
        auto gap = [&](std::size_t from, std::size_t to) {
            while (from < to) {
                const auto comment = source_.text().find('#', from);
                if (comment == std::string::npos || comment >= to) {
                    result += decode_source(source_, {from, to});
                    break;
                }
                result += decode_source(source_, {from, comment});
                const auto newline = source_.text().find('\n', comment);
                from = newline == std::string::npos ? to : std::min(newline, to);
            }
        };
        auto at = begin;
        for (auto i = first_token; i < tree_.tokens.size(); ++i) {
            const auto where = tree_.tokens[i].span;
            if (where.begin >= end)
                break;
            if (where.end <= at)
                continue;
            gap(at, where.begin);
            result += decode_source(source_, {where.begin, std::min(where.end, end)});
            at = where.end;
        }
        gap(at, end);
        return result;
    }
    void fstring_parts(const Node &n, bool raw, JoinedStrExpr &joined)
    {
        if (n.rule == Rule::fstring_replacement_field) {
            auto c = children(n);
            const Node *value = nullptr, *open = nullptr, *equal = nullptr, *next = nullptr;
            const Node *conversion = nullptr, *format = nullptr;
            for (auto p : c) {
                if (is(*p, U"{"))
                    open = p;
                else if (is(*p, U"="))
                    equal = p;
                else if (p->rule == Rule::fstring_conversion)
                    conversion = p;
                else if (p->rule == Rule::fstring_full_format_spec)
                    format = p;
                else if (p->rule == Rule::star_expressions || p->rule == Rule::yield_expr)
                    value = p;
            }
            if (!value || !open)
                unsupported(n);
            if (equal) {
                for (auto p : c)
                    if (p->begin >= equal->end &&
                        (p == conversion || p == format || is(*p, U"}"))) {
                        next = p;
                        break;
                    }
                auto begin = span(*open).end, end = next ? span(*next).begin : span(*equal).end;
                joined.items.push_back(
                    make(n, Literal{Literal::Kind::String, debug_text(begin, end, open->end)}));
            }
            char conv = 0;
            if (conversion) {
                for (auto p : children(*conversion))
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                        auto text = token(*p).text;
                        if (text.size() != 1 ||
                            (text[0] != U's' && text[0] != U'r' && text[0] != U'a'))
                            throw SourceError(source_, span(*p),
                                              "invalid f-string conversion character");
                        conv = static_cast<char>(text[0]);
                    }
            } else if (equal && !format)
                conv = 'r';
            ExprPtr spec;
            if (format) {
                JoinedStrExpr parts;
                fstring_parts(*format, raw, parts);
                spec = make(*format, std::move(parts), 2);
            } else
                spec = make(n, Literal{Literal::Kind::String, U""});
            auto expr = expression(*value);
            auto depth = std::max(expr->depth, spec->depth) + 1;
            joined.items.push_back(
                make(n, FormattedExpr{std::move(expr), std::move(spec), conv}, depth));
            return;
        }
        for (auto p : children(n)) {
            if (p->rule == Rule::Token && token(*p).kind == TokenKind::FSTRING_MIDDLE)
                joined.items.push_back(
                    make(*p, Literal{Literal::Kind::String, fstring_text(*p, raw)}));
            else if (p->rule == Rule::fstring_middle || p->rule == Rule::fstring_format_spec ||
                     p->rule == Rule::fstring_replacement_field)
                fstring_parts(*p, raw, joined);
        }
    }
    ExprPtr fstring(const Node &n)
    {
        bool raw = false;
        for (auto p : children(n))
            if (p->rule == Rule::Token && token(*p).kind == TokenKind::FSTRING_START) {
                const auto &text = token(*p).text;
                raw = text.find_first_of(U"rR") != std::u32string::npos;
            }
        JoinedStrExpr joined;
        fstring_parts(n, raw, joined);
        return make(n, std::move(joined), 2);
    }
    void dict_items(const Node &n, DictExpr &dict)
    {
        auto r = rules(n);
        if (n.rule == Rule::kvpair) {
            dict.items.emplace_back(expression(*r.at(0)), expression(*r.at(1)));
            return;
        }
        if (n.rule == Rule::double_starred_kvpair && has(n, U"**")) {
            dict.items.emplace_back(nullptr, expression(*r.at(0)));
            return;
        }
        for (auto p : r)
            dict_items(*p, dict);
    }
    void arguments(const Node &n, std::vector<ExprPtr> &pos,
                   std::vector<std::pair<std::u32string, ExprPtr>> &kw)
    {
        if (n.rule == Rule::arguments || n.rule == Rule::args || n.rule == Rule::kwargs) {
            for (auto p : rules(n))
                arguments(*p, pos, kw);
            return;
        }
        if (n.rule == Rule::kwarg_or_starred || n.rule == Rule::kwarg_or_double_starred) {
            if (has(n, U"=")) {
                for (auto p : children(n))
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                        auto name = identifier(*p);
                        for (const auto &entry : kw)
                            if (entry.first == name)
                                throw SourceError(source_, span(*p), "keyword argument repeated");
                        kw.emplace_back(std::move(name), expression(*rules(n).at(0)));
                        return;
                    }
            }
            if (has(n, U"**")) {
                kw.emplace_back(U"", expression(*rules(n).at(0)));
                return;
            }
            for (const auto &entry : kw)
                if (entry.first.empty())
                    throw SourceError(
                        source_, span(n),
                        "iterable argument unpacking follows keyword argument unpacking");
            for (auto p : rules(n))
                arguments(*p, pos, kw);
            return;
        }
        if (n.rule == Rule::starred_expression)
            for (const auto &entry : kw)
                if (entry.first.empty())
                    throw SourceError(
                        source_, span(n),
                        "iterable argument unpacking follows keyword argument unpacking");
        pos.push_back(expression(n));
    }
    void validate_target(const Expr &value, bool deleting = false, bool nested = false)
    {
        if (auto star = std::get_if<StarredExpr>(&value.node)) {
            if (deleting || !nested)
                throw SourceError(source_, value.span, "invalid starred assignment target");
            validate_target(*star->value, false, false);
            return;
        }
        const std::vector<ExprPtr> *items = nullptr;
        if (auto tuple = std::get_if<TupleExpr>(&value.node))
            items = &tuple->items;
        if (auto list = std::get_if<ListExpr>(&value.node))
            items = &list->items;
        if (items) {
            unsigned stars = 0;
            for (const auto &item : *items) {
                if (std::holds_alternative<StarredExpr>(item->node))
                    ++stars;
                validate_target(*item, deleting, true);
            }
            if (stars > 1)
                throw SourceError(source_, value.span,
                                  "multiple starred expressions in assignment");
            return;
        }
        if (!std::holds_alternative<NameExpr>(value.node) &&
            !std::holds_alternative<AttributeExpr>(value.node) &&
            !std::holds_alternative<SubscriptExpr>(value.node))
            throw SourceError(source_, value.span, "invalid assignment target");
    }
    void parameters(const Node &n, FunctionStmt &fn,
                    ParameterKind kind = ParameterKind::PositionalOrKeyword)
    {
        switch (n.rule) {
        case Rule::slash_no_default:
        case Rule::slash_with_default:
        case Rule::lambda_slash_no_default:
        case Rule::lambda_slash_with_default:
            kind = ParameterKind::PositionalOnly;
            break;
        case Rule::star_etc:
        case Rule::lambda_star_etc:
            kind = ParameterKind::KeywordOnly;
            break;
        case Rule::param_no_default:
        case Rule::param_with_default:
        case Rule::param_maybe_default:
        case Rule::lambda_param_no_default:
        case Rule::lambda_param_with_default:
        case Rule::lambda_param_maybe_default: {
            auto p = find(n, Rule::param);
            if (!p)
                p = find(n, Rule::lambda_param);
            if (!p)
                unsupported(n);
            if (find(*p, Rule::annotation))
                unsupported(*p);
            const Node *name = nullptr;
            for (auto child : children(*p))
                if (child->rule == Rule::Token && token(*child).kind == TokenKind::NAME) {
                    name = child;
                    break;
                }
            if (!name)
                unsupported(n);
            ExprPtr value;
            if (auto def = find(n, Rule::default_)) {
                auto dr = rules(*def);
                if (dr.size() != 1)
                    unsupported(*def);
                value = expression(*dr[0]);
            }
            fn.parameters.push_back(
                ParameterSpec{identifier(*name), kind, std::move(value), span(*p)});
            return;
        }
        case Rule::kwds:
        case Rule::lambda_kwds: {
            auto p = find(n, Rule::param_no_default);
            if (!p)
                p = find(n, Rule::lambda_param_no_default);
            if (!p)
                unsupported(n);
            FunctionStmt temp;
            parameters(*p, temp, kind);
            fn.varkwargs = temp.parameters.at(0).name;
            return;
        }
        default:
            break;
        }
        // A named star parameter is the first param_no_default immediately following '*'.
        if (n.rule == Rule::star_etc || n.rule == Rule::lambda_star_etc) {
            auto c = children(n);
            bool after_star = false;
            for (auto p : c) {
                if (is(*p, U"*")) {
                    after_star = true;
                    continue;
                }
                if (after_star && (p->rule == Rule::param_no_default ||
                                   p->rule == Rule::lambda_param_no_default)) {
                    FunctionStmt temp;
                    parameters(*p, temp, kind);
                    fn.varargs = temp.parameters.at(0).name;
                    after_star = false;
                    continue;
                }
                if (p->rule == Rule::Token && is(*p, U","))
                    after_star = false;
                if (p->rule != Rule::Token)
                    parameters(*p, fn, kind);
            }
            return;
        }
        for (auto p : rules(n))
            parameters(*p, fn, kind);
    }
    void validate_parameters(const Node &n, const FunctionStmt &fn)
    {
        std::unordered_set<std::u32string> names;
        bool seen_default = false;
        for (const auto &p : fn.parameters) {
            if (!names.insert(p.name).second)
                throw SourceError(source_, p.span, "duplicate argument in function definition");
            if (p.kind != ParameterKind::KeywordOnly) {
                if (p.default_value)
                    seen_default = true;
                else if (seen_default)
                    throw SourceError(source_, p.span,
                                      "non-default argument follows default argument");
            }
        }
        for (const auto *name : {&fn.varargs, &fn.varkwargs})
            if (*name && !names.insert(**name).second)
                throw SourceError(source_, span(n), "duplicate argument in function definition");
    }
    std::u32string definition_name(const Node &n, std::u32string_view keyword)
    {
        bool seen = false;
        for (auto p : children(n))
            if (p->rule == Rule::Token) {
                if (is(*p, keyword)) {
                    seen = true;
                    continue;
                }
                if (seen && token(*p).kind == TokenKind::NAME)
                    return identifier(*p);
            }
        unsupported(n);
    }
    std::u32string dotted(const Node &n)
    {
        std::u32string out;
        for (auto p : children(n)) {
            if (p->rule == Rule::dotted_name)
                out += dotted(*p);
            else if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                out += identifier(*p);
            else if (is(*p, U"."))
                out += U".";
        }
        return out;
    }
    void import_names(const Node &n, std::vector<ImportAlias> &names)
    {
        if (n.rule == Rule::dotted_as_name || n.rule == Rule::import_from_as_name) {
            ImportAlias alias;
            alias.span = span(n);
            bool as = false;
            for (auto p : children(n)) {
                if (p->rule == Rule::dotted_name)
                    alias.name = dotted(*p);
                else if (is(*p, U"as"))
                    as = true;
                else if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                    if (as)
                        alias.asname = identifier(*p);
                    else
                        alias.name = identifier(*p);
                }
            }
            names.push_back(std::move(alias));
            return;
        }
        if (n.rule == Rule::import_from_targets && has(n, U"*")) {
            names.push_back(ImportAlias{U"*", std::nullopt, {}, span(n)});
            return;
        }
        for (auto p : rules(n))
            import_names(*p, names);
    }
    Suite suite(const Node &n)
    {
        Suite result;
        if (n.rule == Rule::file || n.rule == Rule::interactive ||
            n.rule == Rule::statement_newline || n.rule == Rule::statements ||
            n.rule == Rule::statement || n.rule == Rule::simple_stmts ||
            n.rule == Rule::compound_stmt || n.rule == Rule::block) {
            for (auto p : rules(n)) {
                auto s = suite(*p);
                for (auto &item : s)
                    result.push_back(std::move(item));
            }
            return result;
        }
        auto c = children(n);
        auto r = rules(n);
        if (n.rule == Rule::simple_stmt) {
            if (!r.empty())
                return suite(*r[0]);
            for (auto p : c) {
                if (is(*p, U"pass"))
                    result.push_back(stmt(n, ControlStmt::Pass));
                if (is(*p, U"break"))
                    result.push_back(stmt(n, ControlStmt::Break));
                if (is(*p, U"continue"))
                    result.push_back(stmt(n, ControlStmt::Continue));
            }
            if (!result.empty())
                return result;
        }
        switch (n.rule) {
        case Rule::star_expressions:
            result.push_back(stmt(n, ExprStmt{expression(n)}));
            break;
        case Rule::yield_stmt:
            result.push_back(stmt(n, ExprStmt{expression(*r.at(0))}));
            break;
        case Rule::assignment: {
            if (find(n, Rule::augassign)) {
                const Node *op = find(n, Rule::augassign);
                auto oc = children(*op);
                auto target = expression(*r.at(0));
                auto value = expression(*r.back());
                validate_target(*target);
                result.push_back(
                    stmt(n, AugAssignStmt{std::move(target), binary(token(*oc.at(0)).text, *op),
                                          std::move(value)}));
            } else {
                if (has(n, U":"))
                    unsupported(n);
                AssignStmt a;
                for (std::size_t i = 0; i + 1 < r.size(); ++i)
                    a.targets.push_back(expression(*r[i]));
                if (a.targets.empty())
                    unsupported(n);
                for (const auto &target : a.targets)
                    validate_target(*target);
                a.value = expression(*r.back());
                result.push_back(stmt(n, std::move(a)));
            }
            break;
        }
        case Rule::return_stmt:
            result.push_back(stmt(n, ReturnStmt{r.empty() ? nullptr : expression(*r[0])}));
            break;
        case Rule::raise_stmt:
            result.push_back(stmt(n, RaiseStmt{r.empty() ? nullptr : expression(*r[0]),
                                               r.size() < 2 ? nullptr : expression(*r[1])}));
            break;
        case Rule::assert_stmt:
            result.push_back(stmt(
                n, AssertStmt{expression(*r.at(0)), r.size() < 2 ? nullptr : expression(*r[1])}));
            break;
        case Rule::del_stmt: {
            auto e = expression(*r.at(0));
            DeleteStmt d;
            validate_target(*e, true);
            if (auto t = std::get_if<TupleExpr>(&e->node))
                d.targets = std::move(t->items);
            else
                d.targets.push_back(std::move(e));
            result.push_back(stmt(n, std::move(d)));
            break;
        }
        case Rule::global_stmt:
        case Rule::nonlocal_stmt: {
            DeclareStmt d{n.rule == Rule::nonlocal_stmt, {}};
            for (auto p : c)
                if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME &&
                    !is(*p, U"global") && !is(*p, U"nonlocal"))
                    d.names.emplace_back(identifier(*p), span(*p));
            result.push_back(stmt(n, std::move(d)));
            break;
        }
        case Rule::import_stmt:
            if (r.size() == 1)
                return suite(*r[0]);
            unsupported(n);
        case Rule::import_name:
        case Rule::import_from: {
            ImportStmt s;
            if (n.rule == Rule::import_from) {
                s.module = U"";
                if (auto p = find(n, Rule::dotted_name))
                    s.module = dotted(*p);
                for (auto p : c)
                    if (p->rule == Rule::Token) {
                        if (is(*p, U"."))
                            ++s.level;
                        else if (is(*p, U"..."))
                            s.level += 3;
                    }
                auto p = find(n, Rule::import_from_targets);
                if (!p)
                    unsupported(n);
                import_names(*p, s.names);
            } else {
                auto p = find(n, Rule::dotted_as_names);
                if (!p)
                    unsupported(n);
                import_names(*p, s.names);
            }
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::if_stmt:
        case Rule::elif_stmt: {
            IfStmt s;
            s.test = expression(*find(n, Rule::named_expression));
            s.body = suite(*find(n, Rule::block));
            if (auto p = find(n, Rule::elif_stmt))
                s.alternative = suite(*p);
            else if (auto p = find(n, Rule::else_block))
                s.alternative = suite(*find(*p, Rule::block));
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::while_stmt: {
            WhileStmt s;
            s.test = expression(*find(n, Rule::named_expression));
            s.body = suite(*find(n, Rule::block));
            if (auto p = find(n, Rule::else_block))
                s.alternative = suite(*find(*p, Rule::block));
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::for_stmt: {
            if (has(n, U"async"))
                unsupported(n);
            ForStmt s;
            s.target = expression(*find(n, Rule::star_targets));
            s.iterable = expression(*find(n, Rule::star_expressions));
            s.body = suite(*find(n, Rule::block));
            validate_target(*s.target);
            if (auto p = find(n, Rule::else_block))
                s.alternative = suite(*find(*p, Rule::block));
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::with_stmt: {
            if (has(n, U"async"))
                unsupported(n);
            WithStmt s;
            for (auto p : r)
                if (p->rule == Rule::with_item) {
                    auto value = find(*p, Rule::expression);
                    auto target = find(*p, Rule::star_target);
                    if (!value)
                        unsupported(*p);
                    auto binding = target ? expression(*target) : nullptr;
                    if (binding)
                        validate_target(*binding);
                    s.items.push_back(WithItem{expression(*value), std::move(binding)});
                }
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            s.body = suite(*block);
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::try_stmt: {
            TryStmt s;
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            s.body = suite(*block);
            for (auto p : r) {
                if (p->rule == Rule::except_star_block)
                    unsupported(*p);
                if (p->rule == Rule::except_block) {
                    ExceptHandler h;
                    h.span = span(*p);
                    if (auto e = find(*p, Rule::expression))
                        h.type = expression(*e);
                    bool as = false;
                    for (auto q : children(*p)) {
                        if (is(*q, U"as"))
                            as = true;
                        else if (as && q->rule == Rule::Token &&
                                 token(*q).kind == TokenKind::NAME) {
                            h.name = std::pair{identifier(*q), span(*q)};
                            break;
                        }
                    }
                    auto b = find(*p, Rule::block);
                    if (!b)
                        unsupported(*p);
                    h.body = suite(*b);
                    if (!s.handlers.empty() && !s.handlers.back().type)
                        throw SourceError(source_, span(*p), "default except must be last");
                    s.handlers.push_back(std::move(h));
                } else if (p->rule == Rule::else_block)
                    s.alternative = suite(*find(*p, Rule::block));
                else if (p->rule == Rule::finally_block)
                    s.cleanup = suite(*find(*p, Rule::block));
            }
            result.push_back(stmt(n, std::move(s)));
            break;
        }
        case Rule::function_def:
        case Rule::class_def: {
            auto raw = find(n, n.rule == Rule::function_def ? Rule::function_def_raw
                                                            : Rule::class_def_raw);
            if (!raw)
                unsupported(n);
            auto s = suite(*raw);
            s.at(0)->span.begin = span(n).begin;
            if (auto decorators = find(n, Rule::decorators)) {
                for (auto p : rules(*decorators)) {
                    if (n.rule == Rule::function_def)
                        std::get<FunctionStmt>(s.at(0)->node).decorators.push_back(expression(*p));
                    else
                        std::get<ClassStmt>(s.at(0)->node).decorators.push_back(expression(*p));
                }
            }
            return s;
        }
        case Rule::function_def_raw: {
            if (has(n, U"->") || find(n, Rule::type_params))
                unsupported(n);
            FunctionStmt f;
            f.coroutine = has(n, U"async");
            f.name = definition_name(n, U"def");
            if (auto p = find(n, Rule::params))
                parameters(*p, f);
            validate_parameters(n, f);
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            f.body = suite(*block);
            result.push_back(stmt(n, std::move(f)));
            break;
        }
        case Rule::class_def_raw: {
            if (find(n, Rule::type_params))
                unsupported(n);
            ClassStmt cl;
            cl.name = definition_name(n, U"class");
            if (auto p = find(n, Rule::arguments))
                arguments(*p, cl.bases, cl.keywords);
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            cl.body = suite(*block);
            result.push_back(stmt(n, std::move(cl)));
            break;
        }
        default:
            unsupported(n);
        }
        return result;
    }

  public:
    Adapter(const SourceBuffer &source, syntax::Tree tree) : source_(source), tree_(std::move(tree))
    {}
    Suite run(InputMode mode)
    {
        if (mode == InputMode::Eval) {
            auto p = find(*tree_.root, Rule::expressions);
            auto e = expression(*p);
            Suite result;
            result.push_back(stmt(*p, ReturnStmt{std::move(e)}));
            return result;
        }
        return suite(*tree_.root);
    }
};
} // namespace

Suite parse_source(const SourceBuffer &source, InputMode mode)
{
    try {
        auto tree = syntax::parse(source.text(), mode == InputMode::Eval ? syntax::InputMode::Eval
                                                 : mode == InputMode::Single
                                                     ? syntax::InputMode::Interactive
                                                     : syntax::InputMode::File);
        return Adapter(source, std::move(tree)).run(mode);
    } catch (const syntax::Error &error) {
        SourceErrorKind kind = SourceErrorKind::Syntax;
        switch (error.kind) {
        case syntax::ErrorKind::Indentation:
            kind = SourceErrorKind::Indentation;
            break;
        case syntax::ErrorKind::Tab:
            kind = SourceErrorKind::Tab;
            break;
        case syntax::ErrorKind::Encoding:
            kind = SourceErrorKind::Encoding;
            break;
        case syntax::ErrorKind::Syntax:
        case syntax::ErrorKind::Incomplete:
            break;
        }
        throw SourceError(source, {error.span.begin, error.span.end}, error.what(), kind);
    }
}
} // namespace cxxpython::core::detail
