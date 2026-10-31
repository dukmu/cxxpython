#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_socket(Attachment &, Handle, ReturnSlot &);
}
