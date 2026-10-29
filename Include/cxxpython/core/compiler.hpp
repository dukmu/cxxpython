#pragma once

#include "cxxpython/core/execution.hpp"

namespace cxxpython::core
{
    struct Span
    {
        std::size_t begin = 0, end = 0;
    };
    enum class SourceErrorKind { Syntax, Indentation, Tab, Encoding, Capability };
    class SourceBuffer final
    {
    public:
        explicit SourceBuffer(std::string text, std::string filename = "<string>");
        const std::string &text() const noexcept { return text_; }
        const std::string &filename() const noexcept { return filename_; }
        SourcePosition position(Span span) const;
        std::string line(std::uint32_t number) const;
        std::uint32_t character_column(std::uint32_t line, std::uint32_t byte_column) const;
    private:
        std::string text_, filename_;
        std::vector<std::size_t> lines_{0};
    };
    class SourceError final : public std::invalid_argument
    {
    public:
        SourceError(const SourceBuffer &source, Span span, std::string message, SourceErrorKind kind = SourceErrorKind::Syntax);
        const SourceErrorKind kind;
        const Span span;
        const SourcePosition position;
        const std::string filename, source_line;
        const std::uint32_t offset, end_offset;
    };
    enum class InputMode { Exec, Eval };
    enum class ScopeKind { Module, Function };
    enum class NameClass { Local, GlobalExplicit, GlobalImplicit, Free, Cell };
    struct SymbolInfo
    {
        std::u32string name;
        NameClass classification;
        Span declaration;
        std::optional<std::size_t> slot;
    };
    struct ScopeInfo
    {
        ScopeKind kind;
        std::u32string name, qualname;
        std::optional<std::size_t> parent;
        Span span;
        std::vector<SymbolInfo> symbols;
        std::vector<std::u32string> locals;
    };
    namespace detail { struct SourceProgram; }
    // Pure host parse/scope/CFG state; immutable and shareable after construction.
    class SourceUnit final
    {
    public:
        explicit SourceUnit(SourceBuffer source, InputMode mode = InputMode::Exec);
        ~SourceUnit();
        SourceUnit(SourceUnit &&) noexcept;
        SourceUnit &operator=(SourceUnit &&) noexcept;
        const SourceBuffer &source() const noexcept;
        std::span<const ScopeInfo> scopes() const noexcept;
        void freeze(Attachment &context, ReturnSlot &out) const;
    private:
        std::unique_ptr<const detail::SourceProgram> program_;
    };
    struct RunRequest
    {
        std::variant<const SourceUnit *, Handle> program;
        std::optional<Handle> globals = {}, builtins = {};
    };
    ResultView run(Attachment &context, const RunRequest &request, ReturnSlot &out);
    // Native UTF-8 conversion for source and host diagnostics, not a Python codec API.
    std::string source_utf8(std::u32string_view text);
}
