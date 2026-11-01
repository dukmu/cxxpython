// Native scalar formats follow CPython 3.12.14 Objects/memoryobject.c.
#include "internal/memoryformat.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/floatpack.hpp"
#include "internal/long.hpp"

#include <cstring>
#include <limits>

namespace cxxpython::core::detail {
namespace {
template <class T> void unpack(Attachment &c, std::string_view bytes, ReturnSlot &out)
{
    T value;
    std::memcpy(&value, bytes.data(), sizeof(value));
    if constexpr (std::is_floating_point_v<T>)
        make_float(c, value, out);
    else
        publish_integer(c, Integer(value), out);
}
template <class T>
bool pack(Attachment &c, Handle source, std::string &bytes, ReturnSlot &out, bool pointer = false)
{
    ReturnSlot converted(c);
    T value;
    if constexpr (std::is_floating_point_v<T>) {
        if (c.has_layout<StrObject>(source.get(c)) || is_bytes(c, source.get(c)) ||
            is_bytearray(c, source.get(c)) || c.has_layout<MemoryViewObject>(source.get(c))) {
            raise_error(c, CoreObject::TypeErrorType, U"memoryview: invalid type for float format",
                        out);
            return false;
        }
        RootScope roots(c);
        const Value argument = source.get(c);
        call(c, roots.root(c.builtin(CoreObject::FloatType)), {{&argument, 1}, {}}, converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return false;
        }
        value = static_cast<T>(c.payload<FloatObject>(converted.value()).value);
    } else {
        index(c, source, converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return false;
        }
        auto number = integer_value(c.payload<IntObject>(converted.value()));
        const Integer minimum = pointer ? Integer(std::numeric_limits<std::intptr_t>::min())
                                        : Integer(std::numeric_limits<T>::min());
        if (number < minimum || number > Integer(std::numeric_limits<T>::max())) {
            raise_error(c, CoreObject::ValueErrorType, U"memoryview: invalid value for format",
                        out);
            return false;
        }
        if (pointer && number < 0)
            number += Integer(1) << (sizeof(T) * 8);
        value = number.convert_to<T>();
    }
    bytes.resize(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    return true;
}
} // namespace

std::size_t memory_format_size(char code) noexcept
{
    switch (code) {
    case 'b':
    case 'B':
    case 'c':
    case '?':
        return 1;
    case 'e':
        return sizeof(std::uint16_t);
    case 'h':
    case 'H':
        return sizeof(short);
    case 'i':
    case 'I':
        return sizeof(int);
    case 'l':
    case 'L':
        return sizeof(long);
    case 'q':
    case 'Q':
        return sizeof(long long);
    case 'n':
    case 'N':
    case 'P':
        return sizeof(std::size_t);
    case 'f':
        return sizeof(float);
    case 'd':
        return sizeof(double);
    case 'w':
        return sizeof(wchar_t);
    default:
        return 0;
    }
}
void unpack_memory_format(Attachment &c, char code, std::string_view bytes, ReturnSlot &out)
{
    switch (code) {
    case 'b':
        return unpack<signed char>(c, bytes, out);
    case 'B':
        return unpack<unsigned char>(c, bytes, out);
    case 'h':
        return unpack<short>(c, bytes, out);
    case 'H':
        return unpack<unsigned short>(c, bytes, out);
    case 'i':
        return unpack<int>(c, bytes, out);
    case 'I':
        return unpack<unsigned int>(c, bytes, out);
    case 'l':
        return unpack<long>(c, bytes, out);
    case 'L':
        return unpack<unsigned long>(c, bytes, out);
    case 'q':
        return unpack<long long>(c, bytes, out);
    case 'Q':
        return unpack<unsigned long long>(c, bytes, out);
    case 'n':
        return unpack<std::ptrdiff_t>(c, bytes, out);
    case 'N':
    case 'P':
        return unpack<std::size_t>(c, bytes, out);
    case 'f':
        return unpack<float>(c, bytes, out);
    case 'd':
        return unpack<double>(c, bytes, out);
    case 'e': {
        std::uint16_t bits;
        std::memcpy(&bits, bytes.data(), sizeof(bits));
        make_float(c, half_value(bits), out);
        return;
    }
    case '?':
        out.ok(c.builtin(bytes[0] ? CoreObject::True : CoreObject::False));
        return;
    case 'c':
        make_bytes(c, std::string(bytes.substr(0, 1)), out);
        return;
    case 'w':
        raise_error(c, CoreObject::NotImplementedErrorType, U"memoryview: format w not supported",
                    out);
        return;
    default:
        throw std::logic_error("invalid native memory format");
    }
}
bool pack_memory_format(Attachment &c, char code, Handle value, std::string &bytes, ReturnSlot &out)
{
    switch (code) {
    case 'b':
        return pack<signed char>(c, value, bytes, out);
    case 'B':
        return pack<unsigned char>(c, value, bytes, out);
    case 'h':
        return pack<short>(c, value, bytes, out);
    case 'H':
        return pack<unsigned short>(c, value, bytes, out);
    case 'i':
        return pack<int>(c, value, bytes, out);
    case 'I':
        return pack<unsigned int>(c, value, bytes, out);
    case 'l':
        return pack<long>(c, value, bytes, out);
    case 'L':
        return pack<unsigned long>(c, value, bytes, out);
    case 'q':
        return pack<long long>(c, value, bytes, out);
    case 'Q':
        return pack<unsigned long long>(c, value, bytes, out);
    case 'n':
        return pack<std::ptrdiff_t>(c, value, bytes, out);
    case 'N':
        return pack<std::size_t>(c, value, bytes, out);
    case 'P':
        return pack<std::uintptr_t>(c, value, bytes, out, true);
    case 'f':
        return pack<float>(c, value, bytes, out);
    case 'd':
        return pack<double>(c, value, bytes, out);
    case 'e': {
        if (!pack<double>(c, value, bytes, out))
            return false;
        double number;
        std::memcpy(&number, bytes.data(), sizeof(number));
        std::uint64_t bits;
        if (!half_bits(number, bits)) {
            raise_error(c, CoreObject::OverflowErrorType, U"float too large to pack with e format",
                        out);
            return false;
        }
        auto half = static_cast<std::uint16_t>(bits);
        bytes.resize(sizeof(half));
        std::memcpy(bytes.data(), &half, sizeof(half));
        return true;
    }
    case '?':
        truth(c, value, out);
        if (out.tag() == ResultTag::Raised)
            return false;
        bytes.assign(1, out.value() == c.builtin(CoreObject::True) ? '\1' : '\0');
        return true;
    case 'c':
        if (is_bytes(c, value.get(c)) && c.payload<BytesObject>(value).value.size() == 1) {
            bytes = c.payload<BytesObject>(value).value;
            return true;
        }
        raise_error(c, CoreObject::TypeErrorType, U"memoryview: invalid type for format 'c'", out);
        return false;
    case 'w':
        raise_error(c, CoreObject::NotImplementedErrorType, U"memoryview: format w not supported",
                    out);
        return false;
    default:
        throw std::logic_error("invalid native memory format");
    }
}
} // namespace cxxpython::core::detail
