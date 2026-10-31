#pragma once

#include <cstdint>

namespace cxxpython::core {
class Attachment;
class Handle;
class ReturnSlot;
namespace detail {
bool seconds_to_nanoseconds(Attachment &, Handle, std::int64_t &, ReturnSlot &);
}
} // namespace cxxpython::core
