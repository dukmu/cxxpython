#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_abc(Attachment &, Handle module, ReturnSlot &);
}
