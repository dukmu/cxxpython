#pragma once
#include "cxxpython/objects.hpp"

namespace cxxpython::core {
struct CallIteratorObject {
    ObjectHeader header;
    AtomicGcEdge callable, sentinel;
    CallIteratorObject(InitialType head, Value function, Value end)
        : header(head), callable(function), sentinel(end)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(callable);
        tracer.visit(sentinel);
    }
};
namespace detail {
void make_call_iterator(Attachment &, Handle callable, Handle sentinel, ReturnSlot &);
}
} // namespace cxxpython::core
