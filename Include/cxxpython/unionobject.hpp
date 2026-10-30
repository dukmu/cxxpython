#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// args is an immutable tuple. The only published mutable edge is a lazily
// computed immutable parameters tuple, accessed through rooted atomic loads.
// Register as types.UnionType, immutable and non-subclassable.
struct UnionObject {
    ObjectHeader header;
    const Value args;
    AtomicGcEdge parameters;

    UnionObject(InitialType type, Value arguments) : header(type), args(arguments) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(args);
        tracer.visit(parameters);
    }
};

// Invalid operands return NotImplemented; exceptions use the caller's slot.
void make_union(Attachment &, Handle left, Handle right, ReturnSlot &);

// Builtins dispatch these for UnionType classinfo, before metaclass hooks.
// UnionType itself does not expose extra Python checking methods.
void union_instancecheck(Attachment &, Handle subject, Handle union_type, ReturnSlot &);
void union_subclasscheck(Attachment &, Handle subject, Handle union_type, ReturnSlot &);

namespace detail {
void install_union(Attachment &, std::span<const Handle>);
}
} // namespace cxxpython::core
