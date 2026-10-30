#pragma once

#include "cxxpython/grammar.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cxxpython::syntax {
struct Span {
    std::size_t begin = 0, end = 0;
};
enum class ErrorKind { Syntax, Indentation, Tab, Encoding };
class Error : public std::runtime_error {
  public:
    Error(Span span, std::string message, ErrorKind kind = ErrorKind::Syntax)
        : std::runtime_error(std::move(message)), span(span), kind(kind)
    {}
    Span span;
    ErrorKind kind;
};
struct Token {
    TokenKind kind;
    Span span;
    std::u32string text;
};
struct Node;
using NodePtr = std::shared_ptr<const Node>;
struct Node {
    Rule rule;
    std::size_t alternative = 0;
    // Half-open token indexes; terminals have rule == Rule::Token.
    std::size_t begin = 0, end = 0;
    std::vector<NodePtr> children;
};
struct Tree {
    std::vector<Token> tokens;
    NodePtr root;
};
enum class InputMode { File, Eval, Interactive, FunctionType };
std::vector<Token> tokenize(std::string_view source);
std::u32string normalize_identifier(std::u32string_view text);
std::optional<char32_t> unicode_name(std::u32string_view name);
Tree parse(std::string_view source, InputMode mode = InputMode::File);
} // namespace cxxpython::syntax
