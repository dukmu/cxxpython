#pragma once

#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
void install_bytes_methods(Attachment &, std::span<const Handle>);
}
