#pragma once

#include "cxxpython/memoryobject.hpp"

namespace cxxpython::core {
enum class SreStringKind { Any = -1, Unicode = 0, Bytes = 1 };
struct SrePatternObject {
    ObjectHeader header;
    const Value pattern, groupindex, indexgroup;
    const int flags;
    const std::size_t groups;
    const SreStringKind kind;
    const std::vector<std::uint32_t> code;
    SrePatternObject(InitialType head, Value source, Value names, Value indices, int options,
                     std::size_t count, SreStringKind mode, std::vector<std::uint32_t> instructions)
        : header(head), pattern(source), groupindex(names), indexgroup(indices), flags(options),
          groups(count), kind(mode), code(std::move(instructions))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pattern);
        tracer.visit(groupindex);
        tracer.visit(indexgroup);
    }
    std::size_t external_bytes() const noexcept
    {
        return code.capacity() * sizeof(std::uint32_t);
    }
};
struct SreMatchObject {
    ObjectHeader header;
    const Value pattern, string;
    const std::ptrdiff_t pos, endpos;
    const int lastindex;
    const std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> marks;
    AtomicGcEdge regs;
    SreMatchObject(InitialType head, Value regex, Value text, std::ptrdiff_t begin,
                   std::ptrdiff_t end, int last,
                   std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> captures)
        : header(head), pattern(regex), string(text), pos(begin), endpos(end), lastindex(last),
          marks(std::move(captures))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pattern);
        tracer.visit(string);
        tracer.visit(regs);
    }
    std::size_t external_bytes() const noexcept
    {
        return marks.capacity() * sizeof(marks[0]);
    }
};
struct SreScannerObject {
    ObjectHeader header;
    const Value pattern, string;
    const std::ptrdiff_t pos, endpos;
    const std::shared_ptr<MemoryViewExport> lease;
    // Only the operation holding running may access the cursor or native cache.
    std::atomic_flag running = ATOMIC_FLAG_INIT;
    std::ptrdiff_t cursor;
    bool must_advance = false, exhausted = false;
    std::vector<std::uint32_t> text;
    std::optional<std::size_t> version;
    SreScannerObject(InitialType head, Value regex, Value source, std::ptrdiff_t begin,
                     std::ptrdiff_t end, std::shared_ptr<MemoryViewExport> token,
                     std::vector<std::uint32_t> data, std::optional<std::size_t> revision)
        : header(head), pattern(regex), string(source), pos(begin), endpos(end),
          lease(std::move(token)), cursor(begin), text(std::move(data)), version(revision)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pattern);
        tracer.visit(string);
    }
    std::size_t external_bytes() const noexcept
    {
        return text.capacity() * sizeof(std::uint32_t);
    }
};
struct SreTemplateObject {
    ObjectHeader header;
    const Value items;
    SreTemplateObject(InitialType head, Value value) : header(head), items(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(items);
    }
};
namespace detail {
void install_sre(Attachment &, std::span<const Handle>);
void install_sre_match(Attachment &, std::span<const Handle>);
void initialize_sre(Attachment &, Handle module, ReturnSlot &);
void sre_slice(Attachment &, Handle string, std::ptrdiff_t start, std::ptrdiff_t end, ReturnSlot &);
void sre_group_slice(Attachment &, Handle match, std::size_t group, Handle fallback, ReturnSlot &);
void sre_expand(Attachment &, Handle match, Handle replacement, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
