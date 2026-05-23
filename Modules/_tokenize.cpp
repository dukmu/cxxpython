#include "module_registry.hpp"

#include "cxxpython/compiler_internal.hpp"
#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cxxpython
{
    namespace
    {
        constexpr std::int64_t ENDMARKER = 0;
        constexpr std::int64_t NAME = 1;
        constexpr std::int64_t NUMBER = 2;
        constexpr std::int64_t STRING = 3;
        constexpr std::int64_t NEWLINE = 4;
        constexpr std::int64_t INDENT = 5;
        constexpr std::int64_t DEDENT = 6;
        constexpr std::int64_t LPAR = 7;
        constexpr std::int64_t RPAR = 8;
        constexpr std::int64_t LSQB = 9;
        constexpr std::int64_t RSQB = 10;
        constexpr std::int64_t COLON = 11;
        constexpr std::int64_t COMMA = 12;
        constexpr std::int64_t SEMI = 13;
        constexpr std::int64_t PLUS = 14;
        constexpr std::int64_t MINUS = 15;
        constexpr std::int64_t STAR = 16;
        constexpr std::int64_t SLASH = 17;
        constexpr std::int64_t LESS = 20;
        constexpr std::int64_t GREATER = 21;
        constexpr std::int64_t EQUAL = 22;
        constexpr std::int64_t DOT = 23;
        constexpr std::int64_t LBRACE = 25;
        constexpr std::int64_t RBRACE = 26;
        constexpr std::int64_t EQEQUAL = 27;
        constexpr std::int64_t NOTEQUAL = 28;
        constexpr std::int64_t LESSEQUAL = 29;
        constexpr std::int64_t GREATEREQUAL = 30;
        constexpr std::int64_t TILDE = 31;
        constexpr std::int64_t DOUBLESTAR = 35;
        constexpr std::int64_t PLUSEQUAL = 36;
        constexpr std::int64_t MINEQUAL = 37;
        constexpr std::int64_t STAREQUAL = 38;
        constexpr std::int64_t AT = 49;
        constexpr std::int64_t RARROW = 51;

        std::vector<std::string> source_lines(const std::string &source)
        {
            std::vector<std::string> lines;
            std::string current;
            for (char ch : source)
            {
                current.push_back(ch);
                if (ch == '\n')
                {
                    lines.push_back(current);
                    current.clear();
                }
            }
            if (!current.empty())
                lines.push_back(current);
            return lines;
        }

        std::string line_at(const std::vector<std::string> &lines, std::size_t line)
        {
            if (line == 0 || line > lines.size())
                return "";
            return lines[line - 1];
        }

        std::int64_t token_code(compiler_internal::TokenKind kind)
        {
            using compiler_internal::TokenKind;
            switch (kind)
            {
            case TokenKind::Integer:
                return NUMBER;
            case TokenKind::String:
            case TokenKind::FString:
                return STRING;
            case TokenKind::Identifier:
            case TokenKind::If:
            case TokenKind::Elif:
            case TokenKind::Else:
            case TokenKind::For:
            case TokenKind::While:
            case TokenKind::Break:
            case TokenKind::Continue:
            case TokenKind::Def:
            case TokenKind::Return:
            case TokenKind::Raise:
            case TokenKind::Import:
            case TokenKind::From:
            case TokenKind::Global:
            case TokenKind::Nonlocal:
            case TokenKind::Try:
            case TokenKind::Except:
            case TokenKind::Finally:
            case TokenKind::As:
            case TokenKind::In:
            case TokenKind::Is:
            case TokenKind::Not:
            case TokenKind::And:
            case TokenKind::Or:
            case TokenKind::Lambda:
            case TokenKind::Class:
            case TokenKind::Del:
            case TokenKind::Pass:
            case TokenKind::TrueLiteral:
            case TokenKind::FalseLiteral:
            case TokenKind::NoneLiteral:
                return NAME;
            case TokenKind::Plus:
                return PLUS;
            case TokenKind::Minus:
                return MINUS;
            case TokenKind::Arrow:
                return RARROW;
            case TokenKind::Star:
                return STAR;
            case TokenKind::DoubleStar:
                return DOUBLESTAR;
            case TokenKind::Slash:
                return SLASH;
            case TokenKind::At:
                return AT;
            case TokenKind::Tilde:
                return TILDE;
            case TokenKind::LeftParen:
                return LPAR;
            case TokenKind::RightParen:
                return RPAR;
            case TokenKind::Comma:
                return COMMA;
            case TokenKind::Dot:
                return DOT;
            case TokenKind::LeftBracket:
                return LSQB;
            case TokenKind::RightBracket:
                return RSQB;
            case TokenKind::LeftBrace:
                return LBRACE;
            case TokenKind::RightBrace:
                return RBRACE;
            case TokenKind::Colon:
                return COLON;
            case TokenKind::Semicolon:
                return SEMI;
            case TokenKind::Equal:
                return EQUAL;
            case TokenKind::PlusEqual:
                return PLUSEQUAL;
            case TokenKind::MinusEqual:
                return MINEQUAL;
            case TokenKind::StarEqual:
                return STAREQUAL;
            case TokenKind::EqualEqual:
                return EQEQUAL;
            case TokenKind::BangEqual:
                return NOTEQUAL;
            case TokenKind::Less:
                return LESS;
            case TokenKind::LessEqual:
                return LESSEQUAL;
            case TokenKind::Greater:
                return GREATER;
            case TokenKind::GreaterEqual:
                return GREATEREQUAL;
            case TokenKind::Indent:
                return INDENT;
            case TokenKind::Dedent:
                return DEDENT;
            case TokenKind::Newline:
                return NEWLINE;
            case TokenKind::End:
                return ENDMARKER;
            }
            return ENDMARKER;
        }

        Object *tokenize_iterator(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "TokenizerIter", args);
            require_positional_count(&runtime, "TokenizerIter", args, 1);
            auto *source = dynamic_cast<StrObject *>(args.positional[0]);
            if (!source)
                raise_type_error(&runtime, "tokenizeriter() argument 'source' must be str");

            std::vector<compiler_internal::Token> tokens =
                compiler_internal::lex_source(runtime, source->value, "<string>");
            std::vector<std::string> lines = source_lines(source->value);

            std::vector<Object *> result;
            result.reserve(tokens.size());
            std::vector<Rooted<Object>> roots;
            roots.reserve(tokens.size() * 8);
            for (const compiler_internal::Token &token : tokens)
            {
                Object *lexeme = runtime.new_str(token.lexeme);
                Object *kind = runtime.new_int(token_code(token.kind));
                Object *start_line = runtime.new_int(static_cast<std::int64_t>(token.line));
                Object *end_line = runtime.new_int(static_cast<std::int64_t>(token.line));
                std::size_t start_column = token.column == 0 ? 0 : token.column - 1;
                if (token.kind == compiler_internal::TokenKind::End)
                    start_column = 0;
                Object *start_col = runtime.new_int(static_cast<std::int64_t>(start_column));
                Object *end_col = runtime.new_int(static_cast<std::int64_t>(start_column + token.lexeme.size()));
                Object *line = runtime.new_str(line_at(lines, token.line));
                roots.push_back(runtime.root(lexeme));
                roots.push_back(runtime.root(kind));
                roots.push_back(runtime.root(start_line));
                roots.push_back(runtime.root(end_line));
                roots.push_back(runtime.root(start_col));
                roots.push_back(runtime.root(end_col));
                roots.push_back(runtime.root(line));
                Object *entry = runtime.new_tuple({lexeme, kind, start_line, end_line, start_col, end_col, line});
                roots.push_back(runtime.root(entry));
                result.push_back(entry);
            }
            return runtime.new_list(std::move(result));
        }

        constexpr NativeModuleFunctionDef tokenize_methods[] = {
            {
                "TokenizerIter",
                tokenize_iterator,
                "Return tokenizer records for source text.",
                "($module, source, /)",
            },
        };

        constexpr NativeModuleDef tokenize_module = {
            "_tokenize",
            "Low-level tokenizer substrate used by the script tokenize module.",
            tokenize_methods,
        };
    }

    void initialize_tokenize_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, tokenize_module);
    }
}
