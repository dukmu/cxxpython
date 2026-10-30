#pragma once

#include "internal/iomodule.hpp"

namespace cxxpython::core::detail {
void initialize_textio(Attachment &, Handle module, ReturnSlot &);
void initialize_textio_wrapper(Attachment &, Handle module, ReturnSlot &);
} // namespace cxxpython::core::detail
