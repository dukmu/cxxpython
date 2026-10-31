#pragma once

#include "cxxpython/memoryobject.hpp"

namespace cxxpython::core {
struct PickleBufferObject {
    ObjectHeader header;
    AtomicGcEdge exporter;
    std::shared_ptr<MemoryViewExport> lease;
    PickleBufferObject(InitialType head, Value source, std::shared_ptr<MemoryViewExport> token)
        : header(head), exporter(source), lease(std::move(token))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(exporter);
    }
};
namespace detail {
void make_picklebuffer_type(Attachment &, ReturnSlot &);
}
} // namespace cxxpython::core
