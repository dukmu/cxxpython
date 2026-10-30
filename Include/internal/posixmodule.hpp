#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void initialize_posix(Attachment &, Handle, ReturnSlot &);
bool filesystem_path(Attachment &, Value, std::string &, ReturnSlot &, ReturnSlot &filename);
} // namespace cxxpython::core::detail
