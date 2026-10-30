#pragma once

#include "internal/iomodule.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace cxxpython::core {
struct BufferedIOObject {
    ObjectHeader header;
    AtomicGcEdge reader;
    AtomicGcEdge writer;
    std::mutex mutex;
    std::condition_variable ready;
    std::thread::id owner;
    bool active = false;
    bool initialized = false;
    bool detached = false;
    bool closed = false;
    bool can_read = false;
    bool can_write = false;
    bool pair = false;
    std::size_t capacity = 0;
    std::vector<std::uint8_t> input;
    std::size_t position = 0;
    std::vector<std::uint8_t> output;

    BufferedIOObject(InitialType type, Value none) : header(type), reader(none), writer(none) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(reader);
        tracer.visit(writer);
    }
};

namespace detail {
void initialize_bufferedio(Attachment &, Handle module, ReturnSlot &);
}
} // namespace cxxpython::core
