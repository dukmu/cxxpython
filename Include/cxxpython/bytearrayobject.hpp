#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core {
struct ByteArrayObject {
    struct Storage {
        std::mutex mutex;
        std::string bytes;
        std::size_t version = 0;
    };
    ObjectHeader header;
    const std::unique_ptr<Storage> contents;
    explicit ByteArrayObject(InitialType head, std::string data = {})
        : header(head), contents(std::make_unique<Storage>())
    {
        contents->bytes = std::move(data);
    }
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return sizeof(Storage) + contents->bytes.capacity() + 1;
    }
};

bool is_bytearray(Attachment &, Value);
void make_bytearray(Attachment &, std::string, ReturnSlot &);
namespace detail {
// The caller must keep the owner rooted. The returned copy has no exported buffer.
std::string bytearray_snapshot(Attachment &, Handle);
void install_bytearrays(Attachment &, std::span<const Handle>);
} // namespace detail
} // namespace cxxpython::core
