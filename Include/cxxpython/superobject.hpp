#pragma once

#include "cxxpython/runtime.hpp"

#include <span>

namespace cxxpython::core {
// The edge points to an immutable tuple containing type, object and object type.
struct SuperObject {
    ObjectHeader header;
    AtomicGcEdge binding;

    explicit SuperObject(InitialType head) : header(head) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(binding);
    }
};

namespace detail {
void install_super(Attachment &context, std::span<const Handle> core);
} // namespace detail
} // namespace cxxpython::core
