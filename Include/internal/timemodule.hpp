#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_time(Attachment &, Handle module, ReturnSlot &);
}
