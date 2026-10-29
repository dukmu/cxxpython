#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
struct ModuleObject {
    ObjectHeader header;
    explicit ModuleObject(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

bool is_module(Attachment &context, Value value);
Value module_dict(Attachment &context, Value value);
void make_module(Attachment &context, std::u32string name, ReturnSlot &out,
                 std::optional<Handle> dictionary = {});

namespace detail {
void install_modules(Attachment &context, std::span<const Handle> core);
}
} // namespace cxxpython::core
