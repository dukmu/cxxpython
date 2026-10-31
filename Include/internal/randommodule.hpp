#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_random(Attachment &, Handle, ReturnSlot &);
}
