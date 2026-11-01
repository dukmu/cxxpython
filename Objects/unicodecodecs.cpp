#include "cxxpython/memoryobject.hpp"
#include "cxxpython/parser.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecregistry.hpp"
#include "internal/codecs.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <limits>

namespace cxxpython::core {
namespace {
enum class Encoding { Utf8, Ascii, Latin1, Utf16, Utf32, Charmap, RawUnicodeEscape };
enum class Handler {
    Strict,
    Ignore,
    Replace,
    SurrogatePass,
    SurrogateEscape,
    BackslashReplace,
    Unknown
};

bool apply_codec_error(Attachment &, RootScope &, bool, Encoding, Handle &, Handle, std::size_t,
                       std::size_t, std::u32string, std::string &, std::u32string &, std::size_t &,
                       ReturnSlot &, int, Handle *mapping = nullptr);

bool string_value(Attachment &ctx, Value value)
{
    return ctx.has_layout<StrObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::StrType));
}

std::u32string ascii_text(std::string_view text)
{
    return {text.begin(), text.end()};
}

bool options(Attachment &ctx, Handle encoding, Handle errors, Encoding &codec, Handler &handler,
             ReturnSlot &out)
{
    auto none = ctx.builtin(CoreObject::None);
    std::u32string name = U"utf-8";
    if (encoding.get(ctx) != none) {
        if (!string_value(ctx, encoding.get(ctx))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"encoding must be a string", out);
            return false;
        }
        name = ctx.payload<StrObject>(encoding.get(ctx)).value;
    }
    std::u32string normalized;
    bool separator = false;
    for (char32_t c : name) {
        if ((c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z')) {
            if (separator && !normalized.empty())
                normalized.push_back(U'_');
            separator = false;
            normalized.push_back(c >= U'A' && c <= U'Z' ? c + 32 : c);
        } else
            separator = true;
    }
    if (normalized == U"utf_8" || normalized == U"utf8" || normalized == U"u8" ||
        normalized == U"utf" || normalized == U"cp65001")
        codec = Encoding::Utf8;
    else if (normalized == U"ascii" || normalized == U"us_ascii" || normalized == U"646" ||
             normalized == U"ansi_x3_4_1968" || normalized == U"ansi_x3_4_1986" ||
             normalized == U"iso646_us" || normalized == U"iso_646_irv_1991" ||
             normalized == U"iso_ir_6" || normalized == U"us" || normalized == U"ibm367" ||
             normalized == U"cp367" || normalized == U"csascii")
        codec = Encoding::Ascii;
    else if (normalized == U"latin_1" || normalized == U"latin1" || normalized == U"latin" ||
             normalized == U"l1" || normalized == U"iso_8859_1" || normalized == U"iso8859_1" ||
             normalized == U"iso8859" || normalized == U"iso_8859_1_1987" ||
             normalized == U"iso_ir_100" || normalized == U"cp819" || normalized == U"ibm819")
        codec = Encoding::Latin1;
    else if (normalized == U"raw_unicode_escape" && name.find(U'.') == std::u32string::npos)
        codec = Encoding::RawUnicodeEscape;
    else {
        raise_error(ctx, CoreObject::LookupErrorType, U"unknown encoding: " + name, out);
        return false;
    }
    std::u32string policy = U"strict";
    if (errors.get(ctx) != none) {
        if (!string_value(ctx, errors.get(ctx))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"errors must be a string", out);
            return false;
        }
        policy = ctx.payload<StrObject>(errors.get(ctx)).value;
    }
    if (policy == U"strict")
        handler = Handler::Strict;
    else if (policy == U"ignore")
        handler = Handler::Ignore;
    else if (policy == U"replace")
        handler = Handler::Replace;
    else if (policy == U"surrogatepass")
        handler = Handler::SurrogatePass;
    else if (policy == U"surrogateescape")
        handler = Handler::SurrogateEscape;
    else if (policy == U"backslashreplace")
        handler = Handler::BackslashReplace;
    else
        handler = Handler::Unknown;
    return true;
}

const char32_t *codec_name(Encoding codec)
{
    return codec == Encoding::Utf8               ? U"utf-8"
           : codec == Encoding::Ascii            ? U"ascii"
           : codec == Encoding::Latin1           ? U"latin-1"
           : codec == Encoding::Utf16            ? U"utf-16"
           : codec == Encoding::Utf32            ? U"utf-32"
           : codec == Encoding::RawUnicodeEscape ? U"rawunicodeescape"
                                                 : U"charmap";
}

// The tuple, source object and each field remain rooted across every allocation.
void unicode_error(Attachment &ctx, bool encode, Encoding codec, Handle object, std::size_t start,
                   std::size_t end, std::u32string reason, ReturnSlot &out, int order = -1)
{
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    const char32_t *name = codec == Encoding::Utf16   ? (order < 0 ? U"utf-16-le" : U"utf-16-be")
                           : codec == Encoding::Utf32 ? (order < 0 ? U"utf-32-le" : U"utf-32-be")
                                                      : codec_name(codec);
    make_str(ctx, name, temp);
    auto encoding = roots.root(temp.value());
    make_int(ctx, static_cast<std::int64_t>(start), temp);
    auto first = roots.root(temp.value());
    make_int(ctx, static_cast<std::int64_t>(end), temp);
    auto last = roots.root(temp.value());
    make_str(ctx, std::move(reason), temp);
    auto why = roots.root(temp.value());
    std::array fields{encoding.get(ctx), object.get(ctx), first.get(ctx), last.get(ctx),
                      why.get(ctx)};
    cxxpython::core::make_tuple(ctx, fields, temp);
    auto arguments = roots.root(temp.value());
    auto type = roots.root(ctx.builtin(encode ? CoreObject::UnicodeEncodeErrorType
                                              : CoreObject::UnicodeDecodeErrorType));
    ctx.allocate_instance<ExceptionObject>(type, temp, arguments.get(ctx));
    auto error = roots.root(temp.value());
    for (std::size_t i = 0; i < fields.size(); ++i)
        ctx.store(error.get(ctx), ctx.instance_slot(error.get(ctx), i), fields[i]);
    detail::link_exception_context(ctx, error.get(ctx));
    out.raised(error.get(ctx));
}

void append_utf8(std::string &output, char32_t ch)
{
    if (ch < 0x80)
        output.push_back(static_cast<char>(ch));
    else if (ch < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (ch >> 6)));
        output.push_back(static_cast<char>(0x80 | (ch & 63)));
    } else if (ch < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (ch >> 12)));
        output.push_back(static_cast<char>(0x80 | ((ch >> 6) & 63)));
        output.push_back(static_cast<char>(0x80 | (ch & 63)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (ch >> 18)));
        output.push_back(static_cast<char>(0x80 | ((ch >> 12) & 63)));
        output.push_back(static_cast<char>(0x80 | ((ch >> 6) & 63)));
        output.push_back(static_cast<char>(0x80 | (ch & 63)));
    }
}

void append_escape(std::string &output, char32_t ch)
{
    char buffer[16];
    if (ch <= 0xff)
        std::snprintf(buffer, sizeof(buffer), "\\x%02x", static_cast<unsigned>(ch));
    else if (ch <= 0xffff)
        std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
    else
        std::snprintf(buffer, sizeof(buffer), "\\U%08x", static_cast<unsigned>(ch));
    output += buffer;
}

template <std::size_t Index> void get_field(Attachment &ctx, Value owner, ReturnSlot &out)
{
    ctx.load(owner, ctx.instance_slot(owner, Index), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(ctx.builtin(CoreObject::None));
}

template <std::size_t Index>
void set_field(Attachment &ctx, Value owner, std::optional<Value> value, ReturnSlot &out)
{
    if constexpr (Index == 2 || Index == 3) {
        std::int64_t number;
        if (!value || !ctx.has_layout<IntObject>(*value) || !int_fits_signed(ctx, *value, number)) {
            raise_error(ctx, CoreObject::TypeErrorType, U"an integer is required", out);
            return;
        }
    }
    ctx.store(owner, ctx.instance_slot(owner, Index), value);
    out.ok(ctx.builtin(CoreObject::None));
}

template <bool Encode>
void error_init(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    Value self = args[0];
    auto type = Encode ? CoreObject::UnicodeEncodeErrorType : CoreObject::UnicodeDecodeErrorType;
    if (!ctx.has_layout<ExceptionObject>(self) ||
        !is_subtype(ctx, ctx.type_of(self), ctx.builtin(type))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error instance", out);
        return;
    }
    ctx.store(self, ctx.payload<ExceptionObject>(self).args, args[1]);
    ctx.store(self, ctx.instance_slot(self, 0), std::nullopt);
    ctx.store(self, ctx.instance_slot(self, 1), std::nullopt);
    ctx.store(self, ctx.instance_slot(self, 4), std::nullopt);
    const auto &items = ctx.payload<TupleObject>(args[1]).items;
    std::int64_t first, last;
    if (items.size() != 5 || !string_value(ctx, items[0]) ||
        (Encode ? !string_value(ctx, items[1]) : !is_bytes(ctx, items[1])) ||
        !ctx.has_layout<IntObject>(items[2]) || !int_fits_signed(ctx, items[2], first) ||
        !ctx.has_layout<IntObject>(items[3]) || !int_fits_signed(ctx, items[3], last) ||
        !string_value(ctx, items[4])) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error arguments", out);
        return;
    }
    for (std::size_t i = 0; i < 5; ++i)
        ctx.store(self, ctx.instance_slot(self, i), items[i]);
    out.ok(ctx.builtin(CoreObject::None));
}

template <bool Encode>
void error_str(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    std::array<Handle, 5> fields;
    ReturnSlot temp(ctx);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        ctx.load(self.get(ctx), ctx.instance_slot(self.get(ctx), i), temp);
        if (temp.tag() == ResultTag::Empty && i == 1) {
            make_str(ctx, U"", out);
            return;
        }
        fields[i] = roots.root(temp.tag() == ResultTag::Empty ? ctx.builtin(CoreObject::None)
                                                              : temp.value());
    }
    if (fields[1].get(ctx) == ctx.builtin(CoreObject::None)) {
        make_str(ctx, U"", out);
        return;
    }
    if ((Encode && !string_value(ctx, fields[1].get(ctx))) ||
        (!Encode && !is_bytes(ctx, fields[1].get(ctx)))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error object", out);
        return;
    }
    std::int64_t start, end;
    if (!ctx.has_layout<IntObject>(fields[2].get(ctx)) ||
        !ctx.has_layout<IntObject>(fields[3].get(ctx)) ||
        !int_fits_signed(ctx, fields[2].get(ctx), start) ||
        !int_fits_signed(ctx, fields[3].get(ctx), end)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error position", out);
        return;
    }
    str(ctx, fields[0], temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    auto encoding = roots.root(temp.value());
    str(ctx, fields[4], temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    std::u32string message =
        U"'" + ctx.payload<StrObject>(encoding.get(ctx)).value + U"' codec can't ";
    auto size = Encode ? ctx.payload<StrObject>(fields[1].get(ctx)).value.size()
                       : ctx.payload<BytesObject>(fields[1].get(ctx)).value.size();
    bool single = start >= 0 && static_cast<std::uint64_t>(start) < size && end > 0 &&
                  static_cast<std::uint64_t>(end) <= size && end - 1 == start;
    if (single) {
        char32_t bad = Encode ? ctx.payload<StrObject>(fields[1].get(ctx)).value[start]
                              : static_cast<unsigned char>(
                                    ctx.payload<BytesObject>(fields[1].get(ctx)).value[start]);
        std::string escaped;
        append_escape(escaped, bad);
        if (Encode)
            message += U"encode character '" + ascii_text(escaped) + U"' in position ";
        else
            message += U"decode byte 0x" + ascii_text(escaped.substr(2)) + U" in position ";
        message += ascii_text(std::to_string(start));
    } else {
        message += Encode ? U"encode characters in position " : U"decode bytes in position ";
        // Match CPython's index-width wrap without C++ signed overflow.
        const auto last = end == std::numeric_limits<std::int64_t>::min()
                              ? std::numeric_limits<std::int64_t>::max()
                              : end - 1;
        message += ascii_text(std::to_string(start)) + U"-" + ascii_text(std::to_string(last));
    }
    message += U": " + ctx.payload<StrObject>(temp.value()).value;
    make_str(ctx, std::move(message), out);
}

void string_encode(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(ctx, args[1]) || !string_value(ctx, args[2])) {
        raise_error(ctx, CoreObject::TypeErrorType, U"encoding and errors must be strings", out);
        return;
    }
    RootScope roots(ctx);
    detail::encode_text(ctx, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}

std::optional<detail::TextEncoding> wide_name(Attachment &ctx, Handle encoding)
{
    if (!string_value(ctx, encoding.get(ctx)))
        return std::nullopt;
    std::u32string name;
    for (auto ch : ctx.payload<StrObject>(encoding.get(ctx)).value) {
        if (ch >= U'A' && ch <= U'Z')
            ch += 32;
        if (ch != U'-' && ch != U'_' && ch != U' ')
            name.push_back(ch);
    }
    using E = detail::TextEncoding;
    if (name == U"utf16")
        return E::Utf16;
    if (name == U"utf16le")
        return E::Utf16LE;
    if (name == U"utf16be")
        return E::Utf16BE;
    if (name == U"utf32")
        return E::Utf32;
    if (name == U"utf32le")
        return E::Utf32LE;
    if (name == U"utf32be")
        return E::Utf32BE;
    return std::nullopt;
}

void registry_transform(Attachment &ctx, Handle input, Handle encoding, Handle errors, bool encode,
                        ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    detail::lookup_codec(ctx, encoding, temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    auto info = roots.root(temp.value());
    if (ctx.type_of(info.get(ctx)) != ctx.builtin(CoreObject::TupleType)) {
        make_str(ctx, U"_is_text_encoding", temp);
        auto attr = roots.root(temp.value());
        get_attribute(ctx, info, attr, temp);
        if (temp.tag() == ResultTag::Raised) {
            if (!is_subtype(ctx, ctx.type_of(temp.exception()),
                            ctx.builtin(CoreObject::AttributeErrorType))) {
                out.raised(temp.exception());
                return;
            }
        } else {
            auto flag = roots.root(temp.value());
            truth(ctx, flag, temp);
            if (temp.tag() == ResultTag::Raised) {
                out.raised(temp.exception());
                return;
            }
            if (temp.value() == ctx.builtin(CoreObject::False)) {
                raise_error(ctx, CoreObject::LookupErrorType, U"codec is not a text encoding", out);
                return;
            }
        }
    }
    auto callable = roots.root(ctx.payload<TupleObject>(info.get(ctx)).items[encode ? 0 : 1]);
    std::array args{input.get(ctx), errors.get(ctx)};
    auto count = errors.get(ctx) == ctx.builtin(CoreObject::None) ? 1u : 2u;
    call(ctx, callable, CallArgsView{std::span<const Value>(args.data(), count)}, temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    auto result = roots.root(temp.value());
    if (!ctx.has_layout<TupleObject>(result.get(ctx)) ||
        ctx.payload<TupleObject>(result.get(ctx)).items.size() != 2) {
        raise_error(ctx, CoreObject::TypeErrorType, U"codec must return a tuple (result, consumed)",
                    out);
        return;
    }
    auto value = ctx.payload<TupleObject>(result.get(ctx)).items[0];
    if (encode ? !is_bytes(ctx, value) : !string_value(ctx, value)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"codec returned an invalid result", out);
        return;
    }
    out.ok(value);
}
} // namespace

void detail::encode_text(Attachment &ctx, Handle text, Handle encoding, Handle errors,
                         ReturnSlot &out)
{
    if (!string_value(ctx, text.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"encoding requires str", out);
        return;
    }
    if (auto wide = wide_name(ctx, encoding)) {
        RootScope roots(ctx);
        ReturnSlot temp(ctx);
        encode_primitive(ctx, text, *wide, errors, temp);
        if (temp.tag() == ResultTag::Raised)
            out.raised(temp.exception());
        else
            out.ok(ctx.payload<TupleObject>(temp.value()).items[0]);
        return;
    }
    Encoding codec;
    Handler handler;
    ReturnSlot option_result(ctx);
    if (!options(ctx, encoding, errors, codec, handler, option_result)) {
        if (encoding.get(ctx) != ctx.builtin(CoreObject::None) &&
            string_value(ctx, encoding.get(ctx)) &&
            (errors.get(ctx) == ctx.builtin(CoreObject::None) ||
             string_value(ctx, errors.get(ctx))))
            registry_transform(ctx, text, encoding, errors, true, out);
        else
            out.raised(option_result.exception());
        return;
    }
    RootScope roots(ctx);
    auto source = roots.root(text.get(ctx));
    std::string result;
    std::u32string unused;
    auto input = ctx.payload<StrObject>(source.get(ctx)).value;
    if (codec == Encoding::RawUnicodeEscape) {
        // CPython 3.12.14 PyUnicode_AsRawUnicodeEscapeString; PSF license.
        // Latin-1, including backslashes, is literal; only wider ordinals escape.
        for (char32_t ch : input) {
            if (ch < 0x100)
                result.push_back(static_cast<char>(ch));
            else
                append_escape(result, ch);
        }
        make_bytes(ctx, std::move(result), out);
        return;
    }
    for (std::size_t i = 0;;) {
        if (i >= input.size())
            break;
        char32_t ch = input[i];
        bool surrogate = ch >= 0xd800 && ch <= 0xdfff;
        bool valid = codec == Encoding::Utf8
                         ? ch <= 0x10ffff && (!surrogate || handler == Handler::SurrogatePass)
                         : ch < (codec == Encoding::Ascii ? 0x80 : 0x100);
        if (valid) {
            if (codec == Encoding::Utf8)
                append_utf8(result, ch);
            else
                result.push_back(static_cast<char>(ch));
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        if (codec != Encoding::Utf8) {
            auto limit = codec == Encoding::Ascii ? 0x80u : 0x100u;
            while (end < input.size() && input[end] >= limit)
                ++end;
        } else {
            while (end < input.size() && input[end] >= 0xd800 && input[end] <= 0xdfff)
                ++end;
        }
        if (handler == Handler::Ignore) {
            i = end;
            continue;
        }
        if (handler == Handler::Replace) {
            result.append(end - i, '?');
            i = end;
            continue;
        }
        if (handler == Handler::BackslashReplace) {
            for (; i < end; ++i)
                append_escape(result, input[i]);
            continue;
        }
        if (handler == Handler::SurrogateEscape) {
            std::size_t j = i;
            while (j < end && input[j] >= 0xdc80 && input[j] <= 0xdcff)
                result.push_back(static_cast<char>(input[j++] - 0xdc00));
            if (j != i) {
                i = j;
                continue;
            }
        }
        auto previous_source = source.get(ctx);
        if (!apply_codec_error(ctx, roots, true, codec, source, errors, i, end,
                               codec == Encoding::Utf8    ? U"surrogates not allowed"
                               : codec == Encoding::Ascii ? U"ordinal not in range(128)"
                                                          : U"ordinal not in range(256)",
                               result, unused, i, out, -1))
            return;
        if (source.get(ctx) != previous_source)
            input = ctx.payload<StrObject>(source.get(ctx)).value;
    }
    make_bytes(ctx, std::move(result), out);
}

void detail::decode_bytes(Attachment &ctx, Handle bytes, Handle encoding, Handle errors,
                          ReturnSlot &out)
{
    if (!is_bytes(ctx, bytes.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"decoding requires bytes", out);
        return;
    }
    if (auto wide = wide_name(ctx, encoding)) {
        ReturnSlot temp(ctx);
        decode_primitive(ctx, bytes, *wide, errors, true, temp);
        if (temp.tag() == ResultTag::Raised)
            out.raised(temp.exception());
        else
            out.ok(ctx.payload<TupleObject>(temp.value()).items[0]);
        return;
    }
    Encoding codec;
    Handler handler;
    ReturnSlot option_result(ctx);
    if (!options(ctx, encoding, errors, codec, handler, option_result)) {
        if (encoding.get(ctx) != ctx.builtin(CoreObject::None) &&
            string_value(ctx, encoding.get(ctx)) &&
            (errors.get(ctx) == ctx.builtin(CoreObject::None) ||
             string_value(ctx, errors.get(ctx))))
            registry_transform(ctx, bytes, encoding, errors, false, out);
        else
            out.raised(option_result.exception());
        return;
    }
    if (codec == Encoding::RawUnicodeEscape) {
        ReturnSlot temp(ctx);
        decode_primitive(ctx, bytes, TextEncoding::RawUnicodeEscape, errors, true, temp);
        if (temp.tag() == ResultTag::Raised)
            out.raised(temp.exception());
        else
            out.ok(ctx.payload<TupleObject>(temp.value()).items[0]);
        return;
    }
    RootScope roots(ctx);
    auto source = roots.root(bytes.get(ctx));
    std::u32string result;
    std::string unused;
    auto input = ctx.payload<BytesObject>(source.get(ctx)).value;
    for (std::size_t i = 0;;) {
        if (i >= input.size())
            break;
        auto byte = [&](std::size_t n) { return static_cast<unsigned char>(input[n]); };
        unsigned first = byte(i);
        if (first < 0x80 || codec == Encoding::Latin1) {
            result.push_back(first);
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        const char32_t *reason =
            codec == Encoding::Ascii ? U"ordinal not in range(128)" : U"invalid start byte";
        if (codec == Encoding::Utf8 && first >= 0xc2 && first <= 0xf4) {
            std::size_t width = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            char32_t ch = first & (width == 2 ? 0x1f : width == 3 ? 0x0f : 0x07);
            bool invalid = false;
            while (end < i + width && end < input.size()) {
                unsigned next = byte(end);
                bool permitted = next >= 0x80 && next <= 0xbf;
                if (end == i + 1) {
                    if (first == 0xe0 && next < 0xa0)
                        permitted = false;
                    if (first == 0xed && next >= 0xa0 && handler != Handler::SurrogatePass)
                        permitted = false;
                    if (first == 0xf0 && next < 0x90)
                        permitted = false;
                    if (first == 0xf4 && next >= 0x90)
                        permitted = false;
                }
                if (!permitted) {
                    invalid = true;
                    break;
                }
                ch = (ch << 6) | (next & 0x3f);
                ++end;
            }
            if (end == i + width) {
                result.push_back(ch);
                i = end;
                continue;
            }
            reason = invalid ? U"invalid continuation byte" : U"unexpected end of data";
        }
        if (handler == Handler::Ignore) {
            i = end;
            continue;
        }
        if (handler == Handler::Replace) {
            result.push_back(0xfffd);
            i = end;
            continue;
        }
        if (handler == Handler::BackslashReplace) {
            std::string escaped;
            for (; i < end; ++i)
                append_escape(escaped, byte(i));
            result += ascii_text(escaped);
            continue;
        }
        if (handler == Handler::SurrogateEscape) {
            for (; i < end; ++i)
                result.push_back(0xdc00 + byte(i));
            continue;
        }
        auto previous_source = source.get(ctx);
        if (!apply_codec_error(ctx, roots, false, codec, source, errors, i, end, reason, unused,
                               result, i, out, -1))
            return;
        if (source.get(ctx) != previous_source)
            input = ctx.payload<BytesObject>(source.get(ctx)).value;
    }
    make_str(ctx, std::move(result), out);
}

namespace {
bool wide_encoding(detail::TextEncoding encoding)
{
    return encoding == detail::TextEncoding::Utf16 || encoding == detail::TextEncoding::Utf16LE ||
           encoding == detail::TextEncoding::Utf16BE || encoding == detail::TextEncoding::Utf32 ||
           encoding == detail::TextEncoding::Utf32LE || encoding == detail::TextEncoding::Utf32BE;
}

bool wide16(detail::TextEncoding encoding)
{
    return encoding == detail::TextEncoding::Utf16 || encoding == detail::TextEncoding::Utf16LE ||
           encoding == detail::TextEncoding::Utf16BE;
}

int default_order(detail::TextEncoding encoding)
{
    if (encoding == detail::TextEncoding::Utf16LE || encoding == detail::TextEncoding::Utf32LE)
        return -1;
    if (encoding == detail::TextEncoding::Utf16BE || encoding == detail::TextEncoding::Utf32BE)
        return 1;
    return std::endian::native == std::endian::little ? -1 : 1;
}

void append_unit(std::string &result, std::uint32_t unit, unsigned width, int order)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned shift = 8 * (order < 0 ? i : width - i - 1);
        result.push_back(static_cast<char>((unit >> shift) & 0xff));
    }
}

std::uint32_t read_unit(std::string_view input, std::size_t pos, unsigned width, int order)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        unsigned shift = 8 * (order < 0 ? i : width - i - 1);
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[pos + i])) << shift;
    }
    return value;
}

const char32_t *primitive_name(detail::TextEncoding encoding)
{
    switch (encoding) {
    case detail::TextEncoding::Utf8:
        return U"utf-8";
    case detail::TextEncoding::Ascii:
        return U"ascii";
    case detail::TextEncoding::Latin1:
        return U"latin-1";
    case detail::TextEncoding::RawUnicodeEscape:
        return U"raw-unicode-escape";
    default:
        return nullptr;
    }
}

void primitive_result(Attachment &ctx, Handle value, std::size_t consumed, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    make_int(ctx, static_cast<std::int64_t>(consumed), temp);
    auto count = roots.root(temp.value());
    std::array items{value.get(ctx), count.get(ctx)};
    cxxpython::core::make_tuple(ctx, items, out);
}

bool apply_codec_error(Attachment &ctx, RootScope &caller_roots, bool encode, Encoding codec,
                       Handle &object, Handle errors, std::size_t start, std::size_t end,
                       std::u32string reason, std::string &encoded, std::u32string &decoded,
                       std::size_t &position, ReturnSlot &out, int order, Handle *mapping)
{
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    unicode_error(ctx, encode, codec, object, start, end, std::move(reason), temp, order);
    auto exception = roots.root(temp.exception());
    if (errors.get(ctx) == ctx.builtin(CoreObject::None)) {
        out.raised(exception.get(ctx));
        return false;
    }
    auto name = ctx.payload<StrObject>(errors.get(ctx)).value;
    if (name == U"strict" || name == U"ignore" || name == U"replace" ||
        name == U"backslashreplace" || name == U"xmlcharrefreplace" || name == U"surrogateescape" ||
        name == U"surrogatepass") {
        detail::builtin_codec_error(ctx, name, exception, temp);
    } else {
        detail::lookup_codec_error(ctx, errors, temp);
        if (temp.tag() == ResultTag::Raised) {
            out.raised(temp.exception());
            return false;
        }
        auto handler = roots.root(temp.value());
        std::array args{exception.get(ctx)};
        call(ctx, handler, CallArgsView{args}, temp);
    }
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return false;
    }
    auto result = roots.root(temp.value());
    if (!ctx.has_layout<TupleObject>(result.get(ctx)) ||
        ctx.payload<TupleObject>(result.get(ctx)).items.size() != 2) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"error handler must return a tuple of (replacement, new position)", out);
        return false;
    }
    auto replacement = roots.root(ctx.payload<TupleObject>(result.get(ctx)).items[0]);
    auto index = roots.root(ctx.payload<TupleObject>(result.get(ctx)).items[1]);
    std::int64_t next;
    if (!ctx.has_layout<IntObject>(index.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"position from error handler must be an integer", out);
        return false;
    }
    if (!int_fits_signed(ctx, index.get(ctx), next)) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"position from error handler is too large",
                    out);
        return false;
    }
    ctx.load(exception.get(ctx), ctx.instance_slot(exception.get(ctx), 1), temp);
    if (temp.tag() == ResultTag::Empty ||
        (encode ? !string_value(ctx, temp.value()) : !is_bytes(ctx, temp.value()))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error object", out);
        return false;
    }
    if (object.get(ctx) != temp.value())
        object = caller_roots.root(temp.value());
    auto size = encode ? ctx.payload<StrObject>(object.get(ctx)).value.size()
                       : ctx.payload<BytesObject>(object.get(ctx)).value.size();
    if (next < 0 && size <= static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        next += static_cast<std::int64_t>(size);
    if (next < 0 || static_cast<std::uint64_t>(next) > size) {
        raise_error(ctx, CoreObject::IndexErrorType, U"position from error handler out of bounds",
                    out);
        return false;
    }
    if (encode && is_bytes(ctx, replacement.get(ctx)))
        encoded += ctx.payload<BytesObject>(replacement.get(ctx)).value;
    else if (!encode && string_value(ctx, replacement.get(ctx)))
        decoded += ctx.payload<StrObject>(replacement.get(ctx)).value;
    else if (encode && string_value(ctx, replacement.get(ctx))) {
        if (codec == Encoding::Charmap && mapping) {
            ReturnSlot replacement_result(ctx);
            auto strict_name = roots.root(ctx.builtin(CoreObject::None));
            detail::encode_charmap(ctx, replacement, strict_name, *mapping, replacement_result);
            if (replacement_result.tag() == ResultTag::Raised) {
                out.raised(exception.get(ctx));
                return false;
            }
            auto tuple = roots.root(replacement_result.value());
            encoded +=
                ctx.payload<BytesObject>(ctx.payload<TupleObject>(tuple.get(ctx)).items[0]).value;
            position = static_cast<std::size_t>(next);
            return true;
        }
        const auto copy = ctx.payload<StrObject>(replacement.get(ctx)).value;
        for (char32_t ch : copy) {
            if (codec == Encoding::Utf8 && !(ch >= 0xd800 && ch <= 0xdfff))
                append_utf8(encoded, ch);
            else if (codec == Encoding::Ascii && ch < 128)
                encoded.push_back(static_cast<char>(ch));
            else if (codec == Encoding::Latin1 && ch < 256)
                encoded.push_back(static_cast<char>(ch));
            else if ((codec == Encoding::Utf16 || codec == Encoding::Utf32) &&
                     !(ch >= 0xd800 && ch <= 0xdfff) && ch <= 0x10ffff) {
                if (codec == Encoding::Utf32)
                    append_unit(encoded, ch, 4, order);
                else if (ch < 0x10000)
                    append_unit(encoded, ch, 2, order);
                else {
                    ch -= 0x10000;
                    append_unit(encoded, 0xd800 + (ch >> 10), 2, order);
                    append_unit(encoded, 0xdc00 + (ch & 0x3ff), 2, order);
                }
            } else {
                out.raised(exception.get(ctx));
                return false;
            }
        }
    } else {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid error handler replacement", out);
        return false;
    }
    position = static_cast<std::size_t>(next);
    return true;
}
} // namespace

void detail::builtin_codec_error(Attachment &ctx, std::u32string_view name, Handle exception,
                                 ReturnSlot &out)
{
    if (name == U"strict") {
        if (is_subtype(ctx, ctx.type_of(exception), ctx.builtin(CoreObject::BaseExceptionType)))
            out.raised(exception.get(ctx));
        else
            raise_error(ctx, CoreObject::TypeErrorType,
                        U"codec error handler requires an exception", out);
        return;
    }
    bool encode = is_subtype(ctx, ctx.type_of(exception.get(ctx)),
                             ctx.builtin(CoreObject::UnicodeEncodeErrorType));
    bool decode = is_subtype(ctx, ctx.type_of(exception.get(ctx)),
                             ctx.builtin(CoreObject::UnicodeDecodeErrorType));
    if (!encode && !decode) {
        raise_error(ctx, CoreObject::TypeErrorType, U"codec error handler requires a Unicode error",
                    out);
        return;
    }
    if (decode && (name == U"xmlcharrefreplace" || name == U"namereplace")) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"don't know how to handle UnicodeDecodeError in error callback", out);
        return;
    }
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    std::array<Handle, 5> fields;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        ctx.load(exception.get(ctx), ctx.instance_slot(exception.get(ctx), i), temp);
        if (temp.tag() == ResultTag::Empty) {
            raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error", out);
            return;
        }
        fields[i] = roots.root(temp.value());
    }
    std::int64_t first, last;
    if ((encode ? !string_value(ctx, fields[1].get(ctx)) : !is_bytes(ctx, fields[1].get(ctx))) ||
        !ctx.has_layout<IntObject>(fields[2].get(ctx)) ||
        !int_fits_signed(ctx, fields[2].get(ctx), first) ||
        !ctx.has_layout<IntObject>(fields[3].get(ctx)) ||
        !int_fits_signed(ctx, fields[3].get(ctx), last)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid Unicode error fields", out);
        return;
    }
    auto size = encode ? ctx.payload<StrObject>(fields[1].get(ctx)).value.size()
                       : ctx.payload<BytesObject>(fields[1].get(ctx)).value.size();
    auto start = static_cast<std::size_t>(std::max<std::int64_t>(0, first));
    auto end = static_cast<std::size_t>(std::max<std::int64_t>(0, last));
    start = std::min(start, size);
    end = std::min(std::max(end, start), size);
    std::u32string replacement;
    if (name == U"ignore") {
    } else if (name == U"replace") {
        replacement.assign(encode ? end - start : 1, encode ? U'?' : U'\ufffd');
    } else if (name == U"backslashreplace") {
        std::string text;
        for (auto i = start; i < end; ++i) {
            char32_t ch = encode ? ctx.payload<StrObject>(fields[1].get(ctx)).value[i]
                                 : static_cast<unsigned char>(
                                       ctx.payload<BytesObject>(fields[1].get(ctx)).value[i]);
            append_escape(text, ch);
        }
        replacement = ascii_text(text);
    } else if (name == U"namereplace" && encode) {
        std::string text;
        for (auto i = start; i < end; ++i) {
            const auto ch = ctx.payload<StrObject>(fields[1]).value[i];
            if (const auto character_name = syntax::unicode_character_name(ch))
                text += "\\N{" + *character_name + "}";
            else
                append_escape(text, ch);
        }
        replacement = ascii_text(text);
    } else if (name == U"xmlcharrefreplace" && encode) {
        std::string text;
        for (auto i = start; i < end; ++i)
            text += "&#" +
                    std::to_string(static_cast<std::uint32_t>(
                        ctx.payload<StrObject>(fields[1].get(ctx)).value[i])) +
                    ";";
        replacement = ascii_text(text);
    } else if (name == U"surrogateescape" && decode) {
        auto &bytes = ctx.payload<BytesObject>(fields[1].get(ctx)).value;
        std::size_t i = start;
        while (i < end && static_cast<unsigned char>(bytes[i]) >= 0x80)
            replacement.push_back(0xdc00 + static_cast<unsigned char>(bytes[i++]));
        if (i == start) {
            out.raised(exception.get(ctx));
            return;
        }
        end = i;
    } else if (name == U"surrogateescape" && encode) {
        std::string bytes;
        auto &input = ctx.payload<StrObject>(fields[1].get(ctx)).value;
        auto i = start;
        while (i < end && input[i] >= 0xdc80 && input[i] <= 0xdcff)
            bytes.push_back(static_cast<char>(input[i++] - 0xdc00));
        if (i == start) {
            out.raised(exception.get(ctx));
            return;
        }
        make_bytes(ctx, std::move(bytes), temp);
        auto value = roots.root(temp.value());
        primitive_result(ctx, value, i, out);
        return;
    } else if (name == U"surrogatepass") {
        if (!string_value(ctx, fields[0].get(ctx))) {
            out.raised(exception.get(ctx));
            return;
        }
        auto enc = ctx.payload<StrObject>(fields[0].get(ctx)).value;
        bool utf8 = enc == U"utf-8" || enc == U"utf8";
        bool is16 = enc == U"utf-16" || enc == U"utf-16-le" || enc == U"utf-16-be";
        bool is32 = enc == U"utf-32" || enc == U"utf-32-le" || enc == U"utf-32-be";
        int order = enc == U"utf-16-be" || enc == U"utf-32-be"   ? 1
                    : enc == U"utf-16-le" || enc == U"utf-32-le" ? -1
                    : std::endian::native == std::endian::little ? -1
                                                                 : 1;
        if (encode && (utf8 || is16 || is32)) {
            std::string bytes;
            auto &input = ctx.payload<StrObject>(fields[1].get(ctx)).value;
            auto i = start;
            while (i < end && input[i] >= 0xd800 && input[i] <= 0xdfff) {
                if (utf8)
                    append_utf8(bytes, input[i]);
                else
                    append_unit(bytes, input[i], is16 ? 2 : 4, order);
                ++i;
            }
            if (i != start) {
                make_bytes(ctx, std::move(bytes), temp);
                auto value = roots.root(temp.value());
                primitive_result(ctx, value, i, out);
                return;
            }
            out.raised(exception.get(ctx));
            return;
        } else if (decode && (utf8 || is16 || is32)) {
            auto &input = ctx.payload<BytesObject>(fields[1].get(ctx)).value;
            unsigned width = utf8 ? 3 : is16 ? 2 : 4;
            if (start + width <= size) {
                std::uint32_t ch;
                if (utf8) {
                    auto a = static_cast<unsigned char>(input[start]);
                    auto b = static_cast<unsigned char>(input[start + 1]);
                    auto c = static_cast<unsigned char>(input[start + 2]);
                    ch = a == 0xed && b >= 0xa0 && b <= 0xbf && c >= 0x80 && c <= 0xbf
                             ? ((a & 15) << 12) | ((b & 63) << 6) | (c & 63)
                             : 0;
                } else
                    ch = read_unit(input, start, width, order);
                if (ch >= 0xd800 && ch <= 0xdfff) {
                    replacement.push_back(ch);
                    end = start + width;
                }
            }
            if (replacement.empty()) {
                out.raised(exception.get(ctx));
                return;
            }
        } else {
            out.raised(exception.get(ctx));
            return;
        }
    } else {
        out.raised(exception.get(ctx));
        return;
    }
    make_str(ctx, std::move(replacement), temp);
    auto value = roots.root(temp.value());
    primitive_result(ctx, value, end, out);
}

void detail::encode_primitive(Attachment &ctx, Handle text, TextEncoding encoding, Handle errors,
                              ReturnSlot &out)
{
    if (wide_encoding(encoding)) {
        if (!string_value(ctx, text.get(ctx))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"encoding requires str", out);
            return;
        }
        if (errors.get(ctx) != ctx.builtin(CoreObject::None) &&
            !string_value(ctx, errors.get(ctx))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"errors must be a string", out);
            return;
        }
        RootScope roots(ctx);
        auto source = roots.root(text.get(ctx));
        std::string result;
        std::u32string unused;
        int order = default_order(encoding);
        bool is16 = wide16(encoding);
        bool bom = encoding == TextEncoding::Utf16 || encoding == TextEncoding::Utf32;
        if (bom)
            append_unit(result, 0xfeff, is16 ? 2 : 4, order);
        std::size_t i = 0;
        auto input = ctx.payload<StrObject>(source.get(ctx)).value;
        for (;;) {
            if (i >= input.size())
                break;
            char32_t ch = input[i];
            bool surrogate = ch >= 0xd800 && ch <= 0xdfff;
            bool pass = errors.get(ctx) != ctx.builtin(CoreObject::None) &&
                        string_value(ctx, errors.get(ctx)) &&
                        ctx.payload<StrObject>(errors.get(ctx)).value == U"surrogatepass";
            if (surrogate && !pass) {
                auto end = i + 1;
                while (end < input.size() && input[end] >= 0xd800 && input[end] <= 0xdfff)
                    ++end;
                auto previous_source = source.get(ctx);
                if (!apply_codec_error(ctx, roots, true, is16 ? Encoding::Utf16 : Encoding::Utf32,
                                       source, errors, i, end, U"surrogates not allowed", result,
                                       unused, i, out, order))
                    return;
                if (source.get(ctx) != previous_source)
                    input = ctx.payload<StrObject>(source.get(ctx)).value;
                continue;
            }
            if (!is16)
                append_unit(result, ch, 4, order);
            else if (ch < 0x10000)
                append_unit(result, ch, 2, order);
            else {
                ch -= 0x10000;
                append_unit(result, 0xd800 + (ch >> 10), 2, order);
                append_unit(result, 0xdc00 + (ch & 0x3ff), 2, order);
            }
            ++i;
        }
        ReturnSlot temp(ctx);
        make_bytes(ctx, std::move(result), temp);
        auto value = roots.root(temp.value());
        primitive_result(ctx, value, i, out);
        return;
    }
    auto name = primitive_name(encoding);
    if (!name) {
        raise_error(ctx, CoreObject::LookupErrorType, U"unsupported primitive encoding", out);
        return;
    }
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    make_str(ctx, name, temp);
    auto codec = roots.root(temp.value());
    encode_text(ctx, text, codec, errors, temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    auto value = roots.root(temp.value());
    primitive_result(ctx, value, ctx.payload<StrObject>(text.get(ctx)).value.size(), out);
}

void detail::decode_primitive(Attachment &ctx, Handle bytes, TextEncoding encoding, Handle errors,
                              bool final, ReturnSlot &out)
{
    RootScope input_roots(ctx);
    if (encoding == TextEncoding::RawUnicodeEscape && string_value(ctx, bytes.get(ctx))) {
        ReturnSlot temp(ctx);
        encode_primitive(ctx, bytes, TextEncoding::Utf8,
                         input_roots.root(ctx.builtin(CoreObject::None)), temp);
        if (temp.tag() == ResultTag::Raised) {
            out.raised(temp.exception());
            return;
        }
        bytes = input_roots.root(ctx.payload<TupleObject>(temp.value()).items[0]);
    }
    if (!is_bytes(ctx, bytes.get(ctx))) {
        std::string materialized;
        ReturnSlot temp(ctx);
        if (!buffer_bytes(ctx, bytes, materialized, temp)) {
            if (temp.tag() == ResultTag::Raised)
                out.raised(temp.exception());
            else
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"decoding requires a bytes-like object", out);
            return;
        }
        make_bytes(ctx, std::move(materialized), temp);
        bytes = input_roots.root(temp.value());
    }
    if (encoding == TextEncoding::RawUnicodeEscape) {
        if (errors.get(ctx) != ctx.builtin(CoreObject::None) &&
            !string_value(ctx, errors.get(ctx))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"errors must be a string", out);
            return;
        }
        // Adapted from CPython 3.12.14's stateful raw Unicode escape decoder;
        // see Objects/unicodeobject.c and doc/licenses/python-3.12.14.txt (PSF).
        RootScope roots(ctx);
        auto source = roots.root(bytes.get(ctx));
        auto input = ctx.payload<BytesObject>(source).value;
        std::u32string result;
        std::string unused;
        std::size_t position = 0;
        std::size_t consumed = input.size();
        while (position < input.size()) {
            auto ch = static_cast<unsigned char>(input[position++]);
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            const auto start = position - 1;
            if (position == input.size()) {
                if (!final) {
                    consumed = start;
                    break;
                }
                result.push_back(U'\\');
                continue;
            }
            const auto marker = input[position++];
            if (marker != 'u' && marker != 'U') {
                result.push_back(U'\\');
                result.push_back(static_cast<unsigned char>(marker));
                continue;
            }
            unsigned remaining = marker == 'u' ? 4 : 8;
            const char32_t *reason =
                marker == 'u' ? U"truncated \\uXXXX escape" : U"truncated \\UXXXXXXXX escape";
            char32_t ordinal = 0;
            while (remaining && position < input.size()) {
                const auto digit = static_cast<unsigned char>(input[position]);
                unsigned value;
                if (digit >= '0' && digit <= '9')
                    value = digit - '0';
                else if (digit >= 'a' && digit <= 'f')
                    value = digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F')
                    value = digit - 'A' + 10;
                else
                    break;
                ordinal = (ordinal << 4) | value;
                ++position;
                --remaining;
            }
            if (remaining && position == input.size() && !final) {
                consumed = start;
                break;
            }
            if (!remaining) {
                if (ordinal <= 0x10ffff) {
                    result.push_back(ordinal);
                    continue;
                }
                reason = U"\\Uxxxxxxxx out of range";
            }
            const auto previous_source = source.get(ctx);
            if (!apply_codec_error(ctx, roots, false, Encoding::RawUnicodeEscape, source, errors,
                                   start, position, reason, unused, result, position, out, -1))
                return;
            if (source.get(ctx) != previous_source)
                input = ctx.payload<BytesObject>(source).value;
        }
        ReturnSlot temp(ctx);
        make_str(ctx, std::move(result), temp);
        auto value = roots.root(temp.value());
        primitive_result(ctx, value, consumed, out);
        return;
    }
    if (wide_encoding(encoding)) {
        RootScope roots(ctx);
        ReturnSlot temp(ctx);
        int order = encoding == TextEncoding::Utf16LE || encoding == TextEncoding::Utf32LE   ? -1
                    : encoding == TextEncoding::Utf16BE || encoding == TextEncoding::Utf32BE ? 1
                                                                                             : 0;
        decode_extended(ctx, bytes, encoding, errors, order, final, temp);
        if (temp.tag() == ResultTag::Raised) {
            out.raised(temp.exception());
            return;
        }
        auto tuple = roots.root(temp.value());
        std::array items{ctx.payload<TupleObject>(tuple.get(ctx)).items[0],
                         ctx.payload<TupleObject>(tuple.get(ctx)).items[1]};
        cxxpython::core::make_tuple(ctx, items, out);
        return;
    }
    auto name = primitive_name(encoding);
    if (!name) {
        raise_error(ctx, CoreObject::LookupErrorType, U"unsupported primitive encoding", out);
        return;
    }
    if (!is_bytes(ctx, bytes.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"decoding requires bytes", out);
        return;
    }
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    auto source = bytes;
    const auto &input = ctx.payload<BytesObject>(bytes.get(ctx)).value;
    std::size_t consumed = input.size();
    if (!final && encoding == TextEncoding::Utf8 && consumed) {
        bool surrogate_pass = errors.get(ctx) != ctx.builtin(CoreObject::None) &&
                              string_value(ctx, errors.get(ctx)) &&
                              ctx.payload<StrObject>(errors.get(ctx)).value == U"surrogatepass";
        std::size_t start = consumed - 1;
        while (start && (static_cast<unsigned char>(input[start]) & 0xc0) == 0x80 &&
               consumed - start < 4)
            --start;
        unsigned lead = static_cast<unsigned char>(input[start]);
        std::size_t width = lead >= 0xc2 && lead <= 0xdf   ? 2
                            : lead >= 0xe0 && lead <= 0xef ? 3
                            : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                           : 0;
        if (width && consumed - start < width) {
            bool valid = true;
            for (std::size_t j = start + 1; j < consumed; ++j) {
                unsigned c = static_cast<unsigned char>(input[j]);
                if (c < 0x80 || c > 0xbf ||
                    (j == start + 1 && ((lead == 0xe0 && c < 0xa0) ||
                                        (lead == 0xed && c >= 0xa0 && !surrogate_pass) ||
                                        (lead == 0xf0 && c < 0x90) || (lead == 0xf4 && c >= 0x90))))
                    valid = false;
            }
            if (valid) {
                consumed = start;
                make_bytes(ctx, input.substr(0, consumed), temp);
                source = roots.root(temp.value());
            }
        }
    }
    make_str(ctx, name, temp);
    auto codec = roots.root(temp.value());
    decode_bytes(ctx, source, codec, errors, temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return;
    }
    auto value = roots.root(temp.value());
    primitive_result(ctx, value, consumed, out);
}

void detail::decode_extended(Attachment &ctx, Handle bytes, TextEncoding encoding, Handle errors,
                             int byteorder, bool final, ReturnSlot &out)
{
    if (!wide_encoding(encoding)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"wide decoder requires bytes", out);
        return;
    }
    if (errors.get(ctx) != ctx.builtin(CoreObject::None) && !string_value(ctx, errors.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"errors must be a string", out);
        return;
    }
    RootScope roots(ctx);
    if (!is_bytes(ctx, bytes.get(ctx))) {
        std::string materialized;
        ReturnSlot temp(ctx);
        if (!buffer_bytes(ctx, bytes, materialized, temp)) {
            if (temp.tag() == ResultTag::Raised)
                out.raised(temp.exception());
            else
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"decoding requires a bytes-like object", out);
            return;
        }
        make_bytes(ctx, std::move(materialized), temp);
        bytes = roots.root(temp.value());
    }
    auto source = roots.root(bytes.get(ctx));
    auto original = ctx.payload<BytesObject>(source.get(ctx)).value;
    bool is16 = wide16(encoding);
    unsigned width = is16 ? 2 : 4;
    int reported = byteorder < 0 ? -1 : byteorder > 0 ? 1 : 0;
    int order = reported ? reported : default_order(encoding);
    std::size_t i = 0;
    if (!reported && original.size() >= width) {
        if (read_unit(original, 0, width, -1) == 0xfeff) {
            reported = order = -1;
            i = width;
        } else if (read_unit(original, 0, width, 1) == 0xfeff) {
            reported = order = 1;
            i = width;
        }
    }
    std::u32string result;
    std::string unused;
    auto input = std::move(original);
    for (;;) {
        if (i >= input.size())
            break;
        std::size_t end = i;
        const char32_t *reason = U"truncated data";
        if (input.size() - i < width) {
            if (!final)
                break;
            end = input.size();
        } else {
            auto ch = read_unit(input, i, width, order);
            end = i + width;
            if (is16 && ch >= 0xd800 && ch <= 0xdbff) {
                if (input.size() - end < 2) {
                    if (!final)
                        break;
                    reason = U"unexpected end of data";
                } else {
                    auto low = read_unit(input, end, 2, order);
                    if (low >= 0xdc00 && low <= 0xdfff) {
                        result.push_back(0x10000 + ((ch - 0xd800) << 10) + (low - 0xdc00));
                        i = end + 2;
                        continue;
                    }
                    reason = U"illegal UTF-16 surrogate";
                }
            } else if (ch >= 0xd800 && ch <= 0xdfff) {
                reason = is16 ? U"illegal encoding"
                              : U"code point in surrogate code point range(0xd800, 0xe000)";
            } else if (ch > 0x10ffff) {
                reason = U"code point not in range(0x110000)";
            } else {
                result.push_back(ch);
                i = end;
                continue;
            }
            bool pass = errors.get(ctx) != ctx.builtin(CoreObject::None) &&
                        string_value(ctx, errors.get(ctx)) &&
                        ctx.payload<StrObject>(errors.get(ctx)).value == U"surrogatepass";
            if (pass && ch >= 0xd800 && ch <= 0xdfff) {
                result.push_back(ch);
                i = end;
                continue;
            }
        }
        auto previous_source = source.get(ctx);
        if (!apply_codec_error(ctx, roots, false, is16 ? Encoding::Utf16 : Encoding::Utf32, source,
                               errors, i, end, reason, unused, result, i, out, order))
            return;
        if (source.get(ctx) != previous_source)
            input = ctx.payload<BytesObject>(source.get(ctx)).value;
    }
    ReturnSlot temp(ctx);
    make_str(ctx, std::move(result), temp);
    auto text = roots.root(temp.value());
    make_int(ctx, static_cast<std::int64_t>(i), temp);
    auto count = roots.root(temp.value());
    make_int(ctx, reported, temp);
    auto endian = roots.root(temp.value());
    std::array items{text.get(ctx), count.get(ctx), endian.get(ctx)};
    cxxpython::core::make_tuple(ctx, items, out);
}

namespace {
enum class MappingStatus { Mapped, Undefined, Error };

MappingStatus charmap_item(Attachment &ctx, Handle mapping, std::uint32_t key, bool encode,
                           ReturnSlot &out)
{
    if (mapping.get(ctx) == ctx.builtin(CoreObject::None)) {
        if (key > 255)
            return MappingStatus::Undefined;
        make_int(ctx, key, out);
        return MappingStatus::Mapped;
    }
    if (!encode && string_value(ctx, mapping.get(ctx))) {
        const auto &table = ctx.payload<StrObject>(mapping.get(ctx)).value;
        if (key >= table.size() || table[key] == 0xfffe)
            return MappingStatus::Undefined;
        make_int(ctx, table[key], out);
        return MappingStatus::Mapped;
    }
    RootScope roots(ctx);
    ReturnSlot index(ctx);
    make_int(ctx, key, index);
    get_item(ctx, mapping, roots.root(index.value()), out);
    if (out.tag() == ResultTag::Raised) {
        if (is_subtype(ctx, ctx.type_of(out.exception()), ctx.builtin(CoreObject::LookupErrorType)))
            return MappingStatus::Undefined;
        return MappingStatus::Error;
    }
    if (out.value() == ctx.builtin(CoreObject::None))
        return MappingStatus::Undefined;
    if (!encode) {
        std::int64_t number;
        if ((ctx.has_layout<IntObject>(out.value()) && int_fits_signed(ctx, out.value(), number) &&
             number == 0xfffe) ||
            (string_value(ctx, out.value()) &&
             ctx.payload<StrObject>(out.value()).value == U"\ufffe"))
            return MappingStatus::Undefined;
    }
    return MappingStatus::Mapped;
}

bool charmap_errors_valid(Attachment &ctx, Handle errors, ReturnSlot &out)
{
    if (errors.get(ctx) == ctx.builtin(CoreObject::None) || string_value(ctx, errors.get(ctx)))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"errors must be a string", out);
    return false;
}
} // namespace

void detail::encode_charmap(Attachment &ctx, Handle text, Handle errors, Handle mapping,
                            ReturnSlot &out)
{
    if (!string_value(ctx, text.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"charmap encoding requires str", out);
        return;
    }
    if (!charmap_errors_valid(ctx, errors, out))
        return;
    RootScope roots(ctx);
    auto source = roots.root(text.get(ctx));
    auto input = ctx.payload<StrObject>(source.get(ctx)).value;
    const auto consumed = input.size();
    std::string result;
    std::u32string unused;
    std::size_t i = 0;
    ReturnSlot mapped(ctx);
    while (i < input.size()) {
        auto status = charmap_item(ctx, mapping, input[i], true, mapped);
        if (status == MappingStatus::Error) {
            out.raised(mapped.exception());
            return;
        }
        if (status == MappingStatus::Undefined) {
            auto end = i + 1;
            while (end < input.size()) {
                status = charmap_item(ctx, mapping, input[end], true, mapped);
                if (status == MappingStatus::Error) {
                    out.raised(mapped.exception());
                    return;
                }
                if (status != MappingStatus::Undefined)
                    break;
                ++end;
            }
            auto previous = source.get(ctx);
            if (!apply_codec_error(ctx, roots, true, Encoding::Charmap, source, errors, i, end,
                                   U"character maps to <undefined>", result, unused, i, out, -1,
                                   &mapping))
                return;
            if (source.get(ctx) != previous)
                input = ctx.payload<StrObject>(source.get(ctx)).value;
            continue;
        }
        auto value = roots.root(mapped.value());
        if (is_bytes(ctx, value.get(ctx))) {
            result += ctx.payload<BytesObject>(value.get(ctx)).value;
        } else {
            std::int64_t number;
            if (!ctx.has_layout<IntObject>(value.get(ctx))) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"character mapping must return integer, bytes or None", out);
                return;
            }
            if (!int_fits_signed(ctx, value.get(ctx), number) || number < 0 || number > 255) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"character mapping must be in range(256)", out);
                return;
            }
            result.push_back(static_cast<char>(number));
        }
        roots.release(value);
        ++i;
    }
    ReturnSlot temp(ctx);
    make_bytes(ctx, std::move(result), temp);
    auto value = roots.root(temp.value());
    primitive_result(ctx, value, consumed, out);
}

void detail::decode_charmap(Attachment &ctx, Handle bytes, Handle errors, Handle mapping,
                            ReturnSlot &out)
{
    if (!charmap_errors_valid(ctx, errors, out))
        return;
    RootScope roots(ctx);
    if (!is_bytes(ctx, bytes.get(ctx))) {
        std::string data;
        ReturnSlot temp(ctx);
        if (!buffer_bytes(ctx, bytes, data, temp)) {
            if (temp.tag() == ResultTag::Raised)
                out.raised(temp.exception());
            else
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"decoding requires a bytes-like object", out);
            return;
        }
        make_bytes(ctx, std::move(data), temp);
        bytes = roots.root(temp.value());
    }
    auto source = roots.root(bytes.get(ctx));
    auto input = ctx.payload<BytesObject>(source.get(ctx)).value;
    const auto consumed = input.size();
    std::u32string result;
    std::string unused;
    std::size_t i = 0;
    ReturnSlot mapped(ctx);
    while (i < input.size()) {
        auto status =
            charmap_item(ctx, mapping, static_cast<unsigned char>(input[i]), false, mapped);
        if (status == MappingStatus::Error) {
            out.raised(mapped.exception());
            return;
        }
        if (status == MappingStatus::Undefined) {
            auto previous = source.get(ctx);
            if (!apply_codec_error(ctx, roots, false, Encoding::Charmap, source, errors, i, i + 1,
                                   U"character maps to <undefined>", unused, result, i, out, -1))
                return;
            if (source.get(ctx) != previous)
                input = ctx.payload<BytesObject>(source.get(ctx)).value;
            continue;
        }
        auto value = roots.root(mapped.value());
        if (string_value(ctx, value.get(ctx))) {
            result += ctx.payload<StrObject>(value.get(ctx)).value;
        } else {
            std::int64_t number;
            if (!ctx.has_layout<IntObject>(value.get(ctx))) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"character mapping must return integer, str or None", out);
                return;
            }
            if (!int_fits_signed(ctx, value.get(ctx), number) || number < 0 || number > 0x10ffff) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"character mapping must be in range(0x110000)", out);
                return;
            }
            result.push_back(static_cast<char32_t>(number));
        }
        roots.release(value);
        ++i;
    }
    ReturnSlot temp(ctx);
    make_str(ctx, std::move(result), temp);
    auto value = roots.root(temp.value());
    primitive_result(ctx, value, consumed, out);
}

void detail::build_charmap(Attachment &ctx, Handle table, ReturnSlot &out)
{
    if (!string_value(ctx, table.get(ctx)) ||
        ctx.payload<StrObject>(table.get(ctx)).value.empty()) {
        raise_error(ctx, CoreObject::TypeErrorType, U"charmap table must be a nonempty string",
                    out);
        return;
    }
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), temp);
    auto dict = roots.root(temp.value());
    auto input = ctx.payload<StrObject>(table.get(ctx)).value;
    for (std::size_t i = 0; i < input.size(); ++i) {
        make_int(ctx, input[i], temp);
        auto key = roots.root(temp.value());
        make_int(ctx, static_cast<std::int64_t>(i), temp);
        auto value = roots.root(temp.value());
        dict_store(ctx, dict, key, value, temp);
        if (temp.tag() == ResultTag::Raised) {
            out.raised(temp.exception());
            return;
        }
        roots.release(key);
        roots.release(value);
    }
    out.ok(dict.get(ctx));
}

void detail::install_codecs(Attachment &ctx, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout init;
    init.parameters = {{U"self", ParameterKind::PositionalOnly}};
    init.varargs = U"args";
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    for (auto id : {CoreObject::UnicodeEncodeErrorType, CoreObject::UnicodeDecodeErrorType}) {
        auto owner = at(id);
        install_native_attribute(ctx, core, owner, U"__init__", init,
                                 id == CoreObject::UnicodeEncodeErrorType ? error_init<true>
                                                                          : error_init<false>);
        install_native_attribute(ctx, core, owner, U"__str__", unary,
                                 id == CoreObject::UnicodeEncodeErrorType ? error_str<true>
                                                                          : error_str<false>);
        install_native_field(ctx, core, owner, U"encoding", get_field<0>, set_field<0>);
        install_native_field(ctx, core, owner, U"object", get_field<1>, set_field<1>);
        install_native_field(ctx, core, owner, U"start", get_field<2>, set_field<2>);
        install_native_field(ctx, core, owner, U"end", get_field<3>, set_field<3>);
        install_native_field(ctx, core, owner, U"reason", get_field<4>, set_field<4>);
    }
    RootScope roots(ctx);
    ReturnSlot defaults(ctx);
    ctx.allocate_instance<StrObject>(at(CoreObject::StrType), defaults, U"utf-8");
    auto default_encoding = roots.root(defaults.value());
    ctx.allocate_instance<StrObject>(at(CoreObject::StrType), defaults, U"strict");
    auto default_errors = roots.root(defaults.value());
    SignatureLayout encode;
    encode.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"encoding", ParameterKind::PositionalOrKeyword, default_encoding.get(ctx)},
        {U"errors", ParameterKind::PositionalOrKeyword, default_errors.get(ctx)}};
    install_native_attribute(ctx, core, at(CoreObject::StrType), U"encode", encode, string_encode);
}
} // namespace cxxpython::core
