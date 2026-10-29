#pragma once

#include "cxxpython/objects.hpp"
#include <boost/multiprecision/cpp_int.hpp>

namespace cxxpython::core::detail {
using Integer = boost::multiprecision::cpp_int;
// Unicode 15.0 decimal blocks, matching the Python 3.12 text conversion rules.
inline int decimal_digit(char32_t c)
{
    constexpr char32_t zeros[] = {
        0x30,    0x660,   0x6f0,   0x7c0,   0x966,   0x9e6,   0xa66,   0xae6,   0xb66,   0xbe6,
        0xc66,   0xce6,   0xd66,   0xde6,   0xe50,   0xed0,   0xf20,   0x1040,  0x1090,  0x17e0,
        0x1810,  0x1946,  0x19d0,  0x1a80,  0x1a90,  0x1b50,  0x1bb0,  0x1c40,  0x1c50,  0xa620,
        0xa8d0,  0xa900,  0xa9d0,  0xa9f0,  0xaa50,  0xabf0,  0xff10,  0x104a0, 0x10d30, 0x11066,
        0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x11730, 0x118e0,
        0x11950, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16a60, 0x16ac0, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e950, 0x1fbf0,
    };
    for (const auto zero : zeros)
        if (c >= zero && c < zero + 10)
            return static_cast<int>(c - zero);
    return -1;
}
inline bool numeric_whitespace(char32_t c)
{
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000;
}
struct BigInt {
    Integer value;
};
inline Integer integer_value(const IntObject &value)
{
    return value.large ? value.large->value : Integer(value.small);
}
void publish_integer(Attachment &context, Integer value, ReturnSlot &out);
} // namespace cxxpython::core::detail
