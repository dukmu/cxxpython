#pragma once

#include "cxxpython/runtime.hpp"
#include <mutex>

namespace cxxpython::core::detail {
void initialize_posix(Attachment &, Handle, ReturnSlot &);
bool filesystem_path(Attachment &, Value, std::string &, ReturnSlot &, ReturnSlot &filename);
// Shared with calendar calls that read libc's process-wide TZ/environment state.
std::mutex &process_environment_lock();
} // namespace cxxpython::core::detail
