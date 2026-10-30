#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_thread(Attachment &, Handle module, ReturnSlot &);
}
