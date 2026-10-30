#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_opcode(Attachment &, Handle, ReturnSlot &);
}
