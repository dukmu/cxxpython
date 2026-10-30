#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void io_open(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
void initialize_open(Attachment &, Handle module, ReturnSlot &);
void install_open_builtin(Attachment &, std::span<const Handle>, Value dictionary);
} // namespace cxxpython::core::detail
