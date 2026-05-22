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
                    append_statement_with_inline_tail(module.statements, std::move(parsed));
                    if (peek().kind == TokenKind::Newline)
                    {
                        skip_newlines();
                    }
                    else if (compound)
                    {
                        continue;
                    }
                    else if (starts_statement())
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

            class FunctionDepthGuard final
            {
            public:
                explicit FunctionDepthGuard(std::size_t &depth)
                    : depth_(depth)
                {
                    ++depth_;
                }

                ~FunctionDepthGuard()
                {
                    --depth_;
                }

                FunctionDepthGuard(const FunctionDepthGuard &) = delete;
                FunctionDepthGuard &operator=(const FunctionDepthGuard &) = delete;

            private:
                std::size_t &depth_;
            };

            class ClassDepthGuard final
            {
            public:
                explicit ClassDepthGuard(std::size_t &depth)
                    : depth_(depth)
                {
                    ++depth_;
                }

                ~ClassDepthGuard()
                {
                    --depth_;
                }

                ClassDepthGuard(const ClassDepthGuard &) = delete;
                ClassDepthGuard &operator=(const ClassDepthGuard &) = delete;

            private:
                std::size_t &depth_;
            };

            class LoopDepthGuard final
            {
            public:
                explicit LoopDepthGuard(std::size_t &depth)
                    : depth_(depth)
                {
                    ++depth_;
                }

                ~LoopDepthGuard()
                {
                    --depth_;
                }

                LoopDepthGuard(const LoopDepthGuard &) = delete;
                LoopDepthGuard &operator=(const LoopDepthGuard &) = delete;

            private:
                std::size_t &depth_;
            };

            ParsedStatement parse_statement()
            {
                std::vector<std::unique_ptr<Expr>> decorators = parse_decorators();
                if (!decorators.empty())
                {
                    if (peek().kind == TokenKind::Def)
                        return parse_function_def_statement(std::move(decorators));
                    if (peek().kind == TokenKind::Class)
                        return parse_class_def_statement(std::move(decorators));
                    raise_at(peek(), "expected function or class definition after decorator");
                }

                if (peek().kind == TokenKind::If)
                    return parse_if_statement();
                if (peek().kind == TokenKind::For)
                    return parse_for_statement();
                if (peek().kind == TokenKind::While)
                    return parse_while_statement();
                if (peek().kind == TokenKind::Try)
                    return parse_try_statement();
                if (peek().kind == TokenKind::Def)
                    return parse_function_def_statement({});
                if (peek().kind == TokenKind::Class)
                    return parse_class_def_statement({});
                return parse_simple_statement();
            }

            ParsedStatement parse_simple_statement()
            {
                if (peek().kind == TokenKind::Del)
                    return parse_delete_statement();
                if (peek().kind == TokenKind::Return)
                    return parse_return_statement();
                if (peek().kind == TokenKind::Raise)
                    return parse_raise_statement();
                if (peek().kind == TokenKind::Import)
                    return parse_import_statement();
                if (peek().kind == TokenKind::From)
                    return parse_from_import_statement();
                if (peek().kind == TokenKind::Global)
                    return parse_global_statement();
                if (peek().kind == TokenKind::Nonlocal)
                    return parse_nonlocal_statement();
                if (peek().kind == TokenKind::Break)
                    return parse_break_statement();
                if (peek().kind == TokenKind::Continue)
                    return parse_continue_statement();
                if (peek().kind == TokenKind::Pass)
                    return parse_pass_statement();

                const std::size_t line = peek().line;
                std::unique_ptr<Expr> target_or_expression = parse_expression_only();
                if (peek().kind == TokenKind::Equal)
                {
                    const Token &equals = advance();
                    std::unique_ptr<Expr> value = parse_expression_only();
                    std::unique_ptr<AssignTarget> target =
                        make_assignment_target(std::move(target_or_expression), equals);
                    auto statement = std::make_unique<AssignStmt>(std::move(target), std::move(value));
                    statement->line = line;
                    return ParsedStatement{
                        std::move(statement),
                        false,
                    };
                }
                auto statement = std::make_unique<ExprStmt>(std::move(target_or_expression));
                statement->line = line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            void append_statement_with_inline_tail(std::vector<std::unique_ptr<Stmt>> &statements,
                                                   ParsedStatement parsed)
            {
                const bool compound = parsed.compound;
                statements.push_back(std::move(parsed.statement));
                if (compound && peek().kind == TokenKind::Semicolon)
                    raise_at(peek(), "compound statements cannot be chained with ';'");
                while (peek().kind == TokenKind::Semicolon)
                {
                    advance();
                    if (peek().kind == TokenKind::Newline ||
                        peek().kind == TokenKind::Dedent ||
                        peek().kind == TokenKind::End)
                        break;
                    statements.push_back(parse_simple_statement().statement);
                }
            }

            std::vector<std::unique_ptr<Stmt>> parse_suite(const char *newline_message)
            {
                if (peek().kind == TokenKind::Newline)
                {
                    advance();
                    return parse_block();
                }

                if (peek().kind == TokenKind::Dedent || peek().kind == TokenKind::End)
                    raise_at(peek(), newline_message);

                std::vector<std::unique_ptr<Stmt>> body;
                body.push_back(parse_simple_statement().statement);
                while (peek().kind == TokenKind::Semicolon)
                {
                    advance();
                    if (peek().kind == TokenKind::Newline ||
                        peek().kind == TokenKind::Dedent ||
                        peek().kind == TokenKind::End)
                        break;
                    body.push_back(parse_simple_statement().statement);
                }
                if (peek().kind == TokenKind::Newline)
                    advance();
                return body;
            }

            std::vector<std::unique_ptr<Expr>> parse_decorators()
            {
                std::vector<std::unique_ptr<Expr>> decorators;
                while (peek().kind == TokenKind::At)
                {
                    advance();
                    decorators.push_back(parse_expression_only());
                    consume(TokenKind::Newline, "expected newline after decorator");
                }
                return decorators;
            }

            ParsedStatement parse_delete_statement()
            {
                const Token &del_token = advance();
                std::unique_ptr<Expr> target_expression = parse_expression_only();
                std::unique_ptr<AssignTarget> target =
                    make_assignment_target(std::move(target_expression), del_token);
                auto statement = std::make_unique<DeleteStmt>(std::move(target));
                statement->line = del_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_return_statement()
            {
                const Token &return_token = advance();
                if (function_depth_ == 0)
                    raise_at(return_token, "'return' outside function");
                std::unique_ptr<Expr> value;
                if (peek().kind != TokenKind::Newline &&
                    peek().kind != TokenKind::Dedent &&
                    peek().kind != TokenKind::End)
                    value = parse_expression_only();
                auto statement = std::make_unique<ReturnStmt>(std::move(value));
                statement->line = return_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_raise_statement()
            {
                const Token &raise_token = advance();
                if (peek().kind == TokenKind::Newline ||
                    peek().kind == TokenKind::Dedent ||
                    peek().kind == TokenKind::End)
                    raise_at(raise_token, "bare raise is not supported yet");
                auto statement = std::make_unique<RaiseStmt>(parse_expression_only());
                statement->line = raise_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_global_statement()
            {
                const Token &global_token = advance();
                auto statement = std::make_unique<GlobalStmt>(parse_name_list("global"));
                statement->line = global_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_nonlocal_statement()
            {
                const Token &nonlocal_token = advance();
                if (function_depth_ == 0)
                    raise_at(nonlocal_token, "nonlocal declaration not allowed at module level");
                auto statement = std::make_unique<NonlocalStmt>(parse_name_list("nonlocal"));
                statement->line = nonlocal_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_break_statement()
            {
                const Token &break_token = advance();
                if (loop_depth_ == 0)
                    raise_at(break_token, "'break' outside loop");
                auto statement = std::make_unique<BreakStmt>();
                statement->line = break_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_continue_statement()
            {
                const Token &continue_token = advance();
                if (loop_depth_ == 0)
                    raise_at(continue_token, "'continue' outside loop");
                auto statement = std::make_unique<ContinueStmt>();
                statement->line = continue_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_pass_statement()
            {
                const Token &pass_token = advance();
                auto statement = std::make_unique<PassStmt>();
                statement->line = pass_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_import_statement()
            {
                const Token &import_token = advance();
                std::vector<ImportAlias> aliases;
                while (true)
                {
                    std::string name = parse_dotted_name("expected module name after import");
                    std::string binding = import_binding_for(name);
                    bool bind_full_module = false;
                    if (peek().kind == TokenKind::As)
                    {
                        advance();
                        if (peek().kind != TokenKind::Identifier)
                            raise_at(peek(), "expected name after import alias");
                        binding = advance().lexeme;
                        bind_full_module = true;
                    }

                    for (const ImportAlias &existing : aliases)
                    {
                        if (existing.binding == binding)
                            raise_at(previous(), "duplicate binding in import statement");
                    }
                    aliases.emplace_back(std::move(name), std::move(binding), bind_full_module);
                    if (peek().kind != TokenKind::Comma)
                        break;
                    advance();
                }
                auto statement = std::make_unique<ImportStmt>(std::move(aliases));
                statement->line = import_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            ParsedStatement parse_from_import_statement()
            {
                const Token &from_token = advance();
                std::string module = parse_dotted_name("expected module name after from");
                consume(TokenKind::Import, "expected import after from module name");

                std::vector<ImportAlias> aliases;
                bool import_star = false;
                if (peek().kind == TokenKind::Star)
                {
                    if (function_depth_ != 0 || class_depth_ != 0)
                        raise_at(peek(), "import * only allowed at module level");
                    advance();
                    import_star = true;
                }
                while (true)
                {
                    if (import_star)
                        break;
                    if (peek().kind != TokenKind::Identifier)
                        raise_at(peek(), "expected name in from import statement");
                    std::string name = advance().lexeme;
                    std::string binding = name;
                    if (peek().kind == TokenKind::As)
                    {
                        advance();
                        if (peek().kind != TokenKind::Identifier)
                            raise_at(peek(), "expected name after import alias");
                        binding = advance().lexeme;
                    }
                    for (const ImportAlias &existing : aliases)
                    {
                        if (existing.binding == binding)
                            raise_at(previous(), "duplicate binding in from import statement");
                    }
                    aliases.emplace_back(std::move(name), std::move(binding));
                    if (peek().kind != TokenKind::Comma)
                        break;
                    advance();
                }
                auto statement = std::make_unique<FromImportStmt>(
                    std::move(module),
                    std::move(aliases),
                    import_star);
                statement->line = from_token.line;
                return ParsedStatement{
                    std::move(statement),
                    false,
                };
            }

            std::string parse_dotted_name(const char *message)
            {
                if (peek().kind != TokenKind::Identifier)
                    raise_at(peek(), message);
                std::string name = advance().lexeme;
                while (peek().kind == TokenKind::Dot)
                {
                    advance();
                    if (peek().kind != TokenKind::Identifier)
                        raise_at(peek(), "expected identifier after '.'");
                    name += ".";
                    name += advance().lexeme;
                }
                return name;
            }

            std::string import_binding_for(std::string_view module_name)
            {
                const std::size_t dot = module_name.find('.');
                if (dot == std::string_view::npos)
                    return std::string(module_name);
                return std::string(module_name.substr(0, dot));
            }

            std::vector<std::string> parse_name_list(const char *statement_name)
            {
                std::vector<std::string> names;
                while (true)
                {
                    if (peek().kind != TokenKind::Identifier)
                    {
                        std::ostringstream message;
                        message << "expected name after " << statement_name;
                        raise_at(peek(), message.str());
                    }
                    std::string name = advance().lexeme;
                    for (const std::string &existing : names)
                    {
                        if (existing == name)
                        {
                            std::ostringstream message;
                            message << "duplicate name in " << statement_name << " statement";
                            raise_at(previous(), message.str());
                        }
                    }
                    names.push_back(std::move(name));
                    if (peek().kind != TokenKind::Comma)
                        break;
                    advance();
                }
                return names;
            }

            ParsedStatement parse_try_statement()
            {
                const Token &try_token = advance();
                consume(TokenKind::Colon, "expected ':' after try");
                std::vector<std::unique_ptr<Stmt>> body = parse_suite("expected newline after try");
                std::vector<ExceptHandler> handlers;
                std::vector<std::unique_ptr<Stmt>> finally_body;

                while (peek().kind == TokenKind::Except)
                {
                    advance();
                    std::unique_ptr<Expr> exception_type;
                    std::optional<std::string> exception_name;
                    if (peek().kind != TokenKind::Colon)
                    {
                        exception_type = parse_expression_only();
                        if (peek().kind == TokenKind::As)
                        {
                            advance();
                            if (peek().kind != TokenKind::Identifier)
                                raise_at(peek(), "expected name after 'as'");
                            exception_name = advance().lexeme;
                        }
                    }
                    consume(TokenKind::Colon, "expected ':' after except");
                    handlers.emplace_back(std::move(exception_type),
                                          std::move(exception_name),
                                          parse_suite("expected newline after except"));
                }

                if (peek().kind == TokenKind::Finally)
                {
                    advance();
                    consume(TokenKind::Colon, "expected ':' after finally");
                    finally_body = parse_suite("expected newline after finally");
                }

                if (handlers.empty() && finally_body.empty())
                    raise_at(peek(), "expected except or finally after try block");

                auto statement = std::make_unique<TryStmt>(
                    std::move(body),
                    std::move(handlers),
                    std::move(finally_body));
                statement->line = try_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            ParsedStatement parse_if_statement()
            {
                const Token &if_token = advance();
                std::unique_ptr<Expr> condition = parse_or_expression();
                consume(TokenKind::Colon, "expected ':' after if condition");
                std::vector<std::unique_ptr<Stmt>> then_body = parse_suite("expected newline after if condition");
                std::vector<std::unique_ptr<Stmt>> else_body;

                if (peek().kind == TokenKind::Elif)
                {
                    ParsedStatement elif_statement = parse_if_statement();
                    else_body.push_back(std::move(elif_statement.statement));
                }
                else if (peek().kind == TokenKind::Else)
                {
                    advance();
                    consume(TokenKind::Colon, "expected ':' after else");
                    else_body = parse_suite("expected newline after else");
                }

                auto statement = std::make_unique<IfStmt>(
                        std::move(condition),
                        std::move(then_body),
                        std::move(else_body));
                statement->line = if_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            ParsedStatement parse_for_statement()
            {
                const Token &for_token = advance();
                if (peek().kind != TokenKind::Identifier)
                    raise_at(peek(), "expected name after for");
                std::string target = advance().lexeme;
                consume(TokenKind::In, "expected 'in' after for target");
                std::unique_ptr<Expr> iterable = parse_expression_only();
                consume(TokenKind::Colon, "expected ':' after for iterable");
                LoopDepthGuard loop_guard(loop_depth_);
                std::vector<std::unique_ptr<Stmt>> body = parse_suite("expected newline after for statement");
                std::vector<std::unique_ptr<Stmt>> else_body;
                if (peek().kind == TokenKind::Else)
                {
                    advance();
                    consume(TokenKind::Colon, "expected ':' after else");
                    else_body = parse_suite("expected newline after else");
                }
                auto statement =
                    std::make_unique<ForStmt>(
                        std::move(target),
                        std::move(iterable),
                        std::move(body),
                        std::move(else_body));
                statement->line = for_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            ParsedStatement parse_while_statement()
            {
                const Token &while_token = advance();
                std::unique_ptr<Expr> condition = parse_or_expression();
                consume(TokenKind::Colon, "expected ':' after while condition");
                LoopDepthGuard loop_guard(loop_depth_);
                std::vector<std::unique_ptr<Stmt>> body = parse_suite("expected newline after while condition");
                std::vector<std::unique_ptr<Stmt>> else_body;
                if (peek().kind == TokenKind::Else)
                {
                    advance();
                    consume(TokenKind::Colon, "expected ':' after else");
                    else_body = parse_suite("expected newline after else");
                }
                auto statement = std::make_unique<WhileStmt>(
                    std::move(condition),
                    std::move(body),
                    std::move(else_body));
                statement->line = while_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            ParsedStatement parse_function_def_statement(std::vector<std::unique_ptr<Expr>> decorators)
            {
                const Token &def_token = advance();
                if (peek().kind != TokenKind::Identifier)
                    raise_at(peek(), "expected function name");
                std::string name = advance().lexeme;
                consume(TokenKind::LeftParen, "expected '(' after function name");
                FunctionParameters params;
                if (peek().kind != TokenKind::RightParen)
                {
                    bool seen_varargs = false;
                    bool seen_varkeywords = false;
                    while (true)
                    {
                        bool is_varargs = false;
                        bool is_varkeywords = false;
                        if (peek().kind == TokenKind::Star)
                        {
                            if (seen_varargs)
                                raise_at(peek(), "duplicate * parameter");
                            is_varargs = true;
                            seen_varargs = true;
                            advance();
                        }
                        else if (peek().kind == TokenKind::DoubleStar)
                        {
                            if (seen_varkeywords)
                                raise_at(peek(), "duplicate ** parameter");
                            is_varkeywords = true;
                            seen_varkeywords = true;
                            advance();
                        }
                        else if (seen_varargs)
                        {
                            raise_at(peek(), "expected keyword-only or ** parameter");
                        }
                        if (peek().kind != TokenKind::Identifier)
                            raise_at(peek(), "expected parameter name");
                        std::string param = advance().lexeme;
                        ensure_unique_parameter(params, param, previous());
                        if (is_varkeywords)
                            params.varkeywords = std::move(param);
                        else if (is_varargs)
                            params.varargs = std::move(param);
                        else
                            params.positional.push_back(std::move(param));
                        if (peek().kind != TokenKind::Comma)
                            break;
                        advance();
                        if (peek().kind == TokenKind::RightParen)
                            break;
                        if (seen_varkeywords)
                            raise_at(peek(), "expected ')' after ** parameter");
                    }
                }
                consume(TokenKind::RightParen, "expected ')' after parameters");
                consume(TokenKind::Colon, "expected ':' after function signature");

                FunctionDepthGuard guard(function_depth_);
                std::vector<std::unique_ptr<Stmt>> body = parse_suite("expected newline after function signature");
                auto statement = std::make_unique<FunctionDefStmt>(
                    std::move(name),
                    std::move(params),
                    std::move(body),
                    std::move(decorators));
                statement->line = def_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            ParsedStatement parse_class_def_statement(std::vector<std::unique_ptr<Expr>> decorators)
            {
                const Token &class_token = advance();
                if (peek().kind != TokenKind::Identifier)
                    raise_at(peek(), "expected class name");
                std::string name = advance().lexeme;
                std::vector<std::unique_ptr<Expr>> bases;
                std::vector<CallArgument> keywords;
                std::vector<std::string> keyword_names;
                bool seen_keyword = false;
                if (peek().kind == TokenKind::LeftParen)
                {
                    advance();
                    if (peek().kind != TokenKind::RightParen)
                    {
                        while (true)
                        {
                            if (peek().kind == TokenKind::Identifier && peek_next().kind == TokenKind::Equal)
                            {
                                seen_keyword = true;
                                std::string keyword = advance().lexeme;
                                advance();
                                for (const std::string &existing : keyword_names)
                                {
                                    if (existing == keyword)
                                        raise_at(previous(), "class keyword repeated");
                                }
                                keyword_names.push_back(keyword);
                                keywords.emplace_back(std::move(keyword), parse_expression_only());
                            }
                            else
                            {
                                if (seen_keyword)
                                    raise_at(peek(), "positional base follows class keyword");
                                bases.push_back(parse_expression_only());
                            }
                            if (peek().kind != TokenKind::Comma)
                                break;
                            advance();
                            if (peek().kind == TokenKind::RightParen)
                                break;
                        }
                    }
                    consume(TokenKind::RightParen, "expected ')' after class bases");
                }
                consume(TokenKind::Colon, "expected ':' after class name");
                ClassDepthGuard guard(class_depth_);
                std::vector<std::unique_ptr<Stmt>> body = parse_suite("expected newline after class definition");
                auto statement = std::make_unique<ClassDefStmt>(
                    std::move(name),
                    std::move(bases),
                    std::move(keywords),
                    std::move(body),
                    std::move(decorators));
                statement->line = class_token.line;
                return ParsedStatement{
                    std::move(statement),
                    true,
                };
            }

            std::vector<std::unique_ptr<Stmt>> parse_block()
            {
                skip_newlines();
                if (peek().kind != TokenKind::Indent)
                    raise_at(peek(), "expected indented block");
                advance();

                std::vector<std::unique_ptr<Stmt>> body;
                skip_newlines();
                while (peek().kind != TokenKind::Dedent && peek().kind != TokenKind::End)
                {
                    ParsedStatement parsed = parse_statement();
                    const bool compound = parsed.compound;
                    append_statement_with_inline_tail(body, std::move(parsed));
                    if (peek().kind == TokenKind::Newline)
                        skip_newlines();
                    else if (compound)
                        continue;
                    else if (starts_statement())
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

            void ensure_unique_parameter(const FunctionParameters &params,
                                         const std::string &name,
                                         const Token &token) const
            {
                for (const std::string &existing : params.positional)
                {
                    if (existing == name)
                        raise_at(token, "duplicate parameter name");
                }
                if (params.varargs && *params.varargs == name)
                    raise_at(token, "duplicate parameter name");
                if (params.varkeywords && *params.varkeywords == name)
                    raise_at(token, "duplicate parameter name");
            }

            std::unique_ptr<Expr> parse_expression_only()
            {
                std::unique_ptr<Expr> body = parse_or_expression();
                if (peek().kind != TokenKind::If)
                    return body;

                advance();
                std::unique_ptr<Expr> condition = parse_or_expression();
                consume(TokenKind::Else, "expected else after conditional expression");
                std::unique_ptr<Expr> else_body = parse_expression_only();
                return std::make_unique<ConditionalExpr>(
                    std::move(body),
                    std::move(condition),
                    std::move(else_body));
            }

            std::unique_ptr<Expr> parse_or_expression()
            {
                std::vector<std::unique_ptr<Expr>> values;
                values.push_back(parse_and_expression());
                while (peek().kind == TokenKind::Or)
                {
                    advance();
                    values.push_back(parse_and_expression());
                }
                if (values.size() == 1)
                    return std::move(values.front());
                return std::make_unique<BoolExpr>(BoolOp::Or, std::move(values));
            }

            std::unique_ptr<Expr> parse_and_expression()
            {
                std::vector<std::unique_ptr<Expr>> values;
                values.push_back(parse_comparison());
                while (peek().kind == TokenKind::And)
                {
                    advance();
                    values.push_back(parse_comparison());
                }
                if (values.size() == 1)
                    return std::move(values.front());
                return std::make_unique<BoolExpr>(BoolOp::And, std::move(values));
            }

            std::unique_ptr<Expr> parse_comparison()
            {
                std::unique_ptr<Expr> left = parse_sum();
                if (!starts_comparison())
                    return left;

                std::vector<Comparison> comparisons;
                while (starts_comparison())
                {
                    ComparisonOp op = comparison_op();
                    comparisons.emplace_back(op, parse_sum());
                }

                return std::make_unique<CompareExpr>(std::move(left), std::move(comparisons));
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
                        std::vector<CallArgument> args;
                        std::vector<std::string> keyword_names;
                        bool seen_keyword = false;
                        if (peek().kind != TokenKind::RightParen)
                        {
                            while (true)
                            {
                                if (peek().kind == TokenKind::Identifier && peek_next().kind == TokenKind::Equal)
                                {
                                    seen_keyword = true;
                                    std::string keyword = advance().lexeme;
                                    advance();
                                    for (const std::string &existing : keyword_names)
                                    {
                                        if (existing == keyword)
                                            raise_at(previous(), "keyword argument repeated");
                                    }
                                    keyword_names.push_back(keyword);
                                    args.emplace_back(std::move(keyword), parse_expression_only());
                                }
                                else
                                {
                                    if (seen_keyword)
                                        raise_at(peek(), "positional argument follows keyword argument");
                                    args.emplace_back(parse_expression_only());
                                }
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
                        std::unique_ptr<Expr> key = parse_subscript_key();
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

            std::unique_ptr<Expr> parse_subscript_key()
            {
                if (peek().kind == TokenKind::Colon)
                {
                    advance();
                    std::unique_ptr<Expr> stop;
                    std::unique_ptr<Expr> step;
                    if (peek().kind != TokenKind::Colon && peek().kind != TokenKind::RightBracket)
                        stop = parse_expression_only();
                    if (peek().kind == TokenKind::Colon)
                    {
                        advance();
                        if (peek().kind != TokenKind::RightBracket)
                            step = parse_expression_only();
                    }
                    return std::make_unique<SliceExpr>(nullptr, std::move(stop), std::move(step));
                }

                std::unique_ptr<Expr> first = parse_expression_only();
                if (peek().kind != TokenKind::Colon)
                    return first;

                advance();
                std::unique_ptr<Expr> stop;
                std::unique_ptr<Expr> step;
                if (peek().kind != TokenKind::Colon && peek().kind != TokenKind::RightBracket)
                    stop = parse_expression_only();
                if (peek().kind == TokenKind::Colon)
                {
                    advance();
                    if (peek().kind != TokenKind::RightBracket)
                        step = parse_expression_only();
                }
                return std::make_unique<SliceExpr>(std::move(first), std::move(stop), std::move(step));
            }

            std::unique_ptr<Expr> parse_parenthesized_expression()
            {
                advance();
                skip_newlines();
                if (peek().kind == TokenKind::RightParen)
                {
                    advance();
                    return std::make_unique<TupleExpr>(std::vector<std::unique_ptr<Expr>>{});
                }

                std::unique_ptr<Expr> first = parse_expression_only();
                if (peek().kind != TokenKind::Comma)
                {
                    consume(TokenKind::RightParen, "expected ')' after expression");
                    return first;
                }

                std::vector<std::unique_ptr<Expr>> items;
                items.push_back(std::move(first));
                while (peek().kind == TokenKind::Comma)
                {
                    advance();
                    skip_newlines();
                    if (peek().kind == TokenKind::RightParen)
                        break;
                    items.push_back(parse_expression_only());
                }
                skip_newlines();
                consume(TokenKind::RightParen, "expected ')' after tuple literal");
                return std::make_unique<TupleExpr>(std::move(items));
            }

            std::unique_ptr<Expr> parse_dict_expression()
            {
                advance();
                std::vector<DictItem> items;
                skip_newlines();
                if (peek().kind == TokenKind::RightBrace)
                {
                    advance();
                    return std::make_unique<DictExpr>(std::move(items));
                }

                while (true)
                {
                    std::unique_ptr<Expr> key = parse_expression_only();
                    skip_newlines();
                    consume(TokenKind::Colon, "expected ':' after dict key");
                    std::unique_ptr<Expr> value = parse_expression_only();
                    items.emplace_back(std::move(key), std::move(value));
                    skip_newlines();
                    if (peek().kind != TokenKind::Comma)
                        break;
                    advance();
                    skip_newlines();
                    if (peek().kind == TokenKind::RightBrace)
                        break;
                }
                skip_newlines();
                consume(TokenKind::RightBrace, "expected '}' after dict literal");
                return std::make_unique<DictExpr>(std::move(items));
            }

            std::unique_ptr<Expr> parse_list_expression()
            {
                advance();
                std::vector<std::unique_ptr<Expr>> items;
                skip_newlines();
                if (peek().kind == TokenKind::RightBracket)
                {
                    advance();
                    return std::make_unique<ListExpr>(std::move(items));
                }

                while (true)
                {
                    items.push_back(parse_expression_only());
                    skip_newlines();
                    if (peek().kind != TokenKind::Comma)
                        break;
                    advance();
                    skip_newlines();
                    if (peek().kind == TokenKind::RightBracket)
                        break;
                }
                skip_newlines();
                consume(TokenKind::RightBracket, "expected ']' after list literal");
                return std::make_unique<ListExpr>(std::move(items));
            }

            std::unique_ptr<Expr> parse_fstring_expression()
            {
                const Token &token = advance();
                std::vector<FStringPart> parts;
                std::string literal;
                const std::string &text = token.lexeme;
                std::size_t index = 0;
                while (index < text.size())
                {
                    const char ch = text[index];
                    if (ch == '{')
                    {
                        if (index + 1 < text.size() && text[index + 1] == '{')
                        {
                            literal.push_back('{');
                            index += 2;
                            continue;
                        }
                        if (!literal.empty())
                        {
                            parts.emplace_back(std::move(literal));
                            literal.clear();
                        }
                        ParsedFStringExpression parsed = parse_fstring_part_expression(text, index, token);
                        if (parsed.expression_source.empty())
                            raise_at(token, "empty expression in f-string");
                        std::vector<Token> expression_tokens =
                            lex_source(runtime_, parsed.expression_source, filename_);
                        parts.emplace_back(parse_expression(runtime_, expression_tokens, filename_),
                                           parsed.conversion,
                                           std::move(parsed.format_spec));
                        index = parsed.next_index;
                        continue;
                    }
                    if (ch == '}')
                    {
                        if (index + 1 < text.size() && text[index + 1] == '}')
                        {
                            literal.push_back('}');
                            index += 2;
                            continue;
                        }
                        raise_at(token, "single '}' is not allowed in f-string");
                    }
                    literal.push_back(ch);
                    ++index;
                }
                if (!literal.empty())
                    parts.emplace_back(std::move(literal));
                return std::make_unique<FStringExpr>(std::move(parts));
            }

            struct ParsedFStringExpression final
            {
                std::string expression_source;
                FStringConversion conversion = FStringConversion::None;
                std::string format_spec;
                std::size_t next_index = 0;
            };

            std::size_t skip_fstring_delimited(const std::string &text,
                                               std::size_t start,
                                               const Token &token) const
            {
                std::size_t nesting = 0;
                char quote = '\0';
                bool escaped = false;
                for (std::size_t index = start; index < text.size(); ++index)
                {
                    const char ch = text[index];
                    if (quote != '\0')
                    {
                        if (escaped)
                        {
                            escaped = false;
                            continue;
                        }
                        if (ch == '\\')
                        {
                            escaped = true;
                            continue;
                        }
                        if (ch == quote)
                            quote = '\0';
                        continue;
                    }
                    if (ch == '\'' || ch == '"')
                    {
                        quote = ch;
                        continue;
                    }
                    if (ch == '(' || ch == '[' || ch == '{')
                    {
                        ++nesting;
                        continue;
                    }
                    if (ch == ')' || ch == ']')
                    {
                        if (nesting == 0)
                            raise_at(token, "unmatched delimiter in f-string expression");
                        --nesting;
                        continue;
                    }
                    if (ch == '}')
                    {
                        if (nesting == 0)
                            return index;
                        --nesting;
                    }
                }
                raise_at(token, "unterminated expression in f-string");
            }

            ParsedFStringExpression parse_fstring_part_expression(const std::string &text,
                                                                  std::size_t opening_brace_index,
                                                                  const Token &token) const
            {
                const std::size_t expression_start = opening_brace_index + 1;
                std::size_t index = expression_start;
                std::size_t nesting = 0;
                char quote = '\0';
                bool escaped = false;
                while (index < text.size())
                {
                    const char ch = text[index];
                    if (quote != '\0')
                    {
                        if (escaped)
                        {
                            escaped = false;
                            ++index;
                            continue;
                        }
                        if (ch == '\\')
                        {
                            escaped = true;
                            ++index;
                            continue;
                        }
                        if (ch == quote)
                            quote = '\0';
                        ++index;
                        continue;
                    }
                    if (ch == '\'' || ch == '"')
                    {
                        quote = ch;
                        ++index;
                        continue;
                    }
                    if (ch == '(' || ch == '[' || ch == '{')
                    {
                        ++nesting;
                        ++index;
                        continue;
                    }
                    if (ch == ')' || ch == ']')
                    {
                        if (nesting == 0)
                            raise_at(token, "unmatched delimiter in f-string expression");
                        --nesting;
                        ++index;
                        continue;
                    }
                    if (ch == '}')
                    {
                        if (nesting == 0)
                            break;
                        --nesting;
                        ++index;
                        continue;
                    }
                    if ((ch == '!' || ch == ':') && nesting == 0)
                        break;
                    ++index;
                }
                if (index >= text.size())
                    raise_at(token, "unterminated expression in f-string");

                ParsedFStringExpression parsed;
                parsed.expression_source = text.substr(expression_start, index - expression_start);
                if (text[index] == '!')
                {
                    ++index;
                    if (index >= text.size())
                        raise_at(token, "unterminated expression in f-string");
                    if (text[index] == 'r')
                        parsed.conversion = FStringConversion::Repr;
                    else if (text[index] == 's')
                        parsed.conversion = FStringConversion::Str;
                    else
                        raise_at(token, "unsupported f-string conversion");
                    ++index;
                }
                if (index < text.size() && text[index] == ':')
                {
                    const std::size_t spec_start = index + 1;
                    const std::size_t spec_end = skip_fstring_delimited(text, spec_start, token);
                    parsed.format_spec = text.substr(spec_start, spec_end - spec_start);
                    index = spec_end;
                }
                if (index >= text.size() || text[index] != '}')
                    raise_at(token, "unterminated expression in f-string");
                parsed.next_index = index + 1;
                return parsed;
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
                case TokenKind::FString:
                    return parse_fstring_expression();
                case TokenKind::TrueLiteral:
                    advance();
                    return std::make_unique<SingletonExpr>(SingletonKind::TrueValue);
                case TokenKind::FalseLiteral:
                    advance();
                    return std::make_unique<SingletonExpr>(SingletonKind::FalseValue);
                case TokenKind::NoneLiteral:
                    advance();
                    return std::make_unique<SingletonExpr>(SingletonKind::NoneValue);
                case TokenKind::Identifier:
                    return std::make_unique<NameExpr>(advance().lexeme);
                case TokenKind::LeftParen:
                    return parse_parenthesized_expression();
                case TokenKind::LeftBracket:
                    return parse_list_expression();
                case TokenKind::LeftBrace:
                    return parse_dict_expression();
                case TokenKind::End:
                case TokenKind::Newline:
                case TokenKind::Plus:
                case TokenKind::Minus:
                case TokenKind::Star:
                case TokenKind::DoubleStar:
                case TokenKind::At:
                case TokenKind::Tilde:
                case TokenKind::RightParen:
                case TokenKind::Comma:
                case TokenKind::Dot:
                case TokenKind::RightBracket:
                case TokenKind::RightBrace:
                case TokenKind::Colon:
                case TokenKind::Semicolon:
                case TokenKind::Equal:
                case TokenKind::EqualEqual:
                case TokenKind::BangEqual:
                case TokenKind::Less:
                case TokenKind::LessEqual:
                case TokenKind::Greater:
                case TokenKind::GreaterEqual:
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
                case TokenKind::Class:
                case TokenKind::Del:
                case TokenKind::Pass:
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

            const Token &peek_next() const
            {
                return tokens_[index_ + 1];
            }

            const Token &advance()
            {
                return tokens_[index_++];
            }

            const Token &previous() const
            {
                return tokens_[index_ - 1];
            }

            bool starts_comparison() const
            {
                const TokenKind kind = peek().kind;
                return kind == TokenKind::EqualEqual ||
                       kind == TokenKind::BangEqual ||
                       kind == TokenKind::Less ||
                       kind == TokenKind::LessEqual ||
                       kind == TokenKind::Greater ||
                       kind == TokenKind::GreaterEqual ||
                       kind == TokenKind::In ||
                       kind == TokenKind::Is ||
                       (kind == TokenKind::Not && peek_next().kind == TokenKind::In);
            }

            bool starts_statement() const
            {
                switch (peek().kind)
                {
                case TokenKind::Integer:
                case TokenKind::String:
                case TokenKind::FString:
                case TokenKind::Identifier:
                case TokenKind::Plus:
                case TokenKind::Minus:
                case TokenKind::Tilde:
                case TokenKind::LeftParen:
                case TokenKind::LeftBracket:
                case TokenKind::LeftBrace:
                case TokenKind::If:
                case TokenKind::For:
                case TokenKind::While:
                case TokenKind::Def:
                case TokenKind::Return:
                case TokenKind::Raise:
                case TokenKind::Import:
                case TokenKind::From:
                case TokenKind::Global:
                case TokenKind::Nonlocal:
                case TokenKind::Try:
                case TokenKind::Class:
                case TokenKind::Del:
                case TokenKind::Pass:
                case TokenKind::Break:
                case TokenKind::Continue:
                case TokenKind::TrueLiteral:
                case TokenKind::FalseLiteral:
                case TokenKind::NoneLiteral:
                    return true;
                case TokenKind::Star:
                case TokenKind::DoubleStar:
                case TokenKind::At:
                case TokenKind::RightParen:
                case TokenKind::Comma:
                case TokenKind::Dot:
                case TokenKind::RightBracket:
                case TokenKind::RightBrace:
                case TokenKind::Colon:
                case TokenKind::Semicolon:
                case TokenKind::Equal:
                case TokenKind::EqualEqual:
                case TokenKind::BangEqual:
                case TokenKind::Less:
                case TokenKind::LessEqual:
                case TokenKind::Greater:
                case TokenKind::GreaterEqual:
                case TokenKind::Elif:
                case TokenKind::Else:
                case TokenKind::Except:
                case TokenKind::Finally:
                case TokenKind::As:
                case TokenKind::In:
                case TokenKind::Is:
                case TokenKind::Not:
                case TokenKind::And:
                case TokenKind::Or:
                case TokenKind::Indent:
                case TokenKind::Dedent:
                case TokenKind::Newline:
                case TokenKind::End:
                    return false;
                }
                return false;
            }

            ComparisonOp comparison_op()
            {
                const Token &token = advance();
                switch (token.kind)
                {
                case TokenKind::EqualEqual:
                    return ComparisonOp::Equal;
                case TokenKind::BangEqual:
                    return ComparisonOp::NotEqual;
                case TokenKind::Less:
                    return ComparisonOp::Less;
                case TokenKind::LessEqual:
                    return ComparisonOp::LessEqual;
                case TokenKind::Greater:
                    return ComparisonOp::Greater;
                case TokenKind::GreaterEqual:
                    return ComparisonOp::GreaterEqual;
                case TokenKind::In:
                    return ComparisonOp::In;
                case TokenKind::Is:
                    if (peek().kind == TokenKind::Not)
                    {
                        advance();
                        return ComparisonOp::IsNot;
                    }
                    return ComparisonOp::Is;
                case TokenKind::Not:
                    if (peek().kind == TokenKind::In)
                    {
                        advance();
                        return ComparisonOp::NotIn;
                    }
                    break;
                case TokenKind::Integer:
                case TokenKind::String:
                case TokenKind::FString:
                case TokenKind::Identifier:
                case TokenKind::Plus:
                case TokenKind::Minus:
                case TokenKind::Star:
                case TokenKind::DoubleStar:
                case TokenKind::At:
                case TokenKind::Tilde:
                case TokenKind::LeftParen:
                case TokenKind::RightParen:
                case TokenKind::Comma:
                case TokenKind::Dot:
                case TokenKind::LeftBracket:
                case TokenKind::RightBracket:
                case TokenKind::LeftBrace:
                case TokenKind::RightBrace:
                case TokenKind::Colon:
                case TokenKind::Semicolon:
                case TokenKind::Equal:
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
                case TokenKind::And:
                case TokenKind::Or:
                case TokenKind::Class:
                case TokenKind::Del:
                case TokenKind::Pass:
                case TokenKind::TrueLiteral:
                case TokenKind::FalseLiteral:
                case TokenKind::NoneLiteral:
                case TokenKind::Indent:
                case TokenKind::Dedent:
                case TokenKind::Newline:
                case TokenKind::End:
                    break;
                }
                raise_at(token, "expected comparison operator");
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
            std::size_t function_depth_ = 0;
            std::size_t class_depth_ = 0;
            std::size_t loop_depth_ = 0;
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
