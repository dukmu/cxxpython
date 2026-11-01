#pragma once

#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
// The Interpreter retains the builtin type and its immutable MRO. The actual
// descriptor owns its function/lease and has an independent root in the caller's
// scope, even if an internal namespace update removes or replaces the method.
struct BuiltinSlot {
    CoreObject type;
    Protocol protocol;
    SlotState state;
    Handle descriptor;
    std::uint64_t epoch;
    NativeSlotKind native_kind;
    NativeCallKind native_call;
};
BuiltinSlot resolve_builtin_slot(Attachment &, CoreObject, Protocol, RootScope &);
// Receiver/arguments belong to the caller's independent roots or running Frame;
// slot roots remain in roots. Revalidation and binding keep the same contract.
void invoke_protocol(Attachment &, Value, const ResolvedSlot &, std::span<const Value>,
                     ReturnSlot &, RootScope &);
void invoke_protocol(Attachment &, Value, const BuiltinSlot &, std::span<const Value>, ReturnSlot &,
                     RootScope &);
// Inputs and the resolved descriptor remain independently rooted by the caller.
// Only the actual canonical method, complete signature and current receiver/epoch
// may use its shared builtin kernel; otherwise dispatch has no side effects.
bool try_native_operation(Attachment &, const BuiltinSlot &, std::span<const Value>,
                          NativeEntry expected, NativeEntry kernel, ReturnSlot &);
bool try_int_binary(Attachment &, Value, Value, BinaryOp, const BuiltinSlot &, ReturnSlot &);
bool try_float_binary(Attachment &, Value, Value, BinaryOp, const BuiltinSlot &, ReturnSlot &);
bool try_list_item(Attachment &, const BuiltinSlot &, std::span<const Value>, ReturnSlot &);
bool try_sequence_item(Attachment &, const BuiltinSlot &, std::span<const Value>, ReturnSlot &);
bool try_dict_item(Attachment &, const BuiltinSlot &, std::span<const Value>, ReturnSlot &);
// Internal callers retain operands in independent live roots or a running Frame
// until return. These owners must not be cleared by resetting out or callbacks.
// No operand may depend on out, which these operations reset. Public entries
// snapshot their inputs first; native callers may reuse their existing roots.
// Callers translate allocation failures (the VM uses its instruction boundary).
void binary_owned(Attachment &, Value, Value, BinaryOp, ReturnSlot &);
void inplace_owned(Attachment &, Value, Value, BinaryOp, ReturnSlot &);
void unary_owned(Attachment &, Value, UnaryOp, ReturnSlot &);
void get_item_owned(Attachment &, Value, Value, ReturnSlot &);
void set_item_owned(Attachment &, Value, Value, Value, ReturnSlot &);
void del_item_owned(Attachment &, Value, Value, ReturnSlot &);
void index_owned(Attachment &, Handle, ReturnSlot &);
void truth_owned(Attachment &, Handle, ReturnSlot &);
void truth_owned(Attachment &, Value, ReturnSlot &);
void rich_compare_owned(Attachment &, Handle, Handle, CompareOp, ReturnSlot &);
void rich_compare_owned(Attachment &, Value, Value, CompareOp, ReturnSlot &);
void container_equal_owned(Attachment &, Handle, Handle, ReturnSlot &);
NextStatus iter_next_owned(Attachment &, Handle, ReturnSlot &);
// Owns its input; success leaves out Empty. No overflow exception means clipping;
// no message selects CPython's input-type/index-sized overflow diagnostic.
bool index_as_int64(
    Attachment &, Value, std::int64_t &, ReturnSlot &,
    std::optional<CoreObject> overflow = CoreObject::OverflowErrorType,
    std::optional<std::u32string_view> message = U"Python int too large to convert to C ssize_t");
bool index_as_int64(
    Attachment &, Handle, std::int64_t &, ReturnSlot &,
    std::optional<CoreObject> overflow = CoreObject::OverflowErrorType,
    std::optional<std::u32string_view> message = U"Python int too large to convert to C ssize_t");
// Success returns the normalized native hash with out Empty; failure leaves Raised.
std::optional<std::int64_t> hash_word(Attachment &, Handle, ReturnSlot &);
ResultView dict_lookup_owned(Attachment &, Handle, Handle, ReturnSlot &);
ResultView dict_store_owned(Attachment &, Handle, Handle, Handle, ReturnSlot &);
ResultView dict_erase_owned(Attachment &, Handle, Handle, ReturnSlot &);
ResultView dict_setdefault_owned(Attachment &, Handle, Handle, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
