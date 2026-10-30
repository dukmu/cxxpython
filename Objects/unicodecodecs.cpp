#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

#include <array>
#include <cstdio>
#include <limits>

namespace cxxpython::core {
namespace {
enum class Encoding { Utf8, Ascii, Latin1 };
enum class Handler {
    Strict,
    Ignore,
    Replace,
    SurrogatePass,
    SurrogateEscape,
    BackslashReplace,
    Unknown
};

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

void unknown_handler(Attachment &ctx, Handle errors, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::LookupErrorType,
                U"unknown error handler name '" + ctx.payload<StrObject>(errors.get(ctx)).value +
                    U"'",
                out);
}

const char32_t *codec_name(Encoding codec)
{
    return codec == Encoding::Utf8 ? U"utf-8" : codec == Encoding::Ascii ? U"ascii" : U"latin-1";
}

// The tuple, source object and each field remain rooted across every allocation.
void unicode_error(Attachment &ctx, bool encode, Encoding codec, Handle object, std::size_t start,
                   std::size_t end, std::u32string reason, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot temp(ctx);
    make_str(ctx, codec_name(codec), temp);
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
void error_init(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
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
void error_str(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
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

void string_encode(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(ctx, args[1]) || !string_value(ctx, args[2])) {
        raise_error(ctx, CoreObject::TypeErrorType, U"encoding and errors must be strings", out);
        return;
    }
    RootScope roots(ctx);
    detail::encode_text(ctx, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
} // namespace

void detail::encode_text(Attachment &ctx, Handle text, Handle encoding, Handle errors,
                         ReturnSlot &out)
{
    if (!string_value(ctx, text.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"encoding requires str", out);
        return;
    }
    Encoding codec;
    Handler handler;
    if (!options(ctx, encoding, errors, codec, handler, out))
        return;
    const auto &input = ctx.payload<StrObject>(text.get(ctx)).value;
    std::string result;
    for (std::size_t i = 0; i < input.size();) {
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
        if (handler == Handler::Unknown) {
            unknown_handler(ctx, errors, out);
            return;
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
        unicode_error(ctx, true, codec, text, i, end,
                      codec == Encoding::Utf8    ? U"surrogates not allowed"
                      : codec == Encoding::Ascii ? U"ordinal not in range(128)"
                                                 : U"ordinal not in range(256)",
                      out);
        return;
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
    Encoding codec;
    Handler handler;
    if (!options(ctx, encoding, errors, codec, handler, out))
        return;
    const auto &input = ctx.payload<BytesObject>(bytes.get(ctx)).value;
    auto byte = [&](std::size_t i) { return static_cast<unsigned char>(input[i]); };
    std::u32string result;
    for (std::size_t i = 0; i < input.size();) {
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
        if (handler == Handler::Unknown) {
            unknown_handler(ctx, errors, out);
            return;
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
        unicode_error(ctx, false, codec, bytes, i, end, reason, out);
        return;
    }
    make_str(ctx, std::move(result), out);
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
