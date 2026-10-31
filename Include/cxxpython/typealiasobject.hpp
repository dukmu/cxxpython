#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
struct TypeAliasObject {
    ObjectHeader header;
    const Value name;
    const std::optional<Value> type_params, compute_value, module;
    AtomicGcEdge value;

    TypeAliasObject(InitialType head, Value name, std::optional<Value> parameters,
                    std::optional<Value> compute, std::optional<Value> initial,
                    std::optional<Value> module)
        : header(head), name(name), type_params(parameters), compute_value(compute), module(module),
          value(initial)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
        if (type_params)
            tracer.visit(*type_params);
        if (compute_value)
            tracer.visit(*compute_value);
        if (module)
            tracer.visit(*module);
        tracer.visit(value);
    }
};
namespace detail {
void make_type_alias(Attachment &, Handle name, Handle parameters, Handle compute, ReturnSlot &);
void install_typealias(Attachment &, std::span<const Handle>);
void initialize_typing(Attachment &, Handle module, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
