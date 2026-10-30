#pragma once

#include "cxxpython/genobject.hpp"

namespace cxxpython::core {
// AsyncYieldValue allocates this wrapper; ordinary await suspension does not.
struct AsyncGenWrappedValueObject {
    ObjectHeader header;
    const Value value;
    AsyncGenWrappedValueObject(InitialType type, Value value) : header(type), value(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(value);
    }
};

enum class AsyncGenRequestState { Initial, Active, Closed };
struct AsyncGenRequestObject {
    ObjectHeader header;
    const Value generator;
    // asend value, athrow argument tuple, or None for aclose.
    const Value argument;
    const bool throwing, closing;
    std::atomic<AsyncGenRequestState> state{AsyncGenRequestState::Initial};
    std::atomic<bool> executing{false};
    AsyncGenRequestObject(InitialType type, Value generator, Value argument, bool throwing,
                          bool closing)
        : header(type), generator(generator), argument(argument), throwing(throwing),
          closing(closing)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(generator);
        tracer.visit(argument);
    }
};

namespace detail {
void install_async_generators(Attachment &, std::span<const Handle>);
}
} // namespace cxxpython::core
