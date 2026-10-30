#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void install_os_errors(Attachment &, std::span<const Handle>);
}
