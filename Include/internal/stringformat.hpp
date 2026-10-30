#pragma once

#include "cxxpython/runtime.hpp"

#include <span>

namespace cxxpython::core::detail {
void install_string_format(Attachment &context, std::span<const Handle> core);
}
