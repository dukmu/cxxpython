#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core {
struct EnumObject {
    ObjectHeader header;
    const Value iterator;
    AtomicGcEdge index;
    EnumObject(InitialType type, Value iterator_in, Value index_in)
        : header(type), iterator(iterator_in), index(index_in)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(index);
    }
};
struct MultiIteratorObject {
    ObjectHeader header;
    const std::vector<Value> iterators;
    const std::optional<Value> callable;
    const bool strict;
    MultiIteratorObject(InitialType type, std::vector<Value> inputs, std::optional<Value> function,
                        bool strict_in)
        : header(type), iterators(std::move(inputs)), callable(function), strict(strict_in)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto iterator : iterators)
            tracer.visit(iterator);
        if (callable)
            tracer.visit(*callable);
    }
    std::size_t external_bytes() const noexcept
    {
        return iterators.capacity() * sizeof(Value);
    }
};
namespace detail {
void install_reversed(Attachment &context, std::span<const Handle> core);
}
} // namespace cxxpython::core
