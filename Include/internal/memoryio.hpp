#pragma once

#include "cxxpython/bytearrayobject.hpp"
#include "internal/iomodule.hpp"

namespace cxxpython::core {
struct BytesIOObject {
    ObjectHeader header;
    AtomicGcEdge buffer;
    std::shared_ptr<ByteArrayObject::Storage> storage;
    std::int64_t position = 0;
    bool closed = false;
    BytesIOObject(InitialType head, Value owner, std::shared_ptr<ByteArrayObject::Storage> data)
        : header(head), buffer(owner), storage(std::move(data))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(buffer);
    }
};

struct StringIOObject {
    ObjectHeader header;
    std::mutex mutex;
    std::u32string contents;
    std::int64_t position = 0;
    std::size_t version = 0;
    int newline = 0;
    unsigned newlines = 0;
    bool closed = false;
    explicit StringIOObject(InitialType head) : header(head) {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return contents.capacity() * sizeof(char32_t);
    }
};

namespace detail {
void install_memoryio(Attachment &, std::span<const Handle>);
void initialize_memoryio(Attachment &, Handle, ReturnSlot &);
void install_stringio(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
