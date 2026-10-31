// Shared literal semantics adapted from CPython 3.12.14 Parser/string_parser.c.
#include "cxxpython/parser.hpp"
namespace cxxpython::syntax {
namespace {
std::u32string decode_contents(std::u32string_view text, bool raw, bool bytes, Span where)
{
    std::u32string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t ch = text[i];
        if (bytes && ch >= 128)
            throw Error(where, "bytes can only contain ASCII literal characters");
        if (ch == U'\r') {
            if (i + 1 < text.size() && text[i + 1] == U'\n')
                ++i;
            ch = U'\n';
        }
        if (ch != U'\\' || raw || i + 1 >= text.size()) {
            out.push_back(ch);
            continue;
        }
        ch = text[++i];
        if (bytes && ch >= 128)
            throw Error(where, "bytes can only contain ASCII literal characters");
        if (bytes && (ch == U'u' || ch == U'U' || ch == U'N')) {
            out.push_back(U'\\');
            out.push_back(ch);
            continue;
        }
        switch (ch) {
        case U'\r':
            if (i + 1 < text.size() && text[i + 1] == U'\n')
                ++i;
            break;
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
            if (i + 1 >= text.size() || text[++i] != U'{')
                throw Error(where, "malformed named Unicode escape");
            const auto begin = ++i;
            while (i < text.size() && text[i] != U'}')
                ++i;
            if (i == text.size())
                throw Error(where, "malformed named Unicode escape");
            const auto value =
                syntax::unicode_name(std::u32string_view(text).substr(begin, i - begin));
            if (!value)
                throw Error(where, "unknown Unicode character name");
            out.push_back(*value);
            break;
        }
        case U'x':
        case U'u':
        case U'U': {
            int count = ch == U'x' ? 2 : ch == U'u' ? 4 : 8;
            char32_t value = 0;
            for (int j = 0; j < count; ++j) {
                if (++i >= text.size())
                    throw Error(where, "truncated Unicode escape");
                auto h = text[i];
                int digit = h >= U'0' && h <= U'9'   ? h - U'0'
                            : h >= U'a' && h <= U'f' ? h - U'a' + 10
                            : h >= U'A' && h <= U'F' ? h - U'A' + 10
                                                     : -1;
                if (digit < 0)
                    throw Error(where, "invalid Unicode escape");
                value = value * 16 + digit;
            }
            if (value > 0x10ffff)
                throw Error(where, "invalid Unicode escape");
            out.push_back(value);
            break;
        }
        default:
            if (ch >= U'0' && ch <= U'7') {
                char32_t value = ch - U'0';
                for (int j = 0;
                     j < 2 && i + 1 < text.size() && text[i + 1] >= U'0' && text[i + 1] <= U'7';
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
} // namespace
std::u32string decode_string_literal(std::u32string_view text, Span where)
{
    std::size_t quote = text.find_first_of(U"'\"");
    if (quote == std::u32string::npos)
        throw Error(where, "invalid string literal");
    bool raw = false, bytes = false;
    for (std::size_t i = 0; i < quote; ++i) {
        if (text[i] == U'f' || text[i] == U'F')
            throw Error(where, "invalid string literal");
        if (text[i] == U'b' || text[i] == U'B')
            bytes = true;
        if (text[i] == U'r' || text[i] == U'R')
            raw = true;
    }
    std::size_t q =
        quote + 2 < text.size() && text[quote + 1] == text[quote] && text[quote + 2] == text[quote]
            ? 3
            : 1;

    return decode_contents(text.substr(quote + q, text.size() - quote - 2 * q), raw, bytes, where);
}
std::u32string decode_string_fragment(std::u32string_view text, bool raw, Span where)
{
    return decode_contents(text, raw, false, where);
}
} // namespace cxxpython::syntax
