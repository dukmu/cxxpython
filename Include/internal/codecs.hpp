#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
enum class TextEncoding { Utf8, Ascii, Latin1, Utf16, Utf16LE, Utf16BE, Utf32, Utf32LE, Utf32BE };
void encode_primitive(Attachment &, Handle text, TextEncoding, Handle errors, ReturnSlot &);
void decode_primitive(Attachment &, Handle bytes, TextEncoding, Handle errors, bool final,
                      ReturnSlot &);
void decode_extended(Attachment &, Handle bytes, TextEncoding, Handle errors, int byteorder,
                     bool final, ReturnSlot &);
void builtin_codec_error(Attachment &, std::u32string_view name, Handle exception, ReturnSlot &);
void encode_charmap(Attachment &, Handle text, Handle errors, Handle mapping, ReturnSlot &);
void decode_charmap(Attachment &, Handle bytes, Handle errors, Handle mapping, ReturnSlot &);
void build_charmap(Attachment &, Handle table, ReturnSlot &);
void encode_text(Attachment &, Handle text, Handle encoding, Handle errors, ReturnSlot &);
void decode_bytes(Attachment &, Handle bytes, Handle encoding, Handle errors, ReturnSlot &);
void install_codecs(Attachment &, std::span<const Handle>);
} // namespace cxxpython::core::detail
