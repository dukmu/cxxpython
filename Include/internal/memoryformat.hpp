#pragma once
#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
std::size_t memory_format_size(char) noexcept;
void unpack_memory_format(Attachment &, char, std::string_view, ReturnSlot &);
bool pack_memory_format(Attachment &, char, Handle, std::string &, ReturnSlot &);
} // namespace cxxpython::core::detail
