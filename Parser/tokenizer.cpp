// Lexical state machine adapted from CPython 3.12.14 Parser/tokenizer.c.
// PSF license: doc/licenses/python-3.12.14.txt. No CPython object APIs or ABI.
#include "cxxpython/parser.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace cxxpython::syntax {
std::size_t identifier_length(std::u32string_view);
namespace {
constexpr int MAXINDENT = 100, MAXLEVEL = 200, MAXFSTRINGLEVEL = 150;
constexpr int MAX_EXPR_NESTING = 3, TABSIZE = 8, ALTTABSIZE = 1;
enum ErrorCode {
    E_OK,
    E_EOF,
    E_ERROR,
    E_DECODE,
    E_NOMEM,
    E_LINECONT,
    E_TABSPACE,
    E_TOODEEP,
    E_DEDENT,
    E_EOLS,
    E_EOFS,
    E_INTR,
    E_INTERACT_STOP,
    E_COLUMNOVERFLOW
};
enum tokenizer_mode_kind_t { TOK_REGULAR_MODE, TOK_FSTRING_MODE };
struct tokenizer_mode {
    tokenizer_mode_kind_t kind = TOK_REGULAR_MODE;
    int curly_bracket_depth = 0, curly_bracket_expr_start_depth = 0;
    char f_string_quote = 0;
    int f_string_quote_size = 0, f_string_raw = 0;
    const char *f_string_start = nullptr, *f_string_multi_line_start = nullptr;
    int f_string_line_start = 0;
    std::ptrdiff_t f_string_start_offset = -1, f_string_multi_line_start_offset = -1;
    int f_string_debug = 0, in_format_spec = 0;
};
struct token {
    int level = 0, lineno = 0, col_offset = -1, end_lineno = 0, end_col_offset = -1;
    const char *start = nullptr, *end = nullptr;
};
struct tok_state {
    char *buf = nullptr, *cur = nullptr, *inp = nullptr;
    const char *end = nullptr, *start = nullptr;
    int done = E_OK, tabsize = TABSIZE, indent = 0;
    int indstack[MAXINDENT]{}, altindstack[MAXINDENT]{};
    int atbol = 1, pendin = 0;
    const char *prompt = nullptr;
    int lineno = 0, first_lineno = 0, starting_col_offset = -1, col_offset = -1, level = 0;
    char parenstack[MAXLEVEL]{};
    int parenlinenostack[MAXLEVEL]{}, parencolstack[MAXLEVEL]{};
    int decoding_erred = 0, cont_line = 0;
    const char *line_start = nullptr, *multi_line_start = nullptr;
    int type_comments = 0;
    tokenizer_mode tok_mode_stack[MAXFSTRINGLEVEL]{};
    int tok_mode_stack_index = 0, tok_extra_tokens = 0, comment_newline = 0;
    int implicit_newline = 0;
    bool allow_incomplete = false;
    std::size_t buffer_offset = 0, bytes_read = 0, poll_counter = 0;
    const TokenStream::ReadLine *reader = nullptr;
    const TokenStream::Poll *poll = nullptr;
    const TokenStream::Warning *warning = nullptr;
    LexicalDiagnostic diagnostic;
};
bool ascii_digit(int c)
{
    return c >= '0' && c <= '9';
}
bool ascii_xdigit(int c)
{
    return ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool ascii_alnum(int c)
{
    return ascii_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
std::u32string decode_utf8(std::string_view text)
{
    std::u32string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        unsigned char c = text[i++];
        int extra = c < 0x80                 ? 0
                    : c >= 0xc2 && c <= 0xdf ? 1
                    : c >= 0xe0 && c <= 0xef ? 2
                    : c >= 0xf0 && c <= 0xf4 ? 3
                                             : -1;
        if (extra < 0 || i + extra > text.size())
            throw Error({start, i}, "invalid UTF-8", ErrorKind::Encoding);
        char32_t point = extra ? c & ((1 << (6 - extra)) - 1) : c;
        for (int n = 0; n < extra; ++n) {
            unsigned char tail = text[i++];
            if ((tail & 0xc0) != 0x80)
                throw Error({start, i}, "invalid UTF-8", ErrorKind::Encoding);
            point = (point << 6) | (tail & 63);
        }
        if ((extra == 2 && point < 0x800) || (extra == 3 && point < 0x10000) ||
            (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff)
            throw Error({start, i}, "invalid UTF-8", ErrorKind::Encoding);
        result.push_back(point);
    }
    return result;
}
int character_count(const char *begin, const char *end)
{
    return static_cast<int>(
        std::count_if(begin, end, [](unsigned char c) { return (c & 0xc0) != 0x80; }));
}
#define is_potential_identifier_start(c)                                                           \
    ((ascii_alnum(c) && !ascii_digit(c)) || (c) == '_' || (c) >= 128)
#define is_potential_identifier_char(c) (ascii_alnum(c) || (c) == '_' || (c) >= 128)
#define MAKE_TOKEN(type) token_setup(tok, token, type, p_start, p_end)
#define MAKE_TYPE_COMMENT_TOKEN(type, col, endcol)                                                 \
    type_comment_token_setup(tok, token, type, col, endcol, p_start, p_end)
#define ADVANCE_LINENO()                                                                           \
    tok->lineno++;                                                                                 \
    tok->col_offset = 0;
#define INSIDE_FSTRING(tok) ((tok)->tok_mode_stack_index > 0)
#define INSIDE_FSTRING_EXPR(tok) ((tok)->curly_bracket_expr_start_depth >= 0)
#define TOK_GET_MODE(tok) (&(tok)->tok_mode_stack[(tok)->tok_mode_stack_index])
#define TOK_NEXT_MODE(tok) (&(tok)->tok_mode_stack[++(tok)->tok_mode_stack_index])
#define ISSTRINGLIT(type) ((type) == STRING || (type) == FSTRING_MIDDLE)

enum TokenCode {
    ENDMARKER = static_cast<int>(TokenKind::ENDMARKER),
    NAME = static_cast<int>(TokenKind::NAME),
    NUMBER = static_cast<int>(TokenKind::NUMBER),
    STRING = static_cast<int>(TokenKind::STRING),
    NEWLINE = static_cast<int>(TokenKind::NEWLINE),
    INDENT = static_cast<int>(TokenKind::INDENT),
    DEDENT = static_cast<int>(TokenKind::DEDENT),
    LPAR = static_cast<int>(TokenKind::LPAR),
    RPAR = static_cast<int>(TokenKind::RPAR),
    LSQB = static_cast<int>(TokenKind::LSQB),
    RSQB = static_cast<int>(TokenKind::RSQB),
    COLON = static_cast<int>(TokenKind::COLON),
    COMMA = static_cast<int>(TokenKind::COMMA),
    SEMI = static_cast<int>(TokenKind::SEMI),
    PLUS = static_cast<int>(TokenKind::PLUS),
    MINUS = static_cast<int>(TokenKind::MINUS),
    STAR = static_cast<int>(TokenKind::STAR),
    SLASH = static_cast<int>(TokenKind::SLASH),
    VBAR = static_cast<int>(TokenKind::VBAR),
    AMPER = static_cast<int>(TokenKind::AMPER),
    LESS = static_cast<int>(TokenKind::LESS),
    GREATER = static_cast<int>(TokenKind::GREATER),
    EQUAL = static_cast<int>(TokenKind::EQUAL),
    DOT = static_cast<int>(TokenKind::DOT),
    PERCENT = static_cast<int>(TokenKind::PERCENT),
    LBRACE = static_cast<int>(TokenKind::LBRACE),
    RBRACE = static_cast<int>(TokenKind::RBRACE),
    EQEQUAL = static_cast<int>(TokenKind::EQEQUAL),
    NOTEQUAL = static_cast<int>(TokenKind::NOTEQUAL),
    LESSEQUAL = static_cast<int>(TokenKind::LESSEQUAL),
    GREATEREQUAL = static_cast<int>(TokenKind::GREATEREQUAL),
    TILDE = static_cast<int>(TokenKind::TILDE),
    CIRCUMFLEX = static_cast<int>(TokenKind::CIRCUMFLEX),
    LEFTSHIFT = static_cast<int>(TokenKind::LEFTSHIFT),
    RIGHTSHIFT = static_cast<int>(TokenKind::RIGHTSHIFT),
    DOUBLESTAR = static_cast<int>(TokenKind::DOUBLESTAR),
    PLUSEQUAL = static_cast<int>(TokenKind::PLUSEQUAL),
    MINEQUAL = static_cast<int>(TokenKind::MINEQUAL),
    STAREQUAL = static_cast<int>(TokenKind::STAREQUAL),
    SLASHEQUAL = static_cast<int>(TokenKind::SLASHEQUAL),
    PERCENTEQUAL = static_cast<int>(TokenKind::PERCENTEQUAL),
    AMPEREQUAL = static_cast<int>(TokenKind::AMPEREQUAL),
    VBAREQUAL = static_cast<int>(TokenKind::VBAREQUAL),
    CIRCUMFLEXEQUAL = static_cast<int>(TokenKind::CIRCUMFLEXEQUAL),
    LEFTSHIFTEQUAL = static_cast<int>(TokenKind::LEFTSHIFTEQUAL),
    RIGHTSHIFTEQUAL = static_cast<int>(TokenKind::RIGHTSHIFTEQUAL),
    DOUBLESTAREQUAL = static_cast<int>(TokenKind::DOUBLESTAREQUAL),
    DOUBLESLASH = static_cast<int>(TokenKind::DOUBLESLASH),
    DOUBLESLASHEQUAL = static_cast<int>(TokenKind::DOUBLESLASHEQUAL),
    AT = static_cast<int>(TokenKind::AT),
    ATEQUAL = static_cast<int>(TokenKind::ATEQUAL),
    RARROW = static_cast<int>(TokenKind::RARROW),
    ELLIPSIS = static_cast<int>(TokenKind::ELLIPSIS),
    COLONEQUAL = static_cast<int>(TokenKind::COLONEQUAL),
    EXCLAMATION = static_cast<int>(TokenKind::EXCLAMATION),
    OP = static_cast<int>(TokenKind::OP),
    AWAIT = static_cast<int>(TokenKind::AWAIT),
    ASYNC = static_cast<int>(TokenKind::ASYNC),
    TYPE_IGNORE = static_cast<int>(TokenKind::TYPE_IGNORE),
    TYPE_COMMENT = static_cast<int>(TokenKind::TYPE_COMMENT),
    SOFT_KEYWORD = static_cast<int>(TokenKind::SOFT_KEYWORD),
    FSTRING_START = static_cast<int>(TokenKind::FSTRING_START),
    FSTRING_MIDDLE = static_cast<int>(TokenKind::FSTRING_MIDDLE),
    FSTRING_END = static_cast<int>(TokenKind::FSTRING_END),
    COMMENT = static_cast<int>(TokenKind::COMMENT),
    NL = static_cast<int>(TokenKind::NL),
    ERRORTOKEN = static_cast<int>(TokenKind::ERRORTOKEN),
    ENCODING = static_cast<int>(TokenKind::ENCODING)
};
int token_OneChar(int c1)
{
    switch (c1) {
    case '!':
        return EXCLAMATION;
    case '%':
        return PERCENT;
    case '&':
        return AMPER;
    case '(':
        return LPAR;
    case ')':
        return RPAR;
    case '*':
        return STAR;
    case '+':
        return PLUS;
    case ',':
        return COMMA;
    case '-':
        return MINUS;
    case '.':
        return DOT;
    case '/':
        return SLASH;
    case ':':
        return COLON;
    case ';':
        return SEMI;
    case '<':
        return LESS;
    case '=':
        return EQUAL;
    case '>':
        return GREATER;
    case '@':
        return AT;
    case '[':
        return LSQB;
    case ']':
        return RSQB;
    case '^':
        return CIRCUMFLEX;
    case '{':
        return LBRACE;
    case '|':
        return VBAR;
    case '}':
        return RBRACE;
    case '~':
        return TILDE;
    }
    return OP;
}

int token_TwoChars(int c1, int c2)
{
    switch (c1) {
    case '!':
        switch (c2) {
        case '=':
            return NOTEQUAL;
        }
        break;
    case '%':
        switch (c2) {
        case '=':
            return PERCENTEQUAL;
        }
        break;
    case '&':
        switch (c2) {
        case '=':
            return AMPEREQUAL;
        }
        break;
    case '*':
        switch (c2) {
        case '*':
            return DOUBLESTAR;
        case '=':
            return STAREQUAL;
        }
        break;
    case '+':
        switch (c2) {
        case '=':
            return PLUSEQUAL;
        }
        break;
    case '-':
        switch (c2) {
        case '=':
            return MINEQUAL;
        case '>':
            return RARROW;
        }
        break;
    case '/':
        switch (c2) {
        case '/':
            return DOUBLESLASH;
        case '=':
            return SLASHEQUAL;
        }
        break;
    case ':':
        switch (c2) {
        case '=':
            return COLONEQUAL;
        }
        break;
    case '<':
        switch (c2) {
        case '<':
            return LEFTSHIFT;
        case '=':
            return LESSEQUAL;
        case '>':
            return NOTEQUAL;
        }
        break;
    case '=':
        switch (c2) {
        case '=':
            return EQEQUAL;
        }
        break;
    case '>':
        switch (c2) {
        case '=':
            return GREATEREQUAL;
        case '>':
            return RIGHTSHIFT;
        }
        break;
    case '@':
        switch (c2) {
        case '=':
            return ATEQUAL;
        }
        break;
    case '^':
        switch (c2) {
        case '=':
            return CIRCUMFLEXEQUAL;
        }
        break;
    case '|':
        switch (c2) {
        case '=':
            return VBAREQUAL;
        }
        break;
    }
    return OP;
}

int token_ThreeChars(int c1, int c2, int c3)
{
    switch (c1) {
    case '*':
        switch (c2) {
        case '*':
            switch (c3) {
            case '=':
                return DOUBLESTAREQUAL;
            }
            break;
        }
        break;
    case '.':
        switch (c2) {
        case '.':
            switch (c3) {
            case '.':
                return ELLIPSIS;
            }
            break;
        }
        break;
    case '/':
        switch (c2) {
        case '/':
            switch (c3) {
            case '=':
                return DOUBLESLASHEQUAL;
            }
            break;
        }
        break;
    case '<':
        switch (c2) {
        case '<':
            switch (c3) {
            case '=':
                return LEFTSHIFTEQUAL;
            }
            break;
        }
        break;
    case '>':
        switch (c2) {
        case '>':
            switch (c3) {
            case '=':
                return RIGHTSHIFTEQUAL;
            }
            break;
        }
        break;
    }
    return OP;
}
static const char *type_comment_prefix = "# type: ";
std::ptrdiff_t buffer_index(const tok_state &tok, const char *pointer)
{
    return pointer ? pointer - tok.buf : -1;
}
static int tok_reserve_buf(tok_state *tok, std::size_t size)
{
    const auto used = static_cast<std::size_t>(tok->inp - tok->buf);
    const auto capacity = static_cast<std::size_t>(tok->end - tok->buf);
    if (size > static_cast<std::size_t>(PTRDIFF_MAX) - used)
        throw std::bad_alloc();
    const auto needed = used + size;
    if (needed <= capacity)
        return 1;
    const auto grown = used + std::max(size, used / 2);
    const auto cur = tok->cur - tok->buf;
    const auto start = buffer_index(*tok, tok->start);
    const auto line = buffer_index(*tok, tok->line_start);
    const auto multi = buffer_index(*tok, tok->multi_line_start);
    for (int i = 0; i <= tok->tok_mode_stack_index; ++i) {
        auto &mode = tok->tok_mode_stack[i];
        mode.f_string_start_offset = buffer_index(*tok, mode.f_string_start);
        mode.f_string_multi_line_start_offset = buffer_index(*tok, mode.f_string_multi_line_start);
    }
    auto *buffer = static_cast<char *>(std::realloc(tok->buf, grown));
    if (!buffer)
        throw std::bad_alloc();
    tok->buf = buffer;
    tok->cur = buffer + cur;
    tok->inp = buffer + used;
    tok->end = buffer + grown;
    tok->start = start < 0 ? nullptr : buffer + start;
    tok->line_start = line < 0 ? nullptr : buffer + line;
    tok->multi_line_start = multi < 0 ? nullptr : buffer + multi;
    for (int i = 0; i <= tok->tok_mode_stack_index; ++i) {
        auto &mode = tok->tok_mode_stack[i];
        mode.f_string_start =
            mode.f_string_start_offset < 0 ? nullptr : buffer + mode.f_string_start_offset;
        mode.f_string_multi_line_start = mode.f_string_multi_line_start_offset < 0
                                             ? nullptr
                                             : buffer + mode.f_string_multi_line_start_offset;
    }
    return 1;
}
static int tok_underflow(tok_state *tok)
{
    if (!tok->start && !INSIDE_FSTRING(tok)) {
        tok->cur = tok->inp = tok->buf;
        tok->buffer_offset = tok->bytes_read;
    }
    const auto line = (*tok->reader)();
    if (line.empty()) {
        tok->done = E_EOF;
        return 0;
    }
    (void)decode_utf8(line);
    tok_reserve_buf(tok, line.size() + 2);
    std::memcpy(tok->inp, line.data(), line.size());
    tok->inp += line.size();
    *tok->inp = 0;
    tok->implicit_newline = tok->inp[-1] != '\n';
    if (tok->implicit_newline)
        *tok->inp++ = '\n';
    *tok->inp = 0;
    tok->bytes_read += line.size() + tok->implicit_newline;
    ADVANCE_LINENO();
    return 1;
}
template <class... Args>
int syntaxerror_range(tok_state *tok, int column, int end_column, const char *format, Args... args)
{
    std::string message;
    if constexpr (sizeof...(Args) == 0)
        message = format;
    else {
        const int size = std::snprintf(nullptr, 0, format, args...);
        message.resize(static_cast<std::size_t>(std::max(size, 0)));
        std::snprintf(message.data(), message.size() + 1, format, args...);
    }
    auto &error = tok->diagnostic;
    error.kind = ErrorKind::Syntax;
    error.message = std::move(message);
    error.lineno = error.end_lineno = tok->lineno;
    const auto start = tok->line_start ? tok->line_start : tok->buf;
    const auto length = std::strcspn(start, "\n");
    error.line = decode_utf8(std::string_view(start, length));
    error.offset =
        column < 0 ? character_count(start, std::max(start, static_cast<const char *>(tok->cur)))
                   : column;
    error.end_offset = end_column < 0 ? error.offset : end_column;
    const auto at = tok->buffer_offset + static_cast<std::size_t>(tok->cur - tok->buf);
    error.span = {at ? at - 1 : 0, at};
    tok->done = E_ERROR;
    return ERRORTOKEN;
}
template <class... Args> int syntaxerror(tok_state *tok, const char *format, Args... args)
{
    return syntaxerror_range(tok, -1, -1, format, args...);
}
template <class... Args>
int syntaxerror_known_range(tok_state *tok, int column, int end_column, const char *format,
                            Args... args)
{
    return syntaxerror_range(tok, column, end_column, format, args...);
}
static int indenterror(tok_state *tok)
{
    tok->done = E_TABSPACE;
    tok->cur = tok->inp;
    return ERRORTOKEN;
}
template <class... Args> int parser_warn(tok_state *tok, const char *format, Args... args)
{
    if (!tok->warning || !*tok->warning)
        return 0;
    const auto done = tok->done;
    syntaxerror_range(tok, -1, -1, format, args...);
    tok->done = done;
    (*tok->warning)(tok->diagnostic);
    return 0;
}
static int warn_invalid_escape_sequence(tok_state *tok, int ch)
{
    return parser_warn(tok, "invalid escape sequence '\\%c'", ch);
}
static int tok_nextc(tok_state *tok)
{
    for (;;) {
        if (tok->cur != tok->inp) {
            if (!(++tok->poll_counter & 4095) && tok->poll && *tok->poll)
                (*tok->poll)();
            if (static_cast<unsigned>(tok->col_offset) >= static_cast<unsigned>(INT_MAX)) {
                tok->done = E_COLUMNOVERFLOW;
                return EOF;
            }
            ++tok->col_offset;
            return static_cast<unsigned char>(*tok->cur++);
        }
        if (tok->done != E_OK)
            return EOF;
        if (!tok_underflow(tok)) {
            tok->cur = tok->inp;
            return EOF;
        }
        tok->line_start = tok->cur;
        if (std::memchr(tok->line_start, 0, tok->inp - tok->line_start)) {
            syntaxerror(tok, "source code cannot contain null bytes");
            tok->cur = tok->inp;
            return EOF;
        }
    }
}
static void tok_backup(tok_state *tok, int c)
{
    if (c != EOF) {
        assert(tok->cur > tok->buf);
        --tok->cur;
        assert(static_cast<unsigned char>(*tok->cur) == static_cast<unsigned char>(c));
        --tok->col_offset;
    }
}

static int lookahead(struct tok_state *tok, const char *test)
{
    const char *s = test;
    int res = 0;
    while (1) {
        int c = tok_nextc(tok);
        if (*s == 0) {
            res = !is_potential_identifier_char(c);
        } else if (c == *s) {
            s++;
            continue;
        }

        tok_backup(tok, c);
        while (s != test) {
            tok_backup(tok, *--s);
        }
        return res;
    }
}

static int verify_end_of_number(struct tok_state *tok, int c, const char *kind)
{
    if (tok->tok_extra_tokens) {
        // When we are parsing extra tokens, we don't want to emit warnings
        // about invalid literals, because we want to be a bit more liberal.
        return 1;
    }
    /* Emit a deprecation warning only if the numeric literal is immediately
     * followed by one of keywords which can occur after a numeric literal
     * in valid code: "and", "else", "for", "if", "in", "is" and "or".
     * It allows to gradually deprecate existing valid code without adding
     * warning before error in most cases of invalid numeric literal (which
     * would be confusing and break existing tests).
     * Raise a syntax error with slightly better message than plain
     * "invalid syntax" if the numeric literal is immediately followed by
     * other keyword or identifier.
     */
    int r = 0;
    if (c == 'a') {
        r = lookahead(tok, "nd");
    } else if (c == 'e') {
        r = lookahead(tok, "lse");
    } else if (c == 'f') {
        r = lookahead(tok, "or");
    } else if (c == 'i') {
        int c2 = tok_nextc(tok);
        if (c2 == 'f' || c2 == 'n' || c2 == 's') {
            r = 1;
        }
        tok_backup(tok, c2);
    } else if (c == 'o') {
        r = lookahead(tok, "r");
    } else if (c == 'n') {
        r = lookahead(tok, "ot");
    }
    if (r) {
        tok_backup(tok, c);
        if (parser_warn(tok, "invalid %s literal", kind)) {
            return 0;
        }
        tok_nextc(tok);
    } else /* In future releases, only error will remain. */
        if (c < 128 && is_potential_identifier_char(c)) {
            tok_backup(tok, c);
            syntaxerror(tok, "invalid %s literal", kind);
            return 0;
        }
    return 1;
}

/* Verify that the identifier follows PEP 3131.
   All identifier strings are guaranteed to be "ready" unicode objects.
 */
static int verify_identifier(tok_state *tok)
{
    if (tok->tok_extra_tokens)
        return 1;
    const auto text = decode_utf8(std::string_view(tok->start, tok->cur - tok->start));
    if (identifier_length(text) != text.size()) {
        syntaxerror(tok, "invalid character in identifier");
        return 0;
    }
    return 1;
}
static int tok_decimal_tail(struct tok_state *tok)
{
    int c;

    while (1) {
        do {
            c = tok_nextc(tok);
        } while (ascii_digit(c));
        if (c != '_') {
            break;
        }
        c = tok_nextc(tok);
        if (!ascii_digit(c)) {
            tok_backup(tok, c);
            syntaxerror(tok, "invalid decimal literal");
            return 0;
        }
    }
    return c;
}

static inline int tok_continuation_line(struct tok_state *tok)
{
    int c = tok_nextc(tok);
    if (c == '\r') {
        c = tok_nextc(tok);
    }
    if (c != '\n') {
        tok->done = E_LINECONT;
        return -1;
    }
    c = tok_nextc(tok);
    if (c == EOF) {
        tok->done = E_EOF;
        tok->cur = tok->inp;
        return -1;
    } else {
        tok_backup(tok, c);
    }
    return c;
}

static int type_comment_token_setup(struct tok_state *tok, struct token *token, int type,
                                    int col_offset, int end_col_offset, const char *start,
                                    const char *end)
{
    token->level = tok->level;
    token->lineno = token->end_lineno = tok->lineno;
    token->col_offset = col_offset;
    token->end_col_offset = end_col_offset;
    token->start = start;
    token->end = end;
    return type;
}

static int token_setup(struct tok_state *tok, struct token *token, int type, const char *start,
                       const char *end)
{
    assert((start == NULL && end == NULL) || (start != NULL && end != NULL));
    token->level = tok->level;
    if (ISSTRINGLIT(type)) {
        token->lineno = tok->first_lineno;
    } else {
        token->lineno = tok->lineno;
    }
    token->end_lineno = tok->lineno;
    token->col_offset = token->end_col_offset = -1;
    token->start = start;
    token->end = end;

    if (start != NULL && end != NULL) {
        token->col_offset = tok->starting_col_offset;
        token->end_col_offset = tok->col_offset;
    }
    return type;
}

static int tok_get_normal_mode(struct tok_state *tok, tokenizer_mode *current_tok,
                               struct token *token)
{
    int c;
    int blankline, nonascii;

    const char *p_start = NULL;
    const char *p_end = NULL;
nextline:
    tok->start = NULL;
    tok->starting_col_offset = -1;
    blankline = 0;

    /* Get indentation level */
    if (tok->atbol) {
        int col = 0;
        int altcol = 0;
        tok->atbol = 0;
        int cont_line_col = 0;
        for (;;) {
            c = tok_nextc(tok);
            if (c == ' ') {
                col++, altcol++;
            } else if (c == '\t') {
                col = (col / tok->tabsize + 1) * tok->tabsize;
                altcol = (altcol / ALTTABSIZE + 1) * ALTTABSIZE;
            } else if (c == '\014') { /* Control-L (formfeed) */
                col = altcol = 0;     /* For Emacs users */
            } else if (c == '\\') {
                // Indentation cannot be split over multiple physical lines
                // using backslashes. This means that if we found a backslash
                // preceded by whitespace, **the first one we find** determines
                // the level of indentation of whatever comes next.
                cont_line_col = cont_line_col ? cont_line_col : col;
                if ((c = tok_continuation_line(tok)) == -1) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            } else {
                break;
            }
        }
        tok_backup(tok, c);
        if (c == '#' || c == '\n' || c == '\r') {
            /* Lines with only whitespace and/or comments
               shouldn't affect the indentation and are
               not passed to the parser as NEWLINE tokens,
               except *totally* empty lines in interactive
               mode, which signal the end of a command group. */
            if (col == 0 && c == '\n' && tok->prompt != NULL) {
                blankline = 0; /* Let it through */
            } else if (tok->prompt != NULL && tok->lineno == 1) {
                /* In interactive mode, if the first line contains
                   only spaces and/or a comment, let it through. */
                blankline = 0;
                col = altcol = 0;
            } else {
                blankline = 1; /* Ignore completely */
            }
            /* We can't jump back right here since we still
               may need to skip to the end of a comment */
        }
        if (tok->done == E_EOF && tok->prompt && tok->allow_incomplete) {
            col = tok->indstack[tok->indent];
            altcol = tok->altindstack[tok->indent];
        }
        if (!blankline && tok->level == 0) {
            col = cont_line_col ? cont_line_col : col;
            altcol = cont_line_col ? cont_line_col : altcol;
            if (col == tok->indstack[tok->indent]) {
                /* No change */
                if (altcol != tok->altindstack[tok->indent]) {
                    return MAKE_TOKEN(indenterror(tok));
                }
            } else if (col > tok->indstack[tok->indent]) {
                /* Indent -- always one */
                if (tok->indent + 1 >= MAXINDENT) {
                    tok->done = E_TOODEEP;
                    tok->cur = tok->inp;
                    return MAKE_TOKEN(ERRORTOKEN);
                }
                if (altcol <= tok->altindstack[tok->indent]) {
                    return MAKE_TOKEN(indenterror(tok));
                }
                tok->pendin++;
                tok->indstack[++tok->indent] = col;
                tok->altindstack[tok->indent] = altcol;
            } else /* col < tok->indstack[tok->indent] */ {
                /* Dedent -- any number, must be consistent */
                while (tok->indent > 0 && col < tok->indstack[tok->indent]) {
                    tok->pendin--;
                    tok->indent--;
                }
                if (col != tok->indstack[tok->indent]) {
                    tok->done = E_DEDENT;
                    tok->cur = tok->inp;
                    return MAKE_TOKEN(ERRORTOKEN);
                }
                if (altcol != tok->altindstack[tok->indent]) {
                    return MAKE_TOKEN(indenterror(tok));
                }
            }
        }
    }

    tok->start = tok->cur;
    tok->starting_col_offset = tok->col_offset;

    /* Return pending indents/dedents */
    if (tok->pendin != 0) {
        if (tok->pendin < 0) {
            if (tok->tok_extra_tokens) {
                p_start = tok->cur;
                p_end = tok->cur;
            }
            tok->pendin++;
            return MAKE_TOKEN(DEDENT);
        } else {
            if (tok->tok_extra_tokens) {
                p_start = tok->buf;
                p_end = tok->cur;
            }
            tok->pendin--;
            return MAKE_TOKEN(INDENT);
        }
    }

    /* Peek ahead at the next character */
    c = tok_nextc(tok);
    tok_backup(tok, c);

again:
    tok->start = NULL;
    /* Skip spaces */
    do {
        c = tok_nextc(tok);
    } while (c == ' ' || c == '\t' || c == '\014');

    /* Set start of current token */
    tok->start = tok->cur == NULL ? NULL : tok->cur - 1;
    tok->starting_col_offset = tok->col_offset - 1;

    /* Skip comment, unless it's a type comment */
    if (c == '#') {

        const char *p = NULL;
        const char *prefix, *type_start;
        int current_starting_col_offset;

        while (c != EOF && c != '\n' && c != '\r') {
            c = tok_nextc(tok);
        }

        if (tok->tok_extra_tokens) {
            p = tok->start;
        }

        if (tok->type_comments) {
            p = tok->start;
            current_starting_col_offset = tok->starting_col_offset;
            prefix = type_comment_prefix;
            while (*prefix && p < tok->cur) {
                if (*prefix == ' ') {
                    while (*p == ' ' || *p == '\t') {
                        p++;
                        current_starting_col_offset++;
                    }
                } else if (*prefix == *p) {
                    p++;
                    current_starting_col_offset++;
                } else {
                    break;
                }

                prefix++;
            }

            /* This is a type comment if we matched all of type_comment_prefix. */
            if (!*prefix) {
                int is_type_ignore = 1;
                // +6 in order to skip the word 'ignore'
                const char *ignore_end = p + 6;
                const int ignore_end_col_offset = current_starting_col_offset + 6;
                tok_backup(tok, c); /* don't eat the newline or EOF */

                type_start = p;

                /* A TYPE_IGNORE is "type: ignore" followed by the end of the token
                 * or anything ASCII and non-alphanumeric. */
                is_type_ignore = (tok->cur >= ignore_end && memcmp(p, "ignore", 6) == 0 &&
                                  !(tok->cur > ignore_end && ((unsigned char)ignore_end[0] >= 128 ||
                                                              ascii_alnum(ignore_end[0]))));

                if (is_type_ignore) {
                    p_start = ignore_end;
                    p_end = tok->cur;

                    /* If this type ignore is the only thing on the line, consume the
                     * newline also. */
                    if (blankline) {
                        tok_nextc(tok);
                        tok->atbol = 1;
                    }
                    return MAKE_TYPE_COMMENT_TOKEN(TYPE_IGNORE, ignore_end_col_offset,
                                                   tok->col_offset);
                } else {
                    p_start = type_start;
                    p_end = tok->cur;
                    return MAKE_TYPE_COMMENT_TOKEN(TYPE_COMMENT, current_starting_col_offset,
                                                   tok->col_offset);
                }
            }
        }
        if (tok->tok_extra_tokens) {
            tok_backup(tok, c); /* don't eat the newline or EOF */
            p_start = p;
            p_end = tok->cur;
            tok->comment_newline = blankline;
            return MAKE_TOKEN(COMMENT);
        }
    }

    if (tok->done == E_INTERACT_STOP) {
        return MAKE_TOKEN(ENDMARKER);
    }

    /* Check for EOF and errors now */
    if (c == EOF) {
        if (tok->level) {
            return MAKE_TOKEN(ERRORTOKEN);
        }
        return MAKE_TOKEN(tok->done == E_EOF ? ENDMARKER : ERRORTOKEN);
    }

    /* Identifier (most frequent token!) */
    nonascii = 0;
    if (is_potential_identifier_start(c)) {
        /* Process the various legal combinations of b"", r"", u"", and f"". */
        int saw_b = 0, saw_r = 0, saw_u = 0, saw_f = 0;
        while (1) {
            if (!(saw_b || saw_u || saw_f) && (c == 'b' || c == 'B'))
                saw_b = 1;
            /* Since this is a backwards compatibility support literal we don't
               want to support it in arbitrary order like byte literals. */
            else if (!(saw_b || saw_u || saw_r || saw_f) && (c == 'u' || c == 'U')) {
                saw_u = 1;
            }
            /* ur"" and ru"" are not supported */
            else if (!(saw_r || saw_u) && (c == 'r' || c == 'R')) {
                saw_r = 1;
            } else if (!(saw_f || saw_b || saw_u) && (c == 'f' || c == 'F')) {
                saw_f = 1;
            } else {
                break;
            }
            c = tok_nextc(tok);
            if (c == '"' || c == '\'') {
                if (saw_f) {
                    goto f_string_quote;
                }
                goto letter_quote;
            }
        }
        while (is_potential_identifier_char(c)) {
            if (c >= 128) {
                nonascii = 1;
            }
            c = tok_nextc(tok);
        }
        tok_backup(tok, c);
        if (nonascii && !verify_identifier(tok)) {
            return MAKE_TOKEN(ERRORTOKEN);
        }

        p_start = tok->start;
        p_end = tok->cur;

        if (tok->cur - tok->start == 5) {
            if (memcmp(tok->start, "async", 5) == 0)
                return MAKE_TOKEN(ASYNC);
            if (memcmp(tok->start, "await", 5) == 0)
                return MAKE_TOKEN(AWAIT);
        }
        return MAKE_TOKEN(NAME);
    }

    if (c == '\r') {
        c = tok_nextc(tok);
    }

    /* Newline */
    if (c == '\n') {
        tok->atbol = 1;
        if (blankline || tok->level > 0) {
            if (tok->tok_extra_tokens) {
                if (tok->comment_newline) {
                    tok->comment_newline = 0;
                }
                p_start = tok->start;
                p_end = tok->cur;
                return MAKE_TOKEN(NL);
            }
            goto nextline;
        }
        if (tok->comment_newline && tok->tok_extra_tokens) {
            tok->comment_newline = 0;
            p_start = tok->start;
            p_end = tok->cur;
            return MAKE_TOKEN(NL);
        }
        p_start = tok->start;
        p_end = tok->cur - 1; /* Leave '\n' out of the string */
        tok->cont_line = 0;

        return MAKE_TOKEN(NEWLINE);
    }

    /* Period or number starting with period? */
    if (c == '.') {
        c = tok_nextc(tok);
        if (ascii_digit(c)) {
            goto fraction;
        } else if (c == '.') {
            c = tok_nextc(tok);
            if (c == '.') {
                p_start = tok->start;
                p_end = tok->cur;
                return MAKE_TOKEN(ELLIPSIS);
            } else {
                tok_backup(tok, c);
            }
            tok_backup(tok, '.');
        } else {
            tok_backup(tok, c);
        }
        p_start = tok->start;
        p_end = tok->cur;
        return MAKE_TOKEN(DOT);
    }

    /* Number */
    if (ascii_digit(c)) {
        if (c == '0') {
            /* Hex, octal or binary -- maybe. */
            c = tok_nextc(tok);
            if (c == 'x' || c == 'X') {
                /* Hex */
                c = tok_nextc(tok);
                do {
                    if (c == '_') {
                        c = tok_nextc(tok);
                    }
                    if (!ascii_xdigit(c)) {
                        tok_backup(tok, c);
                        return MAKE_TOKEN(syntaxerror(tok, "invalid hexadecimal literal"));
                    }
                    do {
                        c = tok_nextc(tok);
                    } while (ascii_xdigit(c));
                } while (c == '_');
                if (!verify_end_of_number(tok, c, "hexadecimal")) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            } else if (c == 'o' || c == 'O') {
                /* Octal */
                c = tok_nextc(tok);
                do {
                    if (c == '_') {
                        c = tok_nextc(tok);
                    }
                    if (c < '0' || c >= '8') {
                        if (ascii_digit(c)) {
                            return MAKE_TOKEN(
                                syntaxerror(tok, "invalid digit '%c' in octal literal", c));
                        } else {
                            tok_backup(tok, c);
                            return MAKE_TOKEN(syntaxerror(tok, "invalid octal literal"));
                        }
                    }
                    do {
                        c = tok_nextc(tok);
                    } while ('0' <= c && c < '8');
                } while (c == '_');
                if (ascii_digit(c)) {
                    return MAKE_TOKEN(syntaxerror(tok, "invalid digit '%c' in octal literal", c));
                }
                if (!verify_end_of_number(tok, c, "octal")) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            } else if (c == 'b' || c == 'B') {
                /* Binary */
                c = tok_nextc(tok);
                do {
                    if (c == '_') {
                        c = tok_nextc(tok);
                    }
                    if (c != '0' && c != '1') {
                        if (ascii_digit(c)) {
                            return MAKE_TOKEN(
                                syntaxerror(tok, "invalid digit '%c' in binary literal", c));
                        } else {
                            tok_backup(tok, c);
                            return MAKE_TOKEN(syntaxerror(tok, "invalid binary literal"));
                        }
                    }
                    do {
                        c = tok_nextc(tok);
                    } while (c == '0' || c == '1');
                } while (c == '_');
                if (ascii_digit(c)) {
                    return MAKE_TOKEN(syntaxerror(tok, "invalid digit '%c' in binary literal", c));
                }
                if (!verify_end_of_number(tok, c, "binary")) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            } else {
                int nonzero = 0;
                /* maybe old-style octal; c is first char of it */
                /* in any case, allow '0' as a literal */
                while (1) {
                    if (c == '_') {
                        c = tok_nextc(tok);
                        if (!ascii_digit(c)) {
                            tok_backup(tok, c);
                            return MAKE_TOKEN(syntaxerror(tok, "invalid decimal literal"));
                        }
                    }
                    if (c != '0') {
                        break;
                    }
                    c = tok_nextc(tok);
                }
                char *zeros_end = tok->cur;
                if (ascii_digit(c)) {
                    nonzero = 1;
                    c = tok_decimal_tail(tok);
                    if (c == 0) {
                        return MAKE_TOKEN(ERRORTOKEN);
                    }
                }
                if (c == '.') {
                    c = tok_nextc(tok);
                    goto fraction;
                } else if (c == 'e' || c == 'E') {
                    goto exponent;
                } else if (c == 'j' || c == 'J') {
                    goto imaginary;
                } else if (nonzero && !tok->tok_extra_tokens) {
                    /* Old-style octal: now disallowed. */
                    tok_backup(tok, c);
                    return MAKE_TOKEN(
                        syntaxerror_known_range(tok, (int)(tok->start + 1 - tok->line_start),
                                                (int)(zeros_end - tok->line_start),
                                                "leading zeros in decimal integer "
                                                "literals are not permitted; "
                                                "use an 0o prefix for octal integers"));
                }
                if (!verify_end_of_number(tok, c, "decimal")) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            }
        } else {
            /* Decimal */
            c = tok_decimal_tail(tok);
            if (c == 0) {
                return MAKE_TOKEN(ERRORTOKEN);
            }
            {
                /* Accept floating point numbers. */
                if (c == '.') {
                    c = tok_nextc(tok);
                fraction:
                    /* Fraction */
                    if (ascii_digit(c)) {
                        c = tok_decimal_tail(tok);
                        if (c == 0) {
                            return MAKE_TOKEN(ERRORTOKEN);
                        }
                    }
                }
                if (c == 'e' || c == 'E') {
                    int e;
                exponent:
                    e = c;
                    /* Exponent part */
                    c = tok_nextc(tok);
                    if (c == '+' || c == '-') {
                        c = tok_nextc(tok);
                        if (!ascii_digit(c)) {
                            tok_backup(tok, c);
                            return MAKE_TOKEN(syntaxerror(tok, "invalid decimal literal"));
                        }
                    } else if (!ascii_digit(c)) {
                        tok_backup(tok, c);
                        if (!verify_end_of_number(tok, e, "decimal")) {
                            return MAKE_TOKEN(ERRORTOKEN);
                        }
                        tok_backup(tok, e);
                        p_start = tok->start;
                        p_end = tok->cur;
                        return MAKE_TOKEN(NUMBER);
                    }
                    c = tok_decimal_tail(tok);
                    if (c == 0) {
                        return MAKE_TOKEN(ERRORTOKEN);
                    }
                }
                if (c == 'j' || c == 'J') {
                    /* Imaginary part */
                imaginary:
                    c = tok_nextc(tok);
                    if (!verify_end_of_number(tok, c, "imaginary")) {
                        return MAKE_TOKEN(ERRORTOKEN);
                    }
                } else if (!verify_end_of_number(tok, c, "decimal")) {
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            }
        }
        tok_backup(tok, c);
        p_start = tok->start;
        p_end = tok->cur;
        return MAKE_TOKEN(NUMBER);
    }

f_string_quote:
    if (((tolower(*tok->start) == 'f' || tolower(*tok->start) == 'r') && (c == '\'' || c == '"'))) {
        int quote = c;
        int quote_size = 1; /* 1 or 3 */

        /* Nodes of type STRING, especially multi line strings
           must be handled differently in order to get both
           the starting line number and the column offset right.
           (cf. issue 16806) */
        tok->first_lineno = tok->lineno;
        tok->multi_line_start = tok->line_start;

        /* Find the quote size and start of string */
        int after_quote = tok_nextc(tok);
        if (after_quote == quote) {
            int after_after_quote = tok_nextc(tok);
            if (after_after_quote == quote) {
                quote_size = 3;
            } else {
                // TODO: Check this
                tok_backup(tok, after_after_quote);
                tok_backup(tok, after_quote);
            }
        }
        if (after_quote != quote) {
            tok_backup(tok, after_quote);
        }

        p_start = tok->start;
        p_end = tok->cur;
        if (tok->tok_mode_stack_index + 1 >= MAXFSTRINGLEVEL) {
            return MAKE_TOKEN(syntaxerror(tok, "too many nested f-strings"));
        }
        tokenizer_mode *the_current_tok = TOK_NEXT_MODE(tok);
        the_current_tok->kind = TOK_FSTRING_MODE;
        the_current_tok->f_string_quote = quote;
        the_current_tok->f_string_quote_size = quote_size;
        the_current_tok->f_string_start = tok->start;
        the_current_tok->f_string_multi_line_start = tok->line_start;
        the_current_tok->f_string_line_start = tok->lineno;
        the_current_tok->f_string_start_offset = -1;
        the_current_tok->f_string_multi_line_start_offset = -1;
        the_current_tok->f_string_debug = 0;

        switch (*tok->start) {
        case 'F':
        case 'f':
            the_current_tok->f_string_raw = tolower(*(tok->start + 1)) == 'r';
            break;
        case 'R':
        case 'r':
            the_current_tok->f_string_raw = 1;
            break;
        default:
            std::abort();
        }

        the_current_tok->curly_bracket_depth = 0;
        the_current_tok->curly_bracket_expr_start_depth = -1;
        return MAKE_TOKEN(FSTRING_START);
    }

letter_quote:
    /* String */
    if (c == '\'' || c == '"') {
        int quote = c;
        int quote_size = 1; /* 1 or 3 */
        int end_quote_size = 0;

        /* Nodes of type STRING, especially multi line strings
           must be handled differently in order to get both
           the starting line number and the column offset right.
           (cf. issue 16806) */
        tok->first_lineno = tok->lineno;
        tok->multi_line_start = tok->line_start;

        /* Find the quote size and start of string */
        c = tok_nextc(tok);
        if (c == quote) {
            c = tok_nextc(tok);
            if (c == quote) {
                quote_size = 3;
            } else {
                end_quote_size = 1; /* empty string found */
            }
        }
        if (c != quote) {
            tok_backup(tok, c);
        }

        /* Get rest of string */
        while (end_quote_size != quote_size) {
            c = tok_nextc(tok);
            if (tok->done == E_ERROR) {
                return MAKE_TOKEN(ERRORTOKEN);
            }
            if (tok->done == E_DECODE) {
                break;
            }
            if (c == EOF || (quote_size == 1 && c == '\n')) {
                assert(tok->multi_line_start != NULL);
                // shift the tok_state's location into
                // the start of string, and report the error
                // from the initial quote character
                tok->cur = (char *)tok->start;
                tok->cur++;
                tok->line_start = tok->multi_line_start;
                int start = tok->lineno;
                tok->lineno = tok->first_lineno;

                if (INSIDE_FSTRING(tok)) {
                    /* When we are in an f-string, before raising the
                     * unterminated string literal error, check whether
                     * does the initial quote matches with f-strings quotes
                     * and if it is, then this must be a missing '}' token
                     * so raise the proper error */
                    tokenizer_mode *the_current_tok = TOK_GET_MODE(tok);
                    if (the_current_tok->f_string_quote == quote &&
                        the_current_tok->f_string_quote_size == quote_size) {
                        return MAKE_TOKEN(syntaxerror(tok, "f-string: expecting '}'", start));
                    }
                }

                if (quote_size == 3) {
                    syntaxerror(tok,
                                "unterminated triple-quoted string literal"
                                " (detected at line %d)",
                                start);
                    if (c != '\n') {
                        tok->done = E_EOFS;
                    }
                    return MAKE_TOKEN(ERRORTOKEN);
                } else {
                    syntaxerror(tok,
                                "unterminated string literal (detected at"
                                " line %d)",
                                start);
                    if (c != '\n') {
                        tok->done = E_EOLS;
                    }
                    return MAKE_TOKEN(ERRORTOKEN);
                }
            }
            if (c == quote) {
                end_quote_size += 1;
            } else {
                end_quote_size = 0;
                if (c == '\\') {
                    c = tok_nextc(tok); /* skip escaped char */
                    if (c == '\r') {
                        c = tok_nextc(tok);
                    }
                }
            }
        }

        p_start = tok->start;
        p_end = tok->cur;
        return MAKE_TOKEN(STRING);
    }

    /* Line continuation */
    if (c == '\\') {
        if ((c = tok_continuation_line(tok)) == -1) {
            return MAKE_TOKEN(ERRORTOKEN);
        }
        tok->cont_line = 1;
        goto again; /* Read next line */
    }

    /* Punctuation character */
    int is_punctuation = (c == ':' || c == '}' || c == '!' || c == '{');
    if (is_punctuation && INSIDE_FSTRING(tok) && INSIDE_FSTRING_EXPR(current_tok)) {
        /* This code block gets executed before the curly_bracket_depth is
         * incremented by the `{` case, so for ensuring that we are on the 0th
         * level, we need to adjust it manually */
        int cursor = current_tok->curly_bracket_depth - (c != '{');

        if (c == ':' && cursor == current_tok->curly_bracket_expr_start_depth) {
            current_tok->kind = TOK_FSTRING_MODE;
            current_tok->in_format_spec = 1;
            p_start = tok->start;
            p_end = tok->cur;
            return MAKE_TOKEN(token_OneChar(c));
        }
    }

    /* Check for two-character token */
    {
        int c2 = tok_nextc(tok);
        int current_token = token_TwoChars(c, c2);
        if (current_token != OP) {
            int c3 = tok_nextc(tok);
            int current_token3 = token_ThreeChars(c, c2, c3);
            if (current_token3 != OP) {
                current_token = current_token3;
            } else {
                tok_backup(tok, c3);
            }
            p_start = tok->start;
            p_end = tok->cur;
            return MAKE_TOKEN(current_token);
        }
        tok_backup(tok, c2);
    }

    /* Keep track of parentheses nesting level */
    switch (c) {
    case '(':
    case '[':
    case '{':
        if (tok->level >= MAXLEVEL) {
            return MAKE_TOKEN(syntaxerror(tok, "too many nested parentheses"));
        }
        tok->parenstack[tok->level] = c;
        tok->parenlinenostack[tok->level] = tok->lineno;
        tok->parencolstack[tok->level] = (int)(tok->start - tok->line_start);
        tok->level++;
        if (INSIDE_FSTRING(tok)) {
            current_tok->curly_bracket_depth++;
        }
        break;
    case ')':
    case ']':
    case '}':
        if (INSIDE_FSTRING(tok) && !current_tok->curly_bracket_depth && c == '}') {
            return MAKE_TOKEN(syntaxerror(tok, "f-string: single '}' is not allowed"));
        }
        if (!tok->tok_extra_tokens && !tok->level) {
            return MAKE_TOKEN(syntaxerror(tok, "unmatched '%c'", c));
        }
        if (tok->level > 0) {
            tok->level--;
            int opening = tok->parenstack[tok->level];
            if (!tok->tok_extra_tokens &&
                !((opening == '(' && c == ')') || (opening == '[' && c == ']') ||
                  (opening == '{' && c == '}'))) {
                /* If the opening bracket belongs to an f-string's expression
                part (e.g. f"{)}") and the closing bracket is an arbitrary
                nested expression, then instead of matching a different
                syntactical construct with it; we'll throw an unmatched
                parentheses error. */
                if (INSIDE_FSTRING(tok) && opening == '{') {
                    assert(current_tok->curly_bracket_depth >= 0);
                    int previous_bracket = current_tok->curly_bracket_depth - 1;
                    if (previous_bracket == current_tok->curly_bracket_expr_start_depth) {
                        return MAKE_TOKEN(syntaxerror(tok, "f-string: unmatched '%c'", c));
                    }
                }
                if (tok->parenlinenostack[tok->level] != tok->lineno) {
                    return MAKE_TOKEN(syntaxerror(tok,
                                                  "closing parenthesis '%c' does not match "
                                                  "opening parenthesis '%c' on line %d",
                                                  c, opening, tok->parenlinenostack[tok->level]));
                } else {
                    return MAKE_TOKEN(syntaxerror(tok,
                                                  "closing parenthesis '%c' does not match "
                                                  "opening parenthesis '%c'",
                                                  c, opening));
                }
            }
        }

        if (INSIDE_FSTRING(tok)) {
            current_tok->curly_bracket_depth--;
            if (current_tok->curly_bracket_depth < 0) {
                return MAKE_TOKEN(syntaxerror(tok, "f-string: unmatched '%c'", c));
            }
            if (c == '}' &&
                current_tok->curly_bracket_depth == current_tok->curly_bracket_expr_start_depth) {
                current_tok->curly_bracket_expr_start_depth--;
                current_tok->kind = TOK_FSTRING_MODE;
                current_tok->in_format_spec = 0;
                current_tok->f_string_debug = 0;
            }
        }
        break;
    default:
        break;
    }

    if ((c < 32 || c == 127)) {
        return MAKE_TOKEN(syntaxerror(tok, "invalid non-printable character U+%04X", c));
    }

    if (c == '=' && INSIDE_FSTRING_EXPR(current_tok)) {
        current_tok->f_string_debug = 1;
    }

    /* Punctuation character */
    p_start = tok->start;
    p_end = tok->cur;
    return MAKE_TOKEN(token_OneChar(c));
}

static int tok_get_fstring_mode(struct tok_state *tok, tokenizer_mode *current_tok,
                                struct token *token)
{
    const char *p_start = NULL;
    const char *p_end = NULL;
    int end_quote_size = 0;
    int unicode_escape = 0;

    tok->start = tok->cur;
    tok->first_lineno = tok->lineno;
    tok->starting_col_offset = tok->col_offset;

    // If we start with a bracket, we defer to the normal mode as there is nothing
    // for us to tokenize before it.
    int start_char = tok_nextc(tok);
    if (start_char == '{') {
        int peek1 = tok_nextc(tok);
        tok_backup(tok, peek1);
        tok_backup(tok, start_char);
        if (peek1 != '{') {
            current_tok->curly_bracket_expr_start_depth++;
            if (current_tok->curly_bracket_expr_start_depth >= MAX_EXPR_NESTING) {
                return MAKE_TOKEN(syntaxerror(tok, "f-string: expressions nested too deeply"));
            }
            TOK_GET_MODE(tok)->kind = TOK_REGULAR_MODE;
            return tok_get_normal_mode(tok, current_tok, token);
        }
    } else {
        tok_backup(tok, start_char);
    }

    // Check if we are at the end of the string
    for (int i = 0; i < current_tok->f_string_quote_size; i++) {
        int quote = tok_nextc(tok);
        if (quote != current_tok->f_string_quote) {
            tok_backup(tok, quote);
            goto f_string_middle;
        }
    }

    p_start = tok->start;
    p_end = tok->cur;
    tok->tok_mode_stack_index--;
    return MAKE_TOKEN(FSTRING_END);

f_string_middle:

    // TODO: This is a bit of a hack, but it works for now. We need to find a
    // better way to handle this.
    tok->multi_line_start = tok->line_start;
    while (end_quote_size != current_tok->f_string_quote_size) {
        int c = tok_nextc(tok);
        if (tok->done == E_ERROR || tok->done == E_DECODE) {
            return MAKE_TOKEN(ERRORTOKEN);
        }
        int in_format_spec = (current_tok->in_format_spec && INSIDE_FSTRING_EXPR(current_tok));

        if (c == EOF || (current_tok->f_string_quote_size == 1 && c == '\n')) {
            if (tok->decoding_erred) {
                return MAKE_TOKEN(ERRORTOKEN);
            }

            // If we are in a format spec and we found a newline,
            // it means that the format spec ends here and we should
            // return to the regular mode.
            if (in_format_spec && c == '\n') {
                tok_backup(tok, c);
                TOK_GET_MODE(tok)->kind = TOK_REGULAR_MODE;
                current_tok->in_format_spec = 0;
                p_start = tok->start;
                p_end = tok->cur;
                return MAKE_TOKEN(FSTRING_MIDDLE);
            }

            assert(tok->multi_line_start != NULL);
            // shift the tok_state's location into
            // the start of string, and report the error
            // from the initial quote character
            tok->cur = (char *)current_tok->f_string_start;
            tok->cur++;
            tok->line_start = current_tok->f_string_multi_line_start;
            int start = tok->lineno;

            tokenizer_mode *the_current_tok = TOK_GET_MODE(tok);
            tok->lineno = the_current_tok->f_string_line_start;

            if (current_tok->f_string_quote_size == 3) {
                syntaxerror(tok,
                            "unterminated triple-quoted f-string literal"
                            " (detected at line %d)",
                            start);
                if (c != '\n') {
                    tok->done = E_EOFS;
                }
                return MAKE_TOKEN(ERRORTOKEN);
            } else {
                return MAKE_TOKEN(syntaxerror(tok,
                                              "unterminated f-string literal (detected at"
                                              " line %d)",
                                              start));
            }
        }

        if (c == current_tok->f_string_quote) {
            end_quote_size += 1;
            continue;
        } else {
            end_quote_size = 0;
        }

        if (c == '{') {

            int peek = tok_nextc(tok);
            if (peek != '{' || in_format_spec) {
                tok_backup(tok, peek);
                tok_backup(tok, c);
                current_tok->curly_bracket_expr_start_depth++;
                if (current_tok->curly_bracket_expr_start_depth >= MAX_EXPR_NESTING) {
                    return MAKE_TOKEN(syntaxerror(tok, "f-string: expressions nested too deeply"));
                }
                TOK_GET_MODE(tok)->kind = TOK_REGULAR_MODE;
                current_tok->in_format_spec = 0;
                p_start = tok->start;
                p_end = tok->cur;
            } else {
                p_start = tok->start;
                p_end = tok->cur - 1;
            }
            return MAKE_TOKEN(FSTRING_MIDDLE);
        } else if (c == '}') {
            if (unicode_escape) {
                p_start = tok->start;
                p_end = tok->cur;
                return MAKE_TOKEN(FSTRING_MIDDLE);
            }
            int peek = tok_nextc(tok);

            // The tokenizer can only be in the format spec if we have already
            // completed the expression scanning (indicated by the end of the
            // expression being set) and we are not at the top level of the bracket
            // stack (-1 is the top level). Since format specifiers can't legally use
            // double brackets, we can bypass it here.
            int cursor = current_tok->curly_bracket_depth;
            if (peek == '}' && !in_format_spec && cursor == 0) {
                p_start = tok->start;
                p_end = tok->cur - 1;
            } else {
                tok_backup(tok, peek);
                tok_backup(tok, c);
                TOK_GET_MODE(tok)->kind = TOK_REGULAR_MODE;
                p_start = tok->start;
                p_end = tok->cur;
            }
            return MAKE_TOKEN(FSTRING_MIDDLE);
        } else if (c == '\\') {
            int peek = tok_nextc(tok);
            if (peek == '\r') {
                peek = tok_nextc(tok);
            }
            // Special case when the backslash is right before a curly
            // brace. We have to restore and return the control back
            // to the loop for the next iteration.
            if (peek == '{' || peek == '}') {
                if (!current_tok->f_string_raw) {
                    if (warn_invalid_escape_sequence(tok, peek)) {
                        return MAKE_TOKEN(ERRORTOKEN);
                    }
                }
                tok_backup(tok, peek);
                continue;
            }

            if (!current_tok->f_string_raw) {
                if (peek == 'N') {
                    /* Handle named unicode escapes (\N{BULLET}) */
                    peek = tok_nextc(tok);
                    if (peek == '{') {
                        unicode_escape = 1;
                    } else {
                        tok_backup(tok, peek);
                    }
                }
            } /* else {
                skip the escaped character
            }*/
        }
    }

    // Backup the f-string quotes to emit a final FSTRING_MIDDLE and
    // add the quotes to the FSTRING_END in the next tokenizer iteration.
    for (int i = 0; i < current_tok->f_string_quote_size; i++) {
        tok_backup(tok, current_tok->f_string_quote);
    }
    p_start = tok->start;
    p_end = tok->cur;
    return MAKE_TOKEN(FSTRING_MIDDLE);
}

static int tok_get(struct tok_state *tok, struct token *token)
{
    tokenizer_mode *current_tok = TOK_GET_MODE(tok);
    if (current_tok->kind == TOK_REGULAR_MODE) {
        return tok_get_normal_mode(tok, current_tok, token);
    } else {
        return tok_get_fstring_mode(tok, current_tok, token);
    }
}

int lexer_get(struct tok_state *tok, struct token *token)
{
    int result = tok_get(tok, token);
    if (tok->decoding_erred) {
        result = ERRORTOKEN;
        tok->done = E_DECODE;
    }
    return result;
}
} // namespace
struct TokenStream::Impl {
    tok_state state;
    bool finished = false;
    int last_lineno = -1;
    std::shared_ptr<const std::u32string> last_line;
    std::size_t line_revision = 0;
    std::ptrdiff_t byte_column_difference = 0;
    Impl(bool extra, bool interactive, bool incomplete)
    {
        state.buf = static_cast<char *>(std::malloc(8192));
        if (!state.buf)
            throw std::bad_alloc();
        state.cur = state.inp = state.buf;
        state.end = state.buf + 8192;
        *state.buf = 0;
        state.line_start = state.multi_line_start = state.buf;
        state.tok_extra_tokens = extra;
        state.prompt = interactive ? "" : nullptr;
        state.allow_incomplete = incomplete;
    }
    ~Impl()
    {
        std::free(state.buf);
    }
};
TokenStream::TokenStream(bool extra, bool interactive, bool incomplete)
    : impl_(std::make_unique<Impl>(extra, interactive, incomplete))
{}
TokenStream::~TokenStream() = default;
const LexicalDiagnostic &TokenStream::diagnostic() const noexcept
{
    return impl_->state.diagnostic;
}
std::size_t TokenStream::external_bytes() const noexcept
{
    return sizeof(Impl) + static_cast<std::size_t>(impl_->state.end - impl_->state.buf) +
           (impl_->last_line ? impl_->last_line->capacity() * sizeof(char32_t) : 0);
}
std::optional<LexicalToken> TokenStream::next(const ReadLine &reader, const Poll &poll,
                                              const Warning &warning)
{
    if (impl_->finished)
        return {};
    auto &s = impl_->state;
    struct Callbacks {
        tok_state &state;
        ~Callbacks()
        {
            state.reader = nullptr;
            state.poll = nullptr;
            state.warning = nullptr;
        }
    } callbacks{s};
    s.reader = &reader;
    s.poll = &poll;
    s.warning = &warning;
    try {
        s.diagnostic = {};
        token raw;
        const auto kind = lexer_get(&s, &raw);
        if (kind == ERRORTOKEN) {
            if (s.diagnostic.message.empty()) {
                const char *message =
                    s.done == E_EOF        ? "unexpected EOF in multi-line statement"
                    : s.done == E_DEDENT   ? "unindent does not match any outer indentation level"
                    : s.done == E_TABSPACE ? "inconsistent use of tabs and spaces in indentation"
                    : s.done == E_TOODEEP  ? "too many levels of indentation"
                    : s.done == E_LINECONT
                        ? "unexpected character after line continuation character"
                        : "invalid token";
                const auto done = s.done;
                syntaxerror(&s, "%s", message);
                s.done = done;
                s.diagnostic.end_lineno = s.diagnostic.end_offset = -1;
                if (done == E_EOF) {
                    s.diagnostic.location_in_args = false;
                    s.diagnostic.has_text = false;
                    s.diagnostic.offset = static_cast<int>(s.inp - s.buf);
                    s.diagnostic.end_lineno = s.lineno;
                }
            }
            if (s.done == E_TABSPACE)
                s.diagnostic.kind = ErrorKind::Tab;
            if (s.done == E_DEDENT || s.done == E_TOODEEP)
                s.diagnostic.kind = ErrorKind::Indentation;
            if (s.allow_incomplete && (s.done == E_EOF || s.done == E_EOFS || s.done == E_LINECONT))
                s.diagnostic.kind = ErrorKind::Incomplete;
            const auto &d = s.diagnostic;
            throw Error(d.span, d.message, d.kind);
        }
        LexicalToken result;
        result.token.kind = static_cast<TokenKind>(kind);
        const auto at = s.buffer_offset + static_cast<std::size_t>(s.cur - s.buf);
        result.token.span = {at, at};
        if (raw.start && raw.end) {
            result.token.span = {s.buffer_offset + static_cast<std::size_t>(raw.start - s.buf),
                                 s.buffer_offset + static_cast<std::size_t>(raw.end - s.buf)};
            result.token.text = decode_utf8(std::string_view(raw.start, raw.end - raw.start));
        }
        result.lineno = ISSTRINGLIT(kind) ? s.first_lineno : s.lineno;
        result.end_lineno = s.lineno;
        const auto *line = ISSTRINGLIT(kind) ? s.multi_line_start : s.line_start;
        result.trailing = kind == ENDMARKER || (kind == DEDENT && s.done == E_EOF);
        result.implicit_newline = s.implicit_newline;
        const bool line_changed = s.lineno != impl_->last_lineno;
        if (s.tok_extra_tokens && result.trailing) {
            result.line = std::make_shared<const std::u32string>();
            result.line_revision = ++impl_->line_revision;
        } else {
            if (line_changed) {
                auto length = s.inp - line;
                if (length && s.implicit_newline)
                    --length;
                impl_->last_line = std::make_shared<const std::u32string>(
                    decode_utf8(std::string_view(line, static_cast<std::size_t>(length))));
                impl_->last_lineno = result.lineno;
                ++impl_->line_revision;
                impl_->byte_column_difference = 0;
            }
            result.line = impl_->last_line;
            result.line_revision = impl_->line_revision;
        }
        if (raw.start && raw.start >= line) {
            const auto byte_offset = raw.start - line;
            result.column = line_changed
                                ? character_count(line, raw.start)
                                : static_cast<int>(byte_offset - impl_->byte_column_difference);
            if (line_changed)
                impl_->byte_column_difference = byte_offset - result.column;
        }
        if (raw.end && raw.end >= s.line_start) {
            if (result.lineno == result.end_lineno) {
                const auto length = character_count(raw.start, raw.end);
                result.end_column = result.column + length;
                impl_->byte_column_difference += raw.end - raw.start - length;
            } else {
                result.end_column = character_count(s.line_start, raw.end);
                impl_->byte_column_difference += raw.end - s.line_start - result.end_column;
            }
        }
        if (kind == ENDMARKER)
            impl_->finished = true;
        return result;
    } catch (...) {
        impl_->finished = true;
        throw;
    }
}
std::vector<Token> tokenize(std::string_view source, InputMode mode, bool allow_incomplete)
{
    TokenStream stream(false, mode == InputMode::Interactive, allow_incomplete);
    std::size_t offset = 0;
    auto reader = [&]() {
        const auto start = offset;
        if (start == source.size())
            return std::string{};
        const auto newline = source.find('\n', start);
        offset = newline == source.npos ? source.size() : newline + 1;
        return std::string(source.substr(start, offset - start));
    };
    std::vector<Token> result;
    while (auto token = stream.next(reader)) {
        auto &value = token->token;
        value.span.begin = std::min(value.span.begin, source.size());
        value.span.end = std::min(value.span.end, source.size());
        if (value.kind == TokenKind::NEWLINE && value.span.end < source.size())
            value.span.end +=
                source[value.span.end] == '\r' && value.span.end + 1 < source.size() ? 2 : 1;
        if (value.kind == TokenKind::ENDMARKER && mode == InputMode::Interactive &&
            !allow_incomplete && !result.empty() && result.back().kind == TokenKind::DEDENT)
            result.push_back({TokenKind::NEWLINE, value.span, {}});
        result.push_back(std::move(value));
    }
    return result;
}
} // namespace cxxpython::syntax
