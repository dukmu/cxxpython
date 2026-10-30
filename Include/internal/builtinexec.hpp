#pragma once

#include "cxxpython/execution.hpp"

namespace cxxpython::core::detail {
void install_exec_builtins(Attachment &, std::span<const Handle>, Value);
void frame_locals(Attachment &, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
