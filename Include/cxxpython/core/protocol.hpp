#pragma once

#include "cxxpython/core/objects.hpp"

namespace cxxpython::core
{
    enum class Protocol : std::size_t
    {
        Bool, Len, Int, Index, Repr, Str, Hash, Eq, Ne, Lt, Le, Gt, Ge,
        Add, RAdd, IAdd, Sub, RSub, ISub, Mul, RMul, IMul,
        FloorDiv, RFloorDiv, IFloorDiv, Mod, RMod, IMod, Enter, Exit, Pos, Neg, Invert, Count,
    };
    enum class ProtocolResult { Any, Boolean, Length, Integer, String, Hash };
    struct ProtocolDefinition
    {
        std::u32string_view name;
        std::size_t arity;
        ProtocolResult result;
        bool none_disables = false;
    };
    enum class SlotState { Missing, Disabled, Native, PythonDescriptor };
    struct ResolvedSlot
    {
        Protocol protocol;
        SlotState state;
        Handle type;
        Handle owner;
        Handle descriptor;
        std::uint64_t epoch;
    };
    enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge, Is, IsNot };
    enum class BinaryOp { Add, Subtract, Multiply, FloorDivide, Modulo };
    enum class UnaryOp { Positive, Negative, Invert, Not };

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
    ResultView rich_compare(Attachment &context, Handle left, Handle right, CompareOp op, ReturnSlot &out);
    ResultView container_equal(Attachment &context, Handle left, Handle right, ReturnSlot &out);
    ResultView binary(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out);
    ResultView inplace(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out);
}
