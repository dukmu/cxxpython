#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython::compiler_internal
{
    enum class TokenKind
    {
        Integer,
        String,
        Plus,
        End,
    };

    struct Token
    {
        TokenKind kind;
        std::string lexeme;
        std::int64_t int_value = 0;
    };

    std::vector<Token> lex_source(Runtime &runtime, std::string_view source);

    struct Expr
    {
        virtual ~Expr() = default;
    };

    struct IntegerExpr final : Expr
    {
        explicit IntegerExpr(std::int64_t value_in)
            : value(value_in)
        {
        }

        std::int64_t value;
    };

    struct StringExpr final : Expr
    {
        explicit StringExpr(std::string value_in)
            : value(std::move(value_in))
        {
        }

        std::string value;
    };

    struct BinaryAddExpr final : Expr
    {
        BinaryAddExpr(std::unique_ptr<Expr> left_in, std::unique_ptr<Expr> right_in)
            : left(std::move(left_in)), right(std::move(right_in))
        {
        }

        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
    };

    std::unique_ptr<Expr> parse_expression(Runtime &runtime, const std::vector<Token> &tokens);
}
