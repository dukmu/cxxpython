#pragma once

#include "cxxpython/execution.hpp"

namespace cxxpython::core::detail {
// Values remain owned by the running Frame. A match either replaces the top
// two operands with an internal numeric result, or leaves a Python exception.
// A miss leaves the operand stack unchanged for ordinary boxed dispatch.
bool try_vm_numeric(Attachment &, FrameObject &, Opcode, std::uint32_t, ReturnSlot &method,
                    ReturnSlot &error);
bool float_binary_scalar(Attachment &, BinaryOp, double, double, double &, ReturnSlot &);
bool small_int_binary(BinaryOp, std::int64_t, std::int64_t, std::int64_t &) noexcept;
} // namespace cxxpython::core::detail
