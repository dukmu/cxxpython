#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
enum class Protocol : std::size_t {
    Bool,
    Len,
    Int,
    Index,
    Repr,
    Str,
    Hash,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Add,
    RAdd,
    IAdd,
    Sub,
    RSub,
    ISub,
    Mul,
    RMul,
    IMul,
    FloorDiv,
    RFloorDiv,
    IFloorDiv,
    Mod,
    RMod,
    IMod,
    Enter,
    Exit,
    AEnter,
    AExit,
    Pos,
    Neg,
    Invert,
    Iter,
    Next,
    GetItem,
    SetItem,
    DelItem,
    TrueDiv,
    RTrueDiv,
    ITrueDiv,
    Power,
    RPower,
    IPower,
    LeftShift,
    RLeftShift,
    ILeftShift,
    RightShift,
    RRightShift,
    IRightShift,
    BitAnd,
    RBitAnd,
    IBitAnd,
    BitXor,
    RBitXor,
    IBitXor,
    BitOr,
    RBitOr,
    IBitOr,
    MatrixMultiply,
    RMatrixMultiply,
    IMatrixMultiply,
    Contains,
    Count,
};
enum class ProtocolResult { Any, Boolean, Length, Integer, String, Hash };
struct ProtocolDefinition {
    std::u32string_view name;
    std::size_t arity;
    ProtocolResult result;
    bool none_disables = false;
};
enum class SlotState { Missing, Disabled, Native, PythonDescriptor };
struct ResolvedSlot {
    Protocol protocol;
    SlotState state;
    Handle type;
    Handle owner;
    Handle descriptor;
    std::uint64_t epoch;
};
enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge, Is, IsNot, In, NotIn };
enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    FloorDivide,
    Modulo,
    TrueDivide,
    Power,
    LeftShift,
    RightShift,
    BitAnd,
    BitXor,
    BitOr,
    MatrixMultiply
};
enum class UnaryOp { Positive, Negative, Invert, Not };
enum class NextStatus { Item, Done, Raised };

const ProtocolDefinition &protocol_definition(Protocol id);
ResolvedSlot resolve_protocol(Attachment &context, Handle type, Protocol id, RootScope &roots);
ResultView invoke_protocol(Attachment &context, Handle receiver, const ResolvedSlot &slot,
                           std::span<const Value> args, ReturnSlot &out);
ResultView truth(Attachment &context, Handle value, ReturnSlot &out);
ResultView unary(Attachment &context, Handle value, UnaryOp op, ReturnSlot &out);
ResultView length(Attachment &context, Handle value, ReturnSlot &out);
ResultView index(Attachment &context, Handle value, ReturnSlot &out);
ResultView hash(Attachment &context, Handle value, ReturnSlot &out);
ResultView repr(Attachment &context, Handle value, ReturnSlot &out);
ResultView str(Attachment &context, Handle value, ReturnSlot &out);
ResultView format(Attachment &context, Handle value, Handle spec, ReturnSlot &out);
ResultView ascii(Attachment &context, Handle value, ReturnSlot &out);
ResultView rich_compare(Attachment &context, Handle left, Handle right, CompareOp op,
                        ReturnSlot &out);
ResultView container_equal(Attachment &context, Handle left, Handle right, ReturnSlot &out);
ResultView binary(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out);
ResultView divmod(Attachment &context, Handle left, Handle right, ReturnSlot &out);
ResultView power(Attachment &context, Handle base, Handle exponent, Handle modulus,
                 ReturnSlot &out);
ResultView inplace(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out);
ResultView get_iter(Attachment &context, Handle value, ReturnSlot &out);
NextStatus iter_next(Attachment &context, Handle iterator, ReturnSlot &out);
ResultView get_item(Attachment &context, Handle value, Handle key, ReturnSlot &out);
ResultView set_item(Attachment &context, Handle value, Handle key, Handle item, ReturnSlot &out);
ResultView del_item(Attachment &context, Handle value, Handle key, ReturnSlot &out);
namespace detail {
// The caller owns receiver/arguments in live roots and handles allocation errors.
// Descriptor binding and argument preparation reuse that invocation's scope.
void invoke_protocol(Attachment &context, Handle receiver, const ResolvedSlot &slot,
                     std::span<const Value> args, ReturnSlot &out, RootScope &roots);
void install_format(Attachment &context, std::span<const Handle> core);
} // namespace detail
} // namespace cxxpython::core
