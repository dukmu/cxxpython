#pragma once

#include "internal/ast_nodes.hpp"

namespace cxxpython::core::detail {
struct AstObject {
    ObjectHeader header;
    const Value types;
    AstObject(InitialType head, Value classes) : header(head), types(classes) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(types);
    }
};
void initialize_ast(Attachment &, Handle module, ReturnSlot &);
bool is_ast(Attachment &, Value);
ast::NodePtr snapshot_ast(Attachment &, Handle, InputMode, ReturnSlot &);
void materialize_ast(Attachment &, const ast::Node &, const SourceBuffer &, ReturnSlot &);
} // namespace cxxpython::core::detail
