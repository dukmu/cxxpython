#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void encode_text(Attachment &, Handle text, Handle encoding, Handle errors, ReturnSlot &);
void decode_bytes(Attachment &, Handle bytes, Handle encoding, Handle errors, ReturnSlot &);
void install_codecs(Attachment &, std::span<const Handle>);
} // namespace cxxpython::core::detail
