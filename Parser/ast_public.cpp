#include "internal/ast_nodes.hpp"

#include "cxxpython/floatobject.hpp"

#include <algorithm>

namespace cxxpython::core::ast {
namespace {
using syntax::Node;
using syntax::Rule;
using syntax::TokenKind;

class Adapter {
    const SourceBuffer &source_;
    syntax::Tree tree_;
    std::size_t depth_ = 0;

    Span span(const Node &node) const
    {
        auto begin = node.begin < tree_.tokens.size() ? tree_.tokens[node.begin].span.begin
                                                      : source_.text().size();
        auto last = std::min(node.end, tree_.tokens.size());
        while (last > node.begin) {
            auto kind = tree_.tokens[last - 1].kind;
            if (kind != TokenKind::NEWLINE && kind != TokenKind::INDENT &&
                kind != TokenKind::DEDENT && kind != TokenKind::ENDMARKER)
                break;
            --last;
        }
        auto end = last > node.begin ? tree_.tokens[last - 1].span.end : begin;
        return {begin, end};
    }
    [[noreturn]] void unsupported(const Node &node) const
    {
        throw SourceError(source_, span(node),
                          "unsupported public AST syntax: " +
                              std::string(syntax::rule_name(node.rule)),
                          SourceErrorKind::Capability);
    }
    void flatten(const Node &node, std::vector<const Node *> &out) const
    {
        for (const auto &child : node.children) {
            if (child->rule == Rule::Group)
                flatten(*child, out);
            else
                out.push_back(child.get());
        }
    }
    std::vector<const Node *> children(const Node &node) const
    {
        std::vector<const Node *> result;
        flatten(node, result);
        return result;
    }
    std::vector<const Node *> rules(const Node &node) const
    {
        auto result = children(node);
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [](auto p) { return p->rule == Rule::Token; }),
                     result.end());
        return result;
    }
    const syntax::Token &token(const Node &node) const
    {
        return tree_.tokens.at(node.begin);
    }
    bool is(const Node &node, std::u32string_view text) const
    {
        return node.rule == Rule::Token && token(node).text == text;
    }
    bool has(const Node &node, std::u32string_view text) const
    {
        for (auto p : children(node))
            if (is(*p, text))
                return true;
        return false;
    }
    const Node *find(const Node &node, Rule rule) const
    {
        for (auto p : children(node))
            if (p->rule == rule)
                return p;
        return nullptr;
    }
    std::u32string identifier(const Node &node) const
    {
        return syntax::normalize_identifier(token(node).text);
    }
    NodePtr marker(Kind kind) const
    {
        return make(kind, Span{});
    }
    void context(Node &node, Kind kind) const = delete;
    void context(ast::Node &node, Kind kind) const
    {
        switch (node.kind) {
        case Kind::Name:
        case Kind::Attribute:
        case Kind::Subscript:
        case Kind::Starred:
        case Kind::List:
        case Kind::Tuple:
            node.fields.back() = marker(kind);
            if (node.kind == Kind::Starred)
                context(*std::get<NodePtr>(node.fields[0]), kind);
            else if (node.kind == Kind::List || node.kind == Kind::Tuple)
                for (auto &item : std::get<Nodes>(node.fields[0]))
                    context(*item, kind);
            break;
        default:
            break;
        }
    }
    Kind binary(std::u32string_view op, const Node &node) const
    {
        if (op == U"+" || op == U"+=")
            return Kind::Add;
        if (op == U"-" || op == U"-=")
            return Kind::Sub;
        if (op == U"*" || op == U"*=")
            return Kind::Mult;
        if (op == U"@" || op == U"@=")
            return Kind::MatMult;
        if (op == U"/" || op == U"/=")
            return Kind::Div;
        if (op == U"//" || op == U"//=")
            return Kind::FloorDiv;
        if (op == U"%" || op == U"%=")
            return Kind::Mod;
        if (op == U"**" || op == U"**=")
            return Kind::Pow;
        if (op == U"<<" || op == U"<<=")
            return Kind::LShift;
        if (op == U">>" || op == U">>=")
            return Kind::RShift;
        if (op == U"|" || op == U"|=")
            return Kind::BitOr;
        if (op == U"^" || op == U"^=")
            return Kind::BitXor;
        if (op == U"&" || op == U"&=")
            return Kind::BitAnd;
        unsupported(node);
    }
    NodePtr terminal(const Node &node)
    {
        const auto &t = token(node);
        using detail::Literal;
        if (t.kind == TokenKind::NAME) {
            if (t.text == U"None" || t.text == U"True" || t.text == U"False") {
                auto kind = t.text == U"None"   ? Literal::Kind::None
                            : t.text == U"True" ? Literal::Kind::True
                                                : Literal::Kind::False;
                return make(Kind::Constant, span(node), Literal{kind, {}}, std::monostate{});
            }
            return make(Kind::Name, span(node), identifier(node), marker(Kind::Load));
        }
        if (t.kind == TokenKind::ELLIPSIS)
            return make(Kind::Constant, span(node), Literal{Literal::Kind::Ellipsis, {}},
                        std::monostate{});
        if (t.kind == TokenKind::STRING) {
            auto prefix = t.text.substr(0, t.text.find_first_of(U"'\""));
            auto kind = prefix.find_first_of(U"bB") == std::u32string::npos ? Literal::Kind::String
                                                                            : Literal::Kind::Bytes;
            auto value = syntax::decode_string_literal(t.text, {span(node).begin, span(node).end});
            Field string_kind =
                prefix == U"u" ? Field(std::u32string(U"u")) : Field(std::monostate{});
            return make(Kind::Constant, span(node), Literal{kind, std::move(value)},
                        std::move(string_kind));
        }
        if (t.kind == TokenKind::NUMBER) {
            auto text = t.text;
            bool imaginary = !text.empty() && (text.back() == U'j' || text.back() == U'J');
            if (imaginary)
                text.pop_back();
            text.erase(std::remove(text.begin(), text.end(), U'_'), text.end());
            if ((imaginary || text.find_first_of(U".eE") != std::u32string::npos) &&
                !(text.size() > 1 && text[0] == U'0' && (text[1] == U'x' || text[1] == U'X'))) {
                double value = 0;
                if (!detail::parse_float(text, value))
                    throw SourceError(source_, span(node), "invalid float literal");
                return make(Kind::Constant, span(node),
                            Literal{imaginary ? Literal::Kind::Imaginary : Literal::Kind::Float,
                                    text, value},
                            std::monostate{});
            }
            return make(Kind::Constant, span(node), Literal{Literal::Kind::Integer, text},
                        std::monostate{});
        }
        unsupported(node);
    }
    struct Parameters {
        Nodes positional_only, positional, keyword_only, keyword_defaults, defaults;
        NodePtr vararg, kwarg;
    };
    void parameters(const Node &n, Parameters &out, int category = 0)
    {
        switch (n.rule) {
        case Rule::slash_no_default:
        case Rule::slash_with_default:
        case Rule::lambda_slash_no_default:
        case Rule::lambda_slash_with_default:
            category = 1;
            break;
        case Rule::star_etc:
        case Rule::lambda_star_etc:
            category = 2;
            break;
        case Rule::param_no_default:
        case Rule::param_no_default_star_annotation:
        case Rule::param_with_default:
        case Rule::param_maybe_default:
        case Rule::lambda_param_no_default:
        case Rule::lambda_param_with_default:
        case Rule::lambda_param_maybe_default: {
            auto p = find(n, Rule::param);
            if (!p)
                p = find(n, Rule::param_star_annotation);
            if (!p)
                p = find(n, Rule::lambda_param);
            if (!p)
                unsupported(n);
            const Node *name = nullptr;
            for (auto child : children(*p))
                if (child->rule == Rule::Token && token(*child).kind == TokenKind::NAME) {
                    name = child;
                    break;
                }
            if (!name)
                unsupported(n);
            Field annotation = std::monostate{};
            if (auto a = find(*p, Rule::annotation))
                annotation = expression(*rules(*a).at(0));
            else if (auto a = find(*p, Rule::star_annotation))
                annotation = expression(*rules(*a).at(0));
            auto arg = make(Kind::arg, span(*p), identifier(*name), std::move(annotation),
                            std::monostate{});
            NodePtr value;
            if (auto def = find(n, Rule::default_))
                value = expression(*rules(*def).at(0));
            if (category == 3)
                out.vararg = std::move(arg);
            else if (category == 4)
                out.kwarg = std::move(arg);
            else if (category == 2) {
                out.keyword_only.push_back(std::move(arg));
                out.keyword_defaults.push_back(std::move(value));
            } else {
                (category == 1 ? out.positional_only : out.positional).push_back(std::move(arg));
                if (value)
                    out.defaults.push_back(std::move(value));
            }
            return;
        }
        case Rule::kwds:
        case Rule::lambda_kwds:
            for (auto p : rules(n))
                parameters(*p, out, 4);
            return;
        default:
            break;
        }
        if (n.rule == Rule::star_etc || n.rule == Rule::lambda_star_etc) {
            bool after_star = false;
            for (auto p : children(n)) {
                if (is(*p, U"*")) {
                    after_star = true;
                    continue;
                }
                if (after_star && (p->rule == Rule::param_no_default ||
                                   p->rule == Rule::param_no_default_star_annotation ||
                                   p->rule == Rule::lambda_param_no_default)) {
                    parameters(*p, out, 3);
                    after_star = false;
                    continue;
                }
                if (is(*p, U","))
                    after_star = false;
                if (p->rule != Rule::Token)
                    parameters(*p, out, category);
            }
            return;
        }
        for (auto p : rules(n))
            parameters(*p, out, category);
    }
    NodePtr arguments_node(const Node *n)
    {
        Parameters p;
        if (n)
            parameters(*n, p);
        Field vararg = p.vararg ? Field(std::move(p.vararg)) : Field(std::monostate{});
        Field kwarg = p.kwarg ? Field(std::move(p.kwarg)) : Field(std::monostate{});
        return make(Kind::arguments, n ? span(*n) : Span{}, std::move(p.positional_only),
                    std::move(p.positional), std::move(vararg), std::move(p.keyword_only),
                    std::move(p.keyword_defaults), std::move(kwarg), std::move(p.defaults));
    }
    void call_arguments(const Node &n, Nodes &pos, Nodes &kw)
    {
        if (n.rule == Rule::arguments || n.rule == Rule::args || n.rule == Rule::kwargs) {
            for (auto p : rules(n))
                call_arguments(*p, pos, kw);
            return;
        }
        if (n.rule == Rule::kwarg_or_starred || n.rule == Rule::kwarg_or_double_starred) {
            if (has(n, U"=")) {
                for (auto p : children(n))
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                        kw.push_back(make(Kind::keyword, span(n), identifier(*p),
                                          expression(*rules(n).at(0))));
                        return;
                    }
            }
            if (has(n, U"**")) {
                kw.push_back(
                    make(Kind::keyword, span(n), std::monostate{}, expression(*rules(n).at(0))));
                return;
            }
            for (auto p : rules(n))
                call_arguments(*p, pos, kw);
            return;
        }
        pos.push_back(expression(n));
    }
    Nodes generators(const Node &n)
    {
        auto clauses = find(n, Rule::for_if_clauses);
        if (!clauses)
            unsupported(n);
        Nodes result;
        for (auto clause : rules(*clauses)) {
            auto target_node = find(*clause, Rule::star_targets);
            if (!target_node)
                unsupported(*clause);
            auto target = expression(*target_node);
            context(*target, Kind::Store);
            NodePtr iter;
            Nodes filters;
            for (auto p : rules(*clause))
                if (p->rule == Rule::disjunction) {
                    if (!iter)
                        iter = expression(*p);
                    else
                        filters.push_back(expression(*p));
                }
            if (!iter)
                unsupported(*clause);
            result.push_back(make(Kind::comprehension, span(*clause), std::move(target),
                                  std::move(iter), std::move(filters),
                                  std::int64_t(has(*clause, U"async"))));
        }
        return result;
    }
    NodePtr comprehension(const Node &n)
    {
        auto gens = generators(n);
        if (n.rule == Rule::dictcomp) {
            auto pair = find(n, Rule::kvpair);
            if (!pair)
                unsupported(n);
            auto values = rules(*pair);
            return make(Kind::DictComp, span(n), expression(*values.at(0)),
                        expression(*values.at(1)), std::move(gens));
        }
        auto parts = rules(n);
        const auto *value = n.rule == Rule::genexp ? (parts.empty() ? nullptr : parts[0])
                                                   : find(n, Rule::named_expression);
        if (!value)
            unsupported(n);
        auto kind = n.rule == Rule::genexp     ? Kind::GeneratorExp
                    : n.rule == Rule::listcomp ? Kind::ListComp
                                               : Kind::SetComp;
        return make(kind, span(n), expression(*value), std::move(gens));
    }
    void append_string(Nodes &values, Span where, std::u32string text)
    {
        if (text.empty())
            return;
        if (!values.empty() && values.back()->kind == Kind::Constant) {
            auto &literal = std::get<detail::Literal>(values.back()->fields[0]);
            if (literal.kind == detail::Literal::Kind::String) {
                literal.text += text;
                values.back()->span.end = where.end;
                return;
            }
        }
        values.push_back(make(Kind::Constant, where,
                              detail::Literal{detail::Literal::Kind::String, std::move(text)},
                              std::monostate{}));
    }
    std::u32string debug_text(std::size_t begin, std::size_t end, std::size_t first) const
    {
        std::u32string result;
        auto gap = [&](std::size_t from, std::size_t to) {
            while (from < to) {
                auto comment = source_.text().find('#', from);
                if (comment == std::string::npos || comment >= to) {
                    result += detail::decode_source(source_, {from, to});
                    break;
                }
                result += detail::decode_source(source_, {from, comment});
                auto newline = source_.text().find('\n', comment);
                from = newline == std::string::npos ? to : std::min(newline, to);
            }
        };
        auto at = begin;
        for (auto i = first; i < tree_.tokens.size(); ++i) {
            auto where = tree_.tokens[i].span;
            if (where.begin >= end)
                break;
            if (where.end <= at)
                continue;
            gap(at, where.begin);
            result += detail::decode_source(source_, {where.begin, std::min(where.end, end)});
            at = where.end;
        }
        gap(at, end);
        return result;
    }
    void fstring_parts(const Node &n, bool raw, Span whole, Nodes &values)
    {
        if (n.rule == Rule::fstring_replacement_field) {
            const Node *value = nullptr, *open = nullptr, *equal = nullptr, *next = nullptr;
            const Node *conversion = nullptr, *format = nullptr;
            auto c = children(n);
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
                auto end = next ? span(*next).begin : span(*equal).end;
                append_string(values, {span(*open).end, end},
                              debug_text(span(*open).end, end, open->end));
            }
            std::int64_t conv = -1;
            if (conversion) {
                for (auto p : children(*conversion))
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                        auto text = token(*p).text;
                        if (text.size() != 1 ||
                            (text[0] != U's' && text[0] != U'r' && text[0] != U'a'))
                            throw SourceError(source_, span(*p),
                                              "invalid f-string conversion character");
                        conv = text[0];
                    }
            } else if (equal && !format)
                conv = 'r';
            Field spec = std::monostate{};
            if (format) {
                Nodes parts;
                fstring_parts(*format, raw, span(*format), parts);
                spec = make(Kind::JoinedStr, span(*format), std::move(parts));
            }
            values.push_back(
                make(Kind::FormattedValue, span(n), expression(*value), conv, std::move(spec)));
            return;
        }
        for (auto p : children(n)) {
            if (p->rule == Rule::Token && token(*p).kind == TokenKind::FSTRING_MIDDLE) {
                auto where = span(*p);
                if (where.end > where.begin && where.end < source_.text().size()) {
                    char last = source_.text()[where.end - 1];
                    if ((last == '{' || last == '}') && source_.text()[where.end] == last)
                        ++where.end;
                }
                append_string(values, where,
                              syntax::decode_string_fragment(token(*p).text, raw,
                                                             {span(*p).begin, span(*p).end}));
            } else if (p->rule == Rule::fstring_middle || p->rule == Rule::fstring_format_spec ||
                       p->rule == Rule::fstring_replacement_field)
                fstring_parts(*p, raw, whole, values);
        }
    }
    void dict_items(const Node &n, Nodes &keys, Nodes &values)
    {
        auto r = rules(n);
        if (n.rule == Rule::kvpair) {
            keys.push_back(expression(*r.at(0)));
            values.push_back(expression(*r.at(1)));
            return;
        }
        if (n.rule == Rule::double_starred_kvpair && has(n, U"**")) {
            keys.push_back(nullptr);
            values.push_back(expression(*r.at(0)));
            return;
        }
        for (auto p : r)
            dict_items(*p, keys, values);
    }
    NodePtr expression(const Node &input)
    {
        if (depth_ >= 256)
            throw SourceError(source_, span(input),
                              "expression nesting exceeds the current compiler limit");
        ++depth_;
        struct Exit {
            std::size_t &depth;
            ~Exit()
            {
                --depth;
            }
        } exit{depth_};
        const Node *current = &input;
        for (;;) {
            const auto &n = *current;
            if (n.rule == Rule::Token)
                return terminal(n);
            auto c = children(n);
            auto r = rules(n);
            switch (n.rule) {
            case Rule::signed_number:
            case Rule::signed_real_number: {
                NodePtr value;
                if (!r.empty())
                    value = expression(*r[0]);
                else
                    for (auto p : c)
                        if (p->rule == Rule::Token && token(*p).kind == TokenKind::NUMBER)
                            value = terminal(*p);
                if (!value)
                    unsupported(n);
                if (has(n, U"-"))
                    return make(Kind::UnaryOp, span(n), marker(Kind::USub), std::move(value));
                return value;
            }
            case Rule::complex_number:
                return make(Kind::BinOp, span(n), expression(*r.at(0)),
                            marker(has(n, U"-") ? Kind::Sub : Kind::Add), expression(*r.at(1)));
            case Rule::literal_expr:
                if (r.empty())
                    for (auto p : c)
                        if (p->rule == Rule::Token)
                            return terminal(*p);
                break;
            case Rule::subject_expr:
                if (has(n, U",")) {
                    Nodes items;
                    for (auto p : r) {
                        if (p->rule == Rule::star_named_expressions)
                            for (auto item : rules(*p))
                                items.push_back(expression(*item));
                        else
                            items.push_back(expression(*p));
                    }
                    return make(Kind::Tuple, span(n), std::move(items), marker(Kind::Load));
                }
                break;
            case Rule::del_t_atom:
                if (has(n, U"[") || has(n, U"(")) {
                    if (n.alternative == 1) {
                        current = r.at(0);
                        continue;
                    }
                    Nodes items;
                    for (auto p : r)
                        for (auto item : rules(*p))
                            items.push_back(expression(*item));
                    return make(has(n, U"[") ? Kind::List : Kind::Tuple, span(n), std::move(items),
                                marker(Kind::Load));
                }
                if (r.empty())
                    for (auto p : c)
                        if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                            return terminal(*p);
                break;
            case Rule::lambdef:
                return make(Kind::Lambda, span(n), arguments_node(find(n, Rule::lambda_params)),
                            expression(*find(n, Rule::expression)));
            case Rule::listcomp:
            case Rule::setcomp:
            case Rule::dictcomp:
            case Rule::genexp:
                return comprehension(n);
            case Rule::fstring: {
                bool raw = false;
                for (auto p : c)
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::FSTRING_START)
                        raw = token(*p).text.find_first_of(U"rR") != std::u32string::npos;
                Nodes values;
                fstring_parts(n, raw, span(n), values);
                return make(Kind::JoinedStr, span(n), std::move(values));
            }
            case Rule::strings: {
                Nodes values;
                bool formatted = false;
                bool seen_string = false;
                detail::Literal::Kind kind = detail::Literal::Kind::String;
                Field string_kind = std::monostate{};
                std::u32string text;
                for (auto p : r) {
                    auto item = expression(*p);
                    if (item->kind == Kind::JoinedStr) {
                        formatted = true;
                        for (auto &part : std::get<Nodes>(item->fields[0])) {
                            if (part->kind == Kind::Constant)
                                append_string(
                                    values, part->span,
                                    std::move(std::get<detail::Literal>(part->fields[0]).text));
                            else
                                values.push_back(std::move(part));
                        }
                    } else {
                        auto literal = std::get<detail::Literal>(item->fields[0]);
                        if (literal.kind != detail::Literal::Kind::String &&
                            literal.kind != detail::Literal::Kind::Bytes)
                            unsupported(*p);
                        if (seen_string && kind != literal.kind)
                            throw SourceError(source_, span(n),
                                              "cannot mix bytes and nonbytes literals");
                        kind = literal.kind;
                        if (!seen_string)
                            string_kind = std::move(item->fields[1]);
                        seen_string = true;
                        text += literal.text;
                        append_string(values, item->span, std::move(literal.text));
                    }
                }
                if (formatted) {
                    if (kind == detail::Literal::Kind::Bytes)
                        throw SourceError(source_, span(n),
                                          "cannot mix bytes and nonbytes literals");
                    return make(Kind::JoinedStr, span(n), std::move(values));
                }
                return make(Kind::Constant, span(n), detail::Literal{kind, std::move(text)},
                            std::move(string_kind));
            }
            case Rule::dict: {
                Nodes keys, values;
                if (!r.empty())
                    dict_items(*r[0], keys, values);
                return make(Kind::Dict, span(n), std::move(keys), std::move(values));
            }
            case Rule::atom:
            case Rule::name_or_attr:
            case Rule::single_target:
                if (r.empty())
                    for (auto p : c)
                        if (p->rule == Rule::Token && (token(*p).kind == TokenKind::NAME ||
                                                       token(*p).kind == TokenKind::NUMBER ||
                                                       token(*p).kind == TokenKind::ELLIPSIS))
                            return terminal(*p);
                break;
            case Rule::string:
            case Rule::real_number:
            case Rule::imaginary_number:
                for (auto p : c)
                    if (p->rule == Rule::Token && (token(*p).kind == TokenKind::STRING ||
                                                   token(*p).kind == TokenKind::NUMBER)) {
                        auto value = terminal(*p);
                        if (n.rule != Rule::string) {
                            auto imaginary = std::get<detail::Literal>(value->fields[0]).kind ==
                                             detail::Literal::Kind::Imaginary;
                            if (imaginary != (n.rule == Rule::imaginary_number))
                                throw SourceError(
                                    source_, span(n),
                                    imaginary ? "real number required in complex literal"
                                              : "imaginary number required in complex literal");
                        }
                        return value;
                    }
                break;
            case Rule::expressions:
            case Rule::star_expressions:
            case Rule::star_targets:
            case Rule::star_targets_list_seq:
            case Rule::star_targets_tuple_seq:
            case Rule::del_targets:
            case Rule::star_named_expressions:
                if (r.size() == 1 && !has(n, U",")) {
                    current = r[0];
                    continue;
                } else {
                    Nodes items;
                    for (auto p : r)
                        items.push_back(expression(*p));
                    return make(Kind::Tuple, span(n), std::move(items), marker(Kind::Load));
                }
            case Rule::star_expression:
            case Rule::star_named_expression:
            case Rule::star_target:
            case Rule::starred_expression:
                if (has(n, U"*"))
                    return make(Kind::Starred, span(n), expression(*r.at(0)), marker(Kind::Load));
                break;
            case Rule::assignment_expression: {
                const Node *name = nullptr;
                for (auto p : c)
                    if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                        name = p;
                        break;
                    }
                if (!name)
                    unsupported(n);
                auto target = terminal(*name);
                context(*target, Kind::Store);
                return make(Kind::NamedExpr, span(n), std::move(target), expression(*r.at(0)));
            }
            case Rule::comparison:
                if (r.size() > 1) {
                    Nodes ops, comparators;
                    for (std::size_t i = 1; i < r.size(); ++i) {
                        auto pair = rules(*r[i]);
                        if (pair.size() != 1)
                            unsupported(*r[i]);
                        const auto &p = *pair[0];
                        auto rhs = rules(p);
                        if (rhs.size() != 1)
                            unsupported(p);
                        Kind op;
                        switch (p.rule) {
                        case Rule::eq_bitwise_or:
                            op = Kind::Eq;
                            break;
                        case Rule::noteq_bitwise_or:
                            op = Kind::NotEq;
                            break;
                        case Rule::lt_bitwise_or:
                            op = Kind::Lt;
                            break;
                        case Rule::lte_bitwise_or:
                            op = Kind::LtE;
                            break;
                        case Rule::gt_bitwise_or:
                            op = Kind::Gt;
                            break;
                        case Rule::gte_bitwise_or:
                            op = Kind::GtE;
                            break;
                        case Rule::is_bitwise_or:
                            op = Kind::Is;
                            break;
                        case Rule::isnot_bitwise_or:
                            op = Kind::IsNot;
                            break;
                        case Rule::in_bitwise_or:
                            op = Kind::In;
                            break;
                        case Rule::notin_bitwise_or:
                            op = Kind::NotIn;
                            break;
                        default:
                            unsupported(p);
                        }
                        ops.push_back(marker(op));
                        comparators.push_back(expression(*rhs[0]));
                    }
                    return make(Kind::Compare, span(n), expression(*r[0]), std::move(ops),
                                std::move(comparators));
                }
                break;
            case Rule::expression:
                if (r.size() == 3)
                    return make(Kind::IfExp, span(n), expression(*r[1]), expression(*r[0]),
                                expression(*r[2]));
                break;
            case Rule::disjunction:
            case Rule::conjunction:
                if (r.size() > 1) {
                    Nodes values;
                    for (auto p : r)
                        values.push_back(expression(*p));
                    return make(Kind::BoolOp, span(n),
                                marker(n.rule == Rule::conjunction ? Kind::And : Kind::Or),
                                std::move(values));
                }
                break;
            case Rule::inversion:
            case Rule::factor:
                if (!c.empty() && c[0]->rule == Rule::Token) {
                    auto op = token(*c[0]).text;
                    auto kind = op == U"not" ? Kind::Not
                                : op == U"+" ? Kind::UAdd
                                : op == U"-" ? Kind::USub
                                             : Kind::Invert;
                    return make(Kind::UnaryOp, span(n), marker(kind), expression(*r.at(0)));
                }
                break;
            case Rule::bitwise_or:
            case Rule::bitwise_xor:
            case Rule::bitwise_and:
            case Rule::shift_expr:
            case Rule::sum:
            case Rule::term:
            case Rule::power:
                if (r.size() == 2)
                    for (auto p : c)
                        if (p->rule == Rule::Token)
                            return make(Kind::BinOp, span(n), expression(*r[0]),
                                        marker(binary(token(*p).text, n)), expression(*r[1]));
                break;
            case Rule::await_primary:
                if (has(n, U"await"))
                    return make(Kind::Await, span(n), expression(*r.at(0)));
                break;
            case Rule::yield_expr:
                if (has(n, U"from"))
                    return make(Kind::YieldFrom, span(n), expression(*r.at(0)));
                return make(Kind::Yield, span(n),
                            r.empty() ? Field(std::monostate{}) : Field(expression(*r[0])));
            case Rule::primary:
            case Rule::attr:
            case Rule::t_primary:
            case Rule::single_subscript_attribute_target:
            case Rule::target_with_star_atom:
            case Rule::del_target:
                if (has(n, U"."))
                    for (auto p : c)
                        if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                            return make(Kind::Attribute, span(n), expression(*r.at(0)),
                                        identifier(*p), marker(Kind::Load));
                if (has(n, U"[") && r.size() >= 2)
                    return make(Kind::Subscript, span(n), expression(*r[0]), expression(*r[1]),
                                marker(Kind::Load));
                if (r.size() == 2 && r[1]->rule == Rule::genexp) {
                    Nodes args;
                    args.push_back(comprehension(*r[1]));
                    return make(Kind::Call, span(n), expression(*r[0]), std::move(args), Nodes{});
                }
                if (has(n, U"(")) {
                    Nodes args, keywords;
                    if (r.size() > 1)
                        call_arguments(*r[1], args, keywords);
                    return make(Kind::Call, span(n), expression(*r.at(0)), std::move(args),
                                std::move(keywords));
                }
                break;
            case Rule::slice:
                if (has(n, U":")) {
                    Field lower = std::monostate{}, upper = std::monostate{},
                          step = std::monostate{};
                    int section = 0;
                    for (auto p : c) {
                        if (is(*p, U":"))
                            ++section;
                        else if (p->rule != Rule::Token) {
                            if (section == 0)
                                lower = expression(*p);
                            else if (section == 1)
                                upper = expression(*p);
                            else
                                step = expression(*p);
                        }
                    }
                    return make(Kind::Slice, span(n), std::move(lower), std::move(upper),
                                std::move(step));
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
                if (n.rule == Rule::star_atom && n.alternative == 1 && r.size() == 1) {
                    current = r[0];
                    continue;
                }
                bool list = has(n, U"[");
                if (!list && !has(n, U"(") && n.rule != Rule::set)
                    break;
                Nodes items;
                for (auto p : r) {
                    if (p->rule == Rule::star_named_expressions ||
                        p->rule == Rule::star_targets_list_seq ||
                        p->rule == Rule::star_targets_tuple_seq) {
                        for (auto item : rules(*p))
                            items.push_back(expression(*item));
                    } else
                        items.push_back(expression(*p));
                }
                if (n.rule == Rule::set)
                    return make(Kind::Set, span(n), std::move(items));
                return make(list ? Kind::List : Kind::Tuple, span(n), std::move(items),
                            marker(Kind::Load));
            }
            case Rule::slices: {
                Nodes items;
                for (auto p : r)
                    items.push_back(expression(*p));
                if (items.size() == 1 && !has(n, U",") && items[0]->kind != Kind::Starred)
                    return std::move(items[0]);
                return make(Kind::Tuple, span(n), std::move(items), marker(Kind::Load));
            }
            default:
                break;
            }
            if (r.size() == 1) {
                current = r[0];
                continue;
            }
            unsupported(n);
        }
    }
    std::u32string capture(const Node &n) const
    {
        for (auto p : children(n))
            if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME)
                return identifier(*p);
        unsupported(n);
    }
    void pattern_items(const Node &n, Nodes &items)
    {
        for (auto p : rules(n))
            if (p->rule == Rule::maybe_sequence_pattern)
                pattern_items(*p, items);
            else
                items.push_back(pattern(*p));
    }
    NodePtr pattern(const Node &n)
    {
        auto r = rules(n);
        switch (n.rule) {
        case Rule::patterns:
            if (!r.empty() && r[0]->rule == Rule::open_sequence_pattern) {
                Nodes items;
                pattern_items(*r[0], items);
                return make(Kind::MatchSequence, span(n), std::move(items));
            }
            break;
        case Rule::or_pattern:
            if (r.size() > 1) {
                Nodes items;
                for (auto p : r)
                    items.push_back(pattern(*p));
                return make(Kind::MatchOr, span(n), std::move(items));
            }
            break;
        case Rule::as_pattern:
            return make(Kind::MatchAs, span(n), pattern(*r.at(0)), capture(*r.at(1)));
        case Rule::wildcard_pattern:
            return make(Kind::MatchAs, span(n), std::monostate{}, std::monostate{});
        case Rule::capture_pattern:
            return make(Kind::MatchAs, span(n), std::monostate{}, capture(*r.at(0)));
        case Rule::star_pattern:
            return make(Kind::MatchStar, span(n),
                        !r.empty() && r[0]->rule == Rule::pattern_capture_target
                            ? Field(capture(*r[0]))
                            : Field(std::monostate{}));
        case Rule::literal_pattern:
            if (!r.empty())
                return make(Kind::MatchValue, span(n), expression(*r[0]));
            for (auto p : children(n))
                if (p->rule == Rule::Token) {
                    auto value = terminal(*p);
                    return make(Kind::MatchSingleton, span(n),
                                std::move(std::get<detail::Literal>(value->fields[0])));
                }
            unsupported(n);
        case Rule::value_pattern:
            return make(Kind::MatchValue, span(n), expression(*r.at(0)));
        case Rule::sequence_pattern: {
            Nodes items;
            if (!r.empty())
                pattern_items(*r[0], items);
            return make(Kind::MatchSequence, span(n), std::move(items));
        }
        case Rule::mapping_pattern: {
            Nodes keys, values;
            Field rest = std::monostate{};
            for (auto p : r) {
                if (p->rule == Rule::double_star_pattern)
                    rest = capture(*find(*p, Rule::pattern_capture_target));
                else if (p->rule == Rule::items_pattern)
                    for (auto pair : rules(*p)) {
                        auto parts = rules(*pair);
                        keys.push_back(expression(*parts.at(0)));
                        values.push_back(pattern(*parts.at(1)));
                    }
            }
            return make(Kind::MatchMapping, span(n), std::move(keys), std::move(values),
                        std::move(rest));
        }
        case Rule::class_pattern: {
            auto cls = expression(*r.at(0));
            Nodes positional, values;
            Strings attrs;
            for (std::size_t i = 1; i < r.size(); ++i) {
                if (r[i]->rule == Rule::positional_patterns)
                    for (auto item : rules(*r[i]))
                        positional.push_back(pattern(*item));
                else if (r[i]->rule == Rule::keyword_patterns)
                    for (auto item : rules(*r[i])) {
                        for (auto key : children(*item))
                            if (key->rule == Rule::Token && token(*key).kind == TokenKind::NAME) {
                                attrs.push_back(identifier(*key));
                                break;
                            }
                        values.push_back(pattern(*rules(*item).at(0)));
                    }
            }
            return make(Kind::MatchClass, span(n), std::move(cls), std::move(positional),
                        std::move(attrs), std::move(values));
        }
        default:
            break;
        }
        if (r.size() == 1)
            return pattern(*r[0]);
        unsupported(n);
    }
    Nodes type_parameters(const Node &n)
    {
        Nodes result;
        auto params = find(n, Rule::type_params);
        if (!params)
            return result;
        auto seq = find(*params, Rule::type_param_seq);
        if (!seq)
            unsupported(*params);
        for (auto p : rules(*seq)) {
            auto name = capture(*p);
            if ((has(*p, U"*") || has(*p, U"**")) && has(*p, U":")) {
                auto value = find(*p, Rule::expression);
                if (!value)
                    unsupported(*p);
                bool constraints = expression(*value)->kind == Kind::Tuple;
                const Node *colon = nullptr;
                for (auto part : children(*p))
                    if (is(*part, U":")) {
                        colon = part;
                        break;
                    }
                auto where = span(*p);
                if (colon)
                    where.begin = span(*colon).begin;
                where.end = span(*value).end;
                throw SourceError(source_, where,
                                  std::string(constraints ? "cannot use constraints with "
                                                          : "cannot use bound with ") +
                                      (has(*p, U"**") ? "ParamSpec" : "TypeVarTuple"));
            }
            if (has(*p, U"**"))
                result.push_back(make(Kind::ParamSpec, span(*p), std::move(name)));
            else if (has(*p, U"*"))
                result.push_back(make(Kind::TypeVarTuple, span(*p), std::move(name)));
            else {
                auto bound = find(*p, Rule::type_param_bound);
                result.push_back(make(Kind::TypeVar, span(*p), std::move(name),
                                      bound ? Field(expression(*rules(*bound).at(0)))
                                            : Field(std::monostate{})));
            }
        }
        return result;
    }
    std::u32string definition_name(const Node &n, std::u32string_view keyword)
    {
        bool seen = false;
        for (auto p : children(n))
            if (p->rule == Rule::Token) {
                if (!seen && is(*p, keyword)) {
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
    void import_names(const Node &n, Nodes &out)
    {
        if (n.rule == Rule::dotted_as_name || n.rule == Rule::import_from_as_name) {
            std::u32string name;
            Field asname = std::monostate{};
            bool as = false;
            for (auto p : children(n)) {
                if (p->rule == Rule::dotted_name)
                    name = dotted(*p);
                else if (is(*p, U"as"))
                    as = true;
                else if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                    if (as)
                        asname = identifier(*p);
                    else
                        name = identifier(*p);
                }
            }
            out.push_back(make(Kind::alias, span(n), std::move(name), std::move(asname)));
            return;
        }
        if (n.rule == Rule::import_from_targets && has(n, U"*")) {
            out.push_back(make(Kind::alias, span(n), std::u32string(U"*"), std::monostate{}));
            return;
        }
        for (auto p : rules(n))
            import_names(*p, out);
    }
    Nodes suite(const Node &n)
    {
        Nodes result;
        auto r = rules(n);
        if (n.rule == Rule::file || n.rule == Rule::interactive ||
            n.rule == Rule::statement_newline || n.rule == Rule::statements ||
            n.rule == Rule::statement || n.rule == Rule::simple_stmts ||
            n.rule == Rule::compound_stmt || n.rule == Rule::block || n.rule == Rule::import_stmt) {
            for (auto p : r) {
                auto sub = suite(*p);
                for (auto &item : sub)
                    result.push_back(std::move(item));
            }
            return result;
        }
        if (n.rule == Rule::simple_stmt) {
            if (!r.empty())
                return suite(*r[0]);
            for (auto p : children(n)) {
                if (is(*p, U"pass"))
                    result.push_back(make(Kind::Pass, span(n)));
                if (is(*p, U"break"))
                    result.push_back(make(Kind::Break, span(n)));
                if (is(*p, U"continue"))
                    result.push_back(make(Kind::Continue, span(n)));
            }
            if (!result.empty())
                return result;
        }
        switch (n.rule) {
        case Rule::function_def:
        case Rule::class_def: {
            auto raw = find(n, n.rule == Rule::function_def ? Rule::function_def_raw
                                                            : Rule::class_def_raw);
            if (!raw)
                unsupported(n);
            auto result = suite(*raw);
            Nodes decorators;
            if (auto d = find(n, Rule::decorators))
                for (auto p : rules(*d))
                    decorators.push_back(expression(*p));
            auto &fields = result.at(0)->fields;
            fields[n.rule == Rule::function_def ? 3 : 4] = std::move(decorators);
            return result;
        }
        case Rule::function_def_raw: {
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            auto ret = find(n, Rule::expression);
            result.push_back(make(has(n, U"async") ? Kind::AsyncFunctionDef : Kind::FunctionDef,
                                  span(n), definition_name(n, U"def"),
                                  arguments_node(find(n, Rule::params)), suite(*block), Nodes{},
                                  ret ? Field(expression(*ret)) : Field(std::monostate{}),
                                  std::monostate{}, type_parameters(n)));
            break;
        }
        case Rule::class_def_raw: {
            auto block = find(n, Rule::block);
            if (!block)
                unsupported(n);
            Nodes bases, keywords;
            if (auto a = find(n, Rule::arguments))
                call_arguments(*a, bases, keywords);
            result.push_back(make(Kind::ClassDef, span(n), definition_name(n, U"class"),
                                  std::move(bases), std::move(keywords), suite(*block), Nodes{},
                                  type_parameters(n)));
            break;
        }
        case Rule::type_alias: {
            auto name = definition_name(n, U"type");
            const Node *name_node = nullptr;
            bool seen = false;
            for (auto p : children(n)) {
                if (!seen && is(*p, U"type")) {
                    seen = true;
                    continue;
                }
                if (seen && p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) {
                    name_node = p;
                    break;
                }
            }
            if (!name_node)
                unsupported(n);
            auto target = make(Kind::Name, span(*name_node), std::move(name), marker(Kind::Store));
            result.push_back(make(Kind::TypeAlias, span(n), std::move(target), type_parameters(n),
                                  expression(*find(n, Rule::expression))));
            break;
        }
        case Rule::if_stmt:
        case Rule::elif_stmt: {
            Nodes otherwise;
            if (auto e = find(n, Rule::elif_stmt))
                otherwise = suite(*e);
            else if (auto e = find(n, Rule::else_block))
                otherwise = suite(*find(*e, Rule::block));
            result.push_back(make(Kind::If, span(n), expression(*find(n, Rule::named_expression)),
                                  suite(*find(n, Rule::block)), std::move(otherwise)));
            break;
        }
        case Rule::while_stmt: {
            Nodes otherwise;
            if (auto e = find(n, Rule::else_block))
                otherwise = suite(*find(*e, Rule::block));
            result.push_back(make(Kind::While, span(n),
                                  expression(*find(n, Rule::named_expression)),
                                  suite(*find(n, Rule::block)), std::move(otherwise)));
            break;
        }
        case Rule::for_stmt: {
            auto target = expression(*find(n, Rule::star_targets));
            context(*target, Kind::Store);
            Nodes otherwise;
            if (auto e = find(n, Rule::else_block))
                otherwise = suite(*find(*e, Rule::block));
            result.push_back(make(has(n, U"async") ? Kind::AsyncFor : Kind::For, span(n),
                                  std::move(target), expression(*find(n, Rule::star_expressions)),
                                  suite(*find(n, Rule::block)), std::move(otherwise),
                                  std::monostate{}));
            break;
        }
        case Rule::with_stmt: {
            Nodes items;
            for (auto p : r)
                if (p->rule == Rule::with_item) {
                    auto target = find(*p, Rule::star_target);
                    Field binding = std::monostate{};
                    if (target) {
                        auto value = expression(*target);
                        context(*value, Kind::Store);
                        binding = std::move(value);
                    }
                    items.push_back(make(Kind::withitem, span(*p),
                                         expression(*find(*p, Rule::expression)),
                                         std::move(binding)));
                }
            result.push_back(make(has(n, U"async") ? Kind::AsyncWith : Kind::With, span(n),
                                  std::move(items), suite(*find(n, Rule::block)),
                                  std::monostate{}));
            break;
        }
        case Rule::try_stmt: {
            Nodes handlers, otherwise, cleanup;
            bool starred = false;
            for (auto p : r) {
                if (p->rule == Rule::except_block || p->rule == Rule::except_star_block) {
                    starred = p->rule == Rule::except_star_block;
                    auto type = find(*p, Rule::expression);
                    Field name = std::monostate{};
                    bool after = false;
                    for (auto q : children(*p)) {
                        if (is(*q, U"as"))
                            after = true;
                        else if (after && q->rule == Rule::Token &&
                                 token(*q).kind == TokenKind::NAME) {
                            name = identifier(*q);
                            break;
                        }
                    }
                    handlers.push_back(
                        make(Kind::ExceptHandler, span(*p),
                             type ? Field(expression(*type)) : Field(std::monostate{}),
                             std::move(name), suite(*find(*p, Rule::block))));
                } else if (p->rule == Rule::else_block)
                    otherwise = suite(*find(*p, Rule::block));
                else if (p->rule == Rule::finally_block)
                    cleanup = suite(*find(*p, Rule::block));
            }
            result.push_back(make(starred ? Kind::TryStar : Kind::Try, span(n),
                                  suite(*find(n, Rule::block)), std::move(handlers),
                                  std::move(otherwise), std::move(cleanup)));
            break;
        }
        case Rule::match_stmt: {
            Nodes cases;
            for (auto p : r)
                if (p->rule == Rule::case_block) {
                    auto guard = find(*p, Rule::guard);
                    cases.push_back(make(
                        Kind::match_case, span(*p), pattern(*find(*p, Rule::patterns)),
                        guard ? Field(expression(*rules(*guard).at(0))) : Field(std::monostate{}),
                        suite(*find(*p, Rule::block))));
                }
            result.push_back(make(Kind::Match, span(n), expression(*find(n, Rule::subject_expr)),
                                  std::move(cases)));
            break;
        }
        case Rule::import_name:
        case Rule::import_from: {
            Nodes names;
            if (n.rule == Rule::import_name) {
                import_names(*find(n, Rule::dotted_as_names), names);
                result.push_back(make(Kind::Import, span(n), std::move(names)));
            } else {
                auto module = find(n, Rule::dotted_name);
                std::int64_t level = 0;
                for (auto p : children(n)) {
                    if (is(*p, U"."))
                        ++level;
                    else if (is(*p, U"..."))
                        level += 3;
                }
                import_names(*find(n, Rule::import_from_targets), names);
                result.push_back(make(Kind::ImportFrom, span(n),
                                      module ? Field(dotted(*module)) : Field(std::monostate{}),
                                      std::move(names), level));
            }
            break;
        }
        case Rule::del_stmt: {
            Nodes targets;
            for (auto p : rules(*r.at(0))) {
                auto target = expression(*p);
                context(*target, Kind::Del);
                targets.push_back(std::move(target));
            }
            result.push_back(make(Kind::Delete, span(n), std::move(targets)));
            break;
        }
        case Rule::assignment: {
            if (has(n, U":")) {
                const Node *target = nullptr;
                for (auto p : children(n))
                    if ((p->rule == Rule::Token && token(*p).kind == TokenKind::NAME) ||
                        p->rule == Rule::single_target ||
                        p->rule == Rule::single_subscript_attribute_target) {
                        target = p;
                        break;
                    }
                if (!target)
                    unsupported(n);
                auto value = expression(*target);
                context(*value, Kind::Store);
                auto annotation = find(n, Rule::expression);
                if (!annotation)
                    unsupported(n);
                auto rhs = find(n, Rule::annotated_rhs);
                result.push_back(make(Kind::AnnAssign, span(n), std::move(value),
                                      expression(*annotation),
                                      rhs ? Field(expression(*rhs)) : Field(std::monostate{}),
                                      std::int64_t(n.alternative == 0)));
            } else if (auto op = find(n, Rule::augassign)) {
                auto target = expression(*r.at(0));
                context(*target, Kind::Store);
                result.push_back(make(Kind::AugAssign, span(n), std::move(target),
                                      marker(binary(token(*children(*op).at(0)).text, *op)),
                                      expression(*r.back())));
            } else {
                Nodes targets;
                for (std::size_t i = 0; i + 1 < r.size(); ++i) {
                    auto target = expression(*r[i]);
                    context(*target, Kind::Store);
                    targets.push_back(std::move(target));
                }
                if (targets.empty())
                    unsupported(n);
                result.push_back(make(Kind::Assign, span(n), std::move(targets),
                                      expression(*r.back()), std::monostate{}));
            }
            break;
        }
        case Rule::star_expressions:
        case Rule::yield_stmt:
            result.push_back(make(Kind::Expr, span(n), expression(n)));
            break;
        case Rule::return_stmt:
            result.push_back(make(Kind::Return, span(n),
                                  r.empty() ? Field(std::monostate{}) : Field(expression(*r[0]))));
            break;
        case Rule::raise_stmt:
            result.push_back(
                make(Kind::Raise, span(n),
                     r.empty() ? Field(std::monostate{}) : Field(expression(*r[0])),
                     r.size() < 2 ? Field(std::monostate{}) : Field(expression(*r[1]))));
            break;
        case Rule::assert_stmt:
            result.push_back(
                make(Kind::Assert, span(n), expression(*r.at(0)),
                     r.size() < 2 ? Field(std::monostate{}) : Field(expression(*r[1]))));
            break;
        case Rule::global_stmt:
        case Rule::nonlocal_stmt: {
            Strings names;
            for (auto p : children(n))
                if (p->rule == Rule::Token && token(*p).kind == TokenKind::NAME &&
                    !is(*p, U"global") && !is(*p, U"nonlocal"))
                    names.push_back(identifier(*p));
            result.push_back(make(n.rule == Rule::global_stmt ? Kind::Global : Kind::Nonlocal,
                                  span(n), std::move(names)));
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
    NodePtr run(syntax::InputMode mode)
    {
        if (mode == syntax::InputMode::Eval) {
            auto p = find(*tree_.root, Rule::expressions);
            if (!p)
                unsupported(*tree_.root);
            return make(Kind::Expression, span(*tree_.root), expression(*p));
        }
        if (mode == syntax::InputMode::FunctionType) {
            Nodes args;
            if (auto types = find(*tree_.root, Rule::type_expressions))
                for (auto p : rules(*types))
                    args.push_back(expression(*p));
            auto returns = find(*tree_.root, Rule::expression);
            if (!returns)
                unsupported(*tree_.root);
            return make(Kind::FunctionType, span(*tree_.root), std::move(args),
                        expression(*returns));
        }
        auto body = suite(*tree_.root);
        if (mode == syntax::InputMode::Single || mode == syntax::InputMode::Interactive)
            return make(Kind::Interactive, span(*tree_.root), std::move(body));
        return make(Kind::Module, span(*tree_.root), std::move(body), Nodes{});
    }
};
} // namespace

NodePtr parse(const SourceBuffer &source, syntax::InputMode mode, std::uint32_t flags)
{
    try {
        auto options = syntax::ParseFlags::None;
        if (flags & compile_allow_incomplete_input)
            options = options | syntax::ParseFlags::AllowIncompleteInput;
        if (flags & compile_dont_imply_dedent)
            options = options | syntax::ParseFlags::DontImplyDedent;
        return Adapter(source, syntax::parse(source.text(), mode, options)).run(mode);
    } catch (const syntax::Error &error) {
        if (error.kind == syntax::ErrorKind::Incomplete)
            throw SourceError(source, {error.span.begin, error.span.end}, "incomplete input");
        auto kind = SourceErrorKind::Syntax;
        if (error.kind == syntax::ErrorKind::Indentation)
            kind = SourceErrorKind::Indentation;
        if (error.kind == syntax::ErrorKind::Tab)
            kind = SourceErrorKind::Tab;
        if (error.kind == syntax::ErrorKind::Encoding)
            kind = SourceErrorKind::Encoding;
        throw SourceError(source, {error.span.begin, error.span.end}, error.what(), kind);
    }
}
} // namespace cxxpython::core::ast
