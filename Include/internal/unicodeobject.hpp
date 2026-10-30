#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void install_unicode_methods(Attachment &context, std::span<const Handle> core);
}
