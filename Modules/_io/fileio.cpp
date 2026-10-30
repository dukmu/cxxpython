// FileIO contracts follow CPython 3.12.14 Modules/_io/fileio.c (PSF license).
#include "internal/fileio.hpp"
#include "internal/iomodule.hpp"

#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/posixmodule.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cxxpython::core {
FileIOObject::State::~State()
{
    if (fd >= 0 && closefd)
        ::close(fd);
}
namespace detail {
namespace {
struct FileDescriptor {
    int fd = -1;
    bool owned = true;
    ~FileDescriptor()
    {
        if (fd >= 0 && owned)
            ::close(fd);
    }
};
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<FileIOObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a FileIO object", out);
    return false;
}
// Native leases protect descriptor reuse without holding a lock across I/O.
template <class F> bool operate(Attachment &c, Handle self, int mode, F &&body, ReturnSlot &out)
{
    auto &s = *c.payload<FileIOObject>(self).state;
    bool is_closed = false, wrong_mode = false;
    {
        NativeWait wait(c);
        int fd;
        {
            std::lock_guard lock(s.mutex);
            fd = s.fd;
            is_closed = fd < 0;
            wrong_mode = mode == 1 ? !s.readable : mode == 2 && !s.writable;
            if (!is_closed && !wrong_mode)
                ++s.active;
        }
        if (!is_closed && !wrong_mode) {
            struct Completion {
                FileIOObject::State &s;
                ~Completion()
                {
                    std::lock_guard lock(s.mutex);
                    if (!--s.active)
                        s.idle.notify_all();
                }
            } done{s};
            body(fd);
        }
    }
    if (is_closed)
        io_closed_error(c, out);
    else if (wrong_mode)
        io_unsupported(c, mode == 1 ? U"File not open for reading" : U"File not open for writing",
                       out);
    return !is_closed && !wrong_mode;
}
int replace(Attachment &c, FileIOObject::State &s, int fd, bool closefd = true,
            bool readable = false, bool writable = false, bool appending = false,
            std::size_t block = 8192, bool created = false)
{
    NativeWait wait(c);
    std::unique_lock lock(s.mutex);
    const int old = s.closefd ? s.fd : -1;
    s.fd = fd;
    if (fd >= 0) {
        s.closefd = closefd;
        s.readable = readable;
        s.writable = writable;
        s.appending = appending;
        s.created = created;
        s.blksize = block;
    }
    s.idle.wait(lock, [&] { return s.active == 0; });
    lock.unlock();
    return old >= 0 && ::close(old) < 0 ? errno : 0;
}
void create(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(a[0]) ||
        !is_subtype(c, a[0], c.builtin(CoreObject::FileIOType))) {
        raise_error(c, CoreObject::TypeErrorType, U"FileIO.__new__ requires a FileIO subtype", out);
        return;
    }
    c.allocate_instance<FileIOObject>(roots.root(a[0]), out);
}
void initialize(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = *c.payload<FileIOObject>(self).state;
    int error = replace(c, s, -1);
    if (error) {
        io_os_error(c, error, out);
        return;
    }
    if (!c.has_layout<StrObject>(a[2])) {
        raise_error(c, CoreObject::TypeErrorType, U"mode must be a string", out);
        return;
    }
    bool read = false, write = false, append = false, plus = false;
    char selected = 0;
    for (char32_t ch : c.payload<StrObject>(a[2]).value) {
        if (ch == 'b')
            continue;
        else if (ch == '+' && !plus)
            plus = true;
        else if ((ch == 'r' || ch == 'w' || ch == 'a' || ch == 'x') && !selected)
            selected = static_cast<char>(ch);
        else {
            raise_error(c, CoreObject::ValueErrorType, U"invalid mode", out);
            return;
        }
    }
    if (!selected) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"Must have exactly one of create/read/write/append mode", out);
        return;
    }
    read = selected == 'r' || plus;
    write = selected != 'r' || plus;
    append = selected == 'a';
    int flags = read && write ? O_RDWR : read ? O_RDONLY : O_WRONLY;
    if (selected == 'w')
        flags |= O_CREAT | O_TRUNC;
    if (selected == 'x')
        flags |= O_CREAT | O_EXCL;
    if (append)
        flags |= O_CREAT | O_APPEND;
    flags |= O_CLOEXEC;
    ReturnSlot converted(c), name(c);
    truth(c, roots.root(a[3]), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    const bool closefd = converted.value() == c.builtin(CoreObject::True);
    FileDescriptor opened;
    opened.owned = closefd;
    if (type_attribute(c, c.type_of(a[1]), U"__index__")) {
        std::int64_t fd;
        if (!io_index(c, roots.root(a[1]), fd, out))
            return;
        if (fd < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"negative file descriptor", out);
            return;
        }
        if (fd > INT_MAX) {
            raise_error(c, CoreObject::OverflowErrorType, U"file descriptor out of range", out);
            return;
        }
        opened.fd = static_cast<int>(fd);
        name.ok(a[1]);
    } else {
        std::string path;
        if (!filesystem_path(c, a[1], path, out, name))
            return;
        if (!closefd) {
            raise_error(c, CoreObject::ValueErrorType, U"Cannot use closefd=False with file name",
                        out);
            return;
        }
        if (a[4] == c.builtin(CoreObject::None)) {
            NativeWait wait(c);
            do {
                opened.fd = ::open(path.c_str(), flags, 0666);
            } while (opened.fd < 0 && errno == EINTR);
            if (opened.fd < 0)
                error = errno;
        } else {
            ReturnSlot number(c), result(c);
            make_int(c, flags, number);
            const Value arguments[] = {name.value(), number.value()};
            call(c, roots.root(a[4]), {arguments, {}}, result);
            if (result.tag() == ResultTag::Raised) {
                out.raised(result.exception());
                return;
            }
            std::int64_t fd;
            if (!is_int(c, result.value())) {
                raise_error(c, CoreObject::TypeErrorType, U"expected integer from opener", out);
                return;
            }
            if (!int_fits_signed(c, result.value(), fd) || fd > INT_MAX) {
                raise_error(c, CoreObject::OverflowErrorType, U"file descriptor out of range", out);
                return;
            }
            if (fd < 0) {
                raise_error(c, CoreObject::ValueErrorType,
                            U"opener returned a negative file descriptor", out);
                return;
            }
            opened.fd = static_cast<int>(fd);
        }
    }
    struct stat status {};
    if (!error) {
        NativeWait wait(c);
        if (::fstat(opened.fd, &status) < 0)
            error = errno;
        else if (S_ISDIR(status.st_mode))
            error = EISDIR;
        else if (::fcntl(opened.fd, F_SETFD, FD_CLOEXEC) < 0)
            error = errno;
        else if (append && ::lseek(opened.fd, 0, SEEK_END) < 0 && errno != ESPIPE)
            error = errno;
    }
    if (error) {
        io_os_error(c, error, out, name.value());
        return;
    }
    ReturnSlot key(c);
    make_str(c, U"name", key);
    set_attribute(c, self, roots.root(key.value()), roots.root(a[1]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    error = replace(c, s, opened.fd, closefd, read, write, append,
                    status.st_blksize > 1 ? static_cast<std::size_t>(status.st_blksize) : 8192,
                    selected == 'x');
    opened.fd = -1;
    if (error)
        io_os_error(c, error, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
void close(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &state = *c.payload<FileIOObject>(self).state;
    bool already_closed;
    {
        NativeWait wait(c);
        std::lock_guard lock(state.mutex);
        already_closed = state.fd < 0;
    }
    if (already_closed) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    ReturnSlot flushed(c);
    try {
        io_call(c, self, U"flush", {}, flushed);
    } catch (...) {
        replace(c, state, -1);
        throw;
    }
    const int error = replace(c, *c.payload<FileIOObject>(self).state, -1);
    if (error)
        io_os_error(c, error, out);
    else if (flushed.tag() == ResultTag::Raised)
        out.raised(flushed.exception());
    else
        out.ok(c.builtin(CoreObject::None));
}
enum class Method { Readable, Writable, Seekable, Fileno, Isatty, Tell };
template <Method M> void method(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = *c.payload<FileIOObject>(self).state;
    std::int64_t result = 0;
    int error = 0;
    if (!operate(
            c, self, 0,
            [&](int fd) {
                if constexpr (M == Method::Readable || M == Method::Writable) {
                    std::lock_guard lock(s.mutex);
                    result = M == Method::Readable ? s.readable : s.writable;
                } else if constexpr (M == Method::Fileno)
                    result = fd;
                else if constexpr (M == Method::Isatty)
                    result = ::isatty(fd) == 1;
                else {
                    result = ::lseek(fd, 0, SEEK_CUR);
                    if constexpr (M == Method::Seekable)
                        result = result >= 0;
                    else if (result < 0)
                        error = errno;
                }
            },
            out))
        return;
    if (error)
        io_os_error(c, error, out);
    else if constexpr (M == Method::Fileno || M == Method::Tell)
        make_int(c, result, out);
    else
        out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
}
template <bool Truncate> void seek(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::int64_t offset = 0, whence = 0;
    const bool current = Truncate && a[1] == c.builtin(CoreObject::None);
    if (!current && !io_index(c, roots.root(a[1]), offset, out))
        return;
    if constexpr (!Truncate)
        if (!io_index(c, roots.root(a[2]), whence, out))
            return;
    if (whence < INT_MIN || whence > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"whence out of range", out);
        return;
    }
    int error = 0;
    std::int64_t result = -1;
    if (!operate(
            c, self, Truncate ? 2 : 0,
            [&](int fd) {
                if constexpr (Truncate) {
                    if (current) {
                        offset = ::lseek(fd, 0, SEEK_CUR);
                        if (offset < 0)
                            error = errno;
                    } else if (offset < 0)
                        error = EINVAL;
                    if (!error && ::ftruncate(fd, static_cast<off_t>(offset)) < 0)
                        error = errno;
                    if (error)
                        return;
                    else
                        result = offset;
                } else {
                    result = ::lseek(fd, static_cast<off_t>(offset), static_cast<int>(whence));
                    if (result < 0)
                        error = errno;
                }
            },
            out))
        return;
    if (error)
        io_os_error(c, error, out);
    else
        make_int(c, result, out);
}
template <bool All> void read(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::int64_t size = -1;
    if constexpr (!All)
        if (a[1] != c.builtin(CoreObject::None) && !io_index(c, roots.root(a[1]), size, out))
            return;
    std::string data;
    int error = 0;
    ssize_t n = 0;
    if (!operate(
            c, self, 1,
            [&](int fd) {
                if (size >= 0) {
                    if (static_cast<std::uint64_t>(size) > data.max_size())
                        throw std::bad_alloc();
                    data.resize(static_cast<std::size_t>(size));
                    do {
                        n = ::read(fd, data.data(), data.size());
                    } while (n < 0 && errno == EINTR);
                    if (n < 0)
                        error = errno;
                    else
                        data.resize(static_cast<std::size_t>(n));
                } else {
                    char chunk[8192];
                    for (;;) {
                        do {
                            n = ::read(fd, chunk, sizeof(chunk));
                        } while (n < 0 && errno == EINTR);
                        if (n <= 0) {
                            if (n < 0)
                                error = errno;
                            break;
                        }
                        data.append(chunk, static_cast<std::size_t>(n));
                    }
                }
            },
            out))
        return;
    if ((error == EAGAIN || error == EWOULDBLOCK) && data.empty())
        out.ok(c.builtin(CoreObject::None));
    else if (error && error != EAGAIN && error != EWOULDBLOCK)
        io_os_error(c, error, out);
    else
        make_bytes(c, std::move(data), out);
}
void write(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::string data;
    if (!buffer_bytes(c, roots.root(a[1]), data, out)) {
        if (out.tag() != ResultTag::Raised)
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    ssize_t n = -1;
    int error = 0;
    if (!operate(
            c, self, 2,
            [&](int fd) {
                sigset_t blocked, previous, pending;
                sigemptyset(&blocked);
                sigaddset(&blocked, SIGPIPE);
                const bool masked = pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;
                const bool was_pending =
                    masked && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
                do {
                    n = ::write(fd, data.data(), data.size());
                } while (n < 0 && errno == EINTR);
                if (n < 0)
                    error = errno;
                if (masked) {
                    if (error == EPIPE && !was_pending) {
                        const timespec immediate{0, 0};
                        while (sigtimedwait(&blocked, nullptr, &immediate) < 0 && errno == EINTR) {
                        }
                    }
                    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
                }
            },
            out))
        return;
    if (error == EAGAIN || error == EWOULDBLOCK)
        out.ok(c.builtin(CoreObject::None));
    else if (error)
        io_os_error(c, error, out);
    else
        make_int(c, n, out);
}
void readinto(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    WritableBuffer buffer(c);
    if (!writable_buffer(c, roots.root(a[1]), buffer, out))
        return;
    std::string data(buffer.length, '\0');
    ssize_t n = -1;
    int error = 0;
    if (!operate(
            c, self, 1,
            [&](int fd) {
                do {
                    n = ::read(fd, data.data(), data.size());
                } while (n < 0 && errno == EINTR);
                if (n < 0)
                    error = errno;
            },
            out))
        return;
    if (error == EAGAIN || error == EWOULDBLOCK) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    if (error) {
        io_os_error(c, error, out);
        return;
    }
    data.resize(static_cast<std::size_t>(n));
    write_buffer(c, buffer, data);
    make_int(c, n, out);
}
template <int Field> void field(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    auto &s = *c.payload<FileIOObject>(value).state;
    bool closed, closefd, read, write, append, created;
    std::size_t block;
    {
        NativeWait wait(c);
        std::lock_guard lock(s.mutex);
        closed = s.fd < 0;
        closefd = s.closefd;
        read = s.readable;
        write = s.writable;
        append = s.appending;
        block = s.blksize;
        created = s.created;
    }
    if constexpr (Field == 0)
        out.ok(c.builtin(closed ? CoreObject::True : CoreObject::False));
    if constexpr (Field == 1)
        out.ok(c.builtin(closefd ? CoreObject::True : CoreObject::False));
    if constexpr (Field == 2) {
        std::u32string mode = created ? U"xb" : append ? U"ab" : read ? U"rb" : U"wb";
        if (read && write)
            mode += U'+';
        make_str(c, std::move(mode), out);
    }
    if constexpr (Field == 3)
        make_int(c, static_cast<std::int64_t>(block), out);
}
void representation(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = *c.payload<FileIOObject>(self).state;
    int fd;
    bool closefd;
    {
        NativeWait wait(c);
        std::lock_guard lock(s.mutex);
        fd = s.fd;
        closefd = s.closefd;
    }
    if (fd < 0) {
        make_str(c, U"<_io.FileIO [closed]>", out);
        return;
    }
    if (!c.enter_repr(self)) {
        raise_error(c, CoreObject::RuntimeErrorType, U"reentrant call inside FileIO.__repr__", out);
        return;
    }
    struct Leave {
        Attachment &c;
        Value self;
        ~Leave()
        {
            c.leave_repr(self);
        }
    } leave{c, a[0]};
    ReturnSlot name(c), mode(c), value(c);
    make_str(c, U"name", name);
    get_attribute(c, self, roots.root(name.value()), value);
    std::u32string text = U"<_io.FileIO ";
    if (value.tag() == ResultTag::Raised) {
        if (!is_subtype(c, c.type_of(value.exception()),
                        c.builtin(CoreObject::AttributeErrorType))) {
            out.raised(value.exception());
            return;
        }
        auto number = std::to_string(fd);
        text += U"fd=" + std::u32string(number.begin(), number.end());
    } else {
        repr(c, roots.root(value.value()), name);
        if (name.tag() == ResultTag::Raised) {
            out.raised(name.exception());
            return;
        }
        text += U"name=" + c.payload<StrObject>(name.value()).value;
    }
    field<2>(c, a[0], mode);
    text += U" mode='" + c.payload<StrObject>(mode.value()).value + U"' closefd=";
    text += closefd ? U"True>" : U"False>";
    make_str(c, std::move(text), out);
}
} // namespace
void install_fileio(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto type = at(CoreObject::FileIOType);
    RootScope roots(c);
    ReturnSlot text(c), negative(c), wrapper(c);
    c.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"r");
    auto mode = roots.root(text.value());
    c.allocate_instance<IntObject>(at(CoreObject::IntType), negative, -1);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto create_sig = unary;
    create_sig.varargs = U"args";
    create_sig.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", create_sig, create);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__new__");
    name_store(c, dictionary, text.value(), wrapper.value());
    SignatureLayout init;
    init.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"file", ParameterKind::PositionalOrKeyword},
        {U"mode", ParameterKind::PositionalOrKeyword, mode.get(c)},
        {U"closefd", ParameterKind::PositionalOrKeyword, at(CoreObject::True).get(c)},
        {U"opener", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)}};
    install_native_attribute(c, core, type, U"__init__", init, initialize);
    auto sized = unary;
    sized.parameters.push_back({U"size", ParameterKind::PositionalOnly, negative.value()});
    auto binary = unary;
    binary.parameters.push_back({U"buffer", ParameterKind::PositionalOnly});
    auto seek_sig = binary;
    seek_sig.parameters[1].name = U"offset";
    seek_sig.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    auto trunc = unary;
    trunc.parameters.push_back(
        {U"size", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
    install_native_attribute(c, core, type, U"read", sized, read<false>);
    install_native_attribute(c, core, type, U"readall", unary, read<true>);
    install_native_attribute(c, core, type, U"readinto", binary, readinto);
    install_native_attribute(c, core, type, U"write", binary, write);
    install_native_attribute(c, core, type, U"seek", seek_sig, seek<false>);
    install_native_attribute(c, core, type, U"truncate", trunc, seek<true>);
    install_native_attribute(c, core, type, U"close", unary, close);
    install_native_attribute(c, core, type, U"__repr__", unary, representation);
    install_native_attribute(c, core, type, U"tell", unary, method<Method::Tell>);
    install_native_attribute(c, core, type, U"readable", unary, method<Method::Readable>);
    install_native_attribute(c, core, type, U"writable", unary, method<Method::Writable>);
    install_native_attribute(c, core, type, U"seekable", unary, method<Method::Seekable>);
    install_native_attribute(c, core, type, U"fileno", unary, method<Method::Fileno>);
    install_native_attribute(c, core, type, U"isatty", unary, method<Method::Isatty>);
    install_native_field(c, core, type, U"closed", field<0>);
    install_native_field(c, core, type, U"closefd", field<1>);
    install_native_field(c, core, type, U"mode", field<2>);
    install_native_field(c, core, type, U"_blksize", field<3>);
}
} // namespace detail
} // namespace cxxpython::core
