#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_contextvars(Attachment &, Handle module, ReturnSlot &);
}
