#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_select(Attachment &, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
