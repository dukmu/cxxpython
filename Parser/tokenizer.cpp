#include "cxxpython/parser.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace cxxpython::syntax {
namespace {

#include "../Objects/unicode_identifiers.inc"
#include "unicode_normalization.inc"

template <std::size_t N>
bool in_ranges(const std::pair<char32_t, char32_t> (&ranges)[N], char32_t point)
{
    auto it =
        std::upper_bound(std::begin(ranges), std::end(ranges), point,
                         [](char32_t value, const auto &range) { return value < range.first; });
    return it != std::begin(ranges) && point <= (it - 1)->second;
}

unsigned combining_class(char32_t point)
{
    auto it = std::lower_bound(
        std::begin(normalization_combining), std::end(normalization_combining), point,
        [](const auto &entry, char32_t value) { return entry.point < value; });
    return it != std::end(normalization_combining) && it->point == point ? it->value : 0;
}

char32_t compose(char32_t first, char32_t second)
{
    constexpr char32_t sbase = 0xac00, lbase = 0x1100, vbase = 0x1161, tbase = 0x11a7;
    constexpr unsigned lcount = 19, vcount = 21, tcount = 28, ncount = vcount * tcount;
    if (first >= lbase && first < lbase + lcount && second >= vbase && second < vbase + vcount)
        return sbase + ((first - lbase) * vcount + second - vbase) * tcount;
    if (first >= sbase && first < sbase + lcount * ncount && (first - sbase) % tcount == 0 &&
        second > tbase && second < tbase + tcount)
        return first + second - tbase;
    auto key = std::make_pair(first, second);
    auto it = std::lower_bound(std::begin(normalization_compositions),
                               std::end(normalization_compositions), key,
                               [](const auto &entry, const auto &value) {
                                   return std::make_pair(entry.first, entry.second) < value;
                               });
    return it != std::end(normalization_compositions) && it->first == first && it->second == second
               ? it->result
               : 0;
}

std::u32string normalize(std::u32string_view input)
{
    std::u32string decomposed;
    for (char32_t point : input) {
        if (point >= 0xac00 && point <= 0xd7a3) {
            auto index = point - 0xac00;
            decomposed.push_back(0x1100 + index / 588);
            decomposed.push_back(0x1161 + (index % 588) / 28);
            if (index % 28)
                decomposed.push_back(0x11a7 + index % 28);
        } else {
            auto it = std::lower_bound(
                std::begin(normalization_decompositions), std::end(normalization_decompositions),
                point, [](const auto &entry, char32_t value) { return entry.point < value; });
            if (it != std::end(normalization_decompositions) && it->point == point)
                decomposed.append(normalization_decomposition_data + it->offset, it->length);
            else
                decomposed.push_back(point);
        }
    }
    for (std::size_t i = 1; i < decomposed.size(); ++i) {
        unsigned ccc = combining_class(decomposed[i]);
        if (!ccc)
            continue;
        std::size_t j = i;
        while (j && combining_class(decomposed[j - 1]) > ccc) {
            std::swap(decomposed[j], decomposed[j - 1]);
            --j;
        }
    }
    std::u32string result;
    std::size_t starter = 0;
    unsigned previous = 0;
    for (char32_t point : decomposed) {
        unsigned ccc = combining_class(point);
        char32_t composite = !result.empty() && (previous < ccc || previous == 0)
                                 ? compose(result[starter], point)
                                 : 0;
        if (composite) {
            result[starter] = composite;
        } else {
            if (!ccc)
                starter = result.size();
            result.push_back(point);
            previous = ccc;
        }
    }
    return result;
}

class Scanner {
    std::string_view source;
    std::size_t pos = 0;
    std::vector<Token> tokens;
    std::vector<std::pair<int, int>> indents{{0, 0}};
    std::vector<char> brackets;
    bool line_start = true;
    bool line_has_token = false;
    InputMode mode;
    bool allow_incomplete;
    bool interactive_blank = false;

    char peek(std::size_t n = 0) const
    {
        return pos + n < source.size() ? source[pos + n] : '\0';
    }
    [[noreturn]] void fail(std::size_t start, const char *message,
                           ErrorKind kind = ErrorKind::Syntax) const
    {
        throw Error({start, pos}, message, kind);
    }
    char32_t decode(std::size_t &at) const
    {
        const auto start = at;
        auto byte = [&](std::size_t i) { return static_cast<unsigned char>(source[i]); };
        unsigned char c = byte(at++);
        if (c < 0x80)
            return c;
        int n = c >= 0xc2 && c <= 0xdf   ? 1
                : c >= 0xe0 && c <= 0xef ? 2
                : c >= 0xf0 && c <= 0xf4 ? 3
                                         : -1;
        if (n < 0 || at + n > source.size())
            throw Error({start, at}, "invalid UTF-8", ErrorKind::Encoding);
        char32_t value = c & ((1 << (6 - n)) - 1);
        for (int i = 0; i < n; ++i) {
            unsigned char next = byte(at++);
            if ((next & 0xc0) != 0x80)
                throw Error({start, at}, "invalid UTF-8", ErrorKind::Encoding);
            value = (value << 6) | (next & 0x3f);
        }
        if ((n == 2 && value < 0x800) || (n == 3 && value < 0x10000) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
            throw Error({start, at}, "invalid UTF-8", ErrorKind::Encoding);
        return value;
    }
    void emit(TokenKind kind, std::size_t start, std::size_t end)
    {
        std::u32string text;
        for (auto i = start; i < end;)
            text.push_back(decode(i));
        tokens.push_back({kind, {start, end}, std::move(text)});
    }
    static bool digit(char c)
    {
        return c >= '0' && c <= '9';
    }
    static bool alpha(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool name_char(char c)
    {
        return alpha(c) || digit(c) || static_cast<unsigned char>(c) >= 128;
    }
    bool number_suffix_allowed() const
    {
        for (std::string_view word : {"and", "else", "for", "if", "in", "is", "or", "not"})
            if (source.substr(pos, word.size()) == word)
                return true;
        return false;
    }
    void newline()
    {
        if (peek() == '\r') {
            ++pos;
            if (peek() == '\n')
                ++pos;
        } else {
            ++pos;
        }
        line_start = true;
    }
    void indentation()
    {
        auto start = pos;
        int column = 0, alternate = 0;
        while (peek() == ' ' || peek() == '\t' || peek() == '\f') {
            if (peek() == ' ') {
                ++column;
                ++alternate;
            } else if (peek() == '\t') {
                column = (column / 8 + 1) * 8;
                ++alternate;
            } else {
                column = alternate = 0;
            }
            ++pos;
        }
        if (peek() == '#' || peek() == '\n' || peek() == '\r' || !peek())
            return;
        line_start = false;
        auto current = indents.back();
        if (column == current.first) {
            if (alternate != current.second)
                fail(start, "inconsistent use of tabs and spaces in indentation", ErrorKind::Tab);
        } else if (column > current.first) {
            if (alternate <= current.second)
                fail(start, "inconsistent use of tabs and spaces in indentation", ErrorKind::Tab);
            indents.emplace_back(column, alternate);
            emit(TokenKind::INDENT, start, pos);
        } else {
            while (indents.size() > 1 && column < indents.back().first) {
                indents.pop_back();
                emit(TokenKind::DEDENT, pos, pos);
            }
            if (column != indents.back().first)
                fail(start, "unindent does not match any outer indentation level",
                     ErrorKind::Indentation);
            if (alternate != indents.back().second)
                fail(start, "inconsistent use of tabs and spaces in indentation", ErrorKind::Tab);
        }
    }
    bool prefix(std::size_t &length, bool &formatted) const
    {
        std::size_t i = pos;
        while (i < source.size() && i - pos < 2 &&
               (source[i] == 'r' || source[i] == 'R' || source[i] == 'b' || source[i] == 'B' ||
                source[i] == 'u' || source[i] == 'U' || source[i] == 'f' || source[i] == 'F'))
            ++i;
        if (i >= source.size() || (source[i] != '\'' && source[i] != '"'))
            return false;
        std::string p;
        for (auto j = pos; j < i; ++j)
            p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(source[j]))));
        if (!(p.empty() || p == "r" || p == "b" || p == "u" || p == "f" || p == "br" || p == "rb" ||
              p == "fr" || p == "rf"))
            return false;
        length = i - pos;
        formatted = p.find('f') != std::string::npos;
        return true;
    }
    bool closing(char quote, bool triple) const
    {
        return peek() == quote && (!triple || (peek(1) == quote && peek(2) == quote));
    }
    void escape(bool raw, bool bytes, bool formatted, bool triple)
    {
        auto start = pos++;
        if (pos == source.size())
            fail(start, "unterminated string literal",
                 triple && allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
        char c = peek();
        if (bytes && static_cast<unsigned char>(c) >= 128)
            fail(start, "bytes can only contain ASCII literal characters");
        if (formatted && (c == '{' || c == '}'))
            return;
        ++pos;
        if (c == '\r' && peek() == '\n') {
            ++pos;
            return;
        }
        if (raw)
            return;
        int count = c == 'x' ? 2 : !bytes && c == 'u' ? 4 : !bytes && c == 'U' ? 8 : 0;
        if (count) {
            char32_t value = 0;
            for (int i = 0; i < count; ++i) {
                char digit = peek();
                int n = digit >= '0' && digit <= '9'   ? digit - '0'
                        : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                        : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                       : -1;
                if (n < 0)
                    fail(start, "truncated hexadecimal escape");
                value = (value << 4) | n;
                ++pos;
            }
            if (c == 'U' && value > 0x10ffff)
                fail(start, "illegal Unicode character");
        } else if (!bytes && c == 'N') {
            if (peek() != '{')
                fail(start, "malformed named Unicode escape");
            ++pos;
            auto name = pos;
            while (pos < source.size() && peek() != '}' && peek() != '\n' && peek() != '\r')
                ++pos;
            if (pos == name || peek() != '}')
                fail(start, "malformed named Unicode escape");
            std::u32string decoded;
            for (auto at = name; at < pos;)
                decoded.push_back(decode(at));
            if (!unicode_name(decoded))
                fail(start, "unknown Unicode character name");
            ++pos;
        }
    }
    void string_literal(std::size_t prefix_len, bool formatted)
    {
        auto start = pos;
        bool bytes = false, raw = false;
        for (std::size_t i = 0; i < prefix_len; ++i) {
            bytes |= source[pos + i] == 'b' || source[pos + i] == 'B';
            raw |= source[pos + i] == 'r' || source[pos + i] == 'R';
        }
        pos += prefix_len;
        char quote = peek();
        bool triple = peek(1) == quote && peek(2) == quote;
        pos += triple ? 3 : 1;
        if (formatted) {
            emit(TokenKind::FSTRING_START, start, pos);
            fstring_text(quote, triple, false, raw);
            return;
        }
        while (pos < source.size() && !closing(quote, triple)) {
            if (!triple && (peek() == '\n' || peek() == '\r'))
                fail(start, "unterminated string literal");
            if (bytes && static_cast<unsigned char>(peek()) >= 128)
                fail(start, "bytes can only contain ASCII literal characters");
            if (peek() == '\\') {
                escape(raw, bytes, false, triple);
                continue;
            }
            ++pos;
        }
        if (pos == source.size())
            fail(start, "unterminated string literal",
                 triple && allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
        pos += triple ? 3 : 1;
        emit(TokenKind::STRING, start, pos);
    }
    void fstring_text(char quote, bool triple, bool format, bool raw)
    {
        auto start = pos;
        while (pos < source.size()) {
            if (!format && closing(quote, triple)) {
                if (pos > start)
                    emit(TokenKind::FSTRING_MIDDLE, start, pos);
                auto end_start = pos;
                pos += triple ? 3 : 1;
                emit(TokenKind::FSTRING_END, end_start, pos);
                return;
            }
            if (!format && !triple && (peek() == '\n' || peek() == '\r'))
                fail(start, "unterminated f-string literal");
            if (peek() == '{') {
                if (!format && peek(1) == '{') {
                    pos += 2;
                    continue;
                }
                if (pos > start)
                    emit(TokenKind::FSTRING_MIDDLE, start, pos);
                auto open = pos++;
                emit(TokenKind::LBRACE, open, pos);
                replacement(quote, triple, raw);
                start = pos;
                continue;
            }
            if (peek() == '}') {
                if (format) {
                    if (pos > start)
                        emit(TokenKind::FSTRING_MIDDLE, start, pos);
                    return;
                }
                if (peek(1) != '}')
                    fail(pos, "f-string: single '}' is not allowed");
                pos += 2;
                continue;
            }
            if (peek() == '\\') {
                escape(raw, false, true, triple);
                continue;
            }
            ++pos;
        }
        fail(start, "unterminated f-string literal",
             triple && allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
    }
    void replacement(char quote, bool triple, bool raw)
    {
        auto base = brackets.size();
        while (pos < source.size()) {
            if (brackets.size() == base && peek() == '!' && peek(1) != '=') {
                auto start = pos++;
                char conversion = peek();
                if ((conversion != 's' && conversion != 'r' && conversion != 'a') ||
                    name_char(peek(1)))
                    fail(start, "f-string: invalid conversion character");
                emit(TokenKind::EXCLAMATION, start, pos);
                start = pos++;
                emit(TokenKind::NAME, start, pos);
                continue;
            }
            if (brackets.size() == base && peek() == '}') {
                auto start = pos++;
                emit(TokenKind::RBRACE, start, pos);
                return;
            }
            if (brackets.size() == base && peek() == ':') {
                auto start = pos++;
                emit(TokenKind::COLON, start, pos);
                fstring_text(quote, triple, true, raw);
                if (peek() != '}')
                    fail(start, "unterminated f-string replacement field");
                start = pos++;
                emit(TokenKind::RBRACE, start, pos);
                return;
            }
            scan_one(true);
        }
        fail(pos, "unterminated f-string replacement field",
             triple && allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
    }
    void number()
    {
        auto start = pos;
        bool floating = false;
        auto digits = [&](int base, bool allow_initial_underscore) {
            bool any = false, underscore = false;
            if (allow_initial_underscore && peek() == '_') {
                underscore = true;
                ++pos;
            }
            while (true) {
                char c = peek();
                int value = digit(c)               ? c - '0'
                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                   : 99;
                if (value < base) {
                    any = true;
                    underscore = false;
                    ++pos;
                } else if (c == '_' && any && !underscore) {
                    underscore = true;
                    ++pos;
                } else {
                    break;
                }
            }
            if (!any || underscore)
                fail(start, "invalid numeric literal");
        };
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'o' ||
                              peek(1) == 'O' || peek(1) == 'b' || peek(1) == 'B')) {
            char marker = peek(1);
            pos += 2;
            digits(marker == 'x' || marker == 'X'   ? 16
                   : marker == 'o' || marker == 'O' ? 8
                                                    : 2,
                   true);
            if (static_cast<unsigned char>(peek()) < 128 && name_char(peek()) &&
                !number_suffix_allowed())
                fail(start, "invalid numeric literal");
        } else {
            if (peek() != '.')
                digits(10, false);
            if (peek() == '.') {
                floating = true;
                ++pos;
                if (digit(peek()))
                    digits(10, false);
                else if (peek() == '_')
                    fail(start, "invalid numeric literal");
            }
            if ((peek() == 'e' || peek() == 'E') && !number_suffix_allowed()) {
                floating = true;
                ++pos;
                if (peek() == '+' || peek() == '-')
                    ++pos;
                digits(10, false);
            }
            if (peek() == 'j' || peek() == 'J') {
                floating = true;
                ++pos;
            }
            if (!floating && source[start] == '0') {
                for (auto i = start; i < pos; ++i)
                    if (source[i] >= '1' && source[i] <= '9')
                        fail(start, "leading zeros in decimal integer literals are not permitted");
            }
            if (static_cast<unsigned char>(peek()) < 128 && name_char(peek()) &&
                !number_suffix_allowed())
                fail(start, "invalid numeric literal");
        }
        emit(TokenKind::NUMBER, start, pos);
    }
    void scan_one(bool expression)
    {
        if (peek() == ' ' || peek() == '\t' || peek() == '\f') {
            ++pos;
            return;
        }
        if (peek() == '#') {
            while (pos < source.size() && peek() != '\n' && peek() != '\r')
                ++pos;
            return;
        }
        if (peek() == '\n' || peek() == '\r') {
            auto start = pos;
            newline();
            if (expression || !brackets.empty())
                line_start = false;
            if (!expression && brackets.empty() && line_has_token) {
                emit(TokenKind::NEWLINE, start, pos);
                line_has_token = false;
            } else if (!expression && brackets.empty() && mode == InputMode::Interactive &&
                       interactive_blank) {
                while (indents.size() > 1) {
                    indents.pop_back();
                    emit(TokenKind::DEDENT, start, start);
                }
                emit(TokenKind::NEWLINE, start, pos);
            }
            interactive_blank = false;
            return;
        }
        if (peek() == '\\') {
            auto start = pos++;
            if (!peek() && allow_incomplete)
                fail(start, "unexpected EOF after line continuation character",
                     ErrorKind::Incomplete);
            if (peek() != '\n' && peek() != '\r')
                fail(start, "unexpected character after line continuation character");
            newline();
            if (pos == source.size())
                fail(start, "unexpected EOF after line continuation character",
                     allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
            line_start = false;
            return;
        }
        auto start = pos;
        std::size_t length = 0;
        bool formatted = false;
        if (prefix(length, formatted)) {
            string_literal(length, formatted);
        } else if (digit(peek()) || (peek() == '.' && digit(peek(1)))) {
            number();
        } else if (alpha(peek()) || static_cast<unsigned char>(peek()) >= 128) {
            bool first = true;
            while (pos < source.size() && name_char(peek())) {
                auto point_start = pos;
                char32_t point = decode(pos);
                if (point != U'_' &&
                    !(first ? in_ranges(xid_start, point) : in_ranges(xid_continue, point)))
                    fail(point_start, "invalid character in identifier");
                first = false;
            }
            auto name = source.substr(start, pos - start);
            emit(name == "async"   ? TokenKind::ASYNC
                 : name == "await" ? TokenKind::AWAIT
                                   : TokenKind::NAME,
                 start, pos);
        } else {
            struct Operator {
                const char *text;
                TokenKind kind;
            };
            static const Operator operators[] = {{"**=", TokenKind::DOUBLESTAREQUAL},
                                                 {"//=", TokenKind::DOUBLESLASHEQUAL},
                                                 {"<<=", TokenKind::LEFTSHIFTEQUAL},
                                                 {">>=", TokenKind::RIGHTSHIFTEQUAL},
                                                 {"...", TokenKind::ELLIPSIS},
                                                 {"==", TokenKind::EQEQUAL},
                                                 {"!=", TokenKind::NOTEQUAL},
                                                 {"<=", TokenKind::LESSEQUAL},
                                                 {">=", TokenKind::GREATEREQUAL},
                                                 {"<<", TokenKind::LEFTSHIFT},
                                                 {">>", TokenKind::RIGHTSHIFT},
                                                 {"**", TokenKind::DOUBLESTAR},
                                                 {"//", TokenKind::DOUBLESLASH},
                                                 {"+=", TokenKind::PLUSEQUAL},
                                                 {"-=", TokenKind::MINEQUAL},
                                                 {"*=", TokenKind::STAREQUAL},
                                                 {"/=", TokenKind::SLASHEQUAL},
                                                 {"%=", TokenKind::PERCENTEQUAL},
                                                 {"&=", TokenKind::AMPEREQUAL},
                                                 {"|=", TokenKind::VBAREQUAL},
                                                 {"^=", TokenKind::CIRCUMFLEXEQUAL},
                                                 {"@=", TokenKind::ATEQUAL},
                                                 {"->", TokenKind::RARROW},
                                                 {":=", TokenKind::COLONEQUAL},
                                                 {"(", TokenKind::LPAR},
                                                 {")", TokenKind::RPAR},
                                                 {"[", TokenKind::LSQB},
                                                 {"]", TokenKind::RSQB},
                                                 {"{", TokenKind::LBRACE},
                                                 {"}", TokenKind::RBRACE},
                                                 {":", TokenKind::COLON},
                                                 {",", TokenKind::COMMA},
                                                 {";", TokenKind::SEMI},
                                                 {"+", TokenKind::PLUS},
                                                 {"-", TokenKind::MINUS},
                                                 {"*", TokenKind::STAR},
                                                 {"/", TokenKind::SLASH},
                                                 {"|", TokenKind::VBAR},
                                                 {"&", TokenKind::AMPER},
                                                 {"<", TokenKind::LESS},
                                                 {">", TokenKind::GREATER},
                                                 {"=", TokenKind::EQUAL},
                                                 {".", TokenKind::DOT},
                                                 {"%", TokenKind::PERCENT},
                                                 {"~", TokenKind::TILDE},
                                                 {"^", TokenKind::CIRCUMFLEX},
                                                 {"@", TokenKind::AT},
                                                 {"!", TokenKind::EXCLAMATION}};
            bool found = false;
            for (const auto &op : operators) {
                std::string_view spelling(op.text);
                if (source.substr(pos, spelling.size()) != spelling)
                    continue;
                char c = peek();
                pos += spelling.size();
                if (spelling.size() == 1 && (c == '(' || c == '[' || c == '{'))
                    brackets.push_back(c);
                if (spelling.size() == 1 && (c == ')' || c == ']' || c == '}')) {
                    char expected = c == ')' ? '(' : c == ']' ? '[' : '{';
                    if (brackets.empty() || brackets.back() != expected)
                        fail(start, "unmatched closing delimiter");
                    brackets.pop_back();
                }
                emit(op.kind, start, pos);
                found = true;
                break;
            }
            if (!found) {
                ++pos;
                fail(start, "invalid character");
            }
        }
        line_has_token = true;
        line_start = false;
    }

  public:
    explicit Scanner(std::string_view input, InputMode input_mode, bool incomplete)
        : source(input), mode(input_mode), allow_incomplete(incomplete)
    {}
    std::vector<Token> run()
    {
        for (std::size_t i = 0; i < source.size();) {
            auto start = i;
            if (decode(i) == 0)
                throw Error({start, i}, "source code cannot contain null bytes");
        }
        while (pos < source.size()) {
            if (line_start) {
                if (brackets.empty()) {
                    auto start = pos;
                    indentation();
                    interactive_blank = pos == start && (peek() == '\n' || peek() == '\r');
                } else
                    line_start = false;
            }
            if (pos < source.size())
                scan_one(false);
        }
        if (!brackets.empty())
            fail(pos, "unclosed delimiter",
                 allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
        if (line_has_token)
            emit(TokenKind::NEWLINE, pos, pos);
        while (indents.size() > 1 && !(mode == InputMode::Interactive && allow_incomplete)) {
            indents.pop_back();
            emit(TokenKind::DEDENT, pos, pos);
        }
        if (mode == InputMode::Interactive && !allow_incomplete && !tokens.empty() &&
            tokens.back().kind == TokenKind::DEDENT)
            emit(TokenKind::NEWLINE, pos, pos);
        emit(TokenKind::ENDMARKER, pos, pos);
        return std::move(tokens);
    }
};
} // namespace

std::u32string normalize_identifier(std::u32string_view input)
{
    return normalize(input);
}

std::vector<Token> tokenize(std::string_view source, InputMode mode, bool allow_incomplete)
{
    return Scanner(source, mode, allow_incomplete).run();
}
} // namespace cxxpython::syntax
