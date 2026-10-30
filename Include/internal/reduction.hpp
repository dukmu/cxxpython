#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void install_reduction(Attachment &, std::span<const Handle>);
}
