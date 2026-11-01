#pragma once

#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
// Inputs and the resolved descriptor remain independently rooted by the caller.
// Only the actual canonical method, complete signature and current receiver/epoch
// may use its shared builtin kernel; otherwise dispatch has no side effects.
bool try_native_operation(Attachment &, const ResolvedSlot &, std::span<const Value>,
                          NativeEntry expected, NativeEntry kernel, ReturnSlot &);
bool try_int_binary(Attachment &, Value, Value, BinaryOp, const ResolvedSlot &, ReturnSlot &);
bool try_float_binary(Attachment &, Value, Value, BinaryOp, const ResolvedSlot &, ReturnSlot &);
bool try_list_item(Attachment &, const ResolvedSlot &, std::span<const Value>, ReturnSlot &);
bool try_sequence_item(Attachment &, const ResolvedSlot &, std::span<const Value>, ReturnSlot &);
bool try_dict_item(Attachment &, const ResolvedSlot &, std::span<const Value>, ReturnSlot &);
// Internal callers retain operands in independent live roots until return.
// No operand may depend on out, which these operations reset. Public entries
// snapshot their inputs first; native callers may reuse their existing roots.
void index_owned(Attachment &, Handle, ReturnSlot &);
void truth_owned(Attachment &, Handle, ReturnSlot &);
void rich_compare_owned(Attachment &, Handle, Handle, CompareOp, ReturnSlot &);
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
