#include "cxxpython/compiler_internal.hpp"

#include <cctype>
#include <charconv>
#include <sstream>

namespace cxxpython::compiler_internal
{
    namespace
    {
        bool is_inline_space(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
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

        [[noreturn]] void raise_at(Runtime &runtime,
                                   const std::string &filename,
                                   std::size_t line,
                                   std::size_t column,
                                   const std::string &message)
        {
            std::ostringstream out;
            out << filename << ":" << line << ":" << column << ": " << message;
            raise_syntax_error(&runtime, out.str());
        }
    }

    std::vector<Token> lex_source(Runtime &runtime, std::string_view source, std::string filename)
    {
        std::vector<Token> tokens;
        std::size_t index = 0;
        std::size_t line = 1;
        std::size_t column = 1;
        while (index < source.size())
        {
            const char ch = source[index];
            if (is_inline_space(ch))
            {
                ++index;
                ++column;
                continue;
            }

            if (ch == '\n')
            {
                tokens.push_back(Token{TokenKind::Newline, "\n", 0, line, column});
                ++index;
                ++line;
                column = 1;
                continue;
            }

            if (ch == '\r')
            {
                if (index + 1 < source.size() && source[index + 1] == '\n')
                    ++index;
                tokens.push_back(Token{TokenKind::Newline, "\n", 0, line, column});
                ++index;
                ++line;
                column = 1;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
            {
                const std::size_t start = index;
                const std::size_t start_column = column;
                while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                {
                    ++index;
                    ++column;
                }
                const std::string lexeme(source.substr(start, index - start));
                tokens.push_back(Token{TokenKind::Integer, lexeme, parse_integer(runtime, lexeme), line, start_column});
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_')
            {
                const std::size_t start = index;
                const std::size_t start_column = column;
                while (index < source.size())
                {
                    const char current = source[index];
                    if (std::isalnum(static_cast<unsigned char>(current)) == 0 && current != '_')
                        break;
                    ++index;
                    ++column;
                }
                tokens.push_back(Token{TokenKind::Identifier, std::string(source.substr(start, index - start)), 0, line, start_column});
                continue;
            }

            if (ch == '\'')
            {
                const std::size_t start_line = line;
                const std::size_t start_column = column;
                ++index;
                ++column;
                std::string value;
                while (index < source.size() && source[index] != '\'')
                {
                    if (source[index] == '\\')
                    {
                        ++index;
                        ++column;
                        if (index >= source.size())
                            raise_at(runtime, filename, start_line, start_column, "unterminated string literal");
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
                            raise_at(runtime, filename, line, column, "unsupported string escape");
                        }
                        ++index;
                        ++column;
                        continue;
                    }
                    if (source[index] == '\n' || source[index] == '\r')
                        raise_at(runtime, filename, start_line, start_column, "unterminated string literal");
                    value.push_back(source[index]);
                    ++index;
                    ++column;
                }
                if (index >= source.size())
                    raise_at(runtime, filename, start_line, start_column, "unterminated string literal");
                ++index;
                ++column;
                tokens.push_back(Token{TokenKind::String, std::move(value), 0, start_line, start_column});
                continue;
            }

            if (ch == '+')
            {
                tokens.push_back(Token{TokenKind::Plus, "+", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '(')
            {
                tokens.push_back(Token{TokenKind::LeftParen, "(", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == ')')
            {
                tokens.push_back(Token{TokenKind::RightParen, ")", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == ',')
            {
                tokens.push_back(Token{TokenKind::Comma, ",", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '=')
            {
                tokens.push_back(Token{TokenKind::Equal, "=", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            raise_at(runtime, filename, line, column, "unexpected character");
        }

        tokens.push_back(Token{TokenKind::End, "", 0, line, column});
        return tokens;
    }
}
