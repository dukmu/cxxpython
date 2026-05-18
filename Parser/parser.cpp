#include "cxxpython/compiler_internal.hpp"

namespace cxxpython::compiler_internal
{
    namespace
    {
        class Parser final
        {
        public:
            Parser(Runtime &runtime, const std::vector<Token> &tokens)
                : runtime_(runtime), tokens_(tokens)
            {
            }

            std::unique_ptr<Expr> parse_eval_input()
            {
                skip_newlines();
                std::unique_ptr<Expr> expression = parse_expression_only();
                skip_newlines();
                if (peek().kind != TokenKind::End)
                    raise_syntax_error(&runtime_, "unexpected token after expression");
                return expression;
            }

            Module parse_file_input()
            {
                Module module;
                skip_newlines();
                while (peek().kind != TokenKind::End)
                {
                    module.statements.push_back(parse_statement());
                    if (peek().kind == TokenKind::Newline)
                    {
                        skip_newlines();
                    }
                    else if (peek().kind != TokenKind::End)
                    {
                        raise_syntax_error(&runtime_, "expected newline after statement");
                    }
                }
                return module;
            }

        private:
            std::unique_ptr<Stmt> parse_statement()
            {
                if (peek().kind == TokenKind::Identifier && peek_next().kind == TokenKind::Equal)
                {
                    std::string target = advance().lexeme;
                    advance();
                    std::unique_ptr<Expr> value = parse_expression_only();
                    return std::make_unique<AssignStmt>(std::move(target), std::move(value));
                }
                return std::make_unique<ExprStmt>(parse_expression_only());
            }

            std::unique_ptr<Expr> parse_expression_only()
            {
                std::unique_ptr<Expr> left = parse_primary();
                while (peek().kind == TokenKind::Plus)
                {
                    advance();
                    std::unique_ptr<Expr> right = parse_primary();
                    left = std::make_unique<BinaryAddExpr>(std::move(left), std::move(right));
                }
                return left;
            }

            std::unique_ptr<Expr> parse_primary()
            {
                const Token &token = peek();
                switch (token.kind)
                {
                case TokenKind::Integer:
                    return std::make_unique<IntegerExpr>(advance().int_value);
                case TokenKind::String:
                    return std::make_unique<StringExpr>(advance().lexeme);
                case TokenKind::Identifier:
                    return std::make_unique<NameExpr>(advance().lexeme);
                case TokenKind::End:
                case TokenKind::Newline:
                case TokenKind::Plus:
                case TokenKind::Equal:
                    raise_syntax_error(&runtime_, "expected expression");
                }
                raise_syntax_error(&runtime_, "expected expression");
            }

            void skip_newlines()
            {
                while (peek().kind == TokenKind::Newline)
                    advance();
            }

            const Token &peek() const
            {
                return tokens_[index_];
            }

            const Token &peek_next() const
            {
                if (index_ + 1 >= tokens_.size())
                    return tokens_.back();
                return tokens_[index_ + 1];
            }

            const Token &advance()
            {
                return tokens_[index_++];
            }

            Runtime &runtime_;
            const std::vector<Token> &tokens_;
            std::size_t index_ = 0;
        };
    }

    std::unique_ptr<Expr> parse_expression(Runtime &runtime, const std::vector<Token> &tokens)
    {
        Parser parser(runtime, tokens);
        return parser.parse_eval_input();
    }

    Module parse_module(Runtime &runtime, const std::vector<Token> &tokens)
    {
        Parser parser(runtime, tokens);
        return parser.parse_file_input();
    }
}
