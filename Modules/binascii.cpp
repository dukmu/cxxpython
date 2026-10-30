// Algorithms and error behavior adapted from CPython 3.12.14 Modules/binascii.c.
// CPython is distributed under the PSF license; see its accompanying LICENSE.
#include "internal/binascii.hpp"

#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &slot)
{
    return slot.tag() == ResultTag::Raised;
}

std::u32string type_name(Attachment &ctx, Handle value)
{
    auto name = TypeView(ctx, ctx.type_of(value)).name();
    if (auto text = std::get_if<std::u32string>(&name))
        return text->substr(0, 100);
    return ctx.payload<StrObject>(std::get<Value>(name)).value.substr(0, 100);
}

void error(Attachment &ctx, Handle function, std::u32string message, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot text(ctx), args(ctx), exception(ctx);
    make_str(ctx, std::move(message), text);
    const Value items[] = {text.value()};
    make_tuple(ctx, items, args);
    ctx.allocate_instance<ExceptionObject>(type, exception, args.value());
    link_exception_context(ctx, exception.value());
    out.raised(exception.value());
}

bool input(Attachment &ctx, Handle value, bool ascii, std::string &data, ReturnSlot &out)
{
    if (ascii && ctx.has_layout<StrObject>(value.get(ctx))) {
        for (char32_t ch : ctx.payload<StrObject>(value).value) {
            if (ch > 127) {
                raise_error(ctx, CoreObject::ValueErrorType,
                            U"string argument should contain only ASCII characters", out);
                return false;
            }
            data.push_back(static_cast<char>(ch));
        }
        return true;
    }
    ReturnSlot result(ctx);
    if (buffer_bytes(ctx, value, data, result))
        return true;
    if (ascii) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"argument should be bytes, buffer or ASCII string, not '" +
                        type_name(ctx, value) + U"'",
                    out);
    } else if (failed(result)) {
        if (ctx.has_layout<MemoryViewObject>(value.get(ctx)) &&
            is_subtype(ctx, ctx.type_of(result.exception()),
                       ctx.builtin(CoreObject::BufferErrorType)))
            raise_error(ctx, CoreObject::BufferErrorType,
                        U"memoryview: underlying buffer is not C-contiguous", out);
        else
            out.raised(result.exception());
    } else
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"a bytes-like object is required, not '" + type_name(ctx, value) + U"'", out);
    return false;
}

bool boolean(Attachment &ctx, Handle value, bool &result, ReturnSlot &out)
{
    ReturnSlot converted(ctx);
    truth(ctx, value, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    result = converted.value() == ctx.builtin(CoreObject::True);
    return true;
}

int digit(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
int base64_digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

void a2b_uu(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::string data, result;
    if (!input(ctx, roots.root(args[0]), true, data, out))
        return;
    auto first = data.empty() ? 0 : static_cast<unsigned char>(data[0]);
    unsigned remaining = (first - ' ') & 63;
    std::size_t pos = data.empty() ? 0 : 1;
    unsigned left = 0;
    int bits = 0;
    while (remaining) {
        bool present = pos < data.size();
        unsigned ch = present ? static_cast<unsigned char>(data[pos]) : 0;
        if (present)
            ++pos;
        if (ch == '\n' || ch == '\r' || !present) {
            ch = 0;
        } else if (ch < ' ' || ch > '`') {
            error(ctx, function, U"Illegal char", out);
            return;
        } else {
            ch = (ch - ' ') & 63;
        }
        left = (left << 6) | ch;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((left >> bits) & 255));
            left &= (1u << bits) - 1;
            --remaining;
        }
    }
    while (pos < data.size()) {
        char ch = data[pos++];
        if (ch != ' ' && ch != '`' && ch != '\r' && ch != '\n') {
            error(ctx, function, U"Trailing garbage", out);
            return;
        }
    }
    make_bytes(ctx, std::move(result), out);
}

void b2a_uu(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]), option = roots.root(args[1]);
    std::string data, result;
    bool backtick;
    if (!input(ctx, source, false, data, out) || !boolean(ctx, option, backtick, out))
        return;
    if (data.size() > 45) {
        error(ctx, function, U"At most 45 bytes at once", out);
        return;
    }
    result.push_back(backtick && data.empty() ? '`' : static_cast<char>(' ' + data.size()));
    unsigned left = 0;
    int bits = 0;
    std::size_t pos = 0;
    while (pos < data.size() || bits) {
        left = (left << 8) | (pos < data.size() ? static_cast<unsigned char>(data[pos++]) : 0);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            unsigned ch = (left >> bits) & 63;
            result.push_back(backtick && !ch ? '`' : static_cast<char>(' ' + ch));
        }
    }
    result.push_back('\n');
    make_bytes(ctx, std::move(result), out);
}

void a2b_qp(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]), option = roots.root(args[1]);
    std::string data, result;
    bool header;
    if (!input(ctx, source, true, data, out) || !boolean(ctx, option, header, out))
        return;
    for (std::size_t i = 0; i < data.size();) {
        if (data[i] == '=') {
            ++i;
            if (i == data.size())
                break;
            if (data[i] == '\n' || data[i] == '\r') {
                if (data[i] == '\r')
                    while (i < data.size() && data[i] != '\n')
                        ++i;
                if (i < data.size())
                    ++i;
            } else if (data[i] == '=') {
                result.push_back('=');
                ++i;
            } else if (i + 1 < data.size() && digit(static_cast<unsigned char>(data[i])) >= 0 &&
                       digit(static_cast<unsigned char>(data[i + 1])) >= 0) {
                result.push_back(static_cast<char>(digit(static_cast<unsigned char>(data[i])) * 16 +
                                                   digit(static_cast<unsigned char>(data[i + 1]))));
                i += 2;
            } else {
                result.push_back('=');
            }
        } else {
            result.push_back(header && data[i] == '_' ? ' ' : data[i]);
            ++i;
        }
    }
    make_bytes(ctx, std::move(result), out);
}

void b2a_qp(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]), quote_arg = roots.root(args[1]),
         text_arg = roots.root(args[2]), header_arg = roots.root(args[3]);
    std::string data, result;
    bool quote, istext, header;
    if (!input(ctx, source, false, data, out) || !boolean(ctx, quote_arg, quote, out) ||
        !boolean(ctx, text_arg, istext, out) || !boolean(ctx, header_arg, header, out))
        return;
    auto first_newline = data.find('\n');
    bool crlf =
        first_newline != std::string::npos && first_newline && data[first_newline - 1] == '\r';
    auto newline = [&] {
        if (crlf)
            result.push_back('\r');
        result.push_back('\n');
    };
    constexpr char hex[] = "0123456789ABCDEF";
    auto encoded = [&](unsigned char ch) {
        result.push_back('=');
        result.push_back(hex[ch >> 4]);
        result.push_back(hex[ch & 15]);
    };
    std::size_t line = 0;
    for (std::size_t i = 0; i < data.size();) {
        auto ch = static_cast<unsigned char>(data[i]);
        bool at_end = i + 1 == data.size();
        bool escape = ch > 126 || ch == '=' || (header && ch == '_') ||
                      (ch == '.' && line == 0 &&
                       (at_end || data[i + 1] == '\n' || data[i + 1] == '\r' || !data[i + 1])) ||
                      (!istext && (ch == '\r' || ch == '\n')) ||
                      ((ch == '\t' || ch == ' ') && at_end) ||
                      (ch < 33 && ch != '\r' && ch != '\n' && (quote || (ch != '\t' && ch != ' ')));
        if (escape) {
            if (line + 3 >= 76) {
                result.push_back('=');
                newline();
                line = 0;
            }
            encoded(ch);
            line += 3;
            ++i;
        } else if (istext && (ch == '\n' || (ch == '\r' && !at_end && data[i + 1] == '\n'))) {
            line = 0;
            if (!result.empty() && (result.back() == ' ' || result.back() == '\t')) {
                auto last = static_cast<unsigned char>(result.back());
                result.pop_back();
                encoded(last);
            }
            newline();
            i += ch == '\r' ? 2 : 1;
        } else {
            if (!at_end && data[i + 1] != '\n' && line + 1 >= 76) {
                result.push_back('=');
                newline();
                line = 0;
            }
            ++line;
            result.push_back(header && ch == ' ' ? '_' : static_cast<char>(ch));
            ++i;
        }
    }
    make_bytes(ctx, std::move(result), out);
}

void a2b_base64(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]);
    auto strict_arg = roots.root(args[1]);
    std::string data, result;
    bool strict;
    if (!input(ctx, source, true, data, out) || !boolean(ctx, strict_arg, strict, out))
        return;
    if (strict && !data.empty() && data[0] == '=') {
        error(ctx, function, U"Leading padding not allowed", out);
        return;
    }
    int pos = 0, left = 0, pads = 0;
    bool padding = false;
    for (std::size_t i = 0; i < data.size(); ++i) {
        auto ch = static_cast<unsigned char>(data[i]);
        if (ch == '=') {
            padding = true;
            if (strict && pos == 0) {
                error(ctx, function, U"Excess padding not allowed", out);
                return;
            }
            if (pos >= 2 && pos + ++pads >= 4) {
                if (strict && i + 1 < data.size()) {
                    error(ctx, function, U"Excess data after padding", out);
                    return;
                }
                make_bytes(ctx, std::move(result), out);
                return;
            }
            continue;
        }
        int n = base64_digit(ch);
        if (n < 0) {
            if (strict) {
                error(ctx, function, U"Only base64 data is allowed", out);
                return;
            }
            continue;
        }
        if (strict && padding) {
            error(ctx, function, U"Discontinuous padding not allowed", out);
            return;
        }
        pads = 0;
        if (pos == 0) {
            left = n;
            pos = 1;
        } else if (pos == 1) {
            result.push_back(static_cast<char>((left << 2) | (n >> 4)));
            left = n & 15;
            pos = 2;
        } else if (pos == 2) {
            result.push_back(static_cast<char>((left << 4) | (n >> 2)));
            left = n & 3;
            pos = 3;
        } else {
            result.push_back(static_cast<char>((left << 6) | n));
            pos = 0;
        }
    }
    if (pos == 1) {
        auto count = std::to_string((result.size() / 3) * 4 + 1);
        std::string msg = "Invalid base64-encoded string: number of data characters (" + count +
                          ") cannot be 1 more than a multiple of 4";
        error(ctx, function, {msg.begin(), msg.end()}, out);
        return;
    }
    if (pos) {
        error(ctx, function, U"Incorrect padding", out);
        return;
    }
    make_bytes(ctx, std::move(result), out);
}

void b2a_base64(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]);
    auto newline_arg = roots.root(args[1]);
    std::string data, result;
    bool newline;
    if (!input(ctx, source, false, data, out) || !boolean(ctx, newline_arg, newline, out))
        return;
    if (data.size() >
        (static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) - 3) / 2) {
        error(ctx, function, U"Too much data for base64 line", out);
        return;
    }
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < data.size(); i += 3) {
        auto a = static_cast<unsigned char>(data[i]);
        auto b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0;
        auto c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0;
        result.push_back(alphabet[a >> 2]);
        result.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
        result.push_back(i + 1 < data.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=');
        result.push_back(i + 2 < data.size() ? alphabet[c & 63] : '=');
    }
    if (newline)
        result.push_back('\n');
    make_bytes(ctx, std::move(result), out);
}

void unhexlify(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::string data, result;
    if (!input(ctx, roots.root(args[0]), true, data, out))
        return;
    if (data.size() % 2) {
        error(ctx, function, U"Odd-length string", out);
        return;
    }
    for (std::size_t i = 0; i < data.size(); i += 2) {
        int a = digit(static_cast<unsigned char>(data[i])),
            b = digit(static_cast<unsigned char>(data[i + 1]));
        if (a < 0 || b < 0) {
            error(ctx, function, U"Non-hexadecimal digit found", out);
            return;
        }
        result.push_back(static_cast<char>((a << 4) | b));
    }
    make_bytes(ctx, std::move(result), out);
}

bool integer(Attachment &ctx, Handle arg, std::int64_t &value, ReturnSlot &out)
{
    ReturnSlot converted(ctx);
    index(ctx, arg, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(ctx, converted.value(), value)) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    return true;
}

void hexlify(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]), sep = roots.root(args[1]), group = roots.root(args[2]);
    std::string data, result, separator;
    if (!input(ctx, source, false, data, out))
        return;
    std::int64_t width;
    if (!integer(ctx, group, width, out))
        return;
    if (width < std::numeric_limits<int>::min() || width > std::numeric_limits<int>::max()) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return;
    }
    bool has_sep = sep.get(ctx) != ctx.payload<NativeFunctionObject>(function).captures[1];
    if (has_sep) {
        ReturnSlot size(ctx);
        length(ctx, sep, size);
        if (failed(size)) {
            out.raised(size.exception());
            return;
        }
        std::int64_t sep_length;
        if (!int_fits_signed(ctx, size.value(), sep_length) || sep_length != 1) {
            raise_error(ctx, CoreObject::ValueErrorType, U"sep must be length 1.", out);
            return;
        }
        if (ctx.has_layout<StrObject>(sep.get(ctx))) {
            const auto &s = ctx.payload<StrObject>(sep).value;
            // CPython checks the whole Unicode storage kind, not just the first character.
            if (std::any_of(s.begin(), s.end(), [](char32_t ch) { return ch > 255; })) {
                raise_error(ctx, CoreObject::ValueErrorType, U"sep must be ASCII.", out);
                return;
            }
            separator.push_back(s.empty() ? '\0' : static_cast<char>(s[0]));
        } else if (is_bytes(ctx, sep.get(ctx))) {
            const auto &s = ctx.payload<BytesObject>(sep).value;
            separator.push_back(s.empty() ? '\0' : s[0]);
        } else {
            raise_error(ctx, CoreObject::TypeErrorType, U"sep must be str or bytes.", out);
            return;
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    auto magnitude = width < 0 ? std::uint64_t(-(width + 1)) + 1 : std::uint64_t(width);
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (has_sep && magnitude && i &&
            (width < 0 ? i % magnitude == 0 : (data.size() - i) % magnitude == 0))
            result += separator;
        auto c = static_cast<unsigned char>(data[i]);
        result.push_back(digits[c >> 4]);
        result.push_back(digits[c & 15]);
    }
    make_bytes(ctx, std::move(result), out);
}

template <bool Hqx> void crc(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]), seed = roots.root(args[1]);
    std::string data;
    if (!input(ctx, source, false, data, out))
        return;
    // Clinic's unsigned-int conversion masks arbitrary-sized Python integers.
    ReturnSlot converted(ctx);
    index(ctx, seed, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    const auto &number = ctx.payload<IntObject>(converted.value());
    std::uint32_t value;
    if (number.large) {
        const auto &backend = number.large->value.backend();
        value = static_cast<std::uint32_t>(backend.limbs()[0]);
        if (backend.sign())
            value = 0u - value;
    } else {
        value = static_cast<std::uint32_t>(number.small);
    }
    if constexpr (!Hqx)
        value ^= 0xffffffffu;
    else
        value &= 0xffff;
    for (unsigned char byte : data) {
        if constexpr (Hqx) {
            value ^= std::uint32_t(byte) << 8;
            for (int i = 0; i < 8; ++i)
                value = (value & 0x8000) ? (value << 1) ^ 0x1021 : value << 1;
            value &= 0xffff;
        } else {
            value ^= byte;
            for (int i = 0; i < 8; ++i)
                value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
        }
    }
    if constexpr (!Hqx)
        value ^= 0xffffffffu;
    make_int(ctx, value, out);
}
} // namespace

void install_binascii(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto dict = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot value(ctx), key(ctx), result(ctx);
    auto error_type = roots.root(ctx.builtin(CoreObject::BinasciiErrorType));
    auto store = [&](const char32_t *label, Handle item) {
        make_str(ctx, label, key);
        dict_store(ctx, dict, roots.root(key.value()), item, result);
        if (failed(result)) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    if (!store(U"Error", error_type))
        return;
    if (!store(U"Incomplete", roots.root(ctx.builtin(CoreObject::BinasciiIncompleteType))))
        return;
    ReturnSlot sentinel(ctx);
    call(ctx, roots.root(ctx.object_type()), {}, sentinel);
    if (failed(sentinel)) {
        out.raised(sentinel.exception());
        return;
    }
    auto publish = [&](const char *label, const char32_t *key_name, SignatureLayout sig,
                       NativeEntry entry) {
        const Value captures[] = {error_type.get(ctx), sentinel.value()};
        make_native_function(ctx, label, std::move(sig), entry, captures, value,
                             NativeFunctionBinding::Function);
        return store(key_name, roots.root(value.value()));
    };
    SignatureLayout unary;
    unary.parameters = {{U"data", ParameterKind::PositionalOnly}};
    auto a64 = unary;
    a64.parameters.push_back(
        {U"strict_mode", ParameterKind::KeywordOnly, ctx.builtin(CoreObject::False)});
    auto b64 = unary;
    b64.parameters.push_back(
        {U"newline", ParameterKind::KeywordOnly, ctx.builtin(CoreObject::True)});
    auto uu = unary;
    uu.parameters.push_back(
        {U"backtick", ParameterKind::KeywordOnly, ctx.builtin(CoreObject::False)});
    SignatureLayout aqp;
    aqp.parameters = {
        {U"data", ParameterKind::PositionalOrKeyword},
        {U"header", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::False)}};
    auto bqp = aqp;
    bqp.parameters[1] = {U"quotetabs", ParameterKind::PositionalOrKeyword,
                         ctx.builtin(CoreObject::False)};
    bqp.parameters.push_back(
        {U"istext", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::True)});
    bqp.parameters.push_back(
        {U"header", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::False)});
    ReturnSlot zero(ctx), one(ctx);
    make_int(ctx, 0, zero);
    make_int(ctx, 1, one);
    auto hex = unary;
    hex.parameters[0].kind = ParameterKind::PositionalOrKeyword;
    hex.parameters.push_back({U"sep", ParameterKind::PositionalOrKeyword, sentinel.value()});
    hex.parameters.push_back({U"bytes_per_sep", ParameterKind::PositionalOrKeyword, one.value()});
    auto crc32 = unary;
    crc32.parameters.push_back({U"crc", ParameterKind::PositionalOnly, zero.value()});
    auto hqx = unary;
    hqx.parameters.push_back({U"crc", ParameterKind::PositionalOnly});
    if (!publish("a2b_uu", U"a2b_uu", unary, a2b_uu) || !publish("b2a_uu", U"b2a_uu", uu, b2a_uu) ||
        !publish("a2b_qp", U"a2b_qp", aqp, a2b_qp) || !publish("b2a_qp", U"b2a_qp", bqp, b2a_qp) ||
        !publish("a2b_base64", U"a2b_base64", a64, a2b_base64) ||
        !publish("b2a_base64", U"b2a_base64", b64, b2a_base64) ||
        !publish("a2b_hex", U"a2b_hex", unary, unhexlify) ||
        !publish("unhexlify", U"unhexlify", unary, unhexlify) ||
        !publish("b2a_hex", U"b2a_hex", hex, hexlify) ||
        !publish("hexlify", U"hexlify", hex, hexlify) ||
        !publish("crc32", U"crc32", crc32, crc<false>) ||
        !publish("crc_hqx", U"crc_hqx", hqx, crc<true>))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
