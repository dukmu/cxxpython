#pragma once

#include "cxxpython/bytearrayobject.hpp"

namespace cxxpython::core {
struct StructField {
    char code;
    std::size_t offset;
    std::size_t size;
    std::size_t repeat;
};

struct StructObject {
    ObjectHeader header;
    const std::string format;
    const std::vector<StructField> fields;
    const std::size_t size;
    const std::size_t count;
    const bool little_endian;
    StructObject(InitialType type, std::string format_in, std::vector<StructField> fields_in,
                 std::size_t size_in, std::size_t count_in, bool little_in)
        : header(type), format(std::move(format_in)), fields(std::move(fields_in)), size(size_in),
          count(count_in), little_endian(little_in)
    {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return format.capacity() + 1 + fields.capacity() * sizeof(StructField);
    }
};

struct StructBufferExport {
    const std::shared_ptr<ByteArrayObject::Storage> storage;
    explicit StructBufferExport(std::shared_ptr<ByteArrayObject::Storage> storage_in)
        : storage(std::move(storage_in))
    {
        ++storage->exports;
    }
    ~StructBufferExport()
    {
        --storage->exports;
    }
};

struct StructIteratorObject {
    ObjectHeader header;
    const Value compiled;
    const Value buffer;
    std::mutex mutex;
    std::size_t offset = 0;
    bool exhausted = false;
    std::shared_ptr<StructBufferExport> exported;
    StructIteratorObject(InitialType type, Value compiled_in, Value buffer_in,
                         std::shared_ptr<StructBufferExport> exported_in)
        : header(type), compiled(compiled_in), buffer(buffer_in), exported(std::move(exported_in))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(compiled);
        tracer.visit(buffer);
    }
};

namespace detail {
void initialize_struct(Attachment &, Handle, ReturnSlot &);
void install_struct(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
