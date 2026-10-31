// Conversion follows CPython 3.12.14 Python/pytime.c (PSF license).
#include "internal/pytime.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"

#include <cmath>
#include <limits>

namespace cxxpython::core::detail {
bool seconds_to_nanoseconds(Attachment &c, Handle value, std::int64_t &nanos, ReturnSlot &out)
{
    constexpr std::int64_t billion = 1000000000;
    if (is_float(c, value.get(c))) {
        const double seconds = c.payload<FloatObject>(value).value;
        if (std::isnan(seconds)) {
            raise_error(c, CoreObject::ValueErrorType, U"Invalid value NaN (not a number)", out);
            return false;
        }
        const double scaled = seconds * billion;
        const long double rounded = seconds < 0 ? std::floor(scaled) : std::ceil(scaled);
        if (!std::isfinite(seconds) ||
            rounded > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
            rounded < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
            raise_error(c, CoreObject::OverflowErrorType,
                        U"timestamp out of range for platform time_t", out);
            return false;
        }
        nanos = static_cast<std::int64_t>(rounded);
        return true;
    }
    ReturnSlot converted(c);
    index(c, value, converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    std::int64_t seconds;
    if (!int_fits_signed(c, converted.value(), seconds)) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"timestamp too large to convert to C _PyTime_t", out);
        return false;
    }
    if (seconds > std::numeric_limits<std::int64_t>::max() / billion ||
        seconds < std::numeric_limits<std::int64_t>::min() / billion) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"timestamp too large to convert to C _PyTime_t", out);
        return false;
    }
    nanos = seconds * billion;
    return true;
}
} // namespace cxxpython::core::detail
