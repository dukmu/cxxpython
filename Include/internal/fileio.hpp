#pragma once

#include "cxxpython/objects.hpp"

#include <condition_variable>

namespace cxxpython::core {
struct FileIOObject {
    struct State {
        std::mutex mutex;
        std::condition_variable idle;
        int fd = -1;
        std::size_t active = 0;
        bool readable = false, writable = false, appending = false, created = false, closefd = true;
        std::size_t blksize = 8192;
        ~State();
    };
    ObjectHeader header;
    const NativeOwner<State> state;
    explicit FileIOObject(InitialType head) : header(head), state(std::make_unique<State>()) {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return sizeof(State);
    }
};
} // namespace cxxpython::core
