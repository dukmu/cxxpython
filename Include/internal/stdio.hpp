#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_stdio(Attachment &, Handle sys_module, ReturnSlot &);
enum class StdioFlush { AfterExecution, Finalizing };
bool flush_stdio(Attachment &, StdioFlush phase = StdioFlush::Finalizing);
// False leaves redirected/replaced streams to their ordinary Python protocol.
bool read_terminal_input(Attachment &, Handle input, Handle output, std::optional<Handle> prompt,
                         ReturnSlot &);
// Bound arguments: sep, end, file, flush, then the positional-arguments tuple.
void print_builtin(Attachment &, Value, std::span<const Value>, ReturnSlot &);
} // namespace cxxpython::core::detail
