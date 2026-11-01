// Socket semantics adapted from CPython 3.12.14 Modules/socketmodule.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/socketmodule.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/errors.hpp"
#include "internal/pytime.hpp"
#include "internal/signalmodule.hpp"
#include "internal/socket_addresses.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

struct SocketDefaults {
    ObjectHeader header;
    std::atomic<std::int64_t> timeout{-1};
    explicit SocketDefaults(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};
struct SocketState {
    std::mutex mutex;
    int fd = -1;
    const int family, kind, protocol;
    std::int64_t timeout;
    SocketState(int family, int kind, int protocol, std::int64_t timeout)
        : family(family), kind(kind), protocol(protocol), timeout(timeout)
    {}
    ~SocketState()
    {
        // Normal GC calls the post-STW finalizer; this is teardown cleanup
        // after the last attachment has exited, not a collector wait.
        if (fd >= 0)
            ::close(fd);
    }
};
// The internal resource has no user __del__. A socket subclass may resurrect
// itself without either losing the fd or suppressing native finalization.
struct SocketResource {
    ObjectHeader header;
    const NativeOwner<SocketState> state;
    SocketResource(InitialType type, int family, int kind, int protocol, std::int64_t timeout)
        : header(type), state(std::make_unique<SocketState>(family, kind, protocol, timeout))
    {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return sizeof(SocketState);
    }
    static void finalize(Attachment &c, Handle self, ReturnSlot &out)
    {
        auto &state = *c.payload<SocketResource>(self).state;
        {
            NativeWait wait(c);
            int fd;
            {
                std::lock_guard lock(state.mutex);
                fd = std::exchange(state.fd, -1);
            }
            if (fd >= 0)
                ::close(fd);
        }
        out.ok(c.builtin(CoreObject::None));
    }
};
struct SocketObject {
    ObjectHeader header;
    AtomicGcEdge resource;
    explicit SocketObject(InitialType type) : header(type) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(resource);
    }
};
// Independent native fd leases survive public close/detach/reinitialization.
// Their acquisition/disposal parks, including a potentially lingering close.
struct Descriptor {
    Attachment &context;
    int fd = -1;
    explicit Descriptor(Attachment &c) : context(c) {}
    ~Descriptor()
    {
        if (fd >= 0) {
            NativeWait wait(context);
            ::close(fd);
        }
    }
    int release() noexcept
    {
        return std::exchange(fd, -1);
    }
};
bool resource(Attachment &c, Value self, ReturnSlot &out)
{
    if (!c.has_layout<SocketObject>(self)) {
        raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a socket object", out);
        return false;
    }
    c.load(self, c.payload<SocketObject>(self).resource, out);
    return true;
}
bool acquire(Attachment &c, SocketState &state, Descriptor &lease, std::int64_t &timeout,
             ReturnSlot &out)
{
    int error = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(state.mutex);
        timeout = state.timeout;
        if (state.fd < 0)
            error = EBADF;
        else {
            lease.fd = ::fcntl(state.fd, F_DUPFD_CLOEXEC, 0);
            if (lease.fd < 0)
                error = errno;
        }
    }
    if (error)
        raise_os_error(c, error, out);
    return !error;
}
bool number(Attachment &c, Value value, std::int64_t &result, ReturnSlot &out)
{
    RootScope roots(c);
    index(c, roots.root(value), out);
    if (failed(out))
        return false;
    if (!int_fits_signed(c, out.value(), result)) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C long",
                    out);
        return false;
    }
    return true;
}
bool integer(Attachment &c, Value value, int &result, ReturnSlot &out)
{
    std::int64_t n;
    if (!number(c, value, n, out))
        return false;
    if (n < INT_MIN || n > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    result = static_cast<int>(n);
    return true;
}
bool timeout_value(Attachment &c, Value value, std::int64_t &result, ReturnSlot &out)
{
    if (value == c.builtin(CoreObject::None)) {
        result = -1;
        return true;
    }
    RootScope roots(c);
    if (!seconds_to_nanoseconds(c, roots.root(value), result, out))
        return false;
    if (result < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"Timeout value out of range", out);
        return false;
    }
    return true;
}
bool nonblocking(int fd, bool enabled)
{
    int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 &&
           ::fcntl(fd, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) == 0;
}
void create(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(args[0]) ||
        c.payload<TypeObject>(args[0]).instance_layout.native_family !=
            LayoutDescriptor::of<SocketObject>().native_family) {
        raise_error(c, CoreObject::TypeErrorType, U"socket.__new__ requires a socket subtype", out);
        return;
    }
    c.allocate_instance<SocketObject>(roots.root(args[0]), out);
    if (failed(out))
        return;
    auto self = roots.root(out.value());
    ReturnSlot state(c);
    c.allocate<SocketResource>(state, 0, 0, 0, -1);
    if (failed(state)) {
        out.raised(state.exception());
        return;
    }
    c.store(self.get(c), c.payload<SocketObject>(self).resource, state.value());
    out.ok(self.get(c));
}
void initialize(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot old(c), fresh(c);
    if (!resource(c, args[0], old))
        return;
    int family, kind, protocol;
    if (!integer(c, args[1], family, out) || !integer(c, args[2], kind, out) ||
        !integer(c, args[3], protocol, out))
        return;
    auto defaults = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    auto timeout = c.payload<SocketDefaults>(defaults).timeout.load(std::memory_order_relaxed);
    Descriptor opened(c);
    int error = 0;
    if (args[4] == c.builtin(CoreObject::None)) {
        if (family == -1)
            family = AF_INET;
        if (kind == -1)
            kind = SOCK_STREAM;
        if (protocol == -1)
            protocol = 0;
        {
            NativeWait wait(c);
            opened.fd = ::socket(family, kind | SOCK_CLOEXEC, protocol);
            if (opened.fd < 0)
                error = errno;
        }
    } else {
        int fd;
        if (!integer(c, args[4], fd, out))
            return;
        if (fd < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"negative file descriptor", out);
            return;
        }
        sockaddr_storage address{};
        socklen_t length = sizeof(address), size = sizeof(kind);
        {
            NativeWait wait(c);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) < 0)
                error = errno;
            else {
                if (family == -1)
                    family = address.ss_family;
                if (kind == -1 && ::getsockopt(fd, SOL_SOCKET, SO_TYPE, &kind, &size) < 0)
                    error = errno;
                if (protocol == -1) {
#ifdef SO_PROTOCOL
                    size = sizeof(protocol);
                    if (::getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &size) < 0)
                        error = errno;
#else
                    protocol = 0;
#endif
                }
            }
        }
        if (!error)
            opened.fd = fd;
    }
    if (error) {
        raise_os_error(c, error, out);
        return;
    }
    if (kind & SOCK_NONBLOCK)
        timeout = 0;
    kind &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
    {
        NativeWait wait(c);
        if (timeout >= 0 && !nonblocking(opened.fd, true))
            error = errno;
    }
    if (error) {
        raise_os_error(c, error, out);
        return;
    }
    c.allocate<SocketResource>(fresh, family, kind, protocol, timeout);
    if (failed(fresh)) {
        out.raised(fresh.exception());
        return;
    }
    c.payload<SocketResource>(fresh.value()).state->fd = opened.release();
    c.exchange(self.get(c), c.payload<SocketObject>(self).resource, fresh.value(), old);
    SocketResource::finalize(c, roots.root(old.value()), out);
}
template <int Field> void field(Attachment &c, Value self, ReturnSlot &out)
{
    ReturnSlot owner(c);
    if (!resource(c, self, owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    make_int(c, Field == 0 ? state.family : Field == 1 ? state.kind : state.protocol, out);
}
void fileno(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    int fd;
    {
        NativeWait wait(c);
        std::lock_guard lock(state.mutex);
        fd = state.fd;
    }
    make_int(c, fd, out);
}
template <bool Detach>
void close_socket(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    int fd, error = 0;
    {
        NativeWait wait(c);
        {
            std::lock_guard lock(state.mutex);
            fd = std::exchange(state.fd, -1);
        }
        if constexpr (!Detach)
            if (fd >= 0 && ::close(fd) < 0 && errno != ECONNRESET)
                error = errno;
    }
    if (error)
        raise_os_error(c, error, out);
    else if constexpr (Detach)
        make_int(c, fd, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
template <bool Set, bool Blocking>
void timeout_entry(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    std::int64_t timeout = -1;
    if constexpr (Set) {
        if constexpr (Blocking) {
            ReturnSlot converted(c);
            truth(c, roots.root(args[1]), converted);
            if (failed(converted)) {
                out.raised(converted.exception());
                return;
            }
            timeout = converted.value() == c.builtin(CoreObject::False) ? 0 : -1;
        } else if (!timeout_value(c, args[1], timeout, out))
            return;
    }
    int error = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(state.mutex);
        if constexpr (Set) {
            state.timeout = timeout;
            if (state.fd < 0)
                error = EBADF;
            else if (!nonblocking(state.fd, timeout >= 0))
                error = errno;
        } else
            timeout = state.timeout;
    }
    if (error)
        raise_os_error(c, error, out);
    else if constexpr (Set)
        out.ok(c.builtin(CoreObject::None));
    else if constexpr (Blocking)
        out.ok(c.builtin(timeout == 0 ? CoreObject::False : CoreObject::True));
    else if (timeout < 0)
        out.ok(c.builtin(CoreObject::None));
    else
        make_float(c, timeout / 1e9, out);
}

bool wait_ready(Attachment &c, int fd, short events, std::int64_t timeout,
                std::chrono::steady_clock::time_point start, ReturnSlot &out)
{
    for (;;) {
        int millis = -1;
        if (timeout >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            auto left = std::max<std::int64_t>(timeout - elapsed, 0);
            millis = static_cast<int>(
                std::min<std::int64_t>(left / 1000000 + (left % 1000000 != 0), INT_MAX));
        }
        pollfd selected{fd, events, 0};
        int result, error;
        {
            NativeWait wait(c);
            result = ::poll(&selected, 1, millis);
            error = result < 0 ? errno : 0;
        }
        if (error == EINTR) {
            if (check_signals(c, out))
                return false;
            continue;
        }
        if (error) {
            raise_os_error(c, error, out);
            return false;
        }
        if (result)
            return true;
        if (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                 start)
                .count() < timeout)
            continue;
        raise_error(c, CoreObject::TimeoutErrorType, U"timed out", out);
        return false;
    }
}
// One monotonic deadline covers all retries and all of sendall, including EINTR.
template <class Operation>
bool perform(Attachment &c, int fd, short events, std::int64_t timeout,
             std::chrono::steady_clock::time_point start, Operation &&operation, ssize_t &result,
             ReturnSlot &out)
{
    for (;;) {
        if (timeout > 0 && !wait_ready(c, fd, events, timeout, start, out))
            return false;
        int error;
        {
            NativeWait wait(c);
            result = operation();
            error = result < 0 ? errno : 0;
        }
        if (!error)
            return true;
        if (error == EINTR) {
            if (check_signals(c, out))
                return false;
            continue;
        }
        if ((error == EWOULDBLOCK || error == EAGAIN) && timeout != 0) {
            if (timeout < 0 && !wait_ready(c, fd, events, timeout, start, out))
                return false;
            continue;
        }
        raise_os_error(c, error, out);
        return false;
    }
}
template <bool Bind, bool Ex = false>
void address_operation(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    SocketAddress address;
    auto gaierror = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    if (!socket_address(c, state.family, args[1], address, gaierror, out))
        return;
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    int result, error;
    {
        NativeWait wait(c);
        auto *native = reinterpret_cast<const sockaddr *>(&address.storage);
        result = Bind ? ::bind(lease.fd, native, address.size)
                      : ::connect(lease.fd, native, address.size);
        error = result < 0 ? errno : 0;
    }
    if constexpr (!Bind) {
        if (error == EINTR && check_signals(c, out))
            return;
        if ((error == EINPROGRESS && timeout > 0) || (error == EINTR && timeout != 0)) {
            if (!wait_ready(c, lease.fd, POLLOUT, timeout, std::chrono::steady_clock::now(), out)) {
                if constexpr (Ex)
                    if (failed(out) && is_subtype(c, c.type_of(out.exception()),
                                                  c.builtin(CoreObject::TimeoutErrorType)))
                        make_int(c, EWOULDBLOCK, out);
                return;
            }
            int status = 0;
            socklen_t size = sizeof(status);
            {
                NativeWait wait(c);
                result = ::getsockopt(lease.fd, SOL_SOCKET, SO_ERROR, &status, &size);
                error = result < 0 ? errno : status;
            }
        }
    }
    if constexpr (Ex)
        make_int(c, error, out);
    else if (error)
        raise_os_error(c, error, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
template <bool Peer>
void get_address(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    SocketAddress address;
    address.size = sizeof(address.storage);
    int result, error;
    {
        NativeWait wait(c);
        auto *native = reinterpret_cast<sockaddr *>(&address.storage);
        result = Peer ? ::getpeername(lease.fd, native, &address.size)
                      : ::getsockname(lease.fd, native, &address.size);
        error = result < 0 ? errno : 0;
    }
    if (error)
        raise_os_error(c, error, out);
    else
        socket_address_value(c, reinterpret_cast<sockaddr *>(&address.storage), address.size, out);
}
template <bool Shutdown>
void simple_operation(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    int argument;
    if (!integer(c, args[1], argument, out))
        return;
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    int result, error;
    {
        NativeWait wait(c);
        result =
            Shutdown ? ::shutdown(lease.fd, argument) : ::listen(lease.fd, std::max(argument, 0));
        error = result < 0 ? errno : 0;
    }
    if (error)
        raise_os_error(c, error, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
void accept_socket(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot owner(c), address_value(c), fd_value(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    Descriptor lease(c), accepted(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    SocketAddress address;
    ssize_t result;
    if (!perform(
            c, lease.fd, POLLIN, timeout, std::chrono::steady_clock::now(),
            [&] {
                address.size = sizeof(address.storage);
                return ::accept4(lease.fd, reinterpret_cast<sockaddr *>(&address.storage),
                                 &address.size, SOCK_CLOEXEC);
            },
            result, out))
        return;
    accepted.fd = static_cast<int>(result);
    socket_address_value(c, reinterpret_cast<sockaddr *>(&address.storage), address.size,
                         address_value);
    if (failed(address_value)) {
        out.raised(address_value.exception());
        return;
    }
    make_int(c, accepted.fd, fd_value);
    const std::array values{fd_value.value(), address_value.value()};
    cxxpython::core::make_tuple(c, values, out);
    if (!failed(out))
        accepted.release();
}
template <bool Into, bool From>
void receive(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::int64_t size;
    int flags;
    BufferExport buffer(c);
    if constexpr (Into) {
        if (!writable_buffer(c, roots.root(args[1]), buffer, out) ||
            !number(c, args[2], size, out) || !integer(c, args[3], flags, out))
            return;
        if (size == 0)
            size = static_cast<std::int64_t>(buffer.length);
        if (size > static_cast<std::int64_t>(buffer.length)) {
            raise_error(c, CoreObject::ValueErrorType, U"buffer too small for requested bytes",
                        out);
            return;
        }
    } else if (!number(c, args[1], size, out) || !integer(c, args[2], flags, out))
        return;
    if (size < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"negative buffersize in recv", out);
        return;
    }
    std::string data;
    const auto limit = std::min<std::uint64_t>(
        data.max_size(), std::numeric_limits<std::ptrdiff_t>::max() - sizeof(BytesObject));
    if (static_cast<std::uint64_t>(size) > limit) {
        raise_error(c, CoreObject::OverflowErrorType, U"requested buffersize is too large", out);
        return;
    }
    data.resize(static_cast<std::size_t>(size));
    ReturnSlot owner(c), received(c), address_value(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    SocketAddress address;
    ssize_t count = 0;
    if (size || From) {
        if (!perform(
                c, lease.fd, POLLIN, timeout, std::chrono::steady_clock::now(),
                [&] {
                    if constexpr (From) {
                        address.size = sizeof(address.storage);
                        return ::recvfrom(lease.fd, data.data(), data.size(), flags,
                                          reinterpret_cast<sockaddr *>(&address.storage),
                                          &address.size);
                    } else
                        return ::recv(lease.fd, data.data(), data.size(), flags);
                },
                count, out))
            return;
    }
    // MSG_TRUNC can report more bytes than the syscall copied.
    data.resize(std::min<std::size_t>(static_cast<std::size_t>(count), data.size()));
    if constexpr (Into) {
        write_buffer(c, buffer, data);
        make_int(c, count, received);
    } else
        make_bytes(c, std::move(data), received);
    if (failed(received)) {
        out.raised(received.exception());
        return;
    }
    if constexpr (From) {
        socket_address_value(c, reinterpret_cast<sockaddr *>(&address.storage), address.size,
                             address_value);
        if (failed(address_value)) {
            out.raised(address_value.exception());
            return;
        }
        const std::array values{received.value(), address_value.value()};
        cxxpython::core::make_tuple(c, values, out);
    } else
        out.ok(received.value());
}
template <bool All, bool To = false>
void send(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string data;
    if (!buffer_bytes(c, roots.root(args[1]), data, out)) {
        if (!failed(out))
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    int flags = 0;
    SocketAddress address;
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    if constexpr (To) {
        const auto &tail = c.payload<TupleObject>(args[2]).items;
        if (tail.size() != 1 && tail.size() != 2) {
            raise_error(c, CoreObject::TypeErrorType, U"sendto requires data, [flags,] address",
                        out);
            return;
        }
        if (tail.size() == 2 && !integer(c, tail[0], flags, out))
            return;
        auto gaierror = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
        if (!socket_address(c, state.family, tail.back(), address, gaierror, out))
            return;
    } else if (!integer(c, args[2], flags, out))
        return;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    const auto start = std::chrono::steady_clock::now();
    std::size_t offset = 0;
    do {
        ssize_t count;
        if (!perform(
                c, lease.fd, POLLOUT, timeout, start,
                [&] {
                    if constexpr (To)
                        return ::sendto(lease.fd, data.data() + offset, data.size() - offset, flags,
                                        reinterpret_cast<sockaddr *>(&address.storage),
                                        address.size);
                    else
                        return ::send(lease.fd, data.data() + offset, data.size() - offset, flags);
                },
                count, out))
            return;
        offset += static_cast<std::size_t>(count);
        if constexpr (!All) {
            make_int(c, count, out);
            return;
        }
        if (!count && offset < data.size()) {
            raise_os_error(c, EPIPE, out);
            return;
        }
        if (check_signals(c, out))
            return;
    } while (offset < data.size());
    out.ok(c.builtin(CoreObject::None));
}
template <bool Set> void option(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    int level, name;
    if (!integer(c, args[1], level, out) || !integer(c, args[2], name, out))
        return;
    std::string bytes;
    int numeric = 0;
    bool as_integer = true, null_buffer = false;
    socklen_t length = sizeof(numeric);
    if constexpr (Set) {
        const auto &tail = c.payload<TupleObject>(args[3]).items;
        if (tail.size() == 1) {
            if (is_int(c, tail[0]) || type_attribute(c, c.type_of(tail[0]), U"__index__")) {
                if (!integer(c, tail[0], numeric, out))
                    return;
            } else {
                if (!buffer_bytes(c, roots.root(tail[0]), bytes, out)) {
                    if (!failed(out))
                        raise_error(c, CoreObject::TypeErrorType,
                                    U"a bytes-like object is required", out);
                    return;
                }
                as_integer = false;
                length = static_cast<socklen_t>(bytes.size());
            }
        } else if (tail.size() == 2 && tail[0] == c.builtin(CoreObject::None)) {
            int size;
            if (!integer(c, tail[1], size, out))
                return;
            if (size < 0) {
                raise_error(c, CoreObject::ValueErrorType, U"optlen must be non-negative", out);
                return;
            }
            null_buffer = true;
            length = static_cast<socklen_t>(size);
        } else {
            raise_error(c, CoreObject::TypeErrorType, U"invalid setsockopt arguments", out);
            return;
        }
    } else {
        int size;
        if (!integer(c, args[3], size, out))
            return;
        if (size < 0 || size > 1024) {
            raise_error(c, CoreObject::OSErrorType, U"getsockopt buflen out of range", out);
            return;
        }
        as_integer = size == 0;
        if (!as_integer) {
            bytes.resize(static_cast<std::size_t>(size));
            length = static_cast<socklen_t>(size);
        }
    }
    ReturnSlot owner(c);
    if (!resource(c, args[0], owner))
        return;
    auto &state = *c.payload<SocketResource>(owner.value()).state;
    Descriptor lease(c);
    std::int64_t timeout;
    if (!acquire(c, state, lease, timeout, out))
        return;
    int result, error;
    {
        NativeWait wait(c);
        if constexpr (Set)
            result = ::setsockopt(lease.fd, level, name,
                                  null_buffer  ? nullptr
                                  : as_integer ? static_cast<void *>(&numeric)
                                               : static_cast<void *>(bytes.data()),
                                  length);
        else
            result = ::getsockopt(lease.fd, level, name,
                                  as_integer ? static_cast<void *>(&numeric)
                                             : static_cast<void *>(bytes.data()),
                                  &length);
        error = result < 0 ? errno : 0;
    }
    if (error)
        raise_os_error(c, error, out);
    else if constexpr (Set)
        out.ok(c.builtin(CoreObject::None));
    else if (as_integer)
        make_int(c, numeric, out);
    else {
        bytes.resize(length);
        make_bytes(c, std::move(bytes), out);
    }
}
template <bool Set>
void default_timeout(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    auto &state = c.payload<SocketDefaults>(c.payload<NativeFunctionObject>(function).captures[0]);
    if constexpr (Set) {
        std::int64_t timeout;
        if (!timeout_value(c, args[0], timeout, out))
            return;
        state.timeout.store(timeout, std::memory_order_relaxed);
        out.ok(c.builtin(CoreObject::None));
    } else {
        const auto timeout = state.timeout.load(std::memory_order_relaxed);
        if (timeout < 0)
            out.ok(c.builtin(CoreObject::None));
        else
            make_float(c, timeout / 1e9, out);
    }
}
template <bool Close>
void descriptor_operation(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    int fd;
    if (!integer(c, args[0], fd, out))
        return;
    Descriptor duplicate(c);
    int result, error;
    {
        NativeWait wait(c);
        result = Close ? ::close(fd) : ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
        error = result < 0 ? errno : 0;
        if constexpr (!Close)
            duplicate.fd = result;
    }
    if (error)
        raise_os_error(c, error, out);
    else if constexpr (Close)
        out.ok(c.builtin(CoreObject::None));
    else {
        make_int(c, duplicate.fd, out);
        if (!failed(out))
            duplicate.release();
    }
}
void socket_pair(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    int family, kind, protocol;
    if (!integer(c, args[0], family, out) || !integer(c, args[1], kind, out) ||
        !integer(c, args[2], protocol, out))
        return;
    int fds[2], result, error;
    Descriptor left(c), right(c);
    {
        NativeWait wait(c);
        result = ::socketpair(family, kind | SOCK_CLOEXEC, protocol, fds);
        error = result < 0 ? errno : 0;
        if (!error) {
            left.fd = fds[0];
            right.fd = fds[1];
        }
    }
    if (error) {
        raise_os_error(c, error, out);
        return;
    }
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot family_value(c), kind_value(c), protocol_value(c), fd_value(c), first(c), second(c);
    make_int(c, family, family_value);
    make_int(c, kind, kind_value);
    make_int(c, protocol, protocol_value);
    make_int(c, left.fd, fd_value);
    std::array values{family_value.value(), kind_value.value(), protocol_value.value(),
                      fd_value.value()};
    call(c, type, {values, {}}, first);
    if (failed(first)) {
        out.raised(first.exception());
        return;
    }
    left.release();
    make_int(c, right.fd, fd_value);
    values[3] = fd_value.value();
    call(c, type, {values, {}}, second);
    if (failed(second)) {
        out.raised(second.exception());
        return;
    }
    right.release();
    const std::array pair{first.value(), second.value()};
    cxxpython::core::make_tuple(c, pair, out);
}
} // namespace

void initialize_socket(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    const auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot temporary(c), key(c), value(c);
    auto store = [&](const char32_t *name, Value object) {
        auto held = roots.root(object);
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), held, out);
        return !failed(out);
    };
    c.allocate<SocketDefaults>(temporary);
    if (failed(temporary)) {
        out.raised(temporary.exception());
        return;
    }
    auto defaults = roots.root(temporary.value());
    auto make_error = [&](const char32_t *name) {
        const Handle bases[] = {roots.root(c.builtin(CoreObject::OSErrorType))};
        TypeSpec spec{name, bases};
        spec.module = U"socket";
        create_type(c, spec, temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return std::optional<Handle>{};
        }
        auto type = roots.root(temporary.value());
        if (!store(name, type.get(c)))
            return std::optional<Handle>{};
        return std::optional<Handle>{type};
    };
    auto gaierror = make_error(U"gaierror"), herror = make_error(U"herror");
    if (!gaierror || !herror || !store(U"error", c.builtin(CoreObject::OSErrorType)) ||
        !store(U"timeout", c.builtin(CoreObject::TimeoutErrorType)))
        return;
    NativeTypeSpec spec{{"socket"}, LayoutDescriptor::of<SocketObject>()};
    spec.type.module = U"_socket";
    spec.type.immutable = true;
    spec.type.slots = std::vector<std::u32string>{};
    create_native_type(c, spec, temporary);
    if (failed(temporary)) {
        out.raised(temporary.exception());
        return;
    }
    auto type = roots.root(temporary.value());
    auto signature = [](std::initializer_list<Parameter> params) {
        SignatureLayout result;
        result.parameters = params;
        return result;
    };
    auto only = ParameterKind::PositionalOnly;
    auto unary = signature({{U"self", only}});
    auto binary = signature({{U"self", only}, {U"value", only}});
    auto constructor = unary;
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", constructor, create);
    const auto type_dict = roots.root(*TypeView(c, type).dictionary());
    auto new_function = roots.root(*name_lookup(c, type_dict.get(c), U"__new__"));
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            temporary, new_function.get(c));
    make_str(c, U"__new__", key);
    name_store(c, type_dict.get(c), key.value(), temporary.value());
    make_int(c, -1, temporary);
    auto unspecified = roots.root(temporary.value());
    install_native_attribute(
        c, core, type, U"__init__",
        signature({{U"self", only},
                   {U"family", ParameterKind::PositionalOrKeyword, unspecified.get(c)},
                   {U"type", ParameterKind::PositionalOrKeyword, unspecified.get(c)},
                   {U"proto", ParameterKind::PositionalOrKeyword, unspecified.get(c)},
                   {U"fileno", ParameterKind::PositionalOrKeyword, c.builtin(CoreObject::None)}}),
        initialize, {defaults.get(c)});
    install_native_field(c, core, type, U"family", field<0>);
    install_native_field(c, core, type, U"type", field<1>);
    install_native_field(c, core, type, U"proto", field<2>);
    install_native_attribute(c, core, type, U"fileno", unary, fileno);
    install_native_attribute(c, core, type, U"close", unary, close_socket<false>);
    install_native_attribute(c, core, type, U"detach", unary, close_socket<true>);
    install_native_attribute(c, core, type, U"gettimeout", unary, timeout_entry<false, false>);
    install_native_attribute(c, core, type, U"getblocking", unary, timeout_entry<false, true>);
    install_native_attribute(c, core, type, U"settimeout", binary, timeout_entry<true, false>);
    install_native_attribute(c, core, type, U"setblocking", binary, timeout_entry<true, true>);
    install_native_attribute(c, core, type, U"bind", binary, address_operation<true>,
                             {gaierror->get(c)});
    install_native_attribute(c, core, type, U"connect", binary, address_operation<false>,
                             {gaierror->get(c)});
    install_native_attribute(c, core, type, U"connect_ex", binary, address_operation<false, true>,
                             {gaierror->get(c)});
    install_native_attribute(c, core, type, U"getsockname", unary, get_address<false>);
    install_native_attribute(c, core, type, U"getpeername", unary, get_address<true>);
    install_native_attribute(c, core, type, U"_accept", unary, accept_socket);
    install_native_attribute(c, core, type, U"shutdown", binary, simple_operation<true>);
    make_int(c, std::min(SOMAXCONN, 128), temporary);
    auto backlog = roots.root(temporary.value());
    install_native_attribute(c, core, type, U"listen",
                             signature({{U"self", only}, {U"backlog", only, backlog.get(c)}}),
                             simple_operation<false>);
    auto receiving = signature(
        {{U"self", only}, {U"bufsize", only}, {U"flags", only, c.builtin(CoreObject::IntZero)}});
    install_native_attribute(c, core, type, U"recv", receiving, receive<false, false>);
    install_native_attribute(c, core, type, U"recvfrom", receiving, receive<false, true>);
    auto receiving_into =
        signature({{U"self", only},
                   {U"buffer", ParameterKind::PositionalOrKeyword},
                   {U"nbytes", ParameterKind::PositionalOrKeyword, c.builtin(CoreObject::IntZero)},
                   {U"flags", ParameterKind::PositionalOrKeyword, c.builtin(CoreObject::IntZero)}});
    install_native_attribute(c, core, type, U"recv_into", receiving_into, receive<true, false>);
    install_native_attribute(c, core, type, U"recvfrom_into", receiving_into, receive<true, true>);
    auto sending = signature(
        {{U"self", only}, {U"data", only}, {U"flags", only, c.builtin(CoreObject::IntZero)}});
    install_native_attribute(c, core, type, U"send", sending, send<false>);
    install_native_attribute(c, core, type, U"sendall", sending, send<true>);
    auto sending_to = signature({{U"self", only}, {U"data", only}});
    sending_to.varargs = U"args";
    install_native_attribute(c, core, type, U"sendto", sending_to, send<false, true>,
                             {gaierror->get(c)});
    auto options = signature({{U"self", only},
                              {U"level", only},
                              {U"optname", only},
                              {U"buflen", only, c.builtin(CoreObject::IntZero)}});
    install_native_attribute(c, core, type, U"getsockopt", options, option<false>);
    options.parameters.pop_back();
    options.varargs = U"args";
    install_native_attribute(c, core, type, U"setsockopt", options, option<true>);
    if (!store(U"socket", type.get(c)) || !store(U"SocketType", type.get(c)) ||
        !store(U"has_ipv6", c.builtin(CoreObject::True)))
        return;
    auto add = [&](const char *name, SignatureLayout params, NativeEntry entry,
                   std::vector<Value> captures = {}) {
        make_native_function(c, name, std::move(params), entry, captures, value,
                             NativeFunctionBinding::Function);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        std::u32string wide(name, name + std::strlen(name));
        return store(wide.c_str(), value.value());
    };
    if (!add("getdefaulttimeout", {}, default_timeout<false>, {defaults.get(c)}) ||
        !add("setdefaulttimeout", signature({{U"timeout", only}}), default_timeout<true>,
             {defaults.get(c)}) ||
        !add("close", signature({{U"fd", only}}), descriptor_operation<true>) ||
        !add("dup", signature({{U"fd", only}}), descriptor_operation<false>))
        return;
    make_int(c, AF_UNIX, temporary);
    auto unix_family = roots.root(temporary.value());
    make_int(c, SOCK_STREAM, temporary);
    auto stream = roots.root(temporary.value());
    if (!add("socketpair",
             signature({{U"family", only, unix_family.get(c)},
                        {U"type", only, stream.get(c)},
                        {U"proto", only, c.builtin(CoreObject::IntZero)}}),
             socket_pair, {type.get(c)}))
        return;
    const std::pair<const char32_t *, int> constants[] = {
        {U"AF_UNSPEC", AF_UNSPEC},
        {U"AF_UNIX", AF_UNIX},
        {U"AF_INET", AF_INET},
        {U"AF_INET6", AF_INET6},
        {U"SOCK_STREAM", SOCK_STREAM},
        {U"SOCK_DGRAM", SOCK_DGRAM},
        {U"SOCK_RAW", SOCK_RAW},
        {U"SOCK_SEQPACKET", SOCK_SEQPACKET},
        {U"SOCK_NONBLOCK", SOCK_NONBLOCK},
        {U"SOCK_CLOEXEC", SOCK_CLOEXEC},
        {U"SHUT_RD", SHUT_RD},
        {U"SHUT_WR", SHUT_WR},
        {U"SHUT_RDWR", SHUT_RDWR},
        {U"SOL_SOCKET", SOL_SOCKET},
        {U"SO_TYPE", SO_TYPE},
        {U"SO_ERROR", SO_ERROR},
        {U"SO_REUSEADDR", SO_REUSEADDR},
        {U"SO_KEEPALIVE", SO_KEEPALIVE},
        {U"SO_BROADCAST", SO_BROADCAST},
        {U"SO_SNDBUF", SO_SNDBUF},
        {U"SO_RCVBUF", SO_RCVBUF},
        {U"SO_LINGER", SO_LINGER},
        {U"SO_ACCEPTCONN", SO_ACCEPTCONN},
        {U"IPPROTO_IP", IPPROTO_IP},
        {U"IPPROTO_IPV6", IPPROTO_IPV6},
        {U"IPPROTO_TCP", IPPROTO_TCP},
        {U"IPPROTO_UDP", IPPROTO_UDP},
        {U"TCP_NODELAY", TCP_NODELAY},
        {U"IPV6_V6ONLY", IPV6_V6ONLY},
        {U"MSG_PEEK", MSG_PEEK},
        {U"MSG_OOB", MSG_OOB},
        {U"MSG_WAITALL", MSG_WAITALL},
        {U"MSG_DONTWAIT", MSG_DONTWAIT},
        {U"MSG_TRUNC", MSG_TRUNC},
        {U"AI_PASSIVE", AI_PASSIVE},
        {U"AI_CANONNAME", AI_CANONNAME},
        {U"AI_NUMERICHOST", AI_NUMERICHOST},
        {U"AI_NUMERICSERV", AI_NUMERICSERV},
        {U"AI_ADDRCONFIG", AI_ADDRCONFIG},
        {U"AI_V4MAPPED", AI_V4MAPPED},
        {U"AI_ALL", AI_ALL},
        {U"NI_NUMERICHOST", NI_NUMERICHOST},
        {U"NI_NUMERICSERV", NI_NUMERICSERV},
        {U"NI_NAMEREQD", NI_NAMEREQD},
        {U"NI_DGRAM", NI_DGRAM},
        {U"NI_NOFQDN", NI_NOFQDN},
        {U"EAI_NONAME", EAI_NONAME},
        {U"EAI_AGAIN", EAI_AGAIN},
        {U"EAI_FAIL", EAI_FAIL},
        {U"EAI_FAMILY", EAI_FAMILY},
        {U"EAI_SERVICE", EAI_SERVICE},
#ifdef SO_REUSEPORT
        {U"SO_REUSEPORT", SO_REUSEPORT},
#endif
    };
    for (const auto &[name, n] : constants) {
        make_int(c, n, temporary);
        if (!store(name, temporary.value()))
            return;
    }
    initialize_socket_addresses(c, module, *gaierror, *herror, out);
}
} // namespace cxxpython::core::detail
