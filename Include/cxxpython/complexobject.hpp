#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// The scalar payload is immutable. The allocation layout traces the type and
// any instance fields belonging to a Python subclass.
struct ComplexObject {
    ObjectHeader header;
    const double real;
    const double imag;

    ComplexObject(InitialType head, double real_part, double imag_part)
        : header(head), real(real_part), imag(imag_part)
    {}
    void trace(Tracer &) const noexcept {}
};

bool is_complex(Attachment &context, Value value);
void make_complex(Attachment &context, double real, double imag, ReturnSlot &out);

namespace detail {
void install_complex(Attachment &context, std::span<const Handle> core);
} // namespace detail
} // namespace cxxpython::core
