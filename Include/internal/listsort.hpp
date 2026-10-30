#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void sort_list(Attachment &context, Handle self, Handle key, bool reverse, ReturnSlot &out);
}
