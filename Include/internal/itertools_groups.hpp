#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_itertools_groups(Attachment &, Handle module, ReturnSlot &);
}
