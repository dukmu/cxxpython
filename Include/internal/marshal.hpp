#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_marshal(Attachment &, Handle, ReturnSlot &);
}
