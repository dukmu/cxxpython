#include "internal/ceval.hpp"

#include "cxxpython/floatobject.hpp"
#include "internal/abstract.hpp"

namespace cxxpython::core::detail {
namespace {
bool project_number(Attachment &context, OperandSlot &slot)
{
    if (slot.kind() != OperandSlot::Kind::Boxed)
        return true;
    const auto value = slot.boxed();
    const auto type = context.type_of(value);
    if (type == context.builtin(CoreObject::IntType)) {
        const auto *data = context.try_payload<IntObject>(value);
        if (!data || data->large)
            return false;
        slot = OperandSlot::integer(data->small);
        return true;
    }
    if (type == context.builtin(CoreObject::FloatType)) {
        const auto *data = context.try_payload<FloatObject>(value);
        if (!data)
            return false;
        slot = OperandSlot::floating(data->value);
        return true;
    }
    return false;
}
bool operation(BinaryOp op, bool integer, Protocol &normal, Protocol &inplace,
               NativeNumericKind &expected)
{
    switch (op) {
    case BinaryOp::Add:
        normal = Protocol::Add;
        inplace = Protocol::IAdd;
        expected = integer ? NativeNumericKind::IntAdd : NativeNumericKind::FloatAdd;
        return true;
    case BinaryOp::Subtract:
        normal = Protocol::Sub;
        inplace = Protocol::ISub;
        expected = integer ? NativeNumericKind::IntSubtract : NativeNumericKind::FloatSubtract;
        return true;
    case BinaryOp::Multiply:
        normal = Protocol::Mul;
        inplace = Protocol::IMul;
        expected = integer ? NativeNumericKind::IntMultiply : NativeNumericKind::FloatMultiply;
        return true;
    case BinaryOp::TrueDivide:
        normal = Protocol::TrueDiv;
        inplace = Protocol::ITrueDiv;
        expected = NativeNumericKind::FloatTrueDivide;
        return !integer;
    case BinaryOp::FloorDivide:
        normal = Protocol::FloorDiv;
        inplace = Protocol::IFloorDiv;
        expected = NativeNumericKind::FloatFloorDivide;
        return !integer;
    case BinaryOp::Modulo:
        normal = Protocol::Mod;
        inplace = Protocol::IMod;
        expected = NativeNumericKind::FloatModulo;
        return !integer;
    default:
        return false;
    }
}
} // namespace

bool try_vm_numeric(Attachment &context, FrameObject &frame, Opcode opcode, std::uint32_t operand,
                    ReturnSlot &method, ReturnSlot &error)
{
    const auto base = frame.sp - 2;
    auto left = frame.operands[base], right = frame.operands[base + 1];
    if (!project_number(context, left) || !project_number(context, right) ||
        left.kind() != right.kind())
        return false;
    const bool integer = left.kind() == OperandSlot::Kind::Integer;
    const auto type = integer ? CoreObject::IntType : CoreObject::FloatType;
    const auto op = static_cast<BinaryOp>(operand);
    Protocol normal, inplace;
    NativeNumericKind expected;
    if (!operation(op, integer, normal, inplace, expected))
        return false;
    const auto assignment = opcode == Opcode::Inplace ? std::optional{inplace} : std::nullopt;
    auto kind = context.lease_builtin_numeric(type, normal, assignment, method);
    if (!kind) {
        RootScope cold(context);
        resolve_builtin_slot(context, type, normal, cold);
        if (assignment)
            resolve_builtin_slot(context, type, *assignment, cold);
        kind = context.lease_builtin_numeric(type, normal, assignment, method);
    }
    if (!kind || *kind != expected)
        return false;
    // Cold resolution may poll; boxed inputs must still have the projected type.
    for (std::size_t i = base; i < base + 2; ++i)
        if (frame.operands[i].kind() == OperandSlot::Kind::Boxed &&
            context.type_of(frame.operands[i].boxed()) != context.builtin(type))
            return false;

    std::int64_t integral = 0;
    if (integer && !small_int_binary(op, left.integer(), right.integer(), integral))
        return false;
    NativeCallScope call(context, error);
    if (!call.entered)
        return true;
    double floating = 0.0;
    if (!integer &&
        !float_binary_scalar(context, op, left.floating(), right.floating(), floating, error))
        return error.tag() == ResultTag::Raised;

    const auto native_inputs =
        static_cast<std::size_t>(frame.operands[base].kind() != OperandSlot::Kind::Boxed) +
        static_cast<std::size_t>(frame.operands[base + 1].kind() != OperandSlot::Kind::Boxed);
    frame.scalar_operands = frame.scalar_operands - native_inputs + 1;
    frame.operands[base] =
        integer ? OperandSlot::integer(integral) : OperandSlot::floating(floating);
    frame.operands[base + 1] = context.builtin(CoreObject::None);
    --frame.sp;
    return true;
}
} // namespace cxxpython::core::detail
