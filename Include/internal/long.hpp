#pragma once

#include "cxxpython/objects.hpp"
#include <boost/multiprecision/cpp_int.hpp>

namespace cxxpython::core::detail {
using Integer = boost::multiprecision::cpp_int;
struct BigInt {
    Integer value;
};
inline Integer integer_value(const IntObject &value)
{
    return value.large ? value.large->value : Integer(value.small);
}
void publish_integer(Attachment &context, Integer value, ReturnSlot &out);
} // namespace cxxpython::core::detail
