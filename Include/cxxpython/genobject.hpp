#pragma once

#include "cxxpython/execution.hpp"

namespace cxxpython::core {
struct GeneratorObject {
    ObjectHeader header;
    AtomicGcEdge frame;
    const Value code;
    AtomicGcEdge name, qualname;
    GeneratorObject(InitialType type, Value frame, Value code, Value name, Value qualname)
        : header(type), frame(frame), code(code), name(name), qualname(qualname)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(frame);
        tracer.visit(code);
        tracer.visit(name);
        tracer.visit(qualname);
    }
};
namespace detail {
enum class ResumeResult { Yielded, Returned, Raised, Rejected };
// The caller owns the frame's Running claim until this function detaches it.
ResumeResult resume_generator_frame(Attachment &, Handle frame, Handle sent,
                                    std::optional<Handle> thrown, ReturnSlot &,
                                    std::optional<Handle> throw_args = {});
void normalize_generator_throw(Attachment &, std::span<const Value>, ReturnSlot &);
void make_generator(Attachment &, Handle frame, ReturnSlot &);
void install_generators(Attachment &, std::span<const Handle>);
void generator_stop(Attachment &, std::optional<Handle> value, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
