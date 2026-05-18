#include "cxxpython/compiler_internal.hpp"

#include <cctype>
#include <charconv>

namespace cxxpython::compiler_internal
{
    namespace
    {
        bool is_space(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
        }

        std::int64_t parse_integer(Runtime &runtime, std::string_view digits)
        {
            std::int64_t value = 0;
            const char *begin = digits.data();
            const char *end = begin + digits.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end)
                raise_syntax_error(&runtime, "invalid integer literal");
            return value;
        }
    }

    std::vector<Token> lex_source(Runtime &runtime, std::string_view source)
    {
        std::vector<Token> tokens;
        std::size_t index = 0;
        while (index < source.size())
        {
            const char ch = source[index];
            if (is_space(ch))
            {
                ++index;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
            {
                const std::size_t start = index;
                while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                    ++index;
                const std::string lexeme(source.substr(start, index - start));
                tokens.push_back(Token{TokenKind::Integer, lexeme, parse_integer(runtime, lexeme)});
                continue;
            }

            if (ch == '\'')
            {
                ++index;
                std::string value;
                while (index < source.size() && source[index] != '\'')
                {
                    if (source[index] == '\\')
                    {
                        ++index;
                        if (index >= source.size())
                            raise_syntax_error(&runtime, "unterminated string literal");
                        switch (source[index])
                        {
                        case '\\':
                            value.push_back('\\');
                            break;
                        case '\'':
                            value.push_back('\'');
                            break;
                        case 'n':
                            value.push_back('\n');
                            break;
                        case 't':
                            value.push_back('\t');
                            break;
                        case 'r':
                            value.push_back('\r');
                            break;
                        default:
                            raise_syntax_error(&runtime, "unsupported string escape");
                        }
                        ++index;
                        continue;
                    }
                    value.push_back(source[index]);
                    ++index;
                }
                if (index >= source.size())
                    raise_syntax_error(&runtime, "unterminated string literal");
                ++index;
                tokens.push_back(Token{TokenKind::String, std::move(value)});
                continue;
            }

            if (ch == '+')
            {
                tokens.push_back(Token{TokenKind::Plus, "+"});
                ++index;
                continue;
            }

            raise_syntax_error(&runtime, "unexpected character");
        }

        tokens.push_back(Token{TokenKind::End, ""});
        return tokens;
    }
}
