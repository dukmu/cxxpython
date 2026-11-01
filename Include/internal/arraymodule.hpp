#pragma once

#include "cxxpython/bytearrayobject.hpp"

namespace cxxpython::core {
struct ArrayObject {
    ObjectHeader header;
    const char typecode;
    const std::size_t itemsize;
    const std::shared_ptr<ByteArrayObject::Storage> contents;
    ArrayObject(InitialType type, char code, std::size_t size, std::string bytes = {})
        : header(type), typecode(code), itemsize(size),
          contents(std::make_shared<ByteArrayObject::Storage>())
    {
        contents->bytes = std::move(bytes);
    }
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return sizeof(ByteArrayObject::Storage) + contents->bytes.capacity() + 1;
    }
};

namespace detail {
void initialize_array(Attachment &, Handle, ReturnSlot &);
}
} // namespace cxxpython::core
