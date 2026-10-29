#pragma once

#include "cxxpython/compiler.hpp"

namespace cxxpython::core::detail {
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Literal {
    enum class Kind { Integer, String, None, True, False } kind;
    std::u32string text;
};
struct NameExpr {
    std::u32string name;
};
struct UnaryExpr {
    UnaryOp op;
    ExprPtr value;
};
struct BinaryExpr {
    BinaryOp op;
    ExprPtr left, right;
};
struct BoolExpr {
    bool conjunction;
    ExprPtr left, right;
};
struct CompareExpr {
    std::vector<ExprPtr> values;
    std::vector<CompareOp> ops;
};
struct CallExpr {
    ExprPtr target;
    std::vector<ExprPtr> positional;
    std::vector<std::pair<std::u32string, ExprPtr>> keywords;
};
struct AttributeExpr {
    ExprPtr object;
    std::u32string name;
};
struct TupleExpr {
    std::vector<ExprPtr> items;
};
struct SubscriptExpr {
    ExprPtr object, key;
};
struct Expr {
    Span span;
    std::size_t depth;
    std::variant<Literal, NameExpr, UnaryExpr, BinaryExpr, BoolExpr, CompareExpr, CallExpr,
                 AttributeExpr, TupleExpr, SubscriptExpr>
        node;
};
struct Stmt;
using Suite = std::vector<std::unique_ptr<Stmt>>;
struct ExprStmt {
    ExprPtr value;
};
struct AssignStmt {
    std::vector<ExprPtr> targets;
    ExprPtr value;
};
struct AugAssignStmt {
    ExprPtr target;
    BinaryOp op;
    ExprPtr value;
};
struct DeleteStmt {
    std::vector<ExprPtr> targets;
};
struct IfStmt {
    ExprPtr test;
    Suite body, alternative;
};
struct WhileStmt {
    ExprPtr test;
    Suite body, alternative;
};
struct ForStmt {
    ExprPtr target, iterable;
    Suite body, alternative;
};
struct ParameterSpec {
    std::u32string name;
    ParameterKind kind;
    ExprPtr default_value;
    Span span;
};
struct FunctionStmt {
    std::u32string name;
    std::u32string binding;
    std::vector<ExprPtr> decorators;
    std::vector<ParameterSpec> parameters;
    std::optional<std::u32string> varargs, varkwargs;
    Suite body;
    std::size_t scope = 0;
};
struct ClassStmt {
    std::u32string name, binding;
    std::vector<ExprPtr> bases, decorators;
    std::vector<std::pair<std::u32string, ExprPtr>> keywords;
    Suite body;
    std::size_t scope = 0;
};
struct ReturnStmt {
    ExprPtr value;
};
struct RaiseStmt {
    ExprPtr exception, cause;
};
struct ExceptHandler {
    ExprPtr type;
    std::optional<std::pair<std::u32string, Span>> name;
    Suite body;
    Span span;
};
struct TryStmt {
    Suite body;
    std::vector<ExceptHandler> handlers;
    Suite alternative, cleanup;
};
struct WithItem {
    ExprPtr context, target;
};
struct WithStmt {
    std::vector<WithItem> items;
    Suite body;
};
struct DeclareStmt {
    bool nonlocal;
    std::vector<std::pair<std::u32string, Span>> names;
};
enum class ControlStmt { Pass, Break, Continue };
struct Stmt {
    Span span;
    std::variant<ExprStmt, AssignStmt, AugAssignStmt, DeleteStmt, IfStmt, WhileStmt, ForStmt,
                 FunctionStmt, ClassStmt, ReturnStmt, RaiseStmt, TryStmt, WithStmt, DeclareStmt,
                 ControlStmt>
        node;
};
// CFG instructions use native literals/names until one attached freeze task
// materializes them; no Python identity is hidden in the compiler graph.
struct FlowInstruction {
    Opcode opcode;
    Span span;
    std::uint32_t operand = 0;
    std::u32string name;
    std::optional<Literal> literal;
    std::vector<std::u32string> keywords;
    std::optional<std::size_t> function;
};
struct FlowBlock {
    enum class Exit {
        Open,
        Jump,
        Branch,
        Return,
        IterNext,
        Stop,
        Leave,
        EndHandler
    } exit = Exit::Open;
    std::vector<FlowInstruction> instructions;
    std::size_t target = 0, alternative = 0;
    Span span;
    std::optional<std::size_t> handler;
};
struct FlowRegion {
    RegionKind kind;
    std::optional<std::size_t> continue_target, break_target, normal_target;
};
struct FunctionGraph {
    std::vector<FlowBlock> blocks;
    // Physical layout follows lexical region phases, not block allocation order.
    std::vector<std::size_t> order;
    std::vector<FlowRegion> regions;
    SignatureLayout signature;
    std::optional<std::u32string> docstring;
};
struct SourceProgram {
    SourceBuffer source;
    InputMode mode;
    std::vector<ScopeInfo> scopes;
    std::vector<FunctionGraph> graphs;
};
Suite parse_source(const SourceBuffer &source, InputMode mode);
void analyze_scopes(SourceProgram &program, Suite &body);
std::u32string decode_source(const SourceBuffer &source, Span span);
std::u32string decode_filename(const SourceBuffer &source);
} // namespace cxxpython::core::detail
