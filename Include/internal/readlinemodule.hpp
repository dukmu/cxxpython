#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_readline(Attachment &, Handle module, ReturnSlot &);
void read_terminal_line(Attachment &, int input_fd, int output_fd, Handle prompt, ReturnSlot &);
} // namespace cxxpython::core::detail
