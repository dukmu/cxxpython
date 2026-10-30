#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
// The backing dictionary is private and remains reachable for the object's lifetime.
struct SetObject {
    ObjectHeader header;
    const Value table;
    std::atomic<std::int64_t> hash{-1};

    SetObject(InitialType type, Value dictionary) : header(type), table(dictionary) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(table);
    }
};

bool is_set(Attachment &, Value);
void make_set(Attachment &, std::span<const Value>, ReturnSlot &);
void set_add(Attachment &, Handle, Handle, ReturnSlot &);
namespace detail {
void install_sets(Attachment &, std::span<const Handle>);
}
} // namespace cxxpython::core
