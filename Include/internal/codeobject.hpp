#pragma once

#include "cxxpython/execution.hpp"

#include <array>

namespace cxxpython::core::detail {
// Native, versioned instruction/call, source-position and region images.
// These are not CPython wordcode or a CPython extension ABI.
std::array<std::string, 3> encode_code(Attachment &, const VerifiedCode &, Value varnames,
                                       Value call_keywords, Value docstring);
void install_code_descriptors(Attachment &, std::span<const Handle>);
} // namespace cxxpython::core::detail
