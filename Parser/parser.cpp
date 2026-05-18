#include "cxxpython/compiler_internal.hpp"

#include <sstream>

namespace cxxpython::compiler_internal
{
    namespace
    {
        class Parser final
        {
        public:
            Parser(Runtime &runtime, const std::vector<Token> &tokens, std::string filename)
                : runtime_(runtime), tokens_(tokens), filename_(std::move(filename))
            {
            }

            std::unique_ptr<Expr> parse_eval_input()
            {
                skip_newlines();
                std::unique_ptr<Expr> expression = parse_expression_only();
                skip_newlines();
                if (peek().kind != TokenKind::End)
                    raise_at(peek(), "unexpected token after expression");
                return expression;
            }

            Module parse_file_input()
            {
                Module module;
                skip_newlines();
                while (peek().kind != TokenKind::End)
                {
                    ParsedStatement parsed = parse_statement();
                    const bool compound = parsed.compound;
                    module.statements.push_back(std::move(parsed.statement));
                    if (peek().kind == TokenKind::Newline)
                    {
                        skip_newlines();
                    }
                    else if (compound)
                    {
                        continue;
                    }
                    else if (peek().kind != TokenKind::End)
                    {
                        raise_at(peek(), "expected newline after statement");
                    }
                }
                return module;
            }

        private:
            struct ParsedStatement
            {
                std::unique_ptr<Stmt> statement;
                bool compound;
            };

            ParsedStatement parse_statement()
            {
                if (peek().kind == TokenKind::If)
                    return parse_if_statement();

                std::unique_ptr<Expr> target_or_expression = parse_expression_only();
                if (peek().kind == TokenKind::Equal)
                {
                    const Token &equals = advance();
                    std::unique_ptr<Expr> value = parse_expression_only();
                    std::unique_ptr<AssignTarget> target =
                        make_assignment_target(std::move(target_or_expression), equals);
                    return ParsedStatement{
                        std::make_unique<AssignStmt>(std::move(target), std::move(value)),
                        false,
                    };
                }
                return ParsedStatement{
                    std::make_unique<ExprStmt>(std::move(target_or_expression)),
                    false,
                };
            }

            ParsedStatement parse_if_statement()
            {
                advance();
                std::unique_ptr<Expr> condition = parse_expression_only();
                consume(TokenKind::Colon, "expected ':' after if condition");
                consume(TokenKind::Newline, "expected newline after if condition");
                std::vector<std::unique_ptr<Stmt>> then_body = parse_block();
                std::vector<std::unique_ptr<Stmt>> else_body;

                if (peek().kind == TokenKind::Else)
                {
                    advance();
                    consume(TokenKind::Colon, "expected ':' after else");
                    consume(TokenKind::Newline, "expected newline after else");
                    else_body = parse_block();
                }

                return ParsedStatement{
                    std::make_unique<IfStmt>(
                        std::move(condition),
                        std::move(then_body),
                        std::move(else_body)),
                    true,
                };
            }

            std::vector<std::unique_ptr<Stmt>> parse_block()
            {
                if (peek().kind != TokenKind::Indent)
                    raise_at(peek(), "expected indented block");
                advance();

                std::vector<std::unique_ptr<Stmt>> body;
                skip_newlines();
                while (peek().kind != TokenKind::Dedent && peek().kind != TokenKind::End)
                {
                    ParsedStatement parsed = parse_statement();
                    const bool compound = parsed.compound;
                    body.push_back(std::move(parsed.statement));
                    if (peek().kind == TokenKind::Newline)
                        skip_newlines();
                    else if (compound)
                        continue;
                    else if (peek().kind != TokenKind::Dedent && peek().kind != TokenKind::End)
                        raise_at(peek(), "expected newline after statement");
                }
                consume(TokenKind::Dedent, "expected dedent after block");
                if (body.empty())
                    raise_at(previous(), "expected indented block");
                return body;
            }

            std::unique_ptr<AssignTarget> make_assignment_target(std::unique_ptr<Expr> expression,
                                                                 const Token &equals) const
            {
                if (auto *target = dynamic_cast<NameExpr *>(expression.get()))
                    return std::make_unique<NameTarget>(target->value);
                if (auto *target = dynamic_cast<AttributeExpr *>(expression.get()))
                {
                    return std::make_unique<AttributeTarget>(
                        std::move(target->object),
                        target->name);
                }
                if (auto *target = dynamic_cast<SubscriptExpr *>(expression.get()))
                {
                    return std::make_unique<SubscriptTarget>(
                        std::move(target->object),
                        std::move(target->key));
                }
                raise_at(equals, "invalid assignment target");
            }

            std::unique_ptr<Expr> parse_expression_only()
            {
                return parse_comparison();
            }

            std::unique_ptr<Expr> parse_comparison()
            {
                std::unique_ptr<Expr> left = parse_sum();
                if (peek().kind != TokenKind::EqualEqual && peek().kind != TokenKind::BangEqual)
                    return left;

                const Token &op_token = advance();
                std::unique_ptr<Expr> right = parse_sum();
                if (peek().kind == TokenKind::EqualEqual || peek().kind == TokenKind::BangEqual)
                    raise_at(peek(), "chained comparisons are not implemented");

                const RichCompareOp op = op_token.kind == TokenKind::EqualEqual
                                             ? RichCompareOp::Equal
                                             : RichCompareOp::NotEqual;
                return std::make_unique<CompareExpr>(std::move(left), op, std::move(right));
            }

            std::unique_ptr<Expr> parse_sum()
            {
                std::unique_ptr<Expr> left = parse_term();
                while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus)
                {
                    const TokenKind op_kind = advance().kind;
                    std::unique_ptr<Expr> right = parse_term();
                    const BinaryOp op = op_kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract;
                    left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
                }
                return left;
            }

            std::unique_ptr<Expr> parse_term()
            {
                std::unique_ptr<Expr> left = parse_unary();
                while (peek().kind == TokenKind::Star)
                {
                    advance();
                    std::unique_ptr<Expr> right = parse_unary();
                    left = std::make_unique<BinaryExpr>(std::move(left), BinaryOp::Multiply, std::move(right));
                }
                return left;
            }

            std::unique_ptr<Expr> parse_unary()
            {
                if (peek().kind == TokenKind::Plus ||
                    peek().kind == TokenKind::Minus ||
                    peek().kind == TokenKind::Tilde)
                {
                    const TokenKind op_kind = advance().kind;
                    UnaryOp op = UnaryOp::Invert;
                    if (op_kind == TokenKind::Plus)
                        op = UnaryOp::Positive;
                    else if (op_kind == TokenKind::Minus)
                        op = UnaryOp::Negative;
                    return std::make_unique<UnaryExpr>(op, parse_unary());
                }
                return parse_postfix();
            }

            std::unique_ptr<Expr> parse_postfix()
            {
                std::unique_ptr<Expr> expression = parse_primary();
                while (peek().kind == TokenKind::LeftParen ||
                       peek().kind == TokenKind::Dot ||
                       peek().kind == TokenKind::LeftBracket)
                {
                    if (peek().kind == TokenKind::LeftParen)
                    {
                        advance();
                        std::vector<std::unique_ptr<Expr>> args;
                        if (peek().kind != TokenKind::RightParen)
                        {
                            while (true)
                            {
                                args.push_back(parse_expression_only());
                                if (peek().kind != TokenKind::Comma)
                                    break;
                                advance();
                                if (peek().kind == TokenKind::RightParen)
                                    break;
                            }
                        }
                        consume(TokenKind::RightParen, "expected ')' after arguments");
                        expression = std::make_unique<CallExpr>(std::move(expression), std::move(args));
                        continue;
                    }

                    if (peek().kind == TokenKind::LeftBracket)
                    {
                        advance();
                        std::unique_ptr<Expr> key = parse_expression_only();
                        consume(TokenKind::RightBracket, "expected ']' after subscript");
                        expression = std::make_unique<SubscriptExpr>(std::move(expression), std::move(key));
                        continue;
                    }

                    advance();
                    if (peek().kind != TokenKind::Identifier)
                        raise_at(peek(), "expected attribute name");
                    expression = std::make_unique<AttributeExpr>(std::move(expression), advance().lexeme);
                }
                return expression;
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
                case TokenKind::Minus:
                case TokenKind::Star:
                case TokenKind::Tilde:
                case TokenKind::LeftParen:
                case TokenKind::RightParen:
                case TokenKind::Comma:
                case TokenKind::Dot:
                case TokenKind::LeftBracket:
                case TokenKind::RightBracket:
                case TokenKind::Colon:
                case TokenKind::Equal:
                case TokenKind::EqualEqual:
                case TokenKind::BangEqual:
                case TokenKind::If:
                case TokenKind::Else:
                case TokenKind::Indent:
                case TokenKind::Dedent:
                    raise_at(token, "expected expression");
                }
                raise_at(token, "expected expression");
            }

            void consume(TokenKind kind, const std::string &message)
            {
                if (peek().kind != kind)
                    raise_at(peek(), message);
                advance();
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

            const Token &advance()
            {
                return tokens_[index_++];
            }

            const Token &previous() const
            {
                return tokens_[index_ - 1];
            }

            [[noreturn]] void raise_at(const Token &token, const std::string &message) const
            {
                std::ostringstream out;
                out << filename_ << ":" << token.line << ":" << token.column << ": " << message;
                raise_syntax_error(&runtime_, out.str());
            }

            Runtime &runtime_;
            const std::vector<Token> &tokens_;
            std::string filename_;
            std::size_t index_ = 0;
        };
    }

    std::unique_ptr<Expr> parse_expression(Runtime &runtime,
                                           const std::vector<Token> &tokens,
                                           std::string filename)
    {
        Parser parser(runtime, tokens, std::move(filename));
        return parser.parse_eval_input();
    }

    Module parse_module(Runtime &runtime, const std::vector<Token> &tokens, std::string filename)
    {
        Parser parser(runtime, tokens, std::move(filename));
        return parser.parse_file_input();
    }
}
