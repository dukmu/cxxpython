#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
void initialize_codecs(Attachment &, Handle, ReturnSlot &);
void lookup_codec(Attachment &, Handle encoding, ReturnSlot &);
void lookup_codec_error(Attachment &, Handle name, ReturnSlot &);
} // namespace cxxpython::core::detail
