#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// The instance dictionary is the traced atomic dictionary field in the
// type's InstanceFieldPlan, not a second edge in this native payload.
struct NamespaceObject {
    ObjectHeader header;
    explicit NamespaceObject(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

namespace detail {
void install_namespace(Attachment &context, std::span<const Handle> core);
}
} // namespace cxxpython::core
