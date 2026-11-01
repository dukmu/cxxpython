#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// Immutable native payload: its Python type and optional instance fields are
// traced by the allocation layout, not by this scalar payload.
struct FloatObject {
    ObjectHeader header;
    const double value;

    FloatObject(InitialType head, double number) : header(head), value(number) {}
    void trace(Tracer &) const noexcept {}
};

bool is_float(Attachment &context, Value value);
void make_float(Attachment &context, double value, ReturnSlot &out);

namespace detail {
// On success, out is reset; on failure, it contains the raised exception.
bool float_as_double(Attachment &context, Handle input, double &value, ReturnSlot &out);
bool parse_float(std::u32string_view text, double &value);
void install_floats(Attachment &context, std::span<const Handle> core);
} // namespace detail
} // namespace cxxpython::core
