#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_thread(Attachment &, Handle module, ReturnSlot &);
void shutdown_threads(Attachment &, ReturnSlot &);
} // namespace cxxpython::core::detail
