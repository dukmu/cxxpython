#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_posix(Attachment &, Handle, ReturnSlot &);
}
