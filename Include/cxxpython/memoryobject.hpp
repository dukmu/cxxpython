#pragma once

#include "cxxpython/bytearrayobject.hpp"

namespace cxxpython::core {
struct MemoryViewExports {
    std::mutex mutex;
    std::atomic<std::size_t> count{0};
};
struct MemoryViewExport {
    std::shared_ptr<ByteArrayObject::Storage> storage;
    std::shared_ptr<MemoryViewExports> view_exports;
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
        if (view_exports)
            --view_exports->count;
    }
};

struct MemoryViewObject {
    ObjectHeader header;
    AtomicGcEdge owner;
    AtomicGcEdge data_owner;
    std::shared_ptr<MemoryViewExport> exported;
    const std::shared_ptr<MemoryViewExports> exports = std::make_shared<MemoryViewExports>();
    const std::int64_t offset, stride, length;
    const bool readonly;
    const std::string format;
    MemoryViewObject(InitialType head, Value owner, std::shared_ptr<MemoryViewExport> token,
                     std::int64_t offset, std::int64_t stride, std::int64_t length, bool readonly,
                     std::string format = "B", std::optional<Value> data = {})
        : header(head), owner(owner), data_owner(data.value_or(owner)), exported(std::move(token)),
          offset(offset), stride(stride), length(length), readonly(readonly),
          format(std::move(format))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
        tracer.visit(data_owner);
    }
    std::size_t external_bytes() const noexcept
    {
        return sizeof(MemoryViewExports) + format.capacity() + 1;
    }
};

namespace detail {
// Native consumers root the exporter and release the export at scope exit,
// rather than waiting for an unreachable temporary memoryview to be collected.
struct BufferExport {
    ReturnSlot owner, exporter;
    std::shared_ptr<MemoryViewExport> lease;
    std::size_t offset = 0, length = 0;
    std::int64_t stride = 1, count = 0;
    bool readonly = false;
    std::string format = "B";
    explicit BufferExport(Attachment &context) : owner(context), exporter(context) {}
};
bool export_buffer(Attachment &, Handle, BufferExport &, ReturnSlot &);
void memoryview_from_buffer(Attachment &, const BufferExport &, ReturnSlot &, bool raw = false);
bool writable_buffer(Attachment &, Handle, BufferExport &, ReturnSlot &);
void write_buffer(Attachment &, const BufferExport &, std::string_view);
// Returns false for unsupported objects or a released view; errors are in out.
bool buffer_bytes(Attachment &, Handle, std::string &, ReturnSlot &, bool contiguous = true);
void install_memoryviews(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
