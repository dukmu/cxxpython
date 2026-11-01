// POSIX primitives, informed by CPython 3.12.14 Modules/posixmodule.c.
// Each advertised dir_fd/follow_symlinks capability is backed by its native call.
#include "internal/posixmodule.hpp"

#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"
#include "internal/compiler.hpp"
#include "internal/errors.hpp"
#include "internal/long.hpp"
#include "internal/signalmodule.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

namespace cxxpython::core::detail {
namespace {
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

bool descriptor(Attachment &c, Value value, int &fd, ReturnSlot &out)
{
    std::int64_t n;
    if (!index_as_int64(c, value, n, out))
        return false;
    if (n < INT_MIN || n > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"file descriptor out of range", out);
        return false;
    }
    fd = static_cast<int>(n);
    return true;
}

template <bool Set>
void inheritable_entry(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    int fd;
    if (!descriptor(c, args[0], fd, out))
        return;
    bool inheritable = false;
    if constexpr (Set) {
        RootScope roots(c);
        ReturnSlot converted(c);
        truth(c, roots.root(args[1]), converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        inheritable = converted.value() == c.builtin(CoreObject::True);
    }
    int flags, error;
    {
        NativeWait wait(c);
        flags = ::fcntl(fd, F_GETFD);
        error = flags < 0 ? errno : 0;
        if constexpr (Set) {
            if (!error &&
                ::fcntl(fd, F_SETFD, inheritable ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC) < 0)
                error = errno;
        }
    }
    if (error) {
        raise_os_error(c, error, out);
        return;
    }
    if constexpr (Set)
        out.ok(c.builtin(CoreObject::None));
    else
        out.ok(c.builtin((flags & FD_CLOEXEC) ? CoreObject::False : CoreObject::True));
}

void urandom_entry(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    std::int64_t size;
    if (!index_as_int64(c, args[0], size, out))
        return;
    if (size < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"negative argument not allowed", out);
        return;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t count;
        int error;
        {
            NativeWait wait(c);
            count = ::getrandom(bytes.data() + offset,
                                std::min(bytes.size() - offset, std::size_t{33554431}), 0);
            error = count < 0 ? errno : 0;
        }
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (error == EINTR) {
            if (check_signals(c, out))
                return;
        } else {
            raise_os_error(c, error ? error : EIO, out);
            return;
        }
    }
    make_bytes(c, std::move(bytes), out);
}

template <std::size_t I> void terminal_field(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<TupleObject>(self).items[I]);
}

void terminal_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::u32string text = U"os.terminal_size(";
    ReturnSlot item(c);
    for (std::size_t i = 0; i < 2; ++i) {
        text += i == 0 ? U"columns=" : U", lines=";
        repr(c, roots.root(c.payload<TupleObject>(self).items[i]), item);
        if (failed(item)) {
            out.raised(item.exception());
            return;
        }
        text += c.payload<StrObject>(item.value()).value;
    }
    make_str(c, text + U")", out);
}

void terminal_new(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(args[0]);
    if (type.get(c) != c.payload<NativeFunctionObject>(function).captures[0]) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid os.terminal_size type", out);
        return;
    }
    if (!c.has_layout<DictObject>(args[2])) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"os.terminal_size() takes a dict as second arg, if any", out);
        return;
    }
    ReturnSlot sequence(c);
    const std::array input{args[1]};
    call(c, roots.root(c.builtin(CoreObject::TupleType)), {input}, sequence);
    if (failed(sequence)) {
        out.raised(sequence.exception());
        return;
    }
    const auto &items = c.payload<TupleObject>(sequence.value()).items;
    if (items.size() != 2) {
        auto count = std::to_string(items.size());
        raise_error(c, CoreObject::TypeErrorType,
                    U"os.terminal_size() takes a 2-sequence (" +
                        std::u32string(count.begin(), count.end()) + U"-sequence given)",
                    out);
        return;
    }
    c.allocate_instance<TupleObject>(type, out, items);
}

void get_terminal_size(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    int fd;
    if (!descriptor(c, args[0], fd, out))
        return;
    winsize size{};
    if (ioctl(fd, TIOCGWINSZ, &size) != 0) {
        raise_os_error(c, errno, out);
        return;
    }
    RootScope roots(c);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    ReturnSlot item(c);
    make_int(c, size.ws_col, item);
    auto columns = roots.root(item.value());
    make_int(c, size.ws_row, item);
    std::vector<Value> values{columns.get(c), item.value()};
    c.allocate_instance<TupleObject>(type, out, std::move(values));
}
bool path_value(Attachment &c, Handle object, ReturnSlot &out)
{
    auto value = object.get(c);
    if (c.has_layout<StrObject>(value) || is_bytes(c, value)) {
        out.ok(value);
        return true;
    }
    RootScope roots(c);
    auto method = type_attribute(c, c.type_of(value), U"__fspath__");
    if (!method) {
        raise_error(c, CoreObject::TypeErrorType, U"expected str, bytes or os.PathLike object",
                    out);
        return false;
    }
    invoke_special(c, object, roots.root(*method), {{}, {}}, out);
    if (failed(out))
        return false;
    if (!c.has_layout<StrObject>(out.value()) && !is_bytes(c, out.value())) {
        raise_error(c, CoreObject::TypeErrorType, U"__fspath__ must return str or bytes", out);
        return false;
    }
    return true;
}
bool path(Attachment &c, Value value, std::string &result, ReturnSlot &out,
          ReturnSlot *filename = nullptr)
{
    RootScope roots(c);
    ReturnSlot converted(c);
    if (!path_value(c, roots.root(value), converted)) {
        out.raised(converted.exception());
        return false;
    }
    auto v = converted.value();
    if (filename)
        filename->ok(v);
    if (is_bytes(c, v))
        result = c.payload<BytesObject>(v).value;
    else {
        // Encode UTF-8 with POSIX surrogateescape for undecodable filename bytes.
        for (char32_t ch : c.payload<StrObject>(v).value) {
            if (ch >= 0xdc80 && ch <= 0xdcff)
                result.push_back(static_cast<char>(ch - 0xdc00));
            else if (ch >= 0xd800 && ch <= 0xdfff) {
                raise_error(c, CoreObject::ValueErrorType, U"invalid surrogate in path", out);
                return false;
            } else
                result += source_utf8(std::u32string(1, ch));
        }
    }
    if (result.find('\0') != std::string::npos) {
        raise_error(c, CoreObject::ValueErrorType, U"embedded null character", out);
        return false;
    }
    return true;
}
void fspath(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    path_value(c, roots.root(a[0]), out);
}
void cpu_count(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    long count;
    {
        NativeWait wait(c);
        count = ::sysconf(_SC_NPROCESSORS_ONLN);
    }
    // CPython's POSIX API reports an unavailable count as None, not OSError.
    if (count < 1)
        out.ok(c.builtin(CoreObject::None));
    else
        make_int(c, count, out);
}
enum class Op {
    Open,
    Close,
    Read,
    Write,
    Seek,
    Chdir,
    Fchdir,
    Mkdir,
    Rmdir,
    Unlink,
    Rename,
    Symlink,
    Pid,
    Ppid,
    Uid,
    Gid
};
template <Op op> void entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    ReturnSlot filename(c), filename2(c);
    int fd = 0, error = 0;
    std::int64_t n = 0, extra = 0;
    std::string p, q, data;
    ssize_t result = 0;
    bool chdir_fd = false;
    if constexpr (op == Op::Chdir) {
        chdir_fd = is_int(c, a[0]);
        if (chdir_fd) {
            if (!descriptor(c, a[0], fd, out))
                return;
        } else if (!path(c, a[0], p, out, &filename))
            return;
    }
    if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Rmdir || op == Op::Unlink ||
                  op == Op::Rename || op == Op::Symlink)
        if (!path(c, a[0], p, out, &filename))
            return;
    if constexpr (op == Op::Rename || op == Op::Symlink)
        if (!path(c, a[1], q, out, &filename2))
            return;
    if constexpr (op == Op::Symlink) {
        RootScope roots(c);
        ReturnSlot converted(c);
        truth(c, roots.root(a[2]), converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
    }
    if constexpr (op == Op::Close || op == Op::Read || op == Op::Write || op == Op::Seek ||
                  op == Op::Fchdir)
        if (!descriptor(c, a[0], fd, out))
            return;
    if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Read || op == Op::Seek)
        if (!index_as_int64(c, a[1], n, out))
            return;
    if constexpr (op == Op::Open || op == Op::Seek)
        if (!index_as_int64(c, a[2], extra, out))
            return;
    if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Seek) {
        if (n < INT_MIN || n > INT_MAX || extra < INT_MIN || extra > INT_MAX) {
            if constexpr (op != Op::Seek) {
                raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
                return;
            } else if (extra < INT_MIN || extra > INT_MAX) {
                raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
                return;
            }
        }
    }
    if constexpr (op == Op::Read) {
        if (n < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"negative buffersize in read", out);
            return;
        }
        data.resize(static_cast<std::size_t>(n));
    }
    if constexpr (op == Op::Write) {
        if (is_bytes(c, a[1]))
            data = c.payload<BytesObject>(a[1]).value;
        else if (is_bytearray(c, a[1])) {
            RootScope roots(c);
            data = bytearray_snapshot(c, roots.root(a[1]));
        } else {
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
            return;
        }
    }
    {
        NativeWait wait(c);
        if constexpr (op == Op::Open)
            result = ::open(p.c_str(), static_cast<int>(n) | O_CLOEXEC, static_cast<mode_t>(extra));
        if constexpr (op == Op::Close)
            result = ::close(fd);
        if constexpr (op == Op::Read)
            result = ::read(fd, data.data(), data.size());
        if constexpr (op == Op::Write) {
            sigset_t blocked, previous, pending;
            sigemptyset(&blocked);
            sigaddset(&blocked, SIGPIPE);
            const bool masked = pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;
            bool was_pending =
                masked && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
            result = ::write(fd, data.data(), data.size());
            if (result == -1)
                error = errno;
            if (masked) {
                if (error == EPIPE && !was_pending) {
                    const timespec immediate{0, 0};
                    while (sigtimedwait(&blocked, nullptr, &immediate) < 0 && errno == EINTR) {
                    }
                }
                pthread_sigmask(SIG_SETMASK, &previous, nullptr);
            }
        }
        if constexpr (op == Op::Seek)
            result = ::lseek(fd, static_cast<off_t>(n), static_cast<int>(extra));
        if constexpr (op == Op::Chdir)
            result = chdir_fd ? ::fchdir(fd) : ::chdir(p.c_str());
        if constexpr (op == Op::Fchdir)
            result = ::fchdir(fd);
        if constexpr (op == Op::Mkdir)
            result = ::mkdir(p.c_str(), static_cast<mode_t>(n));
        if constexpr (op == Op::Rmdir)
            result = ::rmdir(p.c_str());
        if constexpr (op == Op::Unlink)
            result = ::unlink(p.c_str());
        if constexpr (op == Op::Rename)
            result = ::rename(p.c_str(), q.c_str());
        if constexpr (op == Op::Symlink)
            result = ::symlink(p.c_str(), q.c_str());
        if constexpr (op == Op::Pid)
            result = ::getpid();
        if constexpr (op == Op::Ppid)
            result = ::getppid();
        if constexpr (op == Op::Uid)
            result = ::getuid();
        if constexpr (op == Op::Gid)
            result = ::getgid();
        if (result == -1 && !error)
            error = errno;
    }
    if (error) {
        if constexpr (op == Op::Rename || op == Op::Symlink)
            raise_os_error(c, error, out, filename.value(), filename2.value());
        else if constexpr (op == Op::Chdir) {
            if (chdir_fd)
                raise_os_error(c, error, out);
            else
                raise_os_error(c, error, out, filename.value());
        } else if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Rmdir ||
                             op == Op::Unlink)
            raise_os_error(c, error, out, filename.value());
        else
            raise_os_error(c, error, out);
        return;
    }
    if constexpr (op == Op::Read) {
        data.resize(static_cast<std::size_t>(result));
        make_bytes(c, std::move(data), out);
    } else if constexpr (op == Op::Close || op == Op::Chdir || op == Op::Fchdir ||
                         op == Op::Mkdir || op == Op::Rmdir || op == Op::Unlink ||
                         op == Op::Rename || op == Op::Symlink)
        out.ok(c.builtin(CoreObject::None));
    else
        make_int(c, result, out);
}

// UTF-8 filesystem decoding with surrogateescape, independent of source decoding.
std::u32string fs_decode(const std::string &s)
{
    std::u32string result;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char first = static_cast<unsigned char>(s[i]);
        unsigned length = first < 0x80                     ? 1
                          : first >= 0xc2 && first <= 0xdf ? 2
                          : first >= 0xe0 && first <= 0xef ? 3
                          : first >= 0xf0 && first <= 0xf4 ? 4
                                                           : 0;
        char32_t point = length == 1 ? first : length ? first & (0x7f >> length) : 0;
        bool valid = length && i + length <= s.size();
        for (unsigned j = 1; valid && j < length; ++j) {
            unsigned char ch = static_cast<unsigned char>(s[i + j]);
            if ((ch & 0xc0) != 0x80)
                valid = false;
            else
                point = (point << 6) | (ch & 63);
        }
        if (valid && ((length == 3 && point < 0x800) || (length == 4 && point < 0x10000) ||
                      (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff))
            valid = false;
        if (valid) {
            result.push_back(point);
            i += length;
        } else {
            result.push_back(0xdc00 + first);
            ++i;
        }
    }
    return result;
}
void cwd(Attachment &c, Value function, std::span<const Value>, ReturnSlot &out)
{
    bool bytes = c.payload<NativeFunctionObject>(function).name == "getcwdb";
    std::string result;
    int error = 0;
    {
        NativeWait wait(c);
        std::unique_ptr<char, void (*)(void *)> buffer(::getcwd(nullptr, 0), std::free);
        if (!buffer)
            error = errno;
        else {
            result = buffer.get();
        }
    }
    if (error)
        raise_os_error(c, error, out);
    else if (bytes)
        make_bytes(c, std::move(result), out);
    else
        make_str(c, fs_decode(result), out);
}
void listdir(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot converted(c);
    bool bytes = false;
    std::string p = ".";
    if (a[0] != c.builtin(CoreObject::None)) {
        if (!path_value(c, roots.root(a[0]), converted)) {
            out.raised(converted.exception());
            return;
        }
        bytes = is_bytes(c, converted.value());
        p.clear();
        if (!path(c, converted.value(), p, out))
            return;
    }
    std::vector<std::string> names;
    int error = 0;
    {
        NativeWait wait(c);
        std::unique_ptr<DIR, int (*)(DIR *)> dir(::opendir(p.c_str()), ::closedir);
        if (!dir)
            error = errno;
        else {
            for (;;) {
                errno = 0;
                auto *item = ::readdir(dir.get());
                if (!item) {
                    error = errno;
                    break;
                }
                if (std::strcmp(item->d_name, ".") && std::strcmp(item->d_name, ".."))
                    names.emplace_back(item->d_name);
            }
        }
    }
    if (error) {
        raise_os_error(c, error, out,
                       a[0] == c.builtin(CoreObject::None) ? std::optional<Value>{}
                                                           : converted.value());
        return;
    }
    std::vector<Value> items;
    ReturnSlot item(c);
    for (auto &name : names) {
        if (bytes)
            make_bytes(c, std::move(name), item);
        else
            make_str(c, fs_decode(name), item);
        items.push_back(roots.root(item.value()).get(c));
    }
    make_list(c, items, out);
}
void readlink_entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot converted(c);
    if (!path_value(c, roots.root(a[0]), converted)) {
        out.raised(converted.exception());
        return;
    }
    bool bytes = is_bytes(c, converted.value());
    std::string p, result;
    if (!path(c, converted.value(), p, out))
        return;
    int error = 0;
    {
        NativeWait wait(c);
        std::size_t size = 256;
        for (;;) {
            result.resize(size);
            auto n = ::readlink(p.c_str(), result.data(), size);
            if (n < 0) {
                error = errno;
                break;
            }
            if (static_cast<std::size_t>(n) < size) {
                result.resize(n);
                break;
            }
            size *= 2;
        }
    }
    if (error)
        raise_os_error(c, error, out, converted.value());
    else if (bytes)
        make_bytes(c, std::move(result), out);
    else
        make_str(c, fs_decode(result), out);
}

std::mutex environment_mutex;
template <bool Set>
void environment_change(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    std::string key, value;
    if (!path(c, a[0], key, out))
        return;
    if constexpr (Set)
        if (!path(c, a[1], value, out))
            return;
    if (key.empty() || key.find('=') != std::string::npos) {
        raise_error(c, CoreObject::ValueErrorType, U"illegal environment variable name", out);
        return;
    }
    int error = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(environment_mutex);
        int result;
        if constexpr (Set)
            result = ::setenv(key.c_str(), value.c_str(), 1);
        else
            result = ::unsetenv(key.c_str());
        if (result < 0)
            error = errno;
    }
    if (error)
        raise_os_error(c, error, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
bool environment_snapshot(Attachment &c, Handle dict, ReturnSlot &out)
{
    std::vector<std::string> entries;
    {
        NativeWait wait(c);
        std::lock_guard lock(environment_mutex);
        for (char **p = environ; p && *p; ++p)
            entries.emplace_back(*p);
    }
    RootScope roots(c);
    ReturnSlot key(c), value(c);
    for (const auto &entry : entries) {
        auto split = entry.find('=');
        if (split == std::string::npos)
            continue;
        make_bytes(c, entry.substr(0, split), key);
        make_bytes(c, entry.substr(split + 1), value);
        dict_store(c, dict, roots.root(key.value()), roots.root(value.value()), out);
        if (failed(out))
            return false;
    }
    return true;
}

template <std::size_t I> void stat_field(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<TupleObject>(self).items[I]);
}
const char32_t *stat_extra_names[] = {U"st_atime",    U"st_mtime",    U"st_ctime",
                                      U"st_atime_ns", U"st_mtime_ns", U"st_ctime_ns",
                                      U"st_blksize",  U"st_blocks",   U"st_rdev"};
std::size_t stat_slot(Attachment &c, Value object, const char32_t *name)
{
    const auto &names = c.payload<TypeObject>(c.type_of(object)).slot_names;
    auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end())
        throw std::logic_error("stat_result slot missing");
    return static_cast<std::size_t>(found - names.begin());
}
void stat_extra(Attachment &c, Handle object, std::size_t index, Value value)
{
    auto self = object.get(c);
    c.store(self, c.instance_slot(self, stat_slot(c, self, stat_extra_names[index])), value);
}
template <std::size_t I> void stat_extra_field(Attachment &c, Value self, ReturnSlot &out)
{
    c.load(self, c.instance_slot(self, stat_slot(c, self, stat_extra_names[I])), out);
}
void stat_new(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot iterator(c), item(c);
    get_iter(c, roots.root(a[1]), iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    auto iter = roots.root(iterator.value());
    std::vector<Value> items;
    for (;;) {
        auto status = iter_next(c, iter, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        items.push_back(roots.root(item.value()).get(c));
        if (items.size() > 19)
            break;
    }
    if (items.size() < 10 || items.size() > 19) {
        raise_error(c, CoreObject::TypeErrorType, U"stat_result requires 10 to 19 items", out);
        return;
    }
    auto extras = items;
    items.erase(items.begin() + 10, items.end());
    c.allocate_instance<TupleObject>(roots.root(a[0]), out, std::move(items));
    auto object = roots.root(out.value());
    for (std::size_t i = 0; i < 9; ++i) {
        Value value = i + 10 < extras.size() ? extras[i + 10]
                      : i < 3                ? extras[i + 7]
                                             : c.builtin(CoreObject::None);
        stat_extra(c, object, i, value);
    }
    out.ok(object.get(c));
}
void access_entry(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string target;
    int directory = AT_FDCWD;
    std::int64_t numeric_mode;
    if (!path(c, args[0], target, out) || !index_as_int64(c, args[1], numeric_mode, out))
        return;
    if (numeric_mode < INT_MIN || numeric_mode > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C long",
                    out);
        return;
    }
    const auto mode = static_cast<int>(numeric_mode);
    if (args[2] != c.builtin(CoreObject::None) && !descriptor(c, args[2], directory, out))
        return;
    ReturnSlot converted(c);
    truth(c, roots.root(args[3]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    int flags = converted.value() == c.builtin(CoreObject::True) ? AT_EACCESS : 0;
    truth(c, roots.root(args[4]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    if (converted.value() == c.builtin(CoreObject::False))
        flags |= AT_SYMLINK_NOFOLLOW;
    bool allowed;
    {
        NativeWait wait(c);
        allowed = faccessat(directory, target.c_str(), mode, flags) == 0;
    }
    out.ok(c.builtin(allowed ? CoreObject::True : CoreObject::False));
}

void make_stat_result(Attachment &, Handle, const struct stat &, ReturnSlot &);

void stat_entry(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot filename(c);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    const auto &name = c.payload<NativeFunctionObject>(function).name;
    bool by_fd = name == "fstat" || (name == "stat" && is_int(c, a[0]));
    bool nofollow = name == "lstat";
    std::string p;
    int fd = -1;
    int dir_fd = AT_FDCWD;
    if (name != "fstat" && a[1] != c.builtin(CoreObject::None)) {
        if (!descriptor(c, a[1], dir_fd, out))
            return;
        if (by_fd) {
            raise_error(c, CoreObject::ValueErrorType,
                        U"stat: can't specify dir_fd without matching path", out);
            return;
        }
    }
    if (name == "stat") {
        ReturnSlot follows(c);
        truth(c, roots.root(a[2]), follows);
        if (failed(follows)) {
            out.raised(follows.exception());
            return;
        }
        nofollow = follows.value() == c.builtin(CoreObject::False);
    }
    if (by_fd) {
        if (!descriptor(c, a[0], fd, out))
            return;
    } else if (!path(c, a[0], p, out, &filename))
        return;
    struct stat st {};
    int error = 0;
    {
        NativeWait wait(c);
        int result = by_fd ? ::fstat(fd, &st)
                           : ::fstatat(dir_fd, p.c_str(), &st, nofollow ? AT_SYMLINK_NOFOLLOW : 0);
        if (result < 0)
            error = errno;
    }
    if (error) {
        raise_os_error(c, error, out, by_fd ? std::optional<Value>{} : filename.value());
        return;
    }
    make_stat_result(c, type, st, out);
}

void make_stat_result(Attachment &c, Handle type, const struct stat &st, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Value> items;
    ReturnSlot temp(c), result(c);
#if defined(__APPLE__)
    const timespec times[] = {st.st_atimespec, st.st_mtimespec, st.st_ctimespec};
#else
    const timespec times[] = {st.st_atim, st.st_mtim, st.st_ctim};
#endif
    const std::int64_t values[] = {
        static_cast<std::int64_t>(st.st_mode),      static_cast<std::int64_t>(st.st_ino),
        static_cast<std::int64_t>(st.st_dev),       static_cast<std::int64_t>(st.st_nlink),
        static_cast<std::int64_t>(st.st_uid),       static_cast<std::int64_t>(st.st_gid),
        static_cast<std::int64_t>(st.st_size),      static_cast<std::int64_t>(times[0].tv_sec),
        static_cast<std::int64_t>(times[1].tv_sec), static_cast<std::int64_t>(times[2].tv_sec)};
    for (auto value : values) {
        make_int(c, value, temp);
        items.push_back(roots.root(temp.value()).get(c));
    }
    c.allocate_instance<TupleObject>(type, result, std::move(items));
    auto object = roots.root(result.value());
    for (int i = 0; i < 3; ++i) {
        make_float(c, static_cast<double>(times[i].tv_sec) + times[i].tv_nsec * 1e-9, temp);
        stat_extra(c, object, i, temp.value());
        publish_integer(c, Integer(times[i].tv_sec) * 1000000000 + times[i].tv_nsec, temp);
        stat_extra(c, object, i + 3, temp.value());
    }
    const std::int64_t extra[] = {static_cast<std::int64_t>(st.st_blksize),
                                  static_cast<std::int64_t>(st.st_blocks),
                                  static_cast<std::int64_t>(st.st_rdev)};
    for (int i = 0; i < 3; ++i) {
        make_int(c, extra[i], temp);
        stat_extra(c, object, i + 6, temp.value());
    }
    out.ok(object.get(c));
}
// DirEntry behavior is adapted from CPython 3.12.14 Modules/posixmodule.c.
// PSF license: doc/licenses/python-3.12.14.txt. Unlike its live DIR iterator,
// this base slice closes the directory in NativeWait before publishing names.
struct ScanName {
    std::string name;
    ino_t inode;
    unsigned char kind;
};
struct ScanSnapshot {
    std::string directory;
    std::vector<ScanName> names;
    bool bytes = false;
    std::size_t storage_bytes = 0;
    std::size_t external_bytes() const noexcept
    {
        return storage_bytes;
    }
};
struct ScandirObject {
    ObjectHeader header;
    const Value entry_type, stat_type;
    const ScanSnapshot snapshot;
    std::atomic<std::size_t> position{0};
    ScandirObject(InitialType type, Value entry, Value stat, ScanSnapshot input)
        : header(type), entry_type(entry), stat_type(stat), snapshot(std::move(input))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(entry_type);
        tracer.visit(stat_type);
    }
    std::size_t external_bytes() const noexcept
    {
        return snapshot.external_bytes();
    }
};
struct DirEntryObject {
    ObjectHeader header;
    const Value name, path, stat_type;
    const std::string native_path;
    const ino_t inode;
    const unsigned char kind;
    AtomicGcEdge stat, lstat;
    DirEntryObject(InitialType type, Value entry_name, Value entry_path, Value stat_class,
                   std::string filename, ino_t number, unsigned char hint)
        : header(type), name(entry_name), path(entry_path), stat_type(stat_class),
          native_path(std::move(filename)), inode(number), kind(hint)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
        tracer.visit(path);
        tracer.visit(stat_type);
        tracer.visit(stat);
        tracer.visit(lstat);
    }
    std::size_t external_bytes() const noexcept
    {
        return native_path.capacity() + 1;
    }
};
template <class T> bool scan_receiver(Attachment &c, Value self, ReturnSlot &out)
{
    if (c.has_layout<T>(self))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"invalid directory iterator or entry receiver", out);
    return false;
}
void scan_new(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType,
                U"cannot create directory iterator or entry instances", out);
}
void scan_iter(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (scan_receiver<ScandirObject>(c, args[0], out))
        out.ok(args[0]);
}
void scan_close(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!scan_receiver<ScandirObject>(c, args[0], out))
        return;
    auto &self = c.payload<ScandirObject>(args[0]);
    self.position.store(self.snapshot.names.size(), std::memory_order_relaxed);
    out.ok(c.builtin(CoreObject::None));
}
void scan_next(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!scan_receiver<ScandirObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto owner = roots.root(args[0]);
    auto &self = c.payload<ScandirObject>(owner);
    auto position = self.position.load(std::memory_order_relaxed);
    for (;;) {
        if (position == self.snapshot.names.size()) {
            stop_iteration(c, out);
            return;
        }
        if (self.position.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
            break;
    }
    const auto &entry = self.snapshot.names[position];
    auto filename = self.snapshot.directory;
    if (!filename.empty() && filename.back() != '/')
        filename += '/';
    filename += entry.name;
    ReturnSlot name(c), path(c);
    if (self.snapshot.bytes) {
        make_bytes(c, entry.name, name);
        make_bytes(c, filename, path);
    } else {
        make_str(c, fs_decode(entry.name), name);
        make_str(c, fs_decode(filename), path);
    }
    c.allocate_instance<DirEntryObject>(roots.root(self.entry_type), out, name.value(),
                                        path.value(), self.stat_type, std::move(filename),
                                        entry.inode, entry.kind);
}
void scan_entry(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot converted(c);
    ScanSnapshot snapshot;
    snapshot.directory = ".";
    if (args[0] != c.builtin(CoreObject::None)) {
        if (is_int(c, args[0])) {
            raise_error(c, CoreObject::NotImplementedErrorType,
                        U"scandir: file descriptor paths are not supported yet", out);
            return;
        }
        snapshot.directory.clear();
        if (!path(c, args[0], snapshot.directory, out, &converted))
            return;
        snapshot.bytes = is_bytes(c, converted.value());
    }
    int error = 0;
    {
        NativeWait wait(c);
        std::unique_ptr<DIR, int (*)(DIR *)> directory(::opendir(snapshot.directory.c_str()),
                                                       ::closedir);
        if (!directory)
            error = errno;
        else {
            for (;;) {
                errno = 0;
                const auto *entry = ::readdir(directory.get());
                if (!entry) {
                    error = errno;
                    break;
                }
                if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
                    continue;
#ifdef _DIRENT_HAVE_D_TYPE
                const auto kind = entry->d_type;
#else
                const unsigned char kind = DT_UNKNOWN;
#endif
                snapshot.names.push_back({entry->d_name, entry->d_ino, kind});
            }
        }
        snapshot.storage_bytes =
            snapshot.directory.capacity() + 1 + snapshot.names.capacity() * sizeof(ScanName);
        for (const auto &name : snapshot.names)
            snapshot.storage_bytes += name.name.capacity() + 1;
    }
    if (error) {
        raise_os_error(c, error, out,
                       args[0] == c.builtin(CoreObject::None) ? std::optional<Value>{}
                                                              : converted.value());
        return;
    }
    const auto &types = c.payload<NativeFunctionObject>(function).captures;
    c.allocate_instance<ScandirObject>(roots.root(types[0]), out, types[1], types[2],
                                       std::move(snapshot));
}
void direntry_name(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<DirEntryObject>(self).name);
}
void direntry_path(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<DirEntryObject>(self).path);
}
void direntry_fspath(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (scan_receiver<DirEntryObject>(c, args[0], out))
        direntry_path(c, args[0], out);
}
void direntry_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!scan_receiver<DirEntryObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    repr(c, roots.root(c.payload<DirEntryObject>(self).name), out);
    if (!failed(out))
        make_str(c, U"<DirEntry " + c.payload<StrObject>(out.value()).value + U">", out);
}
void direntry_inode(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (scan_receiver<DirEntryObject>(c, args[0], out))
        publish_integer(c, Integer(c.payload<DirEntryObject>(args[0]).inode), out);
}
void entry_stat(Attachment &c, Handle object, bool follow, ReturnSlot &out)
{
    auto &self = c.payload<DirEntryObject>(object);
    auto &cache = follow ? self.stat : self.lstat;
    c.load(object.get(c), cache, out);
    if (out.tag() == ResultTag::Ok)
        return;
    RootScope roots(c);
    bool symlink = self.kind == DT_LNK;
    if (follow && self.kind == DT_UNKNOWN) {
        entry_stat(c, object, false, out);
        if (failed(out))
            return;
        std::int64_t mode;
        int_fits_signed(c, c.payload<TupleObject>(out.value()).items[0], mode);
        symlink = S_ISLNK(mode);
    }
    if (follow && !symlink) {
        entry_stat(c, object, false, out);
        if (failed(out))
            return;
    } else {
        struct stat status {};
        int error = 0;
        const auto filename = self.native_path;
        {
            NativeWait wait(c);
            if ((follow ? ::stat(filename.c_str(), &status) : ::lstat(filename.c_str(), &status)) <
                0)
                error = errno;
        }
        if (error) {
            raise_os_error(c, error, out, self.path);
            return;
        }
        make_stat_result(c, roots.root(self.stat_type), status, out);
        if (failed(out))
            return;
    }
    // No Python callback or blocking operation is inside cache publication.
    // Concurrent misses return the same first-published, precisely traced record.
    if (!c.compare_exchange(object.get(c), cache, {}, out.value()))
        c.load(object.get(c), cache, out);
}
bool entry_follow(Attachment &c, Value value, bool &follow, ReturnSlot &out)
{
    RootScope roots(c);
    truth(c, roots.root(value), out);
    if (failed(out))
        return false;
    follow = out.value() == c.builtin(CoreObject::True);
    return true;
}
void direntry_stat(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!scan_receiver<DirEntryObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    bool follow;
    if (entry_follow(c, args[1], follow, out))
        entry_stat(c, self, follow, out);
}
template <unsigned short Mode>
void direntry_test(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!scan_receiver<DirEntryObject>(c, args[0], out))
        return;
    RootScope roots(c);
    auto object = roots.root(args[0]);
    auto &self = c.payload<DirEntryObject>(object);
    bool follow = false;
    if constexpr (Mode != S_IFLNK)
        if (!entry_follow(c, args[1], follow, out))
            return;
    bool result;
    if (self.kind != DT_UNKNOWN && !(follow && self.kind == DT_LNK)) {
        result = self.kind == (Mode == S_IFDIR ? DT_DIR : Mode == S_IFREG ? DT_REG : DT_LNK);
    } else {
        entry_stat(c, object, follow, out);
        if (failed(out)) {
            if (is_subtype(c, c.type_of(out.exception()),
                           c.builtin(CoreObject::FileNotFoundErrorType)))
                out.ok(c.builtin(CoreObject::False));
            return;
        }
        std::int64_t mode;
        int_fits_signed(c, c.payload<TupleObject>(out.value()).items[0], mode);
        result = (mode & S_IFMT) == Mode;
    }
    out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
}
void install_scandir(Attachment &c, Handle module_dictionary, Handle stat_type, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    ReturnSlot temporary(c), name(c);
    auto signature = [](std::initializer_list<Parameter> parameters) {
        SignatureLayout result;
        result.parameters = parameters;
        return result;
    };
    const auto only = ParameterKind::PositionalOnly;
    const auto unary = signature({{U"self", only}});
    auto create = [&](const char32_t *type_name, LayoutDescriptor layout) {
        TypeSpec spec{type_name};
        spec.module = U"posix";
        spec.slots = std::vector<std::u32string>{};
        spec.immutable = true;
        spec.subclassable = false;
        create_native_type(c, NativeTypeSpec{spec, layout}, temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return std::optional<Handle>{};
        }
        auto type = roots.root(temporary.value());
        auto constructor = signature({{U"cls", only}});
        constructor.varargs = U"args";
        constructor.varkwargs = U"kwargs";
        install_native_attribute(c, core, type, U"__new__", constructor, scan_new);
        auto dictionary = roots.root(*TypeView(c, type).dictionary());
        auto function = roots.root(*name_lookup(c, dictionary.get(c), U"__new__"));
        c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                                temporary, function.get(c));
        make_str(c, U"__new__", name);
        name_store(c, dictionary.get(c), name.value(), temporary.value());
        return std::optional<Handle>{type};
    };
    auto entry_type = create(U"DirEntry", LayoutDescriptor::of<DirEntryObject>());
    if (!entry_type)
        return;
    auto iterator_type = create(U"ScandirIterator", LayoutDescriptor::of<ScandirObject>());
    if (!iterator_type)
        return;
    install_native_field(c, core, *entry_type, U"name", direntry_name);
    install_native_field(c, core, *entry_type, U"path", direntry_path);
    install_native_attribute(c, core, *entry_type, U"__fspath__", unary, direntry_fspath);
    install_native_attribute(c, core, *entry_type, U"__repr__", unary, direntry_repr);
    install_native_attribute(c, core, *entry_type, U"inode", unary, direntry_inode);
    install_native_attribute(c, core, *entry_type, U"is_symlink", unary, direntry_test<S_IFLNK>);
    auto follow = unary;
    follow.parameters.push_back(
        {U"follow_symlinks", ParameterKind::KeywordOnly, c.builtin(CoreObject::True)});
    install_native_attribute(c, core, *entry_type, U"stat", follow, direntry_stat);
    install_native_attribute(c, core, *entry_type, U"is_dir", follow, direntry_test<S_IFDIR>);
    install_native_attribute(c, core, *entry_type, U"is_file", follow, direntry_test<S_IFREG>);
    install_native_attribute(c, core, *iterator_type, U"__iter__", unary, scan_iter);
    install_native_attribute(c, core, *iterator_type, U"__next__", unary, scan_next);
    install_native_attribute(c, core, *iterator_type, U"close", unary, scan_close);
    install_native_attribute(c, core, *iterator_type, U"__enter__", unary, scan_iter);
    install_native_attribute(
        c, core, *iterator_type, U"__exit__",
        signature(
            {{U"self", only}, {U"exc_type", only}, {U"exc_value", only}, {U"traceback", only}}),
        scan_close);
    make_str(c, U"DirEntry", name);
    dict_store(c, module_dictionary, roots.root(name.value()), *entry_type, out);
    if (failed(out))
        return;
    const Value captures[] = {iterator_type->get(c), entry_type->get(c), stat_type.get(c)};
    make_native_function(
        c, "scandir",
        signature({{U"path", ParameterKind::PositionalOrKeyword, c.builtin(CoreObject::None)}}),
        scan_entry, captures, temporary, NativeFunctionBinding::Function);
    make_str(c, U"scandir", name);
    dict_store(c, module_dictionary, roots.root(name.value()), roots.root(temporary.value()), out);
}
} // namespace

std::mutex &process_environment_lock()
{
    return environment_mutex;
}

bool filesystem_path(Attachment &c, Value value, std::string &result, ReturnSlot &out,
                     ReturnSlot &filename)
{
    return path(c, value, result, out, &filename);
}

void initialize_posix(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    ReturnSlot name(c), value(c), mode(c);
    make_int(c, 0777, mode);
    auto mode_default = roots.root(mode.value());
    auto install = [&](const char *key, SignatureLayout sig, NativeEntry function,
                       std::vector<Value> captures = {}) {
        make_native_function(c, key, std::move(sig), function, captures, value,
                             NativeFunctionBinding::Function);
        make_str(c, std::u32string(key, key + std::strlen(key)), name);
        dict_store(c, dict, roots.root(name.value()), roots.root(value.value()), out);
        return !failed(out);
    };
    auto signature = [](std::initializer_list<Parameter> params) {
        SignatureLayout s;
        s.parameters = params;
        return s;
    };
    const auto only = ParameterKind::PositionalOnly;
    const auto patharg = Parameter{U"path", only};
    const auto fdarg = Parameter{U"fd", only};
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    Handle base[] = {roots.root(c.builtin(CoreObject::TupleType))};
    TypeSpec type_spec{U"stat_result", base};
    type_spec.module = U"posix";
    type_spec.immutable = true;
    type_spec.subclassable = false;
    type_spec.slots =
        std::vector<std::u32string>(std::begin(stat_extra_names), std::end(stat_extra_names));
    create_native_type(c, NativeTypeSpec{type_spec, LayoutDescriptor::of<TupleObject>()}, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto stat_type = roots.root(value.value());
    const char32_t *fields[] = {U"st_mode", U"st_ino", U"st_dev", U"st_nlink",
                                U"st_uid",  U"st_gid", U"st_size"};
    NativeGetter getters[] = {stat_field<0>, stat_field<1>, stat_field<2>, stat_field<3>,
                              stat_field<4>, stat_field<5>, stat_field<6>};
    for (std::size_t i = 0; i < 7; ++i)
        install_native_field(c, core, stat_type, fields[i], getters[i]);
    NativeGetter extra_getters[] = {stat_extra_field<0>, stat_extra_field<1>, stat_extra_field<2>,
                                    stat_extra_field<3>, stat_extra_field<4>, stat_extra_field<5>,
                                    stat_extra_field<6>, stat_extra_field<7>, stat_extra_field<8>};
    for (std::size_t i = 0; i < 9; ++i)
        install_native_field(c, core, stat_type, stat_extra_names[i], extra_getters[i]);
    install_native_attribute(c, core, stat_type, U"__new__",
                             signature({{U"cls", only}, {U"sequence", only}}), stat_new);
    auto type_dict = *TypeView(c, stat_type).dictionary();
    auto constructor = *name_lookup(c, type_dict, U"__new__");
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            value, constructor);
    make_str(c, U"__new__", name);
    name_store(c, type_dict, name.value(), value.value());
    make_str(c, U"stat_result", name);
    dict_store(c, dict, roots.root(name.value()), stat_type, out);
    if (failed(out))
        return;
    const std::vector<Value> stat_capture{stat_type.get(c)};
    install_scandir(c, dict, stat_type, out);
    if (failed(out))
        return;
    TypeSpec terminal_spec{U"terminal_size", base};
    terminal_spec.module = U"os";
    terminal_spec.slots = std::vector<std::u32string>{};
    terminal_spec.immutable = true;
    terminal_spec.subclassable = false;
    create_native_type(c, NativeTypeSpec{terminal_spec, LayoutDescriptor::of<TupleObject>()},
                       value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto terminal_type = roots.root(value.value());
    install_native_field(c, core, terminal_type, U"columns", terminal_field<0>);
    install_native_field(c, core, terminal_type, U"lines", terminal_field<1>);
    install_native_attribute(c, core, terminal_type, U"__repr__", signature({{U"self", only}}),
                             terminal_repr);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), value);
    auto defaults = roots.root(value.value());
    install_native_attribute(
        c, core, terminal_type, U"__new__",
        signature({{U"cls", only},
                   {U"sequence"},
                   {U"dict", ParameterKind::PositionalOrKeyword, defaults.get(c)}}),
        terminal_new, {terminal_type.get(c)});
    auto terminal_dict = roots.root(*TypeView(c, terminal_type).dictionary());
    auto terminal_constructor = roots.root(*name_lookup(c, terminal_dict.get(c), U"__new__"));
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            value, terminal_constructor.get(c));
    make_str(c, U"__new__", name);
    name_store(c, terminal_dict.get(c), name.value(), value.value());
    for (const auto &[field, number] :
         {std::pair{U"n_fields", 2}, {U"n_sequence_fields", 2}, {U"n_unnamed_fields", 0}}) {
        make_int(c, number, value);
        auto number_handle = roots.root(value.value());
        make_str(c, field, name);
        name_store(c, terminal_dict.get(c), name.value(), number_handle.get(c));
    }
    make_str(c, U"terminal_size", name);
    dict_store(c, dict, roots.root(name.value()), terminal_type, out);
    if (failed(out))
        return;
    make_int(c, STDOUT_FILENO, value);
    auto stdout_fd = roots.root(value.value());
    if (!install("get_terminal_size", signature({{U"fd", only, stdout_fd.get(c)}}),
                 get_terminal_size, {terminal_type.get(c)}))
        return;
    if (!install("urandom", signature({{U"size", only}}), urandom_entry) ||
        !install("fspath", signature({patharg}), fspath) ||
        !install(
            "access",
            signature(
                {{U"path", ParameterKind::PositionalOrKeyword},
                 {U"mode", ParameterKind::PositionalOrKeyword},
                 {U"dir_fd", ParameterKind::KeywordOnly, c.builtin(CoreObject::None)},
                 {U"effective_ids", ParameterKind::KeywordOnly, c.builtin(CoreObject::False)},
                 {U"follow_symlinks", ParameterKind::KeywordOnly, c.builtin(CoreObject::True)}}),
            access_entry) ||
        !install("stat",
                 signature({patharg,
                            {U"dir_fd", ParameterKind::KeywordOnly, c.builtin(CoreObject::None)},
                            {U"follow_symlinks", ParameterKind::KeywordOnly,
                             c.builtin(CoreObject::True)}}),
                 stat_entry, stat_capture) ||
        !install("lstat",
                 signature({patharg,
                            {U"dir_fd", ParameterKind::KeywordOnly, c.builtin(CoreObject::None)}}),
                 stat_entry, stat_capture) ||
        !install("fstat", signature({fdarg}), stat_entry, stat_capture) ||
        !install("getcwd", {}, cwd) || !install("getcwdb", {}, cwd) ||
        !install("listdir", signature({{U"path", only, c.builtin(CoreObject::None)}}), listdir) ||
        !install("readlink", signature({patharg}), readlink_entry) ||
        !install("putenv", signature({{U"name", only}, {U"value", only}}),
                 environment_change<true>) ||
        !install("unsetenv", signature({{U"name", only}}), environment_change<false>) ||
        !install("open",
                 signature({patharg, {U"flags", only}, {U"mode", only, mode_default.get(c)}}),
                 entry<Op::Open>) ||
        !install("close", signature({fdarg}), entry<Op::Close>) ||
        !install("get_inheritable", signature({fdarg}), inheritable_entry<false>) ||
        !install("set_inheritable", signature({fdarg, {U"inheritable", only}}),
                 inheritable_entry<true>) ||
        !install("chdir", signature({patharg}), entry<Op::Chdir>) ||
        !install("fchdir", signature({fdarg}), entry<Op::Fchdir>) ||
        !install("read", signature({fdarg, {U"length", only}}), entry<Op::Read>) ||
        !install("write", signature({fdarg, {U"data", only}}), entry<Op::Write>) ||
        !install("lseek", signature({fdarg, {U"position", only}, {U"how", only}}),
                 entry<Op::Seek>) ||
        !install("mkdir",
                 signature(
                     {patharg, {U"mode", ParameterKind::PositionalOrKeyword, mode_default.get(c)}}),
                 entry<Op::Mkdir>) ||
        !install("rmdir", signature({patharg}), entry<Op::Rmdir>) ||
        !install("unlink", signature({patharg}), entry<Op::Unlink>) ||
        !install("remove", signature({patharg}), entry<Op::Unlink>) ||
        !install("rename", signature({{U"src", only}, {U"dst", only}}), entry<Op::Rename>) ||
        !install("replace", signature({{U"src", only}, {U"dst", only}}), entry<Op::Rename>) ||
        !install("symlink",
                 signature({{U"src", only},
                            {U"dst", only},
                            {U"target_is_directory", ParameterKind::PositionalOrKeyword,
                             c.builtin(CoreObject::False)}}),
                 entry<Op::Symlink>) ||
        !install("cpu_count", {}, cpu_count) || !install("getpid", {}, entry<Op::Pid>) ||
        !install("getppid", {}, entry<Op::Ppid>) || !install("getuid", {}, entry<Op::Uid>) ||
        !install("getgid", {}, entry<Op::Gid>))
        return;
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), value);
    auto environment = roots.root(value.value());
    if (!environment_snapshot(c, environment, out))
        return;
    make_str(c, U"environ", name);
    dict_store(c, dict, roots.root(name.value()), environment, out);
    if (failed(out))
        return;
    // Only advertise interfaces that the Python-visible functions implement.
    make_str(c, U"HAVE_FCHDIR", value);
    auto fchdir_capability = roots.root(value.value());
    make_str(c, U"HAVE_FSTATAT", value);
    auto stat_capability = roots.root(value.value());
    make_str(c, U"HAVE_FACCESSAT", value);
    Value capabilities[] = {fchdir_capability.get(c), stat_capability.get(c),
                            roots.root(value.value()).get(c)};
    make_list(c, capabilities, value);
    make_str(c, U"_have_functions", name);
    dict_store(c, dict, roots.root(name.value()), roots.root(value.value()), out);
    if (failed(out))
        return;
    const std::pair<const char *, int> constants[] = {
        {"O_RDONLY", O_RDONLY}, {"O_WRONLY", O_WRONLY},   {"O_RDWR", O_RDWR},
        {"O_CREAT", O_CREAT},   {"O_EXCL", O_EXCL},       {"O_TRUNC", O_TRUNC},
        {"O_APPEND", O_APPEND}, {"O_CLOEXEC", O_CLOEXEC}, {"SEEK_SET", SEEK_SET},
        {"SEEK_CUR", SEEK_CUR}, {"SEEK_END", SEEK_END},   {"F_OK", F_OK},
        {"R_OK", R_OK},         {"W_OK", W_OK},           {"X_OK", X_OK}};
    for (const auto &[key, number] : constants) {
        make_str(c, std::u32string(key, key + std::strlen(key)), name);
        make_int(c, number, value);
        dict_store(c, dict, roots.root(name.value()), roots.root(value.value()), out);
        if (failed(out))
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
