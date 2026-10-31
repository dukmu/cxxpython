#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_math_reductions(Attachment &, Handle module, ReturnSlot &);
}
