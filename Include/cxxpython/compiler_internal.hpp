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
    struct UnaryExpr;
    struct BinaryExpr;
    struct CompareExpr;
    struct SingletonExpr;
    struct NameExpr;
    struct AttributeExpr;
    struct SubscriptExpr;
    struct TupleExpr;
    struct DictExpr;
    struct CallExpr;
    struct NameTarget;
    struct AttributeTarget;
    struct SubscriptTarget;
    struct ExprStmt;
    struct AssignStmt;
    struct IfStmt;
    struct ReturnStmt;
    struct FunctionDefStmt;
    struct ClassDefStmt;

    struct ExprVisitor
    {
        virtual ~ExprVisitor() = default;
        virtual void visit(const IntegerExpr &expression) = 0;
        virtual void visit(const StringExpr &expression) = 0;
        virtual void visit(const UnaryExpr &expression) = 0;
        virtual void visit(const BinaryExpr &expression) = 0;
        virtual void visit(const CompareExpr &expression) = 0;
        virtual void visit(const SingletonExpr &expression) = 0;
        virtual void visit(const NameExpr &expression) = 0;
        virtual void visit(const AttributeExpr &expression) = 0;
        virtual void visit(const SubscriptExpr &expression) = 0;
        virtual void visit(const TupleExpr &expression) = 0;
        virtual void visit(const DictExpr &expression) = 0;
        virtual void visit(const CallExpr &expression) = 0;
    };

    struct TargetVisitor
    {
        virtual ~TargetVisitor() = default;
        virtual void visit(const NameTarget &target) = 0;
        virtual void visit(const AttributeTarget &target) = 0;
        virtual void visit(const SubscriptTarget &target) = 0;
    };

    struct StmtVisitor
    {
        virtual ~StmtVisitor() = default;
        virtual void visit(const ExprStmt &statement) = 0;
        virtual void visit(const AssignStmt &statement) = 0;
        virtual void visit(const IfStmt &statement) = 0;
        virtual void visit(const ReturnStmt &statement) = 0;
        virtual void visit(const FunctionDefStmt &statement) = 0;
        virtual void visit(const ClassDefStmt &statement) = 0;
    };

    enum class TokenKind
    {
        Integer,
        String,
        Identifier,
        Plus,
        Minus,
        Star,
        Tilde,
        LeftParen,
        RightParen,
        Comma,
        Dot,
        LeftBracket,
        RightBracket,
        LeftBrace,
        RightBrace,
        Colon,
        Equal,
        EqualEqual,
        BangEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        If,
        Else,
        Def,
        Return,
        Class,
        TrueLiteral,
        FalseLiteral,
        NoneLiteral,
        Indent,
        Dedent,
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

    struct BinaryExpr final : Expr
    {
        BinaryExpr(std::unique_ptr<Expr> left_in, BinaryOp op_in, std::unique_ptr<Expr> right_in)
            : left(std::move(left_in)), op(op_in), right(std::move(right_in))
        {
        }

        std::unique_ptr<Expr> left;
        BinaryOp op;
        std::unique_ptr<Expr> right;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct UnaryExpr final : Expr
    {
        UnaryExpr(UnaryOp op_in, std::unique_ptr<Expr> operand_in)
            : op(op_in), operand(std::move(operand_in))
        {
        }

        UnaryOp op;
        std::unique_ptr<Expr> operand;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct Comparison final
    {
        Comparison(RichCompareOp op_in, std::unique_ptr<Expr> right_in)
            : op(op_in), right(std::move(right_in))
        {
        }

        RichCompareOp op;
        std::unique_ptr<Expr> right;
    };

    struct CompareExpr final : Expr
    {
        CompareExpr(std::unique_ptr<Expr> left_in, std::vector<Comparison> comparisons_in)
            : left(std::move(left_in)), comparisons(std::move(comparisons_in))
        {
        }

        std::unique_ptr<Expr> left;
        std::vector<Comparison> comparisons;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    enum class SingletonKind
    {
        TrueValue,
        FalseValue,
        NoneValue,
    };

    struct SingletonExpr final : Expr
    {
        explicit SingletonExpr(SingletonKind kind_in)
            : kind(kind_in)
        {
        }

        SingletonKind kind;

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

    struct AttributeExpr final : Expr
    {
        AttributeExpr(std::unique_ptr<Expr> object_in, std::string name_in)
            : object(std::move(object_in)), name(std::move(name_in))
        {
        }

        std::unique_ptr<Expr> object;
        std::string name;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct SubscriptExpr final : Expr
    {
        SubscriptExpr(std::unique_ptr<Expr> object_in, std::unique_ptr<Expr> key_in)
            : object(std::move(object_in)), key(std::move(key_in))
        {
        }

        std::unique_ptr<Expr> object;
        std::unique_ptr<Expr> key;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct TupleExpr final : Expr
    {
        explicit TupleExpr(std::vector<std::unique_ptr<Expr>> items_in)
            : items(std::move(items_in))
        {
        }

        std::vector<std::unique_ptr<Expr>> items;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct DictItem final
    {
        DictItem(std::unique_ptr<Expr> key_in, std::unique_ptr<Expr> value_in)
            : key(std::move(key_in)), value(std::move(value_in))
        {
        }

        std::unique_ptr<Expr> key;
        std::unique_ptr<Expr> value;
    };

    struct DictExpr final : Expr
    {
        explicit DictExpr(std::vector<DictItem> items_in)
            : items(std::move(items_in))
        {
        }

        std::vector<DictItem> items;

        void accept(ExprVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    std::unique_ptr<Expr> parse_expression(Runtime &runtime,
                                           const std::vector<Token> &tokens,
                                           std::string filename);

    struct AssignTarget
    {
        virtual ~AssignTarget() = default;
        virtual void accept(TargetVisitor &visitor) const = 0;
    };

    struct NameTarget final : AssignTarget
    {
        explicit NameTarget(std::string name_in)
            : name(std::move(name_in))
        {
        }

        std::string name;

        void accept(TargetVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct AttributeTarget final : AssignTarget
    {
        AttributeTarget(std::unique_ptr<Expr> object_in, std::string name_in)
            : object(std::move(object_in)), name(std::move(name_in))
        {
        }

        std::unique_ptr<Expr> object;
        std::string name;

        void accept(TargetVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct SubscriptTarget final : AssignTarget
    {
        SubscriptTarget(std::unique_ptr<Expr> object_in, std::unique_ptr<Expr> key_in)
            : object(std::move(object_in)), key(std::move(key_in))
        {
        }

        std::unique_ptr<Expr> object;
        std::unique_ptr<Expr> key;

        void accept(TargetVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

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
        AssignStmt(std::unique_ptr<AssignTarget> target_in, std::unique_ptr<Expr> value_in)
            : target(std::move(target_in)), value(std::move(value_in))
        {
        }

        std::unique_ptr<AssignTarget> target;
        std::unique_ptr<Expr> value;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct IfStmt final : Stmt
    {
        IfStmt(std::unique_ptr<Expr> condition_in,
               std::vector<std::unique_ptr<Stmt>> then_body_in,
               std::vector<std::unique_ptr<Stmt>> else_body_in)
            : condition(std::move(condition_in)),
              then_body(std::move(then_body_in)),
              else_body(std::move(else_body_in))
        {
        }

        std::unique_ptr<Expr> condition;
        std::vector<std::unique_ptr<Stmt>> then_body;
        std::vector<std::unique_ptr<Stmt>> else_body;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct ReturnStmt final : Stmt
    {
        explicit ReturnStmt(std::unique_ptr<Expr> value_in)
            : value(std::move(value_in))
        {
        }

        std::unique_ptr<Expr> value;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct FunctionDefStmt final : Stmt
    {
        FunctionDefStmt(std::string name_in,
                        std::vector<std::string> params_in,
                        std::vector<std::unique_ptr<Stmt>> body_in)
            : name(std::move(name_in)), params(std::move(params_in)), body(std::move(body_in))
        {
        }

        std::string name;
        std::vector<std::string> params;
        std::vector<std::unique_ptr<Stmt>> body;

        void accept(StmtVisitor &visitor) const override
        {
            visitor.visit(*this);
        }
    };

    struct ClassDefStmt final : Stmt
    {
        ClassDefStmt(std::string name_in,
                     std::vector<std::unique_ptr<Expr>> bases_in,
                     std::vector<std::unique_ptr<Stmt>> body_in)
            : name(std::move(name_in)), bases(std::move(bases_in)), body(std::move(body_in))
        {
        }

        std::string name;
        std::vector<std::unique_ptr<Expr>> bases;
        std::vector<std::unique_ptr<Stmt>> body;

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
