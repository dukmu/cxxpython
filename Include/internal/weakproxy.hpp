#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void install_weakproxies(Attachment &, std::span<const Handle>);
void make_weakproxy(Attachment &, Handle target, Handle callback, ReturnSlot &);
} // namespace cxxpython::core::detail
