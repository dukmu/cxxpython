#pragma once

#include "cxxpython/grammar.hpp"
#include "cxxpython/nativeowner.hpp"

#include <cstddef>
#include <functional>
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
enum class ErrorKind { Syntax, Indentation, Tab, Encoding, Incomplete };
enum class InputMode { File, Eval, Single, Interactive, FunctionType };
enum class ParseFlags : unsigned {
    None = 0,
    DontImplyDedent = 1,
    AllowIncompleteInput = 2,
    TypeComments = 4
};
constexpr ParseFlags operator|(ParseFlags left, ParseFlags right)
{
    return static_cast<ParseFlags>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}
constexpr bool enabled(ParseFlags flags, ParseFlags flag)
{
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}
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
    std::size_t lineno = 0;
};
struct LexicalToken {
    Token token;
    int lineno = 0, column = -1, end_lineno = 0, end_column = -1;
    std::shared_ptr<const std::u32string> line;
    std::size_t line_revision = 0;
    bool implicit_newline = false, trailing = false;
};
struct LexicalDiagnostic {
    ErrorKind kind = ErrorKind::Syntax;
    Span span;
    std::string message;
    int lineno = 0, offset = 0, end_lineno = 0, end_offset = 0;
    std::u32string line;
    bool location_in_args = true, has_text = true;
};
// A caller-owned stream. Callbacks are borrowed only for the duration of next().
class TokenStream {
  public:
    using ReadLine = std::function<std::string()>;
    using Poll = std::function<void()>;
    using Warning = std::function<void(const LexicalDiagnostic &)>;
    explicit TokenStream(bool extra_tokens = false, InputMode mode = InputMode::File,
                         ParseFlags flags = ParseFlags::None);
    ~TokenStream();
    TokenStream(const TokenStream &) = delete;
    TokenStream &operator=(const TokenStream &) = delete;
    std::optional<LexicalToken> next(const ReadLine &, const Poll & = {}, const Warning & = {});
    const LexicalDiagnostic &diagnostic() const noexcept;
    bool end_of_source() const noexcept;
    std::size_t external_bytes() const noexcept;

  private:
    struct Impl;
    core::NativeOwner<Impl> impl_;
};
std::u32string decode_string_literal(std::u32string_view, Span);
std::u32string decode_string_fragment(std::u32string_view, bool raw, Span);
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
    std::vector<Token> type_ignores = {};
};
std::vector<Token> tokenize(std::string_view source, InputMode mode = InputMode::File,
                            ParseFlags flags = ParseFlags::None);
std::u32string normalize_identifier(std::u32string_view text);
std::optional<char32_t> unicode_name(std::u32string_view name);
std::optional<std::string> unicode_character_name(char32_t codepoint);
Tree parse(std::string_view source, InputMode mode = InputMode::File,
           ParseFlags flags = ParseFlags::None);
} // namespace cxxpython::syntax
