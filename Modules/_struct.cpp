// Format rules follow CPython 3.12.14 Modules/_struct.c.
#include "internal/structmodule.hpp"

#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace cxxpython::core {
namespace {
struct Format {
    std::string text;
    std::vector<StructField> fields;
    std::size_t size = 0;
    std::size_t count = 0;
    bool little = std::endian::native == std::endian::little;
};

void format_error(Attachment &ctx, const char32_t *message, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::StructErrorType, message, out);
}

bool compile_format(Attachment &ctx, Value value, Format &format, ReturnSlot &out)
{
    if (ctx.has_layout<StrObject>(value)) {
        for (char32_t ch : ctx.payload<StrObject>(value).value) {
            if (ch > 127) {
                format_error(ctx, U"bad char in struct format", out);
                return false;
            }
            format.text.push_back(static_cast<char>(ch));
        }
    } else if (is_bytes(ctx, value)) {
        format.text = ctx.payload<BytesObject>(value).value;
    } else {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"Struct() argument 1 must be a str or bytes object", out);
        return false;
    }
    std::size_t pos = 0;
    bool native = true;
    if (!format.text.empty()) {
        switch (format.text[0]) {
        case '@':
            ++pos;
            break;
        case '=':
            native = false;
            ++pos;
            break;
        case '<':
            native = false;
            format.little = true;
            ++pos;
            break;
        case '>':
        case '!':
            native = false;
            format.little = false;
            ++pos;
            break;
        }
    }
    constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    while (pos < format.text.size()) {
        if (std::isspace(static_cast<unsigned char>(format.text[pos]))) {
            ++pos;
            continue;
        }
        std::size_t repeat = 0;
        bool digits = false;
        while (pos < format.text.size() && format.text[pos] >= '0' && format.text[pos] <= '9') {
            digits = true;
            auto digit = static_cast<std::size_t>(format.text[pos++] - '0');
            if (repeat > (limit - digit) / 10) {
                format_error(ctx, U"total struct size too long", out);
                return false;
            }
            repeat = repeat * 10 + digit;
        }
        if (!digits)
            repeat = 1;
        if (pos == format.text.size()) {
            format_error(ctx, U"repeat count given without format specifier", out);
            return false;
        }
        char code = format.text[pos++];
        std::size_t size = 0, align = 1;
        switch (code) {
        case 'x':
        case 'c':
        case 'b':
        case 'B':
        case 's':
        case 'p':
            size = 1;
            break;
        case '?':
            size = native ? sizeof(bool) : 1;
            align = native ? alignof(bool) : 1;
            break;
        case 'h':
        case 'H':
            size = native ? sizeof(short) : 2;
            align = native ? alignof(short) : 1;
            break;
        case 'i':
        case 'I':
            size = native ? sizeof(int) : 4;
            align = native ? alignof(int) : 1;
            break;
        case 'l':
        case 'L':
            size = native ? sizeof(long) : 4;
            align = native ? alignof(long) : 1;
            break;
        case 'q':
        case 'Q':
            size = native ? sizeof(long long) : 8;
            align = native ? alignof(long long) : 1;
            break;
        case 'n':
        case 'N':
            if (native) {
                size = sizeof(std::size_t);
                align = alignof(std::size_t);
            }
            break;
        case 'P':
            if (native) {
                size = sizeof(void *);
                align = alignof(void *);
            }
            break;
        case 'e':
            size = 2;
            break;
        case 'f':
            size = native ? sizeof(float) : 4;
            align = native ? alignof(float) : 1;
            break;
        case 'd':
            size = native ? sizeof(double) : 8;
            align = native ? alignof(double) : 1;
            break;
        }
        if (!size) {
            format_error(ctx, U"bad char in struct format", out);
            return false;
        }
        auto padding = (align - format.size % align) % align;
        if (padding > limit - format.size || repeat > (limit - format.size - padding) / size) {
            format_error(ctx, U"total struct size too long", out);
            return false;
        }
        format.size += padding;
        format.fields.push_back({code, format.size, size, repeat});
        format.size += repeat * size;
        auto count = code == 'x' ? 0 : code == 's' || code == 'p' ? 1 : repeat;
        if (count > limit - format.count) {
            format_error(ctx, U"total struct size too long", out);
            return false;
        }
        format.count += count;
    }
    return true;
}

void calcsize(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    Format format;
    if (compile_format(ctx, args[0], format, out))
        make_int(ctx, static_cast<std::int64_t>(format.size), out);
}

bool snapshot(Attachment &ctx, Handle source, std::string &bytes, ReturnSlot &out)
{
    ReturnSlot error(ctx);
    if (detail::buffer_bytes(ctx, source, bytes, error))
        return true;
    if (error.tag() == ResultTag::Raised) {
        out.raised(error.exception());
        return false;
    }
    raise_error(ctx, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
    return false;
}

bool half_bits(double number, std::uint64_t &bits)
{
    auto sign = std::signbit(number) ? 0x8000u : 0u;
    auto magnitude = std::abs(number);
    if (std::isnan(number)) {
        bits = sign | 0x7e00u;
        return true;
    }
    if (std::isinf(number)) {
        bits = sign | 0x7c00u;
        return true;
    }
    if (magnitude >= 65520.0)
        return false;
    if (magnitude < std::ldexp(1.0, -14)) {
        bits = sign | static_cast<unsigned>(std::nearbyint(std::ldexp(magnitude, 24)));
        return true;
    }
    int exponent;
    double fraction = std::frexp(magnitude, &exponent);
    auto mantissa = static_cast<unsigned>(std::nearbyint((fraction * 2 - 1) * 1024));
    auto exp = static_cast<unsigned>(exponent + 14);
    if (mantissa == 1024) {
        mantissa = 0;
        ++exp;
    }
    bits = sign | (exp << 10) | mantissa;
    return true;
}

double half_value(std::uint16_t bits)
{
    auto exponent = (bits >> 10) & 31;
    auto mantissa = bits & 1023;
    double value;
    if (exponent == 31)
        value = mantissa ? std::numeric_limits<double>::quiet_NaN()
                         : std::numeric_limits<double>::infinity();
    else if (!exponent)
        value = std::ldexp(static_cast<double>(mantissa), -24);
    else
        value = std::ldexp(static_cast<double>(1024 + mantissa), exponent - 25);
    return bits & 0x8000 ? -value : value;
}

bool pack_values(Attachment &ctx, const Format &format, std::span<const Value> args,
                 std::string &bytes, ReturnSlot &out)
{
    if (args.size() != format.count) {
        format_error(ctx, U"pack expected a different number of items", out);
        return false;
    }
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    bytes.assign(format.size, '\0');
    std::size_t arg = 0;
    for (const auto &field : format.fields) {
        if (field.code == 'x')
            continue;
        if (field.code == 's' || field.code == 'p') {
            auto value = roots.root(args[arg++]);
            if (!is_bytes(ctx, value.get(ctx))) {
                format_error(ctx, U"argument for 's' must be a bytes object", out);
                return false;
            }
            const auto &source = ctx.payload<BytesObject>(value).value;
            auto length = field.repeat;
            if (field.code == 'p') {
                if (!length)
                    continue;
                auto copied = std::min(source.size(), length - 1);
                bytes[field.offset] = static_cast<char>(std::min(copied, std::size_t{255}));
                bytes.replace(field.offset + 1, copied, source.data(), copied);
            } else {
                auto copied = std::min(source.size(), length);
                bytes.replace(field.offset, copied, source.data(), copied);
            }
            continue;
        }
        for (std::size_t i = 0; i < field.repeat; ++i) {
            auto value = roots.root(args[arg++]);
            std::uint64_t bits = 0;
            if (field.code == 'c') {
                if (!is_bytes(ctx, value.get(ctx)) ||
                    ctx.payload<BytesObject>(value).value.size() != 1) {
                    format_error(ctx, U"char format requires a bytes object of length 1", out);
                    return false;
                }
                bits = static_cast<unsigned char>(ctx.payload<BytesObject>(value).value[0]);
            } else if (field.code == '?') {
                truth(ctx, value, converted);
                if (converted.tag() == ResultTag::Raised) {
                    out.raised(converted.exception());
                    return false;
                }
                bits = converted.value() == ctx.builtin(CoreObject::True);
            } else if (field.code == 'e' || field.code == 'f' || field.code == 'd') {
                if (ctx.has_layout<StrObject>(value.get(ctx)) || is_bytes(ctx, value.get(ctx)) ||
                    is_bytearray(ctx, value.get(ctx))) {
                    raise_error(ctx, CoreObject::TypeErrorType, U"required argument is not a float",
                                out);
                    return false;
                }
                auto type = roots.root(ctx.builtin(CoreObject::FloatType));
                Value number = value.get(ctx);
                call(ctx, type, {{&number, 1}, {}}, converted);
                if (converted.tag() == ResultTag::Raised) {
                    out.raised(converted.exception());
                    return false;
                }
                double number_value = ctx.payload<FloatObject>(converted.value()).value;
                if (field.code == 'e') {
                    if (!half_bits(number_value, bits)) {
                        raise_error(ctx, CoreObject::OverflowErrorType,
                                    U"float too large to pack with e format", out);
                        return false;
                    }
                } else if (field.code == 'f') {
                    auto single = static_cast<float>(number_value);
                    if (std::isfinite(number_value) && std::isinf(single)) {
                        raise_error(ctx, CoreObject::OverflowErrorType,
                                    U"float too large to pack with f format", out);
                        return false;
                    }
                    bits = std::bit_cast<std::uint32_t>(single);
                } else
                    bits = std::bit_cast<std::uint64_t>(number_value);
            } else {
                index(ctx, value, converted);
                if (converted.tag() == ResultTag::Raised) {
                    out.raised(converted.exception());
                    return false;
                }
                auto decimal = int_decimal(ctx, converted.value());
                bool negative = !decimal.empty() && decimal[0] == '-';
                std::string_view magnitude(decimal);
                if (negative)
                    magnitude.remove_prefix(1);
                auto parsed =
                    std::from_chars(magnitude.data(), magnitude.data() + magnitude.size(), bits);
                bool signed_code = field.code == 'b' || field.code == 'h' || field.code == 'i' ||
                                   field.code == 'l' || field.code == 'q' || field.code == 'n';
                unsigned width = static_cast<unsigned>(field.size * 8);
                std::uint64_t max = width == 64 ? UINT64_MAX : (std::uint64_t{1} << width) - 1;
                std::uint64_t bound = signed_code ? (std::uint64_t{1} << (width - 1)) : 0;
                bool pointer_negative =
                    field.code == 'P' && negative && bits <= (std::uint64_t{1} << (width - 1));
                if (parsed.ec != std::errc{} || parsed.ptr != magnitude.data() + magnitude.size() ||
                    (signed_code ? (negative ? bits > bound : bits >= bound)
                                 : (negative && !pointer_negative) || bits > max)) {
                    format_error(ctx, U"argument out of range", out);
                    return false;
                }
                if (negative)
                    bits = 0 - bits;
            }
            for (std::size_t j = 0; j < field.size; ++j) {
                auto shift = format.little ? j : field.size - j - 1;
                bytes[field.offset + i * field.size + j] = static_cast<char>(bits >> (shift * 8));
            }
        }
    }
    return true;
}

void pack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    Format format;
    if (!compile_format(ctx, args[0], format, out))
        return;
    std::string bytes;
    if (pack_values(ctx, format, ctx.payload<TupleObject>(args[1]).items, bytes, out))
        make_bytes(ctx, std::move(bytes), out);
}

void unpack_values(Attachment &ctx, const Format &format, std::string_view bytes, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot value(ctx);
    std::vector<Handle> held;
    for (const auto &field : format.fields) {
        if (field.code == 'x')
            continue;
        if (field.code == 's' || field.code == 'p') {
            auto length = field.repeat;
            auto offset = field.offset;
            if (field.code == 'p' && length) {
                length =
                    std::min(length - 1,
                             static_cast<std::size_t>(static_cast<unsigned char>(bytes[offset])));
                ++offset;
            }
            make_bytes(ctx, std::string(bytes.substr(offset, length)), value);
            held.push_back(roots.root(value.value()));
            continue;
        }
        for (std::size_t i = 0; i < field.repeat; ++i) {
            std::uint64_t bits = 0;
            for (std::size_t j = 0; j < field.size; ++j) {
                auto shift = format.little ? j : field.size - j - 1;
                bits |= std::uint64_t{static_cast<unsigned char>(
                            bytes[field.offset + i * field.size + j])}
                        << (shift * 8);
            }
            if (field.code == 'c')
                make_bytes(ctx, std::string(1, static_cast<char>(bits)), value);
            else if (field.code == '?')
                value.ok(ctx.builtin(bits ? CoreObject::True : CoreObject::False));
            else if (field.code == 'e')
                make_float(ctx, half_value(static_cast<std::uint16_t>(bits)), value);
            else if (field.code == 'f')
                make_float(ctx, std::bit_cast<float>(static_cast<std::uint32_t>(bits)), value);
            else if (field.code == 'd')
                make_float(ctx, std::bit_cast<double>(bits), value);
            else {
                bool signed_code = field.code == 'b' || field.code == 'h' || field.code == 'i' ||
                                   field.code == 'l' || field.code == 'q' || field.code == 'n';
                unsigned width = static_cast<unsigned>(field.size * 8);
                if (signed_code && (bits & (std::uint64_t{1} << (width - 1)))) {
                    if (width < 64)
                        bits |= UINT64_MAX << width;
                    make_int(ctx, std::bit_cast<std::int64_t>(bits), value);
                } else if (bits <= INT64_MAX)
                    make_int(ctx, static_cast<std::int64_t>(bits), value);
                else
                    make_int_decimal(ctx, std::to_string(bits), value);
            }
            held.push_back(roots.root(value.value()));
        }
    }
    std::vector<Value> values;
    for (auto handle : held)
        values.push_back(handle.get(ctx));
    cxxpython::core::make_tuple(ctx, std::span<const Value>(values), out);
}

void unpack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    Format format;
    if (!compile_format(ctx, args[0], format, out))
        return;
    RootScope roots(ctx);
    std::string bytes;
    if (!snapshot(ctx, roots.root(args[1]), bytes, out))
        return;
    if (bytes.size() != format.size) {
        format_error(ctx, U"unpack requires a buffer of the correct size", out);
        return;
    }
    unpack_values(ctx, format, bytes, out);
}

Format object_format(const StructObject &object)
{
    return {object.format, object.fields, object.size, object.count, object.little_endian};
}

void struct_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto &positional = ctx.payload<TupleObject>(args[1]).items;
    DictView keywords(ctx, args[2]);
    if (positional.size() != 1 || !keywords.entries().empty()) {
        raise_error(ctx, CoreObject::TypeErrorType, U"Struct() takes exactly one argument", out);
        return;
    }
    Format format;
    if (!compile_format(ctx, positional[0], format, out))
        return;
    ctx.allocate_instance<StructObject>(type, out, std::move(format.text), std::move(format.fields),
                                        format.size, format.count, format.little);
}

void struct_size(Attachment &ctx, Value self, ReturnSlot &out)
{
    make_int(ctx, static_cast<std::int64_t>(ctx.payload<StructObject>(self).size), out);
}

void struct_format(Attachment &ctx, Value self, ReturnSlot &out)
{
    const auto &format = ctx.payload<StructObject>(self).format;
    make_str(ctx, std::u32string(format.begin(), format.end()), out);
}

void struct_pack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    auto format = object_format(ctx.payload<StructObject>(args[0]));
    std::string bytes;
    if (pack_values(ctx, format, ctx.payload<TupleObject>(args[1]).items, bytes, out))
        make_bytes(ctx, std::move(bytes), out);
}

void struct_unpack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    auto format = object_format(ctx.payload<StructObject>(args[0]));
    RootScope roots(ctx);
    std::string bytes;
    if (!snapshot(ctx, roots.root(args[1]), bytes, out))
        return;
    if (bytes.size() != format.size) {
        format_error(ctx, U"unpack requires a buffer of the correct size", out);
        return;
    }
    unpack_values(ctx, format, bytes, out);
}

bool offset_value(Attachment &ctx, Value value, std::size_t length, std::size_t required,
                  std::size_t &offset, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    index(ctx, roots.root(value), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    std::int64_t n;
    if (!int_fits_signed(ctx, converted.value(), n)) {
        raise_error(ctx, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return false;
    }
    if (n < 0) {
        if (n < -static_cast<std::int64_t>(length)) {
            format_error(ctx, U"offset out of range", out);
            return false;
        }
        n += static_cast<std::int64_t>(length);
    }
    offset = static_cast<std::size_t>(n);
    if (offset > length || required > length - offset) {
        format_error(ctx, U"buffer is too small", out);
        return false;
    }
    return true;
}

void unpack_from_impl(Attachment &ctx, const Format &format, Value buffer, Value offset_arg,
                      ReturnSlot &out)
{
    RootScope roots(ctx);
    std::string bytes;
    if (!snapshot(ctx, roots.root(buffer), bytes, out))
        return;
    std::size_t offset;
    if (offset_value(ctx, offset_arg, bytes.size(), format.size, offset, out))
        unpack_values(ctx, format, std::string_view(bytes).substr(offset, format.size), out);
}

void unpack_from(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    Format format;
    if (compile_format(ctx, args[0], format, out))
        unpack_from_impl(ctx, format, args[1], args[2], out);
}

void struct_unpack_from(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    unpack_from_impl(ctx, object_format(ctx.payload<StructObject>(args[0])), args[1], args[2], out);
}

void pack_into_impl(Attachment &ctx, const Format &format, Value buffer, Value offset_arg,
                    std::span<const Value> values, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto owner = roots.root(buffer);
    if (!is_bytearray(ctx, buffer)) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"argument must be read-write bytes-like object", out);
        return;
    }
    auto &storage = *ctx.payload<ByteArrayObject>(owner).contents;
    std::size_t length;
    {
        NativeWait wait(ctx);
        std::lock_guard lock(storage.mutex);
        length = storage.bytes.size();
    }
    std::size_t offset;
    if (!offset_value(ctx, offset_arg, length, format.size, offset, out))
        return;
    std::string bytes;
    if (!pack_values(ctx, format, values, bytes, out))
        return;
    bool fits;
    {
        NativeWait wait(ctx);
        std::lock_guard lock(storage.mutex);
        fits = offset <= storage.bytes.size() && bytes.size() <= storage.bytes.size() - offset;
        if (fits) {
            storage.bytes.replace(offset, bytes.size(), bytes);
            ++storage.version;
        }
    }
    if (!fits) {
        format_error(ctx, U"buffer is too small", out);
        return;
    }
    out.ok(ctx.builtin(CoreObject::None));
}

void pack_into(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    Format format;
    if (compile_format(ctx, args[0], format, out))
        pack_into_impl(ctx, format, args[1], args[2], ctx.payload<TupleObject>(args[3]).items, out);
}

void struct_pack_into(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    pack_into_impl(ctx, object_format(ctx.payload<StructObject>(args[0])), args[1], args[2],
                   ctx.payload<TupleObject>(args[3]).items, out);
}

void iter_unpack_impl(Attachment &ctx, Handle compiled, Handle buffer, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto size = ctx.payload<StructObject>(compiled).size;
    if (!size) {
        format_error(ctx, U"cannot iteratively unpack with a struct of length 0", out);
        return;
    }
    std::shared_ptr<StructBufferExport> exported;
    std::size_t length;
    if (is_bytearray(ctx, buffer.get(ctx))) {
        auto storage = ctx.payload<ByteArrayObject>(buffer).contents;
        NativeWait wait(ctx);
        std::lock_guard lock(storage->mutex);
        length = storage->bytes.size();
        if (length % size == 0)
            exported = std::make_shared<StructBufferExport>(std::move(storage));
    } else if (is_bytes(ctx, buffer.get(ctx)))
        length = ctx.payload<BytesObject>(buffer).value.size();
    else {
        raise_error(ctx, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    if (length % size) {
        format_error(ctx, U"iterative unpacking requires a buffer of a multiple of the struct size",
                     out);
        return;
    }
    ctx.allocate_instance<StructIteratorObject>(
        roots.root(ctx.builtin(CoreObject::StructIteratorType)), out, compiled.get(ctx),
        buffer.get(ctx), std::move(exported));
}

void iter_unpack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    Format format;
    if (!compile_format(ctx, args[0], format, out))
        return;
    ReturnSlot compiled(ctx);
    auto buffer = roots.root(args[1]);
    ctx.allocate_instance<StructObject>(roots.root(ctx.builtin(CoreObject::StructType)), compiled,
                                        std::move(format.text), std::move(format.fields),
                                        format.size, format.count, format.little);
    iter_unpack_impl(ctx, roots.root(compiled.value()), buffer, out);
}

void struct_iter_unpack(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    iter_unpack_impl(ctx, roots.root(args[0]), roots.root(args[1]), out);
}

void iterator_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}

void iterator_next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &iterator = ctx.payload<StructIteratorObject>(self);
    auto compiled = roots.root(iterator.compiled);
    auto buffer = roots.root(iterator.buffer);
    auto format = object_format(ctx.payload<StructObject>(compiled));
    const std::string *immutable = nullptr;
    ByteArrayObject::Storage *mutable_storage = nullptr;
    if (is_bytes(ctx, buffer.get(ctx)))
        immutable = &ctx.payload<BytesObject>(buffer).value;
    else
        mutable_storage = ctx.payload<ByteArrayObject>(buffer).contents.get();
    std::string bytes;
    bool done = false;
    {
        NativeWait wait(ctx);
        std::lock_guard lock(iterator.mutex);
        auto copy = [&](const std::string &data) {
            done = iterator.exhausted || iterator.offset > data.size() ||
                   format.size > data.size() - iterator.offset;
            if (!done)
                bytes = data.substr(iterator.offset, format.size);
        };
        if (immutable)
            copy(*immutable);
        else {
            std::lock_guard buffer_lock(mutable_storage->mutex);
            copy(mutable_storage->bytes);
        }
        if (!done)
            iterator.offset += format.size;
        else {
            iterator.exhausted = true;
            iterator.exported.reset();
        }
    }
    if (done) {
        detail::stop_iteration(ctx, out);
        return;
    }
    unpack_values(ctx, format, bytes, out);
}

void clearcache(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

namespace detail {
void initialize_struct(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot key(ctx), function(ctx), result(ctx);
    SignatureLayout signature;
    signature.parameters = {{U"format", ParameterKind::PositionalOnly}};
    auto publish = [&](const char *name, const char32_t *key_name, SignatureLayout sig,
                       NativeEntry entry) {
        make_native_function(ctx, name, std::move(sig), entry, {}, function,
                             NativeFunctionBinding::Function);
        make_str(ctx, key_name, key);
        dict_store(ctx, dictionary, roots.root(key.value()), roots.root(function.value()), result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    if (!publish("calcsize", U"calcsize", signature, calcsize))
        return;
    auto variadic = signature;
    variadic.varargs = U"values";
    if (!publish("pack", U"pack", variadic, pack))
        return;
    signature.parameters.push_back({U"buffer", ParameterKind::PositionalOnly});
    if (!publish("unpack", U"unpack", signature, unpack))
        return;
    if (!publish("iter_unpack", U"iter_unpack", signature, iter_unpack))
        return;
    ReturnSlot zero(ctx);
    make_int(ctx, 0, zero);
    signature.parameters.push_back({U"offset", ParameterKind::PositionalOnly, zero.value()});
    if (!publish("unpack_from", U"unpack_from", signature, unpack_from))
        return;
    signature.parameters.back().default_value.reset();
    signature.varargs = U"values";
    if (!publish("pack_into", U"pack_into", signature, pack_into))
        return;
    if (!publish("_clearcache", U"_clearcache", {}, clearcache))
        return;
    for (auto [name, type] : {std::pair{U"Struct", CoreObject::StructType},
                              std::pair{U"error", CoreObject::StructErrorType}}) {
        make_str(ctx, name, key);
        dict_store(ctx, dictionary, roots.root(key.value()), roots.root(ctx.builtin(type)), result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
    }
    make_str(ctx, U"__doc__", key);
    make_str(ctx, U"Functions to convert between Python values and C structs.", function);
    dict_store(ctx, dictionary, roots.root(key.value()), roots.root(function.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    out.ok(ctx.builtin(CoreObject::None));
}

void install_struct(Attachment &ctx, std::span<const Handle> core)
{
    RootScope roots(ctx);
    auto type = core[static_cast<std::size_t>(CoreObject::StructType)];
    auto iterator = core[static_cast<std::size_t>(CoreObject::StructIteratorType)];
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto constructor = unary;
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(ctx, core, type, U"__new__", constructor, struct_new);
    ReturnSlot wrapper(ctx), key(ctx), zero(ctx);
    auto dictionary = *TypeView(ctx, type).dictionary();
    auto function = roots.root(*name_lookup(ctx, dictionary, U"__new__"));
    ctx.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapper, function.get(ctx));
    ctx.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                     U"__new__");
    name_store(ctx, dictionary, key.value(), wrapper.value());
    install_native_field(ctx, core, type, U"size", struct_size);
    install_native_field(ctx, core, type, U"format", struct_format);
    auto variadic = unary;
    variadic.varargs = U"values";
    install_native_attribute(ctx, core, type, U"pack", variadic, struct_pack);
    auto binary = unary;
    binary.parameters.push_back({U"buffer", ParameterKind::PositionalOnly});
    install_native_attribute(ctx, core, type, U"unpack", binary, struct_unpack);
    install_native_attribute(ctx, core, type, U"iter_unpack", binary, struct_iter_unpack);
    zero.ok(core[static_cast<std::size_t>(CoreObject::IntZero)].get(ctx));
    auto from = binary;
    from.parameters.push_back({U"offset", ParameterKind::PositionalOnly, zero.value()});
    install_native_attribute(ctx, core, type, U"unpack_from", from, struct_unpack_from);
    from.parameters.back().default_value.reset();
    from.varargs = U"values";
    install_native_attribute(ctx, core, type, U"pack_into", from, struct_pack_into);
    install_native_attribute(ctx, core, iterator, U"__iter__", unary, iterator_self);
    install_native_attribute(ctx, core, iterator, U"__next__", unary, iterator_next);
}
} // namespace detail
} // namespace cxxpython::core
