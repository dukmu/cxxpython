#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_itertools_combinatorics(Attachment &context, Handle module, ReturnSlot &out);
}
