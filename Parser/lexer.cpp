#include "cxxpython/compiler_internal.hpp"

#include <cctype>
#include <charconv>
#include <sstream>

namespace cxxpython::compiler_internal
{
    const char *token_kind_name(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::Integer:
            return "Integer";
        case TokenKind::String:
            return "String";
        case TokenKind::FString:
            return "FString";
        case TokenKind::Identifier:
            return "Identifier";
        case TokenKind::Plus:
            return "Plus";
        case TokenKind::Minus:
            return "Minus";
        case TokenKind::Arrow:
            return "Arrow";
        case TokenKind::Star:
            return "Star";
        case TokenKind::DoubleStar:
            return "DoubleStar";
        case TokenKind::Slash:
            return "Slash";
        case TokenKind::At:
            return "At";
        case TokenKind::Tilde:
            return "Tilde";
        case TokenKind::LeftParen:
            return "LeftParen";
        case TokenKind::RightParen:
            return "RightParen";
        case TokenKind::Comma:
            return "Comma";
        case TokenKind::Dot:
            return "Dot";
        case TokenKind::LeftBracket:
            return "LeftBracket";
        case TokenKind::RightBracket:
            return "RightBracket";
        case TokenKind::LeftBrace:
            return "LeftBrace";
        case TokenKind::RightBrace:
            return "RightBrace";
        case TokenKind::Colon:
            return "Colon";
        case TokenKind::Semicolon:
            return "Semicolon";
        case TokenKind::Equal:
            return "Equal";
        case TokenKind::PlusEqual:
            return "PlusEqual";
        case TokenKind::MinusEqual:
            return "MinusEqual";
        case TokenKind::StarEqual:
            return "StarEqual";
        case TokenKind::EqualEqual:
            return "EqualEqual";
        case TokenKind::BangEqual:
            return "BangEqual";
        case TokenKind::Less:
            return "Less";
        case TokenKind::LessEqual:
            return "LessEqual";
        case TokenKind::Greater:
            return "Greater";
        case TokenKind::GreaterEqual:
            return "GreaterEqual";
        case TokenKind::If:
            return "If";
        case TokenKind::Elif:
            return "Elif";
        case TokenKind::Else:
            return "Else";
        case TokenKind::For:
            return "For";
        case TokenKind::While:
            return "While";
        case TokenKind::Break:
            return "Break";
        case TokenKind::Continue:
            return "Continue";
        case TokenKind::Def:
            return "Def";
        case TokenKind::Return:
            return "Return";
        case TokenKind::Raise:
            return "Raise";
        case TokenKind::Import:
            return "Import";
        case TokenKind::From:
            return "From";
        case TokenKind::Global:
            return "Global";
        case TokenKind::Nonlocal:
            return "Nonlocal";
        case TokenKind::Try:
            return "Try";
        case TokenKind::Except:
            return "Except";
        case TokenKind::Finally:
            return "Finally";
        case TokenKind::As:
            return "As";
        case TokenKind::In:
            return "In";
        case TokenKind::Is:
            return "Is";
        case TokenKind::Not:
            return "Not";
        case TokenKind::And:
            return "And";
        case TokenKind::Or:
            return "Or";
        case TokenKind::Lambda:
            return "Lambda";
        case TokenKind::Class:
            return "Class";
        case TokenKind::Del:
            return "Del";
        case TokenKind::Pass:
            return "Pass";
        case TokenKind::TrueLiteral:
            return "True";
        case TokenKind::FalseLiteral:
            return "False";
        case TokenKind::NoneLiteral:
            return "None";
        case TokenKind::Indent:
            return "Indent";
        case TokenKind::Dedent:
            return "Dedent";
        case TokenKind::Newline:
            return "Newline";
        case TokenKind::End:
            return "End";
        }
        return "Unknown";
    }

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
        std::vector<std::size_t> indent_stack{0};
        std::size_t index = 0;
        std::size_t line = 1;
        std::size_t column = 1;
        std::size_t nesting = 0;
        bool at_line_start = true;
        auto is_quote = [](char value) {
            return value == '\'' || value == '"';
        };
        auto advance_one = [&]() {
            const char value = source[index++];
            if (value == '\n')
            {
                ++line;
                column = 1;
            }
            else
            {
                ++column;
            }
            return value;
        };
        auto skip_comment = [&]() {
            while (index < source.size() && source[index] != '\n' && source[index] != '\r')
                advance_one();
        };
        auto scan_string = [&](char quote, std::size_t start_line, std::size_t start_column) {
            const bool triple = index + 2 < source.size() && source[index] == quote &&
                                source[index + 1] == quote && source[index + 2] == quote;
            if (triple)
            {
                advance_one();
                advance_one();
                advance_one();
            }
            else
            {
                advance_one();
            }

            std::string value;
            while (index < source.size())
            {
                if (triple)
                {
                    if (index + 2 < source.size() && source[index] == quote &&
                        source[index + 1] == quote && source[index + 2] == quote)
                    {
                        advance_one();
                        advance_one();
                        advance_one();
                        return value;
                    }
                }
                else if (source[index] == quote)
                {
                    advance_one();
                    return value;
                }

                if (!triple && (source[index] == '\n' || source[index] == '\r'))
                    raise_at(runtime, filename, start_line, start_column, "unterminated string literal");

                if (source[index] == '\\')
                {
                    advance_one();
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
                    case '"':
                        value.push_back('"');
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
                    advance_one();
                    continue;
                }

                value.push_back(advance_one());
            }
            raise_at(runtime, filename, start_line, start_column, "unterminated string literal");
        };

        while (index < source.size())
        {
            if (at_line_start)
            {
                const std::size_t indent_line = line;
                std::size_t indent_column = column;
                std::size_t indentation = 0;
                while (index < source.size() && (source[index] == ' ' || source[index] == '\t'))
                {
                    if (source[index] == '\t')
                        indentation += 8 - (indentation % 8);
                    else
                        ++indentation;
                    ++index;
                    ++column;
                }

                if (index >= source.size())
                    break;
                if (nesting != 0)
                {
                    if (source[index] == '#')
                    {
                        skip_comment();
                        continue;
                    }
                    if (source[index] != '\n' && source[index] != '\r')
                        at_line_start = false;
                    continue;
                }
                if (source[index] == '#')
                {
                    skip_comment();
                    continue;
                }
                if (source[index] != '\n' && source[index] != '\r')
                {
                    const std::size_t current = indent_stack.back();
                    if (indentation > current)
                    {
                        indent_stack.push_back(indentation);
                        tokens.push_back(Token{TokenKind::Indent, "", 0, indent_line, indent_column});
                    }
                    else if (indentation < current)
                    {
                        while (indent_stack.size() > 1 && indentation < indent_stack.back())
                        {
                            indent_stack.pop_back();
                            tokens.push_back(Token{TokenKind::Dedent, "", 0, indent_line, indent_column});
                        }
                        if (indentation != indent_stack.back())
                            raise_at(runtime, filename, indent_line, indent_column, "unindent does not match any outer indentation level");
                    }
                    at_line_start = false;
                }
            }

            const char ch = source[index];
            if (is_inline_space(ch))
            {
                ++index;
                ++column;
                continue;
            }

            if (ch == '#')
            {
                skip_comment();
                continue;
            }

            if (ch == '\n')
            {
                if (nesting == 0)
                    tokens.push_back(Token{TokenKind::Newline, "\n", 0, line, column});
                advance_one();
                at_line_start = true;
                continue;
            }

            if (ch == '\r')
            {
                if (index + 1 < source.size() && source[index + 1] == '\n')
                    ++index;
                if (nesting == 0)
                    tokens.push_back(Token{TokenKind::Newline, "\n", 0, line, column});
                advance_one();
                at_line_start = true;
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

            if ((ch == 'f' || ch == 'F') && index + 1 < source.size() && is_quote(source[index + 1]))
            {
                const std::size_t start_line = line;
                const std::size_t start_column = column;
                advance_one();
                tokens.push_back(Token{TokenKind::FString,
                                       scan_string(source[index], start_line, start_column),
                                       0,
                                       start_line,
                                       start_column});
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
                const std::string lexeme(source.substr(start, index - start));
                TokenKind kind = TokenKind::Identifier;
                if (lexeme == "if")
                    kind = TokenKind::If;
                else if (lexeme == "elif")
                    kind = TokenKind::Elif;
                else if (lexeme == "else")
                    kind = TokenKind::Else;
                else if (lexeme == "for")
                    kind = TokenKind::For;
                else if (lexeme == "while")
                    kind = TokenKind::While;
                else if (lexeme == "break")
                    kind = TokenKind::Break;
                else if (lexeme == "continue")
                    kind = TokenKind::Continue;
                else if (lexeme == "def")
                    kind = TokenKind::Def;
                else if (lexeme == "return")
                    kind = TokenKind::Return;
                else if (lexeme == "raise")
                    kind = TokenKind::Raise;
                else if (lexeme == "import")
                    kind = TokenKind::Import;
                else if (lexeme == "from")
                    kind = TokenKind::From;
                else if (lexeme == "global")
                    kind = TokenKind::Global;
                else if (lexeme == "nonlocal")
                    kind = TokenKind::Nonlocal;
                else if (lexeme == "try")
                    kind = TokenKind::Try;
                else if (lexeme == "except")
                    kind = TokenKind::Except;
                else if (lexeme == "finally")
                    kind = TokenKind::Finally;
                else if (lexeme == "as")
                    kind = TokenKind::As;
                else if (lexeme == "in")
                    kind = TokenKind::In;
                else if (lexeme == "is")
                    kind = TokenKind::Is;
                else if (lexeme == "not")
                    kind = TokenKind::Not;
                else if (lexeme == "and")
                    kind = TokenKind::And;
                else if (lexeme == "or")
                    kind = TokenKind::Or;
                else if (lexeme == "lambda")
                    kind = TokenKind::Lambda;
                else if (lexeme == "class")
                    kind = TokenKind::Class;
                else if (lexeme == "del")
                    kind = TokenKind::Del;
                else if (lexeme == "pass")
                    kind = TokenKind::Pass;
                else if (lexeme == "True")
                    kind = TokenKind::TrueLiteral;
                else if (lexeme == "False")
                    kind = TokenKind::FalseLiteral;
                else if (lexeme == "None")
                    kind = TokenKind::NoneLiteral;
                tokens.push_back(Token{kind, lexeme, 0, line, start_column});
                continue;
            }

            if (is_quote(ch))
            {
                const std::size_t start_line = line;
                const std::size_t start_column = column;
                tokens.push_back(Token{TokenKind::String, scan_string(ch, start_line, start_column), 0, start_line, start_column});
                continue;
            }

            if (ch == '+')
            {
                if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::PlusEqual, "+=", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Plus, "+", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            if (ch == '-')
            {
                if (index + 1 < source.size() && source[index + 1] == '>')
                {
                    tokens.push_back(Token{TokenKind::Arrow, "->", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::MinusEqual, "-=", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Minus, "-", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            if (ch == '*')
            {
                if (index + 1 < source.size() && source[index + 1] == '*')
                {
                    tokens.push_back(Token{TokenKind::DoubleStar, "**", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::StarEqual, "*=", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Star, "*", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            if (ch == '~')
            {
                tokens.push_back(Token{TokenKind::Tilde, "~", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '/')
            {
                tokens.push_back(Token{TokenKind::Slash, "/", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '@')
            {
                tokens.push_back(Token{TokenKind::At, "@", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '(')
            {
                tokens.push_back(Token{TokenKind::LeftParen, "(", 0, line, column});
                ++nesting;
                ++index;
                ++column;
                continue;
            }

            if (ch == ')')
            {
                tokens.push_back(Token{TokenKind::RightParen, ")", 0, line, column});
                if (nesting != 0)
                    --nesting;
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

            if (ch == '.')
            {
                tokens.push_back(Token{TokenKind::Dot, ".", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '[')
            {
                tokens.push_back(Token{TokenKind::LeftBracket, "[", 0, line, column});
                ++nesting;
                ++index;
                ++column;
                continue;
            }

            if (ch == ']')
            {
                tokens.push_back(Token{TokenKind::RightBracket, "]", 0, line, column});
                if (nesting != 0)
                    --nesting;
                ++index;
                ++column;
                continue;
            }

            if (ch == '{')
            {
                tokens.push_back(Token{TokenKind::LeftBrace, "{", 0, line, column});
                ++nesting;
                ++index;
                ++column;
                continue;
            }

            if (ch == '}')
            {
                tokens.push_back(Token{TokenKind::RightBrace, "}", 0, line, column});
                if (nesting != 0)
                    --nesting;
                ++index;
                ++column;
                continue;
            }

            if (ch == ':')
            {
                tokens.push_back(Token{TokenKind::Colon, ":", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == ';')
            {
                tokens.push_back(Token{TokenKind::Semicolon, ";", 0, line, column});
                ++index;
                ++column;
                continue;
            }

            if (ch == '=')
            {
                if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::EqualEqual, "==", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Equal, "=", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            if (ch == '!' && index + 1 < source.size() && source[index + 1] == '=')
            {
                tokens.push_back(Token{TokenKind::BangEqual, "!=", 0, line, column});
                index += 2;
                column += 2;
                continue;
            }

            if (ch == '<')
            {
                if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::LessEqual, "<=", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Less, "<", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            if (ch == '>')
            {
                if (index + 1 < source.size() && source[index + 1] == '=')
                {
                    tokens.push_back(Token{TokenKind::GreaterEqual, ">=", 0, line, column});
                    index += 2;
                    column += 2;
                }
                else
                {
                    tokens.push_back(Token{TokenKind::Greater, ">", 0, line, column});
                    ++index;
                    ++column;
                }
                continue;
            }

            raise_at(runtime, filename, line, column, "unexpected character");
        }

        while (indent_stack.size() > 1)
        {
            indent_stack.pop_back();
            tokens.push_back(Token{TokenKind::Dedent, "", 0, line, column});
        }
        tokens.push_back(Token{TokenKind::End, "", 0, line, column});
        return tokens;
    }
}
