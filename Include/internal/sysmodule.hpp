#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_sys_hooks(Attachment &, Handle sys, ReturnSlot &);
void install_system_exit(Attachment &, std::span<const Handle> core);
void display_value(Attachment &, Handle value, ReturnSlot &);
} // namespace cxxpython::core::detail
