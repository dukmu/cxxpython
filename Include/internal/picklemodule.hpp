#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_pickle(Attachment &, Handle, ReturnSlot &);
}
