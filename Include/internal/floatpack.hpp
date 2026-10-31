#pragma once
// Binary16 conversion shared by native struct and memoryview formats.
#include <cmath>
#include <cstdint>
#include <limits>

namespace cxxpython::core::detail {
inline bool half_bits(double number, std::uint64_t &bits)
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

inline double half_value(std::uint16_t bits)
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
} // namespace cxxpython::core::detail
