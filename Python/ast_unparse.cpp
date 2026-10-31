#include "internal/compiler.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

#include <boost/multiprecision/cpp_int.hpp>

namespace cxxpython::core::detail {
namespace {
// Unicode 15.0 data shared with Objects/representation.cpp.
#include "../Objects/unicode_nonprintable.inc"

bool printable(char32_t point)
{
    if (point == U' ')
        return true;
    auto next = std::upper_bound(std::begin(non_printable), std::end(non_printable), point,
                                 [](auto value, auto range) { return value < range.first; });
    return next == std::begin(non_printable) || point > (next - 1)->second;
}

std::u32string quoted(const std::u32string &value, bool bytes)
{
    const char32_t quote =
        value.find(U'\'') != value.npos && value.find(U'"') == value.npos ? U'"' : U'\'';
    std::u32string result = bytes ? U"b" : U"";
    result += quote;
    constexpr char32_t digits[] = U"0123456789abcdef";
    for (auto point : value) {
        if (point == quote || point == U'\\') {
            result += U'\\';
            result += point;
        } else if (point == U'\n')
            result += U"\\n";
        else if (point == U'\r')
            result += U"\\r";
        else if (point == U'\t')
            result += U"\\t";
        else if (bytes ? point >= 32 && point < 127 : printable(point))
            result += point;
        else {
            const unsigned count = bytes || point <= 0xff ? 2 : point <= 0xffff ? 4 : 8;
            result += count == 2 ? U"\\x" : count == 4 ? U"\\u" : U"\\U";
            for (auto remaining = count; remaining; --remaining)
                result += digits[(point >> ((remaining - 1) * 4)) & 15];
        }
    }
    result += quote;
    return result;
}

// Adapted from Objects/floatobject.cpp's shortest float representation.
std::string float_text(double value, bool imaginary)
{
    if (std::isinf(value))
        return std::signbit(value) ? "-1e309" : "1e309";
    if (std::isnan(value))
        return "nan";
    char buffer[128];
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    if (converted.ec != std::errc{})
        throw std::runtime_error("float representation failed");
    std::string scientific(buffer, converted.ptr);
    const auto epos = scientific.find('e');
    const int exponent = std::stoi(scientific.substr(epos + 1));
    if (exponent < -4 || exponent >= 16)
        return scientific;
    auto mantissa = scientific.substr(0, epos);
    std::string text;
    if (mantissa.front() == '-') {
        text.push_back('-');
        mantissa.erase(0, 1);
    }
    const auto point = mantissa.find('.');
    if (point != std::string::npos)
        mantissa.erase(point, 1);
    const int decimal = exponent + 1;
    if (decimal <= 0)
        text += "0." + std::string(static_cast<std::size_t>(-decimal), '0') + mantissa;
    else if (static_cast<std::size_t>(decimal) >= mantissa.size()) {
        text += mantissa + std::string(static_cast<std::size_t>(decimal) - mantissa.size(), '0');
        if (!imaginary)
            text += ".0";
    } else
        text += mantissa.substr(0, decimal) + "." + mantissa.substr(decimal);
    return text;
}
} // namespace

// Literal rendering follows CPython 3.12.14 Python/ast_unparse.c append_repr,
// with string and bytes escaping adapted from this runtime's object reprs.
std::u32string annotation_literal(const Literal &value)
{
    switch (value.kind) {
    case Literal::Kind::None:
        return U"None";
    case Literal::Kind::True:
        return U"True";
    case Literal::Kind::False:
        return U"False";
    case Literal::Kind::Ellipsis:
        return U"...";
    case Literal::Kind::String:
        return quoted(value.text, false);
    case Literal::Kind::Bytes:
        return quoted(value.text, true);
    case Literal::Kind::Float:
    case Literal::Kind::Imaginary: {
        const bool imaginary = value.kind == Literal::Kind::Imaginary;
        auto text = float_text(value.number, imaginary);
        if (imaginary)
            text += 'j';
        return {text.begin(), text.end()};
    }
    case Literal::Kind::Integer: {
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
            const auto digit = ch <= U'9'   ? ch - U'0'
                               : ch <= U'F' ? ch - U'A' + 10
                                            : ch - U'a' + 10;
            number *= base;
            number += digit;
        }
        const auto text = number.str();
        return {text.begin(), text.end()};
    }
    }
    throw std::logic_error("unknown literal kind");
}

namespace {
// Precedence and formatting follow CPython 3.12.14 Python/ast_unparse.c.
enum Priority {
    Tuple,
    Test,
    Or,
    And,
    Not,
    Compare,
    BitOr,
    BitXor,
    BitAnd,
    Shift,
    Arithmetic,
    Term,
    Factor,
    Power,
    Await,
    Atom
};
std::u32string expression(const Expr &, int);
std::u32string parenthesize(std::u32string text, int level, int priority)
{
    return level > priority ? U"(" + text + U")" : std::move(text);
}
std::pair<std::u32string_view, int> binary_operator(BinaryOp op)
{
    switch (op) {
    case BinaryOp::Add:
        return {U" + ", Arithmetic};
    case BinaryOp::Subtract:
        return {U" - ", Arithmetic};
    case BinaryOp::Multiply:
        return {U" * ", Term};
    case BinaryOp::MatrixMultiply:
        return {U" @ ", Term};
    case BinaryOp::FloorDivide:
        return {U" // ", Term};
    case BinaryOp::Modulo:
        return {U" % ", Term};
    case BinaryOp::TrueDivide:
        return {U" / ", Term};
    case BinaryOp::Power:
        return {U" ** ", Power};
    case BinaryOp::LeftShift:
        return {U" << ", Shift};
    case BinaryOp::RightShift:
        return {U" >> ", Shift};
    case BinaryOp::BitAnd:
        return {U" & ", BitAnd};
    case BinaryOp::BitXor:
        return {U" ^ ", BitXor};
    case BinaryOp::BitOr:
        return {U" | ", BitOr};
    }
    throw std::logic_error("unknown binary operator");
}
std::u32string items(const std::vector<ExprPtr> &values)
{
    std::u32string text;
    for (const auto &item : values) {
        if (!text.empty())
            text += U", ";
        text += expression(*item, Test);
    }
    return text;
}
std::u32string arguments(const FunctionStmt &function)
{
    std::u32string text;
    auto append = [&](std::u32string value) {
        if (!text.empty())
            text += U", ";
        text += value;
    };
    bool keyword = false;
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        const auto &parameter = function.parameters[i];
        if (parameter.kind == ParameterKind::KeywordOnly && !keyword) {
            append(U"*" + function.varargs.value_or(U""));
            keyword = true;
        }
        auto value = parameter.name;
        if (parameter.default_value)
            value += U"=" + expression(*parameter.default_value, Test);
        append(std::move(value));
        if (parameter.kind == ParameterKind::PositionalOnly &&
            (i + 1 == function.parameters.size() ||
             function.parameters[i + 1].kind != ParameterKind::PositionalOnly))
            append(U"/");
    }
    if (function.varargs && !keyword)
        append(U"*" + *function.varargs);
    if (function.varkwargs)
        append(U"**" + *function.varkwargs);
    return text;
}
std::u32string fstring_body(const Expr &value)
{
    if (const auto *literal = std::get_if<Literal>(&value.node)) {
        std::u32string text;
        for (auto point : literal->text) {
            text += point;
            if (point == U'{' || point == U'}')
                text += point;
        }
        return text;
    }
    if (const auto *joined = std::get_if<JoinedStrExpr>(&value.node)) {
        std::u32string text;
        for (const auto &item : joined->items)
            text += fstring_body(*item);
        return text;
    }
    const auto &formatted = std::get<FormattedExpr>(value.node);
    auto item = expression(*formatted.value, Test + 1);
    auto text = item.starts_with(U"{") ? U"{ " + item : U"{" + item;
    if (formatted.conversion) {
        text += U'!';
        text += static_cast<char32_t>(formatted.conversion);
    }
    if (formatted.has_spec)
        text += U":" + fstring_body(*formatted.spec);
    return text + U"}";
}
std::u32string comprehension(const ComprehensionExpr &value)
{
    const auto &function = *value.function;
    const bool generator = function.name == U"<genexpr>";
    const bool dictionary = function.name == U"<dictcomp>";
    const bool list = function.name == U"<listcomp>";
    const Suite *body = &function.body;
    const Expr *element = nullptr, *key = nullptr;
    std::u32string clauses;
    bool first = true;
    for (;;) {
        const Stmt *statement = nullptr;
        for (const auto &item : *body)
            if (!std::holds_alternative<AssignStmt>(item->node) &&
                !std::holds_alternative<ReturnStmt>(item->node)) {
                statement = item.get();
                break;
            }
        if (!statement)
            throw std::logic_error("invalid lowered comprehension");
        if (const auto *loop = std::get_if<ForStmt>(&statement->node)) {
            clauses += loop->asynchronous ? U" async for " : U" for ";
            clauses += expression(*loop->target, Tuple) + U" in ";
            clauses += expression(first ? *value.iterable : *loop->iterable, Test + 1);
            first = false;
            body = &loop->body;
        } else if (const auto *filter = std::get_if<IfStmt>(&statement->node)) {
            clauses += U" if " + expression(*filter->test, Test + 1);
            body = &filter->body;
        } else if (const auto *add = std::get_if<CollectionAddStmt>(&statement->node)) {
            key = add->key.get();
            element = add->value.get();
            break;
        } else {
            element =
                std::get<YieldExpr>(std::get<ExprStmt>(statement->node).value->node).value.get();
            break;
        }
    }
    std::u32string text = dictionary ? expression(*key, Test) + U": " : U"";
    text += expression(*element, Test) + clauses;
    return generator ? U"(" + text + U")" : list ? U"[" + text + U"]" : U"{" + text + U"}";
}
std::u32string expression(const Expr &value, int level)
{
    return std::visit(
        [&](const auto &node) -> std::u32string {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Literal>)
                return annotation_literal(node);
            else if constexpr (std::is_same_v<T, NameExpr>)
                return node.name;
            else if constexpr (std::is_same_v<T, UnaryExpr>) {
                auto priority = node.op == UnaryOp::Not ? Not : Factor;
                std::u32string text = node.op == UnaryOp::Not        ? U"not "
                                      : node.op == UnaryOp::Positive ? U"+"
                                      : node.op == UnaryOp::Negative ? U"-"
                                                                     : U"~";
                return parenthesize(text + expression(*node.value, priority), level, priority);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                auto [op, priority] = binary_operator(node.op);
                const bool right = node.op == BinaryOp::Power;
                auto text = expression(*node.left, priority + right);
                text += op;
                text += expression(*node.right, priority + !right);
                return parenthesize(std::move(text), level, priority);
            } else if constexpr (std::is_same_v<T, BoolExpr>) {
                const auto priority = node.conjunction ? And : Or;
                std::u32string text;
                for (const auto &item : node.values) {
                    if (!text.empty())
                        text += node.conjunction ? U" and " : U" or ";
                    text += expression(*item, priority + 1);
                }
                return parenthesize(std::move(text), level, priority);
            } else if constexpr (std::is_same_v<T, CompareExpr>) {
                constexpr std::u32string_view operators[] = {
                    U" == ", U" != ", U" < ",      U" <= ", U" > ",
                    U" >= ", U" is ", U" is not ", U" in ", U" not in "};
                auto text = expression(*node.values[0], Compare + 1);
                for (std::size_t i = 0; i < node.ops.size(); ++i) {
                    text += operators[static_cast<std::size_t>(node.ops[i])];
                    text += expression(*node.values[i + 1], Compare + 1);
                }
                return parenthesize(std::move(text), level, Compare);
            } else if constexpr (std::is_same_v<T, ConditionalExpr>)
                return parenthesize(expression(*node.body, Test + 1) + U" if " +
                                        expression(*node.test, Test + 1) + U" else " +
                                        expression(*node.alternative, Test),
                                    level, Test);
            else if constexpr (std::is_same_v<T, NamedExpr>)
                return parenthesize(expression(*node.target, Atom) + U" := " +
                                        expression(*node.value, Atom),
                                    level, Tuple);
            else if constexpr (std::is_same_v<T, StarredExpr>)
                return U"*" + expression(*node.value, BitOr);
            else if constexpr (std::is_same_v<T, TupleExpr>) {
                if (node.items.empty())
                    return U"()";
                auto text = items(node.items);
                if (node.items.size() == 1)
                    text += U",";
                return parenthesize(std::move(text), level, Tuple);
            } else if constexpr (std::is_same_v<T, ListExpr>)
                return U"[" + items(node.items) + U"]";
            else if constexpr (std::is_same_v<T, SetExpr>)
                return U"{" + items(node.items) + U"}";
            else if constexpr (std::is_same_v<T, DictExpr>) {
                std::u32string text;
                for (const auto &[key, item] : node.items) {
                    if (!text.empty())
                        text += U", ";
                    text += key ? expression(*key, Test) + U": " + expression(*item, Test)
                                : U"**" + expression(*item, BitOr);
                }
                return U"{" + text + U"}";
            } else if constexpr (std::is_same_v<T, AttributeExpr>) {
                auto text = expression(*node.object, Atom);
                if (const auto *literal = std::get_if<Literal>(&node.object->node);
                    literal && literal->kind == Literal::Kind::Integer)
                    text += U" ";
                return text + U"." + node.name;
            } else if constexpr (std::is_same_v<T, SubscriptExpr>)
                return expression(*node.object, Atom) + U"[" + expression(*node.key, Tuple) + U"]";
            else if constexpr (std::is_same_v<T, SliceExpr>) {
                auto text = node.start ? expression(*node.start, Test) : U"";
                text += U":";
                if (node.stop)
                    text += expression(*node.stop, Test);
                if (node.step)
                    text += U":" + expression(*node.step, Test);
                return text;
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                auto text = expression(*node.target, Atom);
                if (node.positional.size() == 1 && node.keywords.empty())
                    if (const auto *comp =
                            std::get_if<ComprehensionExpr>(&node.positional[0]->node);
                        comp && comp->function->name == U"<genexpr>")
                        return text + comprehension(*comp);
                text += U"(";
                text += items(node.positional);
                bool first = node.positional.empty();
                for (const auto &[name, item] : node.keywords) {
                    if (!first)
                        text += U", ";
                    first = false;
                    text += (name.empty() ? U"**" : name + U"=") + expression(*item, Test);
                }
                return text + U")";
            } else if constexpr (std::is_same_v<T, LambdaExpr>) {
                const auto &fn = *node.function;
                const bool positional =
                    std::any_of(fn.parameters.begin(), fn.parameters.end(),
                                [](const auto &p) { return p.kind != ParameterKind::KeywordOnly; });
                const auto &body = std::get<ReturnStmt>(fn.body[0]->node).value;
                return parenthesize((positional ? U"lambda " : U"lambda") + arguments(fn) + U": " +
                                        expression(*body, Test),
                                    level, Test);
            } else if constexpr (std::is_same_v<T, ComprehensionExpr>)
                return comprehension(node);
            else if constexpr (std::is_same_v<T, JoinedStrExpr>)
                return U"f" + quoted(fstring_body(value), false);
            else if constexpr (std::is_same_v<T, FormattedExpr>)
                return fstring_body(value);
            else if constexpr (std::is_same_v<T, AwaitExpr>)
                return parenthesize(U"await " + expression(*node.value, Atom), level, Await);
            else {
                static_assert(std::is_same_v<T, YieldExpr>);
                return node.value ? (node.delegated ? U"(yield from " : U"(yield ") +
                                        expression(*node.value, Test) + U")"
                                  : U"(yield)";
            }
        },
        value.node);
}
} // namespace

std::u32string unparse_expression(const Expr &value, const SourceBuffer &)
{
    return expression(value, Test);
}
} // namespace cxxpython::core::detail
