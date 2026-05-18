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

            std::unique_ptr<Expr> parse_expression()
            {
                std::unique_ptr<Expr> left = parse_primary();
                while (peek().kind == TokenKind::Plus)
                {
                    advance();
                    std::unique_ptr<Expr> right = parse_primary();
                    left = std::make_unique<BinaryAddExpr>(std::move(left), std::move(right));
                }
                if (peek().kind != TokenKind::End)
                    raise_syntax_error(&runtime_, "unexpected token after expression");
                return left;
            }

        private:
            std::unique_ptr<Expr> parse_primary()
            {
                const Token &token = peek();
                switch (token.kind)
                {
                case TokenKind::Integer:
                    return parse_integer();
                case TokenKind::String:
                    return parse_string();
                case TokenKind::Identifier:
                    return parse_name();
                case TokenKind::End:
                    raise_syntax_error(&runtime_, "expected expression");
                case TokenKind::Plus:
                    raise_syntax_error(&runtime_, "expected expression");
                }
                raise_syntax_error(&runtime_, "expected expression");
            }

            std::unique_ptr<Expr> parse_integer()
            {
                return std::make_unique<IntegerExpr>(advance().int_value);
            }

            std::unique_ptr<Expr> parse_string()
            {
                return std::make_unique<StringExpr>(advance().lexeme);
            }

            std::unique_ptr<Expr> parse_name()
            {
                return std::make_unique<NameExpr>(advance().lexeme);
            }

            const Token &peek() const
            {
                return tokens_[index_];
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
        return parser.parse_expression();
    }
}
