#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void match_keys(Attachment &, Handle subject, Handle keys_tuple, ReturnSlot &);
void match_mapping_rest(Attachment &, Handle subject, Handle keys_tuple, ReturnSlot &);
void match_class(Attachment &, Handle subject, Handle cls, Handle keyword_names_tuple,
                 std::size_t positional, ReturnSlot &);
} // namespace cxxpython::core::detail
