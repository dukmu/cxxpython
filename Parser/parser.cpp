#include "cxxpython/floatobject.hpp"
#include "internal/compiler.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core {
SourceBuffer::SourceBuffer(std::string text, std::string filename) : filename_(std::move(filename))
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            ch = '\n';
        }
        text_.push_back(ch);
        if (ch == '\n')
            lines_.push_back(text_.size());
    }
    (void)detail::decode_source(*this, {0, text_.size()});
}
SourcePosition SourceBuffer::position(Span span) const
{
    auto at = [&](std::size_t offset) {
        offset = std::min(offset, text_.size());
        const auto row = static_cast<std::size_t>(
            std::upper_bound(lines_.begin(), lines_.end(), offset) - lines_.begin() - 1);
        return std::pair{static_cast<std::uint32_t>(row + 1),
                         static_cast<std::uint32_t>(offset - lines_[row])};
    };
    const auto [line, column] = at(span.begin);
    const auto [end_line, end_column] = at(span.end);
    return {line, column, end_line, end_column};
}
std::string SourceBuffer::line(std::uint32_t number) const
{
    if (!number || number > lines_.size())
        return {};
    const auto start = lines_[number - 1];
    const auto end = text_.find('\n', start);
    return text_.substr(start, end == std::string::npos ? end : end - start + 1);
}
SourceError::SourceError(const SourceBuffer &source, Span where, std::string message,
                         SourceErrorKind category)
    : std::invalid_argument(std::move(message)), kind(category), span(where),
      position(source.position(where)), filename(source.filename()),
      source_line(source.line(position.line)),
      offset(source.character_column(position.line, position.column) + 1),
      end_offset(source.character_column(position.end_line, position.end_column) + 1)
{}
std::uint32_t SourceBuffer::character_column(std::uint32_t number, std::uint32_t byte_column) const
{
    const auto text = line(number);
    const auto end = text.begin() + std::min<std::size_t>(byte_column, text.size());
    return static_cast<std::uint32_t>(
        std::count_if(text.begin(), end, [](unsigned char byte) { return (byte & 0xc0) != 0x80; }));
}
std::string source_utf8(std::u32string_view text)
{
    std::string out;
    for (auto point : text) {
        if (point >= 0xd800 && point <= 0xdfff) {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\u";
            for (int shift : {12, 8, 4, 0})
                out += hex[(point >> shift) & 15];
        } else if (point < 0x80)
            out += static_cast<char>(point);
        else if (point < 0x800) {
            out += static_cast<char>(0xc0 | (point >> 6));
            out += static_cast<char>(0x80 | (point & 63));
        } else if (point < 0x10000) {
            out += static_cast<char>(0xe0 | (point >> 12));
            out += static_cast<char>(0x80 | ((point >> 6) & 63));
            out += static_cast<char>(0x80 | (point & 63));
        } else if (point <= 0x10ffff) {
            out += static_cast<char>(0xf0 | (point >> 18));
            out += static_cast<char>(0x80 | ((point >> 12) & 63));
            out += static_cast<char>(0x80 | ((point >> 6) & 63));
            out += static_cast<char>(0x80 | (point & 63));
        } else
            throw std::invalid_argument("invalid Unicode codepoint");
    }
    return out;
}
} // namespace cxxpython::core

namespace cxxpython::core::detail {
static std::u32string decode_utf8(const SourceBuffer &source, std::string_view text, Span span)
{
    std::u32string out;
    for (auto i = span.begin; i < span.end;) {
        const auto start = i;
        const auto first = static_cast<unsigned char>(text[i++]);
        char32_t point;
        int extra;
        if (first < 0x80) {
            point = first;
            extra = 0;
        } else if (first >= 0xc2 && first <= 0xdf) {
            point = first & 31;
            extra = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            point = first & 15;
            extra = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            point = first & 7;
            extra = 3;
        } else
            throw SourceError(source, {start, i}, "invalid UTF-8 source",
                              SourceErrorKind::Encoding);
        for (int n = 0; n < extra; ++n) {
            if (i == span.end || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                throw SourceError(source, {start, i}, "invalid UTF-8 source",
                                  SourceErrorKind::Encoding);
            point = (point << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        if (!point || (extra == 1 && point < 0x80) || (extra == 2 && point < 0x800) ||
            (extra == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff))
            throw SourceError(source, {start, i}, "invalid UTF-8 source or null character",
                              SourceErrorKind::Encoding);
        out.push_back(point);
    }
    return out;
}
std::u32string decode_source(const SourceBuffer &source, Span span)
{
    return decode_utf8(source, source.text(), span);
}
std::u32string decode_filename(const SourceBuffer &source)
{
    return decode_utf8(source, source.filename(), {0, source.filename().size()});
}
namespace {
enum class TokenKind {
    End,
    Newline,
    Indent,
    Dedent,
    Name,
    Integer,
    Float,
    String,
    Left,
    Right,
    LeftBracket,
    RightBracket,
    Colon,
    Comma,
    Dot,
    At,
    Semi,
    Plus,
    Minus,
    Star,
    Power,
    Slash,
    Floor,
    Mod,
    Tilde,
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    FloorAssign,
    ModAssign,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    If,
    Elif,
    Else,
    While,
    For,
    In,
    Def,
    Class,
    Return,
    Raise,
    From,
    Try,
    Except,
    Finally,
    With,
    As,
    Pass,
    Break,
    Continue,
    Global,
    Nonlocal,
    Delete,
    And,
    Or,
    Not,
    Is,
    None,
    True,
    False,
    Unsupported,
};
struct Token {
    TokenKind kind;
    Span span;
    std::u32string text = {};
};
bool alpha(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}
bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}
class Lexer final {
  public:
    explicit Lexer(const SourceBuffer &source) : source_(source), text_(source.text()) {}
    std::vector<Token> scan()
    {
        while (index_ < text_.size()) {
            if (line_start_) {
                const auto start = index_;
                std::size_t column = 0, alternate = 0;
                while (index_ < text_.size() &&
                       (text_[index_] == ' ' || text_[index_] == '\t' || text_[index_] == '\f')) {
                    const auto ch = text_[index_++];
                    if (ch == '\f')
                        column = alternate = 0;
                    else {
                        column = ch == '\t' ? (column / 8 + 1) * 8 : column + 1;
                        ++alternate;
                    }
                }
                line_start_ = false;
                if (index_ == text_.size() || text_[index_] == '#' || text_[index_] == '\n')
                    continue;
                if (groups_.empty() && !continued_) {
                    if (column == indents_.back().first && alternate != indents_.back().second)
                        tab(start);
                    if (column > indents_.back().first) {
                        if (alternate <= indents_.back().second)
                            tab(start);
                        indents_.push_back({column, alternate});
                        push(TokenKind::Indent, start);
                    } else {
                        while (column < indents_.back().first) {
                            indents_.pop_back();
                            push(TokenKind::Dedent, start);
                        }
                        if (column != indents_.back().first)
                            fail({start, index_},
                                 "unindent does not match an outer indentation level",
                                 SourceErrorKind::Indentation);
                        if (alternate != indents_.back().second)
                            tab(start);
                    }
                }
                continued_ = false;
            }
            const auto start = index_;
            const auto ch = text_[index_++];
            if (ch == ' ' || ch == '\t' || ch == '\f')
                continue;
            if (ch == '#') {
                while (index_ < text_.size() && text_[index_] != '\n')
                    ++index_;
                continue;
            }
            if (ch == '\n') {
                if (groups_.empty())
                    push(TokenKind::Newline, start);
                line_start_ = true;
                continue;
            }
            if (ch == '\\') {
                if (index_ == text_.size() || text_[index_++] != '\n')
                    fail({start, index_}, "unexpected character after line continuation character");
                line_start_ = continued_ = true;
                continue;
            }
            if (alpha(ch)) {
                while (index_ < text_.size() && (alpha(text_[index_]) || digit(text_[index_])))
                    ++index_;
                auto word = text_.substr(start, index_ - start);
                if (index_ < text_.size() && (text_[index_] == '\'' || text_[index_] == '"')) {
                    if (word != "r" && word != "R")
                        fail(
                            {start, index_},
                            "prefixed bytes/f-string literals require the later literal capability",
                            SourceErrorKind::Capability);
                    string(start, true);
                    continue;
                }
                constexpr std::pair<std::string_view, TokenKind> words[] = {
                    {"if", TokenKind::If},
                    {"elif", TokenKind::Elif},
                    {"else", TokenKind::Else},
                    {"while", TokenKind::While},
                    {"def", TokenKind::Def},
                    {"return", TokenKind::Return},
                    {"pass", TokenKind::Pass},
                    {"break", TokenKind::Break},
                    {"continue", TokenKind::Continue},
                    {"global", TokenKind::Global},
                    {"nonlocal", TokenKind::Nonlocal},
                    {"del", TokenKind::Delete},
                    {"and", TokenKind::And},
                    {"or", TokenKind::Or},
                    {"not", TokenKind::Not},
                    {"is", TokenKind::Is},
                    {"None", TokenKind::None},
                    {"True", TokenKind::True},
                    {"False", TokenKind::False},
                    {"for", TokenKind::For},
                    {"in", TokenKind::In},
                    {"lambda", TokenKind::Unsupported},
                    {"class", TokenKind::Class},
                    {"try", TokenKind::Try},
                    {"except", TokenKind::Except},
                    {"finally", TokenKind::Finally},
                    {"with", TokenKind::With},
                    {"raise", TokenKind::Raise},
                    {"yield", TokenKind::Unsupported},
                    {"import", TokenKind::Unsupported},
                    {"from", TokenKind::From},
                    {"async", TokenKind::Unsupported},
                    {"await", TokenKind::Unsupported},
                    {"assert", TokenKind::Unsupported},
                    {"as", TokenKind::As},
                };
                auto kind = TokenKind::Name;
                for (auto [name, value] : words)
                    if (word == name) {
                        kind = value;
                        break;
                    }
                tokens_.push_back({kind, {start, index_}, {word.begin(), word.end()}});
                continue;
            }
            if (digit(ch) || (ch == '.' && index_ < text_.size() && digit(text_[index_]))) {
                const bool prefixed =
                    ch == '0' && index_ < text_.size() &&
                    std::string_view("xXoObB").find(text_[index_]) != std::string_view::npos;
                bool floating = ch == '.';
                if (!prefixed) {
                    auto digits = [&] {
                        while (index_ < text_.size() &&
                               (digit(text_[index_]) || text_[index_] == '_'))
                            ++index_;
                    };
                    digits();
                    if (!floating && index_ < text_.size() && text_[index_] == '.') {
                        floating = true;
                        ++index_;
                        digits();
                    }
                    if (index_ < text_.size() && (text_[index_] == 'e' || text_[index_] == 'E')) {
                        floating = true;
                        ++index_;
                        if (index_ < text_.size() && (text_[index_] == '+' || text_[index_] == '-'))
                            ++index_;
                        digits();
                    }
                }
                while (index_ < text_.size() && (alpha(text_[index_]) || digit(text_[index_])))
                    ++index_;
                const auto word = text_.substr(start, index_ - start);
                if (!prefixed && word.find_first_of("jJ") != std::string::npos)
                    fail({start, index_}, "complex literals require the complex numeric type",
                         SourceErrorKind::Capability);
                tokens_.push_back({floating ? TokenKind::Float : TokenKind::Integer,
                                   {start, index_},
                                   {word.begin(), word.end()}});
                continue;
            }
            if (ch == '\'' || ch == '"') {
                --index_;
                string(start, false);
                continue;
            }
            if (static_cast<unsigned char>(ch) >= 128)
                fail({start, index_}, "non-ASCII identifiers require Unicode NFKC normalization",
                     SourceErrorKind::Capability);
            auto equals = [&] {
                if (index_ < text_.size() && text_[index_] == '=') {
                    ++index_;
                    return true;
                }
                return false;
            };
            switch (ch) {
            case '(':
                groups_.push_back(start);
                push(TokenKind::Left, start);
                break;
            case ')':
                if (groups_.empty())
                    fail({start, index_}, "unmatched ')'");
                groups_.pop_back();
                push(TokenKind::Right, start);
                break;
            case '[':
                groups_.push_back(start);
                push(TokenKind::LeftBracket, start);
                break;
            case ']':
                if (groups_.empty())
                    fail({start, index_}, "unmatched ']'");
                groups_.pop_back();
                push(TokenKind::RightBracket, start);
                break;
            case ':':
                push(TokenKind::Colon, start);
                break;
            case ',':
                push(TokenKind::Comma, start);
                break;
            case '.':
                push(TokenKind::Dot, start);
                break;
            case ';':
                push(TokenKind::Semi, start);
                break;
            case '+':
                push(equals() ? TokenKind::AddAssign : TokenKind::Plus, start);
                break;
            case '-':
                push(equals() ? TokenKind::SubAssign : TokenKind::Minus, start);
                break;
            case '*':
                if (index_ < text_.size() && text_[index_] == '*') {
                    ++index_;
                    push(TokenKind::Power, start);
                } else
                    push(equals() ? TokenKind::MulAssign : TokenKind::Star, start);
                break;
            case '/':
                if (index_ < text_.size() && text_[index_] == '/') {
                    ++index_;
                    push(equals() ? TokenKind::FloorAssign : TokenKind::Floor, start);
                } else
                    push(equals() ? TokenKind::DivAssign : TokenKind::Slash, start);
                break;
            case '%':
                push(equals() ? TokenKind::ModAssign : TokenKind::Mod, start);
                break;
            case '~':
                push(TokenKind::Tilde, start);
                break;
            case '=':
                push(equals() ? TokenKind::Eq : TokenKind::Assign, start);
                break;
            case '!':
                if (!equals())
                    fail({start, index_}, "invalid syntax");
                push(TokenKind::Ne, start);
                break;
            case '<':
                push(equals() ? TokenKind::Le : TokenKind::Lt, start);
                break;
            case '>':
                push(equals() ? TokenKind::Ge : TokenKind::Gt, start);
                break;
            case '@':
                push(TokenKind::At, start);
                break;
            case '{':
            case '}':
            case '&':
            case '|':
            case '^':
                fail({start, index_},
                     "this container/operator syntax is outside the current L0 capability",
                     SourceErrorKind::Capability);
            default:
                fail({start, index_}, "invalid character in source");
            }
        }
        if (!groups_.empty())
            fail({groups_.back(), groups_.back() + 1}, "'(' was never closed");
        if (!tokens_.empty() && tokens_.back().kind != TokenKind::Newline)
            push(TokenKind::Newline, index_);
        while (indents_.size() > 1) {
            indents_.pop_back();
            push(TokenKind::Dedent, index_);
        }
        push(TokenKind::End, index_);
        return std::move(tokens_);
    }

  private:
    [[noreturn]] void fail(Span where, std::string message,
                           SourceErrorKind kind = SourceErrorKind::Syntax) const
    {
        throw SourceError(source_, where, std::move(message), kind);
    }
    [[noreturn]] void tab(std::size_t start) const
    {
        fail({start, index_}, "inconsistent use of tabs and spaces in indentation",
             SourceErrorKind::Tab);
    }
    void push(TokenKind kind, std::size_t start)
    {
        tokens_.push_back({kind, {start, index_}});
    }
    void string(std::size_t start, bool raw)
    {
        const auto quote = text_[index_++];
        const bool triple =
            index_ + 1 < text_.size() && text_[index_] == quote && text_[index_ + 1] == quote;
        if (triple)
            index_ += 2;
        std::u32string value;
        while (index_ < text_.size()) {
            if (text_[index_] == quote &&
                (!triple || (index_ + 2 < text_.size() && text_[index_ + 1] == quote &&
                             text_[index_ + 2] == quote))) {
                index_ += triple ? 3 : 1;
                tokens_.push_back({TokenKind::String, {start, index_}, std::move(value)});
                return;
            }
            const auto begin = index_;
            const auto ch = text_[index_++];
            if (ch == '\n' && !triple)
                fail({start, index_}, "unterminated string literal");
            if (ch != '\\') {
                const auto byte = static_cast<unsigned char>(ch);
                index_ = begin + (byte < 0x80 ? 1 : byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4);
                value += decode_source(source_, {begin, index_});
                continue;
            }
            if (index_ == text_.size())
                break;
            const auto escape = text_[index_++];
            if (static_cast<unsigned char>(escape) >= 128) {
                const auto byte = static_cast<unsigned char>(escape);
                const auto begin = index_ - 1;
                index_ = begin + (byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4);
                value += U'\\';
                value += decode_source(source_, {begin, index_});
                continue;
            }
            if (raw) {
                value += U'\\';
                value += static_cast<unsigned char>(escape);
                continue;
            }
            switch (escape) {
            case '\n':
                break;
            case '\\':
                value += U'\\';
                break;
            case '\'':
                value += U'\'';
                break;
            case '"':
                value += U'"';
                break;
            case 'n':
                value += U'\n';
                break;
            case 'r':
                value += U'\r';
                break;
            case 't':
                value += U'\t';
                break;
            case 'a':
                value += U'\a';
                break;
            case 'b':
                value += U'\b';
                break;
            case 'f':
                value += U'\f';
                break;
            case 'v':
                value += U'\v';
                break;
            case 'x':
            case 'u':
            case 'U': {
                const auto count = escape == 'x' ? 2 : escape == 'u' ? 4 : 8;
                char32_t point = 0;
                for (int n = 0; n < count; ++n) {
                    if (index_ == text_.size())
                        fail({begin, index_}, "truncated Unicode/hex escape");
                    const auto digit = text_[index_++];
                    const auto value = digit >= '0' && digit <= '9'   ? digit - '0'
                                       : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                                       : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                                      : -1;
                    if (value < 0)
                        fail({begin, index_}, "invalid Unicode/hex escape");
                    point = (point << 4) | value;
                }
                if (point > 0x10ffff)
                    fail({begin, index_}, "illegal Unicode character");
                value += point;
                break;
            }
            case 'N':
                fail({begin, index_}, "named Unicode escapes require the Unicode-name database",
                     SourceErrorKind::Capability);
            default:
                if (escape >= '0' && escape <= '7') {
                    char32_t point = escape - '0';
                    for (int n = 1; n < 3 && index_ < text_.size() && text_[index_] >= '0' &&
                                    text_[index_] <= '7';
                         ++n)
                        point = point * 8 + text_[index_++] - '0';
                    value += point;
                } else {
                    value += U'\\';
                    value += static_cast<unsigned char>(escape);
                }
            }
        }
        fail({start, index_}, "unterminated string literal");
    }
    const SourceBuffer &source_;
    const std::string &text_;
    std::size_t index_ = 0;
    bool line_start_ = true, continued_ = false;
    std::vector<std::pair<std::size_t, std::size_t>> indents_{{0, 0}};
    std::vector<std::size_t> groups_;
    std::vector<Token> tokens_;
};

class Parser final {
  public:
    explicit Parser(const SourceBuffer &source) : source_(source), tokens_(Lexer(source).scan()) {}
    Suite parse(InputMode mode)
    {
        Suite result;
        newlines();
        if (mode == InputMode::Eval) {
            auto value = expression_list();
            const auto span = value->span;
            result.push_back(statement(ReturnStmt{std::move(value)}, span));
            newlines();
            expect(TokenKind::End);
            return result;
        }
        while (!is(TokenKind::End)) {
            append(result, compound());
            newlines();
        }
        return result;
    }

  private:
    using K = TokenKind;
    class Recursion final {
      public:
        explicit Recursion(Parser &parser) : parser_(parser)
        {
            if (++parser_.recursion_ > 256)
                parser_.fail(parser_.peek(), "source nesting exceeds the current compiler limit");
        }
        ~Recursion()
        {
            --parser_.recursion_;
        }

      private:
        Parser &parser_;
    };
    bool is(K kind) const
    {
        return tokens_[index_].kind == kind;
    }
    const Token &peek() const
    {
        return tokens_[index_];
    }
    bool accept(K kind)
    {
        if (!is(kind))
            return false;
        ++index_;
        return true;
    }
    Token take()
    {
        return tokens_[index_++];
    }
    Token expect(K kind)
    {
        if (!is(kind)) {
            if (kind == K::Name)
                throw SourceError(source_, peek().span, "expected an identifier");
            fail(peek(), "invalid syntax");
        }
        return take();
    }
    [[noreturn]] void fail(const Token &token, std::string message,
                           SourceErrorKind kind = SourceErrorKind::Syntax) const
    {
        if (token.kind == K::Unsupported && kind == SourceErrorKind::Syntax) {
            kind = SourceErrorKind::Capability;
            message =
                "syntax '" + source_utf8(token.text) + "' requires a later language capability";
        }
        if (token.kind == K::Indent && kind == SourceErrorKind::Syntax) {
            kind = SourceErrorKind::Indentation;
            message = "unexpected indent";
        }
        throw SourceError(source_, token.span, std::move(message), kind);
    }
    void newlines()
    {
        while (accept(K::Newline)) {
        }
    }
    template <class T> ExprPtr node(T value, Span span, std::size_t depth = 1)
    {
        if (depth > 128)
            throw SourceError(source_, span,
                              "expression nesting exceeds the current compiler limit");
        return std::make_unique<Expr>(Expr{span, depth, std::move(value)});
    }
    template <class T> std::unique_ptr<Stmt> statement(T value, Span span)
    {
        return std::make_unique<Stmt>(Stmt{span, std::move(value)});
    }
    static void append(Suite &to, Suite from)
    {
        for (auto &item : from)
            to.push_back(std::move(item));
    }
    Suite suite()
    {
        expect(K::Colon);
        if (!accept(K::Newline))
            return simple_line();
        newlines();
        if (!accept(K::Indent))
            fail(peek(), "expected an indented block", SourceErrorKind::Indentation);
        if (++suite_depth_ > 128)
            fail(peek(), "too many nested suites");
        Suite body;
        newlines();
        while (!is(K::Dedent) && !is(K::End)) {
            append(body, compound());
            newlines();
        }
        expect(K::Dedent);
        --suite_depth_;
        return body;
    }
    Suite compound()
    {
        Recursion recursion(*this);
        if (is(K::At)) {
            const auto start = peek().span.begin;
            std::vector<ExprPtr> decorators;
            while (accept(K::At)) {
                decorators.push_back(expression());
                expect(K::Newline);
                newlines();
            }
            if (!is(K::Def) && !is(K::Class))
                fail(peek(), "decorator must precede a function or class");
            auto declaration = compound();
            auto &first = *declaration.front();
            first.span.begin = start;
            if (auto *function = std::get_if<FunctionStmt>(&first.node))
                function->decorators = std::move(decorators);
            else
                std::get<ClassStmt>(first.node).decorators = std::move(decorators);
            return declaration;
        }
        if (accept(K::Class)) {
            const auto start = tokens_[index_ - 1].span.begin;
            ClassStmt definition;
            definition.name = expect(K::Name).text;
            if (accept(K::Left)) {
                arguments(definition.bases, definition.keywords);
                expect(K::Right);
            }
            if (is(K::LeftBracket))
                fail(peek(), "type parameters require annotation-scope support",
                     SourceErrorKind::Capability);
            definition.body = suite();
            Suite result;
            result.push_back(
                statement(std::move(definition), {start, tokens_[index_ - 1].span.end}));
            return result;
        }
        if (accept(K::Try)) {
            const auto start = tokens_[index_ - 1].span.begin;
            TryStmt attempt;
            attempt.body = suite();
            bool bare = false;
            while (accept(K::Except)) {
                const auto token = tokens_[index_ - 1];
                if (bare)
                    fail(token, "default except must be last");
                if (is(K::Star))
                    fail(peek(), "except* requires exception-group support",
                         SourceErrorKind::Capability);
                ExceptHandler handler;
                if (!is(K::Colon)) {
                    handler.type = expression();
                    if (accept(K::As)) {
                        const auto name = expect(K::Name);
                        handler.name = {name.text, name.span};
                    }
                } else
                    bare = true;
                handler.body = suite();
                handler.span = {token.span.begin, tokens_[index_ - 1].span.end};
                attempt.handlers.push_back(std::move(handler));
            }
            if (accept(K::Else)) {
                if (attempt.handlers.empty())
                    fail(tokens_[index_ - 1], "try else requires except");
                attempt.alternative = suite();
            }
            if (accept(K::Finally))
                attempt.cleanup = suite();
            if (attempt.handlers.empty() && attempt.cleanup.empty())
                fail(peek(), "expected except or finally");
            Suite result;
            result.push_back(statement(std::move(attempt), {start, tokens_[index_ - 1].span.end}));
            return result;
        }
        if (accept(K::With)) {
            const auto start = tokens_[index_ - 1].span.begin;
            bool parenthesized = false;
            if (is(K::Left)) {
                std::size_t depth = 0;
                for (auto i = index_; i < tokens_.size(); ++i) {
                    if (tokens_[i].kind == K::Left)
                        ++depth;
                    else if (tokens_[i].kind == K::Right && --depth == 0) {
                        parenthesized = i > index_ + 1 && i + 1 < tokens_.size() &&
                                        tokens_[i + 1].kind == K::Colon;
                        break;
                    }
                }
            }
            if (parenthesized)
                expect(K::Left);
            WithStmt manager;
            do {
                auto value = expression();
                ExprPtr assigned;
                if (accept(K::As)) {
                    assigned = expression();
                    target(*assigned);
                }
                manager.items.push_back({std::move(value), std::move(assigned)});
                if (!accept(K::Comma))
                    break;
                if (parenthesized && is(K::Right))
                    break;
            } while (true);
            if (parenthesized)
                expect(K::Right);
            manager.body = suite();
            Suite result;
            result.push_back(statement(std::move(manager), {start, tokens_[index_ - 1].span.end}));
            return result;
        }
        if (accept(K::For)) {
            const auto start = tokens_[index_ - 1].span.begin;
            auto assigned = expression_list();
            target(*assigned);
            expect(K::In);
            auto iterable = expression_list();
            auto body = suite();
            Suite alternative;
            if (accept(K::Else))
                alternative = suite();
            Suite result;
            result.push_back(statement(ForStmt{std::move(assigned), std::move(iterable),
                                               std::move(body), std::move(alternative)},
                                       {start, tokens_[index_ - 1].span.end}));
            return result;
        }
        if (is(K::If) || is(K::While)) {
            const auto first = take();
            auto test = expression();
            auto body = suite();
            Suite alternative;
            if (first.kind == K::If && is(K::Elif)) {
                tokens_[index_].kind = K::If;
                alternative = compound();
            } else if (accept(K::Else))
                alternative = suite();
            Suite result;
            const Span span{first.span.begin, tokens_[index_ - 1].span.end};
            if (first.kind == K::If)
                result.push_back(statement(
                    IfStmt{std::move(test), std::move(body), std::move(alternative)}, span));
            else
                result.push_back(statement(
                    WhileStmt{std::move(test), std::move(body), std::move(alternative)}, span));
            return result;
        }
        if (accept(K::Def)) {
            const auto start = tokens_[index_ - 1].span.begin;
            FunctionStmt fn;
            fn.name = expect(K::Name).text;
            expect(K::Left);
            bool keyword_only = false, slash = false, default_seen = false, bare_star = false;
            std::unordered_set<std::u32string> names;
            auto parameter_name = [&] {
                auto token = expect(K::Name);
                if (token.text == U"__debug__")
                    fail(token, "cannot assign __debug__");
                if (!names.insert(token.text).second)
                    fail(token, "duplicate parameter name");
                return token;
            };
            while (!is(K::Right)) {
                if (accept(K::Slash)) {
                    if (slash || keyword_only || fn.parameters.empty())
                        fail(tokens_[index_ - 1], "invalid positional-only separator");
                    for (auto &parameter : fn.parameters)
                        parameter.kind = ParameterKind::PositionalOnly;
                    slash = true;
                } else if (accept(K::Power)) {
                    fn.varkwargs = parameter_name().text;
                    accept(K::Comma);
                    if (!is(K::Right))
                        fail(peek(), "arguments follow **kwargs");
                    break;
                } else if (accept(K::Star)) {
                    if (keyword_only)
                        fail(tokens_[index_ - 1], "multiple * separators");
                    keyword_only = true;
                    if (is(K::Name))
                        fn.varargs = parameter_name().text;
                    else
                        bare_star = true;
                } else {
                    auto token = parameter_name();
                    ExprPtr value;
                    if (accept(K::Assign))
                        value = expression();
                    if (!keyword_only && default_seen && !value)
                        fail(token, "non-default argument follows default argument");
                    if (!keyword_only)
                        default_seen |= static_cast<bool>(value);
                    fn.parameters.push_back({token.text,
                                             keyword_only ? ParameterKind::KeywordOnly
                                                          : ParameterKind::PositionalOrKeyword,
                                             std::move(value), token.span});
                }
                if (is(K::Colon))
                    fail(peek(), "annotations require the later annotation-scope capability",
                         SourceErrorKind::Capability);
                if (!accept(K::Comma))
                    break;
            }
            if (bare_star &&
                std::none_of(fn.parameters.begin(), fn.parameters.end(),
                             [](const auto &p) { return p.kind == ParameterKind::KeywordOnly; }))
                fail(peek(), "named arguments must follow bare *");
            expect(K::Right);
            fn.body = suite();
            Suite result;
            result.push_back(statement(std::move(fn), {start, tokens_[index_ - 1].span.end}));
            return result;
        }
        return simple_line();
    }
    bool line_end() const
    {
        return is(K::Newline) || is(K::Semi) || is(K::End) || is(K::Dedent);
    }
    void target(const Expr &value)
    {
        if (const auto *tuple = std::get_if<TupleExpr>(&value.node)) {
            for (const auto &item : tuple->items)
                target(*item);
            return;
        }
        if (!std::holds_alternative<NameExpr>(value.node) &&
            !std::holds_alternative<AttributeExpr>(value.node) &&
            !std::holds_alternative<SubscriptExpr>(value.node))
            throw SourceError(source_, value.span, "cannot assign/delete this expression");
    }
    Suite simple_line()
    {
        Suite result;
        do {
            const auto start = peek().span.begin;
            if (is(K::Pass) || is(K::Break) || is(K::Continue)) {
                const auto token = take();
                result.push_back(statement(token.kind == K::Pass    ? ControlStmt::Pass
                                           : token.kind == K::Break ? ControlStmt::Break
                                                                    : ControlStmt::Continue,
                                           token.span));
            } else if (accept(K::Return)) {
                auto value = line_end() ? ExprPtr{} : expression_list();
                result.push_back(
                    statement(ReturnStmt{std::move(value)}, {start, tokens_[index_ - 1].span.end}));
            } else if (accept(K::Raise)) {
                RaiseStmt raised;
                if (!line_end()) {
                    raised.exception = expression();
                    if (accept(K::From))
                        raised.cause = expression();
                }
                result.push_back(
                    statement(std::move(raised), {start, tokens_[index_ - 1].span.end}));
            } else if (is(K::Global) || is(K::Nonlocal)) {
                DeclareStmt declaration{take().kind == K::Nonlocal, {}};
                do {
                    auto token = expect(K::Name);
                    declaration.names.push_back({token.text, token.span});
                } while (accept(K::Comma));
                result.push_back(
                    statement(std::move(declaration), {start, tokens_[index_ - 1].span.end}));
            } else if (accept(K::Delete)) {
                DeleteStmt deletion;
                do {
                    auto value = expression();
                    target(*value);
                    deletion.targets.push_back(std::move(value));
                } while (accept(K::Comma));
                result.push_back(
                    statement(std::move(deletion), {start, tokens_[index_ - 1].span.end}));
            } else {
                auto value = expression_list();
                if (accept(K::Assign)) {
                    AssignStmt assign;
                    do {
                        target(*value);
                        assign.targets.push_back(std::move(value));
                        value = expression_list();
                    } while (accept(K::Assign));
                    assign.value = std::move(value);
                    result.push_back(
                        statement(std::move(assign), {start, tokens_[index_ - 1].span.end}));
                } else if (is(K::AddAssign) || is(K::SubAssign) || is(K::MulAssign) ||
                           is(K::FloorAssign) || is(K::ModAssign) || is(K::DivAssign)) {
                    target(*value);
                    if (std::holds_alternative<TupleExpr>(value->node))
                        fail(peek(), "illegal augmented assignment target");
                    const auto kind = take().kind;
                    const auto op = kind == K::AddAssign     ? BinaryOp::Add
                                    : kind == K::SubAssign   ? BinaryOp::Subtract
                                    : kind == K::MulAssign   ? BinaryOp::Multiply
                                    : kind == K::FloorAssign ? BinaryOp::FloorDivide
                                    : kind == K::DivAssign   ? BinaryOp::TrueDivide
                                                             : BinaryOp::Modulo;
                    auto right = expression();
                    result.push_back(
                        statement(AugAssignStmt{std::move(value), op, std::move(right)},
                                  {start, tokens_[index_ - 1].span.end}));
                } else
                    result.push_back(statement(ExprStmt{std::move(value)},
                                               {start, tokens_[index_ - 1].span.end}));
            }
        } while (accept(K::Semi) && !line_end());
        if (!accept(K::Newline) && !is(K::End) && !is(K::Dedent))
            fail(peek(), "expected a statement separator");
        return result;
    }
    ExprPtr expression()
    {
        return boolean(false);
    }
    std::size_t arguments(std::vector<ExprPtr> &positional,
                          std::vector<std::pair<std::u32string, ExprPtr>> &named)
    {
        std::unordered_set<std::u32string> keywords;
        std::size_t depth = 0;
        while (!is(K::Right)) {
            if (is(K::Star) || is(K::Power))
                fail(peek(), "call unpacking requires the unpacking capability",
                     SourceErrorKind::Capability);
            if (is(K::Name) && tokens_[index_ + 1].kind == K::Assign) {
                const auto name = take();
                take();
                if (!keywords.insert(name.text).second)
                    fail(name, "keyword argument repeated");
                auto argument = expression();
                depth = std::max(depth, argument->depth);
                named.push_back({name.text, std::move(argument)});
            } else {
                if (!keywords.empty())
                    fail(peek(), "positional argument follows keyword argument");
                auto argument = expression();
                depth = std::max(depth, argument->depth);
                positional.push_back(std::move(argument));
            }
            if (!accept(K::Comma))
                break;
        }
        return depth;
    }
    ExprPtr expression_list()
    {
        auto value = expression();
        if (!accept(K::Comma))
            return value;
        const auto start = value->span.begin;
        auto end = value->span.end;
        auto depth = value->depth;
        TupleExpr tuple;
        tuple.items.push_back(std::move(value));
        while (!line_end() && !is(K::Right) && !is(K::RightBracket) && !is(K::Colon) &&
               !is(K::In) && !is(K::Assign)) {
            auto item = expression();
            end = item->span.end;
            depth = std::max(depth, item->depth);
            tuple.items.push_back(std::move(item));
            if (!accept(K::Comma))
                break;
        }
        return node(std::move(tuple), {start, end}, depth + 1);
    }
    ExprPtr boolean(bool conjunction)
    {
        auto left = conjunction ? logical_not() : boolean(true);
        while (accept(conjunction ? K::And : K::Or)) {
            auto right = conjunction ? logical_not() : boolean(true);
            const Span span{left->span.begin, right->span.end};
            const auto depth = std::max(left->depth, right->depth) + 1;
            left = node(BoolExpr{conjunction, std::move(left), std::move(right)}, span, depth);
        }
        return left;
    }
    ExprPtr logical_not()
    {
        Recursion recursion(*this);
        if (!accept(K::Not))
            return comparison();
        const auto start = tokens_[index_ - 1].span.begin;
        auto value = logical_not();
        const auto end = value->span.end, depth = value->depth + 1;
        return node(UnaryExpr{UnaryOp::Not, std::move(value)}, {start, end}, depth);
    }
    ExprPtr comparison()
    {
        CompareExpr compare;
        compare.values.push_back(arithmetic(false));
        std::size_t depth = compare.values[0]->depth;
        while (is(K::Eq) || is(K::Ne) || is(K::Lt) || is(K::Le) || is(K::Gt) || is(K::Ge) ||
               is(K::Is)) {
            const auto kind = take().kind;
            auto op = kind == K::Eq   ? CompareOp::Eq
                      : kind == K::Ne ? CompareOp::Ne
                      : kind == K::Lt ? CompareOp::Lt
                      : kind == K::Le ? CompareOp::Le
                      : kind == K::Gt ? CompareOp::Gt
                      : kind == K::Ge ? CompareOp::Ge
                                      : CompareOp::Is;
            if (kind == K::Is && accept(K::Not))
                op = CompareOp::IsNot;
            compare.ops.push_back(op);
            compare.values.push_back(arithmetic(false));
            depth = std::max(depth, compare.values.back()->depth);
        }
        if (compare.ops.empty())
            return std::move(compare.values[0]);
        const Span span{compare.values.front()->span.begin, compare.values.back()->span.end};
        return node(std::move(compare), span, depth + 1);
    }
    ExprPtr arithmetic(bool term)
    {
        auto left = term ? factor() : arithmetic(true);
        while (is(term ? K::Star : K::Plus) || is(term ? K::Floor : K::Minus) ||
               (term && (is(K::Mod) || is(K::Slash)))) {
            const auto kind = take().kind;
            const auto op = kind == K::Plus    ? BinaryOp::Add
                            : kind == K::Minus ? BinaryOp::Subtract
                            : kind == K::Star  ? BinaryOp::Multiply
                            : kind == K::Floor ? BinaryOp::FloorDivide
                            : kind == K::Slash ? BinaryOp::TrueDivide
                                               : BinaryOp::Modulo;
            auto right = term ? factor() : arithmetic(true);
            const Span span{left->span.begin, right->span.end};
            const auto depth = std::max(left->depth, right->depth) + 1;
            left = node(BinaryExpr{op, std::move(left), std::move(right)}, span, depth);
        }
        if (is(K::Power))
            fail(peek(), "power requires the corresponding numeric protocol",
                 SourceErrorKind::Capability);
        return left;
    }
    ExprPtr factor()
    {
        Recursion recursion(*this);
        if (is(K::Plus) || is(K::Minus) || is(K::Tilde)) {
            const auto token = take();
            auto value = factor();
            const auto end = value->span.end, depth = value->depth + 1;
            return node(UnaryExpr{token.kind == K::Plus    ? UnaryOp::Positive
                                  : token.kind == K::Minus ? UnaryOp::Negative
                                                           : UnaryOp::Invert,
                                  std::move(value)},
                        {token.span.begin, end}, depth);
        }
        auto value = atom();
        while (is(K::Left) || is(K::Dot) || is(K::LeftBracket)) {
            const auto start = value->span.begin;
            if (accept(K::LeftBracket)) {
                auto key = expression_list();
                const auto end = expect(K::RightBracket).span.end;
                const auto depth = std::max(value->depth, key->depth) + 1;
                value = node(SubscriptExpr{std::move(value), std::move(key)}, {start, end}, depth);
                continue;
            }
            if (accept(K::Dot)) {
                const auto name = expect(K::Name);
                const auto depth = value->depth + 1;
                value =
                    node(AttributeExpr{std::move(value), name.text}, {start, name.span.end}, depth);
                continue;
            }
            expect(K::Left);
            CallExpr call;
            call.target = std::move(value);
            const auto depth =
                std::max(call.target->depth, arguments(call.positional, call.keywords));
            const auto end = expect(K::Right).span.end;
            value = node(std::move(call), {start, end}, depth + 1);
        }
        return value;
    }
    ExprPtr atom()
    {
        const auto token = take();
        switch (token.kind) {
        case K::Name:
            return node(NameExpr{token.text}, token.span);
        case K::Float: {
            double number;
            if (!detail::parse_float(token.text, number))
                fail(token, "invalid float literal");
            return node(Literal{Literal::Kind::Float, token.text, number}, token.span);
        }
        case K::Integer: {
            auto text = token.text;
            unsigned base = 10;
            std::size_t begin = 0;
            if (text.size() > 1 && text[0] == U'0') {
                const auto ch = text[1];
                base = ch == U'x' || ch == U'X'   ? 16
                       : ch == U'o' || ch == U'O' ? 8
                       : ch == U'b' || ch == U'B' ? 2
                                                  : 10;
                if (base != 10)
                    begin = 2;
            }
            bool previous_digit = false, nonzero = false, any = false;
            for (auto i = begin; i < text.size(); ++i) {
                if (text[i] == U'_') {
                    if ((!previous_digit && !(i == begin && begin)) || i + 1 == text.size())
                        fail(token, "invalid integer literal");
                    previous_digit = false;
                    continue;
                }
                const auto ch = text[i];
                const auto value = ch >= U'0' && ch <= U'9'   ? ch - U'0'
                                   : ch >= U'a' && ch <= U'f' ? ch - U'a' + 10
                                   : ch >= U'A' && ch <= U'F' ? ch - U'A' + 10
                                                              : 99;
                if (value >= base)
                    fail(token, "invalid integer literal");
                previous_digit = any = true;
                nonzero |= value != 0;
            }
            if (!any || (base == 10 && text.size() > 1 && text[0] == U'0' && nonzero))
                fail(token, "invalid leading zero or integer literal");
            return node(Literal{Literal::Kind::Integer, std::move(text)}, token.span);
        }
        case K::String: {
            auto text = token.text;
            auto end = token.span.end;
            while (is(K::String)) {
                auto next = take();
                text += next.text;
                end = next.span.end;
            }
            return node(Literal{Literal::Kind::String, std::move(text)}, {token.span.begin, end});
        }
        case K::None:
        case K::True:
        case K::False:
            return node(Literal{token.kind == K::None   ? Literal::Kind::None
                                : token.kind == K::True ? Literal::Kind::True
                                                        : Literal::Kind::False,
                                {}},
                        token.span);
        case K::Left: {
            if (++expression_depth_ > 128)
                fail(token, "too many nested parentheses");
            if (accept(K::Right)) {
                --expression_depth_;
                return node(TupleExpr{}, {token.span.begin, tokens_[index_ - 1].span.end});
            }
            auto value = expression_list();
            expect(K::Right);
            --expression_depth_;
            return value;
        }
        case K::LeftBracket:
            fail(token, "list literals require the remaining mutable-container capability",
                 SourceErrorKind::Capability);
        default:
            fail(token, "expected an expression");
        }
    }
    const SourceBuffer &source_;
    std::vector<Token> tokens_;
    std::size_t index_ = 0, suite_depth_ = 0, expression_depth_ = 0, recursion_ = 0;
};
} // namespace
Suite parse_source(const SourceBuffer &source, InputMode mode)
{
    return Parser(source).parse(mode);
}
} // namespace cxxpython::core::detail
