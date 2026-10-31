#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_time(Attachment &, Handle module, ReturnSlot &);
void initialize_time_calendar(Attachment &, Handle module, ReturnSlot &);
} // namespace cxxpython::core::detail
