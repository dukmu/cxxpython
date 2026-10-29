#pragma once

#include "cxxpython/core/compiler.hpp"

namespace cxxpython::core::detail
{
    struct Expr;
    using ExprPtr = std::unique_ptr<Expr>;
    struct Literal
    {
        enum class Kind { Integer, String, None, True, False } kind;
        std::u32string text;
    };
    struct NameExpr { std::u32string name; };
    struct UnaryExpr { UnaryOp op; ExprPtr value; };
    struct BinaryExpr { BinaryOp op; ExprPtr left, right; };
    struct BoolExpr { bool conjunction; ExprPtr left, right; };
    struct CompareExpr { std::vector<ExprPtr> values; std::vector<CompareOp> ops; };
    struct CallExpr { ExprPtr target; std::vector<ExprPtr> positional; std::vector<std::pair<std::u32string, ExprPtr>> keywords; };
    struct AttributeExpr { ExprPtr object; std::u32string name; };
    struct Expr
    {
        Span span;
        std::size_t depth;
        std::variant<Literal, NameExpr, UnaryExpr, BinaryExpr, BoolExpr, CompareExpr, CallExpr, AttributeExpr> node;
    };
    struct Stmt;
    using Suite = std::vector<std::unique_ptr<Stmt>>;
    struct ExprStmt { ExprPtr value; };
    struct AssignStmt { std::vector<ExprPtr> targets; ExprPtr value; };
    struct AugAssignStmt { ExprPtr target; BinaryOp op; ExprPtr value; };
    struct DeleteStmt { std::vector<ExprPtr> targets; };
    struct IfStmt { ExprPtr test; Suite body, alternative; };
    struct WhileStmt { ExprPtr test; Suite body, alternative; };
    struct ParameterSpec { std::u32string name; ParameterKind kind; ExprPtr default_value; Span span; };
    struct FunctionStmt
    {
        std::u32string name;
        std::vector<ParameterSpec> parameters;
        std::optional<std::u32string> varargs, varkwargs;
        Suite body;
        std::size_t scope = 0;
    };
    struct ReturnStmt { ExprPtr value; };
    struct DeclareStmt { bool nonlocal; std::vector<std::pair<std::u32string, Span>> names; };
    enum class ControlStmt { Pass, Break, Continue };
    struct Stmt
    {
        Span span;
        std::variant<ExprStmt, AssignStmt, AugAssignStmt, DeleteStmt, IfStmt, WhileStmt, FunctionStmt, ReturnStmt, DeclareStmt, ControlStmt> node;
    };
    // CFG instructions use native literals/names until one attached freeze task
    // materializes them; no Python identity is hidden in the compiler graph.
    struct FlowInstruction
    {
        Opcode opcode;
        Span span;
        std::uint32_t operand = 0;
        std::u32string name;
        std::optional<Literal> literal;
        std::vector<std::u32string> keywords;
        std::optional<std::size_t> function;
    };
    struct FlowBlock
    {
        enum class Exit { Open, Jump, Branch, Return } exit = Exit::Open;
        std::vector<FlowInstruction> instructions;
        std::size_t target = 0, alternative = 0;
        Span span;
    };
    struct FunctionGraph { std::vector<FlowBlock> blocks; SignatureLayout signature; std::optional<std::u32string> docstring; };
    struct SourceProgram
    {
        SourceBuffer source;
        InputMode mode;
        std::vector<ScopeInfo> scopes;
        std::vector<FunctionGraph> graphs;
    };
    Suite parse_source(const SourceBuffer &source, InputMode mode);
    std::u32string decode_source(const SourceBuffer &source, Span span);
    std::u32string decode_filename(const SourceBuffer &source);
}
