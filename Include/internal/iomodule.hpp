#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
bool io_index(Attachment &, Handle, std::int64_t &, ReturnSlot &);
bool io_call(Attachment &, Handle, std::u32string_view, std::span<const Value>, ReturnSlot &);
void io_closed_error(Attachment &, ReturnSlot &);
void io_unsupported(Attachment &, std::u32string, ReturnSlot &);
void install_iobases(Attachment &, std::span<const Handle>);
void install_fileio(Attachment &, std::span<const Handle>);
void initialize_io(Attachment &, Handle, ReturnSlot &);
} // namespace cxxpython::core::detail
