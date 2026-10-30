#pragma once

#include "cxxpython/setobject.hpp"

namespace cxxpython::core::detail {
void install_set_operations(Attachment &, std::span<const Handle>);
void set_update(Attachment &, Handle, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
