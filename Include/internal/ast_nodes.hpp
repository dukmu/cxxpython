#pragma once

#include "cxxpython/parser.hpp"
#include "internal/compiler.hpp"

#include <array>
#include <span>
#include <string_view>

namespace cxxpython::core::ast {
#include "internal/ast_generated.inc"

struct Node;
using NodePtr = std::unique_ptr<Node>;
using Nodes = std::vector<NodePtr>;
using Strings = std::vector<std::u32string>;
using Field = std::variant<std::monostate, std::int64_t, std::u32string, detail::Literal, NodePtr,
                           Nodes, Strings>;
struct Node {
    Kind kind;
    Span span;
    std::vector<Field> fields;
};
template <class... Fields> NodePtr make(Kind kind, Span span, Fields &&...fields)
{
    std::vector<Field> values;
    values.reserve(sizeof...(fields));
    (values.emplace_back(std::forward<Fields>(fields)), ...);
    return std::make_unique<Node>(Node{kind, span, std::move(values)});
}
NodePtr parse(const SourceBuffer &, syntax::InputMode, std::uint32_t flags);
detail::Suite lower(const Node &, const SourceBuffer &, InputMode);
} // namespace cxxpython::core::ast
