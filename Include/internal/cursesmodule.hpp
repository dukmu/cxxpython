#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_curses(Attachment &, Handle, ReturnSlot &);
}
