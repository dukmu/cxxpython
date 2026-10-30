#pragma once

#include "cxxpython/genobject.hpp"

namespace cxxpython::core {
struct AnextAwaitableObject {
    ObjectHeader header;
    const Value wrapped, default_value;
    std::atomic<bool> executing{false};
    AnextAwaitableObject(InitialType type, Value wrapped, Value default_value)
        : header(type), wrapped(wrapped), default_value(default_value)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(wrapped);
        tracer.visit(default_value);
    }
};

namespace detail {
void builtin_aiter(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
void builtin_anext(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
void install_async_iteration(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
