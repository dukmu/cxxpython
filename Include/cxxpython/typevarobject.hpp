#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
struct TypeVarObject {
    ObjectHeader header;
    const Value name;
    const std::optional<Value> evaluate_bound, evaluate_constraints;
    AtomicGcEdge bound, constraints;
    const bool covariant, contravariant, infer_variance;
    TypeVarObject(InitialType head, Value n, std::optional<Value> b, std::optional<Value> eb,
                  std::optional<Value> cs, std::optional<Value> ec, bool co, bool contra,
                  bool infer)
        : header(head), name(n), evaluate_bound(eb), evaluate_constraints(ec), bound(b),
          constraints(cs), covariant(co), contravariant(contra), infer_variance(infer)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
        if (evaluate_bound)
            tracer.visit(*evaluate_bound);
        if (evaluate_constraints)
            tracer.visit(*evaluate_constraints);
        tracer.visit(bound);
        tracer.visit(constraints);
    }
};
struct ParamSpecObject {
    ObjectHeader header;
    const Value name, bound;
    const bool covariant, contravariant, infer_variance;
    ParamSpecObject(InitialType head, Value n, Value b, bool co, bool contra, bool infer)
        : header(head), name(n), bound(b), covariant(co), contravariant(contra),
          infer_variance(infer)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
        tracer.visit(bound);
    }
};
struct TypeVarTupleObject {
    ObjectHeader header;
    const Value name;
    TypeVarTupleObject(InitialType head, Value n) : header(head), name(n) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
    }
};
struct ParamSpecAttributeObject {
    ObjectHeader header;
    const Value origin;
    ParamSpecAttributeObject(InitialType head, Value value) : header(head), origin(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(origin);
    }
};
namespace detail {
void install_typevars(Attachment &, std::span<const Handle>);
void install_paramspec_views(Attachment &, std::span<const Handle>);
void make_paramspec_attribute(Attachment &, Handle origin, bool keywords, ReturnSlot &);
void make_typevar(Attachment &, Handle name, std::optional<Handle> evaluate_bound,
                  std::optional<Handle> evaluate_constraints, ReturnSlot &);
void make_paramspec(Attachment &, Handle name, ReturnSlot &);
void make_typevartuple(Attachment &, Handle name, ReturnSlot &);
void unpack_typevartuples(Attachment &, Handle tuple, ReturnSlot &);
void make_generic_base(Attachment &, Handle tuple, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
