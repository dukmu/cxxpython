#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
// The existing ExceptionObject tail owns these readonly group fields; its
// inherited instance dictionary is separate from these indexed slots.
inline constexpr std::size_t exception_group_exceptions_slot = 0;
inline constexpr std::size_t exception_group_message_slot = 1;
void install_exception_groups(Attachment &, std::span<const Handle>);
void exception_group_match(Attachment &, Handle exception, Handle matcher, ReturnSlot &match,
                           ReturnSlot &rest);
void prepare_reraise_star(Attachment &, Handle original, Handle exception_list, ReturnSlot &out);
} // namespace cxxpython::core::detail
