#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
char32_t unicode_simple_lower(char32_t) noexcept;
char32_t unicode_simple_upper(char32_t) noexcept;
bool unicode_decimal(char32_t) noexcept;
bool unicode_space(char32_t) noexcept;
bool unicode_linebreak(char32_t) noexcept;
bool unicode_alnum(char32_t) noexcept;
void install_unicode_methods(Attachment &context, std::span<const Handle> core);
} // namespace cxxpython::core::detail
