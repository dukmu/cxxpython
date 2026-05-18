#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython::compiler_internal
{
    struct IntegerExpr;
    struct StringExpr;
    struct BinaryAddExpr;
    struct NameExpr;
    struct CallExpr;
    struct ExprStmt;
    struct AssignStmt;

    struct ExprVisitor
    {
        virtual ~ExprVisitor() = default;
        virtual void visit(const IntegerExpr &expression) = 0;
        virtual void visit(const StringExpr &expression) = 0;
        virtual void visit(const BinaryAddExpr &expression) = 0;
        virtual void visit(const NameExpr &expression) = 0;
        virtual void visit(const CallExpr &expression) = 0;
    };

    struct StmtVisitor
    {
        virtual ~StmtVisitor() = default;
        virtual void visit(const ExprStmt &statement) = 0;
        virtual void visit(const AssignStmt &statement) = 0;
    };

    enum class TokenKind
    {
        Integer,
        String,
        Identifier,
        Plus,
        LeftParen,
        RightParen,
        Comma,
        Equal,
        Newline,
        End,
    };

    struct Token
    {
        TokenKind kind;
        std::string lexeme;
        std::int64_t int_value = 0;
        std::size_t line = 1;
        std::size_t column = 1;
    };

    std::vector<Token> lex_source(Runtime &runtime, std::string_view source, std::string filename);

    struct Expr
    {
        virtual ~Expr() = default;
        virtual void accept(ExprVisitor &visitor) const = 0;
    };

    struct IntegerExpr final : Expr
    {
        explicit IntegerExpr(std::int64_t value_in)
            : value(value_in)
        {
        }

        std::int64_t value;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct StringExpr final : Expr
    {
        explicit StringExpr(std::string value_in)
            : value(std::move(value_in))
        {
        }

        std::string value;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct BinaryAddExpr final : Expr
    {
        BinaryAddExpr(std::unique_ptr<Expr> left_in, std::unique_ptr<Expr> right_in)
            : left(std::move(left_in)), right(std::move(right_in))
        {
        }

        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct NameExpr final : Expr
    {
        explicit NameExpr(std::string value_in)
            : value(std::move(value_in))
        {
        }

        std::string value;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct CallExpr final : Expr
    {
        CallExpr(std::unique_ptr<Expr> callee_in, std::vector<std::unique_ptr<Expr>> args_in)
            : callee(std::move(callee_in)), args(std::move(args_in))
        {
        }

        std::unique_ptr<Expr> callee;
        std::vector<std::unique_ptr<Expr>> args;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    std::unique_ptr<Expr> parse_expression(Runtime &runtime,
                                           const std::vector<Token> &tokens,
                                           std::string filename);

    struct Stmt
    {
        virtual ~Stmt() = default;
        virtual void accept(StmtVisitor &visitor) const = 0;
    };

    struct ExprStmt final : Stmt
    {
        explicit ExprStmt(std::unique_ptr<Expr> expression_in)
            : expression(std::move(expression_in))
        {
        }

        std::unique_ptr<Expr> expression;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct AssignStmt final : Stmt
    {
        AssignStmt(std::string target_in, std::unique_ptr<Expr> value_in)
            : target(std::move(target_in)), value(std::move(value_in))
        {
        }

        std::string target;
        std::unique_ptr<Expr> value;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct Module final
    {
        std::vector<std::unique_ptr<Stmt>> statements;
    };

    Module parse_module(Runtime &runtime, const std::vector<Token> &tokens, std::string filename);
}
