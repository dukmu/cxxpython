#pragma once

#include "cxxpython/runtime.hpp"

#include <atomic>
#include <cstdint>

namespace cxxpython::core {

// The collector owns the links. The target is weak;
// shared target and callback reads must go through Attachment::load.
struct WeakRefObject {
    ObjectHeader header;
    AtomicGcEdge target, callback;
    WeakRefObject *previous = nullptr, *next = nullptr, *pending_next = nullptr;
    std::atomic<std::int64_t> hash{-1};
    bool basic = false;

    WeakRefObject(InitialType type, Value target_in, Value callback_in)
        : header(type), target(target_in), callback(callback_in)
    {}

    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(callback);
    }
};

namespace detail {
void install_weakrefs(Attachment &, std::span<const Handle>);
void initialize_weakref(Attachment &, Handle module, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
