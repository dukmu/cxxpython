#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_atexit(Attachment &, Handle module, ReturnSlot &);
void run_exitfuncs(Attachment &, ReturnSlot &);
} // namespace cxxpython::core::detail
