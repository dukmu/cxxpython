#pragma once

#include "cxxpython/execution.hpp"

namespace cxxpython::core {
class SourceError;
namespace detail {
void raise_source_error(Attachment &, const SourceError &, ReturnSlot &);
void install_exec_builtins(Attachment &, std::span<const Handle>, Value);
void frame_locals(Attachment &, Handle, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
