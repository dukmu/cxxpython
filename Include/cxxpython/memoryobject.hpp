#pragma once

#include "cxxpython/bytearrayobject.hpp"

namespace cxxpython::core {
struct MemoryViewExport {
    std::shared_ptr<ByteArrayObject::Storage> storage;
    explicit MemoryViewExport(std::shared_ptr<ByteArrayObject::Storage> value)
        : storage(std::move(value))
    {
        if (storage)
            ++storage->exports;
    }
    ~MemoryViewExport()
    {
        if (storage)
            --storage->exports;
    }
};

struct MemoryViewObject {
    ObjectHeader header;
    AtomicGcEdge owner;
    std::shared_ptr<MemoryViewExport> exported;
    const std::int64_t offset, stride, length;
    const bool readonly;
    const std::string format;
    MemoryViewObject(InitialType head, Value owner, std::shared_ptr<MemoryViewExport> token,
                     std::int64_t offset, std::int64_t stride, std::int64_t length, bool readonly,
                     std::string format = "B")
        : header(head), owner(owner), exported(std::move(token)), offset(offset), stride(stride),
          length(length), readonly(readonly), format(std::move(format))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
    }
    std::size_t external_bytes() const noexcept
    {
        return format.capacity() + 1;
    }
};

namespace detail {
// Native consumers root the exporter and release the export at scope exit,
// rather than waiting for an unreachable temporary memoryview to be collected.
struct WritableBuffer {
    ReturnSlot owner;
    std::shared_ptr<MemoryViewExport> lease;
    std::size_t offset = 0, length = 0;
    explicit WritableBuffer(Attachment &context) : owner(context) {}
};
bool writable_buffer(Attachment &, Handle, WritableBuffer &, ReturnSlot &);
void write_buffer(Attachment &, const WritableBuffer &, std::string_view);
// Returns false for unsupported objects or a released view; errors are in out.
bool buffer_bytes(Attachment &, Handle, std::string &, ReturnSlot &, bool contiguous = true);
void install_memoryviews(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
