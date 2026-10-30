#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_operator(Attachment &, Handle module, ReturnSlot &);
}
