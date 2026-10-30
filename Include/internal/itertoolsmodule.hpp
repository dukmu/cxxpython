#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_itertools(Attachment &context, Handle module, ReturnSlot &out);
}
