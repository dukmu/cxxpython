#pragma once

#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
// Internal callers retain operands in independent live roots until return.
// No operand may depend on out, which these operations reset. Public entries
// snapshot their inputs first; native callers may reuse their existing roots.
void index_owned(Attachment &, Handle, ReturnSlot &);
void truth_owned(Attachment &, Handle, ReturnSlot &);
void rich_compare_owned(Attachment &, Handle, Handle, CompareOp, ReturnSlot &);
void container_equal_owned(Attachment &, Handle, Handle, ReturnSlot &);
// Success returns the normalized native hash with out Empty; failure leaves Raised.
std::optional<std::int64_t> hash_word(Attachment &, Handle, ReturnSlot &);
ResultView dict_lookup_owned(Attachment &, Handle, Handle, ReturnSlot &);
ResultView dict_store_owned(Attachment &, Handle, Handle, Handle, ReturnSlot &);
ResultView dict_erase_owned(Attachment &, Handle, Handle, ReturnSlot &);
ResultView dict_setdefault_owned(Attachment &, Handle, Handle, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
