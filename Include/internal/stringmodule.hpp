#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_string(Attachment &, Handle, ReturnSlot &);
}
