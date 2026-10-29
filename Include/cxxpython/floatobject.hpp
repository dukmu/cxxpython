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
void install_floats(Attachment &context, std::span<const Handle> core);
}
} // namespace cxxpython::core
