#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_signal(Attachment &, Handle, ReturnSlot &);
// Only Active main-thread safe points may run handlers. True means out is Raised.
bool check_signals(Attachment &, ReturnSlot &);
} // namespace cxxpython::core::detail
