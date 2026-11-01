#pragma once

#include "cxxpython/objects.hpp"

#include <string_view>

namespace cxxpython::core::detail {
void get_sys_attribute(Attachment &, std::u32string_view, ReturnSlot &);
void initialize_sys_hooks(Attachment &, Handle sys, ReturnSlot &);
void configure_sys_flags(Attachment &, Handle sys, bool interactive, bool quiet, ReturnSlot &);
void install_system_exit(Attachment &, std::span<const Handle> core);
void display_value(Attachment &, Handle value, ReturnSlot &);
} // namespace cxxpython::core::detail
