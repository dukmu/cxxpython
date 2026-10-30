#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void install_os_errors(Attachment &, std::span<const Handle>);
void add_exception_note(Attachment &, Handle exception, Handle note, ReturnSlot &);
} // namespace cxxpython::core::detail
