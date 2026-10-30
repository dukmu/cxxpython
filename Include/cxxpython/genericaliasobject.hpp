#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// The two constructor fields are immutable after publication. Parameters are
// a lazily populated immutable tuple and must be loaded into a rooted slot.
struct GenericAliasObject {
    ObjectHeader header;
    const Value origin, args;
    const bool starred;
    AtomicGcEdge parameters;

    GenericAliasObject(InitialType head, Value origin_in, Value args_in, bool starred_in = false)
        : header(head), origin(origin_in), args(args_in), starred(starred_in)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(origin);
        tracer.visit(args);
        tracer.visit(parameters);
    }
};

struct GenericAliasIteratorObject {
    ObjectHeader header;
    AtomicGcEdge alias;
    GenericAliasIteratorObject(InitialType head, Value value) : header(head), alias(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(alias);
    }
};

void make_generic_alias(Attachment &, Handle origin, Handle arguments, ReturnSlot &);

namespace detail {
void install_genericalias(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
