// POSIX primitives, informed by CPython 3.12.14 Modules/posixmodule.c.
// dir_fd and follow_symlinks variants are not yet exposed.
#include "internal/posixmodule.hpp"

#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/compiler.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

namespace cxxpython::core::detail {
namespace {
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void os_error(Attachment &c, int error, ReturnSlot &out, std::optional<Value> filename = {},
              std::optional<Value> filename2 = {})
{
    RootScope roots(c);
    if (filename)
        roots.root(*filename);
    if (filename2)
        roots.root(*filename2);
    ReturnSlot number(c), message(c), exception(c);
    make_int(c, error, number);
    make_str(c, decode_filename(SourceBuffer("", std::strerror(error))), message);
    std::vector<Value> values{number.value(), message.value()};
    if (filename)
        values.push_back(*filename);
    if (filename2) {
        values.push_back(c.builtin(CoreObject::None));
        values.push_back(*filename2);
    }
    call(c, roots.root(c.builtin(CoreObject::OSErrorType)), {values, {}}, exception);
    if (failed(exception))
        out.raised(exception.exception());
    else
        out.raised(exception.value());
}

bool integer(Attachment &c, Value value, std::int64_t &number, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot indexed(c);
    index(c, roots.root(value), indexed);
    if (failed(indexed)) {
        out.raised(indexed.exception());
        return false;
    }
    if (!int_fits_signed(c, indexed.value(), number)) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return false;
    }
    return true;
}
bool descriptor(Attachment &c, Value value, int &fd, ReturnSlot &out)
{
    std::int64_t n;
    if (!integer(c, value, n, out))
        return false;
    if (n < INT_MIN || n > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"file descriptor out of range", out);
        return false;
    }
    fd = static_cast<int>(n);
    return true;
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
void fspath(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    path_value(c, roots.root(a[0]), out);
}
enum class Op {
    Open,
    Close,
    Read,
    Write,
    Seek,
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
template <Op op> void entry(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    ReturnSlot filename(c), filename2(c);
    int fd = 0, error = 0;
    std::int64_t n = 0, extra = 0;
    std::string p, q, data;
    ssize_t result = 0;
    if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Rmdir || op == Op::Unlink ||
                  op == Op::Rename || op == Op::Symlink)
        if (!path(c, a[0], p, out, &filename))
            return;
    if constexpr (op == Op::Rename || op == Op::Symlink)
        if (!path(c, a[1], q, out, &filename2))
            return;
    if constexpr (op == Op::Close || op == Op::Read || op == Op::Write || op == Op::Seek)
        if (!descriptor(c, a[0], fd, out))
            return;
    if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Read || op == Op::Seek)
        if (!integer(c, a[1], n, out))
            return;
    if constexpr (op == Op::Open || op == Op::Seek)
        if (!integer(c, a[2], extra, out))
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
            os_error(c, error, out, filename.value(), filename2.value());
        else if constexpr (op == Op::Open || op == Op::Mkdir || op == Op::Rmdir || op == Op::Unlink)
            os_error(c, error, out, filename.value());
        else
            os_error(c, error, out);
        return;
    }
    if constexpr (op == Op::Read) {
        data.resize(static_cast<std::size_t>(result));
        make_bytes(c, std::move(data), out);
    } else if constexpr (op == Op::Close || op == Op::Mkdir || op == Op::Rmdir ||
                         op == Op::Unlink || op == Op::Rename || op == Op::Symlink)
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
void cwd(Attachment &c, Handle function, std::span<const Value>, ReturnSlot &out)
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
        os_error(c, error, out);
    else if (bytes)
        make_bytes(c, std::move(result), out);
    else
        make_str(c, fs_decode(result), out);
}
void listdir(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
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
        os_error(c, error, out,
                 a[0] == c.builtin(CoreObject::None) ? std::optional<Value>{} : converted.value());
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
void readlink_entry(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
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
        os_error(c, error, out, converted.value());
    else if (bytes)
        make_bytes(c, std::move(result), out);
    else
        make_str(c, fs_decode(result), out);
}

std::mutex environment_mutex;
template <bool Set>
void environment_change(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
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
        os_error(c, error, out);
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
void stat_new(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
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
void stat_entry(Attachment &c, Handle function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot filename(c);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    const auto &name = c.payload<NativeFunctionObject>(function).name;
    bool by_fd = name == "fstat", nofollow = name == "lstat";
    std::string p;
    int fd = -1;
    if (by_fd) {
        if (!descriptor(c, a[0], fd, out))
            return;
    } else if (!path(c, a[0], p, out, &filename))
        return;
    struct stat st {};
    int error = 0;
    {
        NativeWait wait(c);
        int result = by_fd      ? ::fstat(fd, &st)
                     : nofollow ? ::lstat(p.c_str(), &st)
                                : ::stat(p.c_str(), &st);
        if (result < 0)
            error = errno;
    }
    if (error) {
        os_error(c, error, out, by_fd ? std::optional<Value>{} : filename.value());
        return;
    }
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
} // namespace

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
    c.payload<TypeObject>(stat_type).immutable = true;
    c.payload<TypeObject>(stat_type).subclassable = false;
    make_str(c, U"stat_result", name);
    dict_store(c, dict, roots.root(name.value()), stat_type, out);
    if (failed(out))
        return;
    const std::vector<Value> stat_capture{stat_type.get(c)};
    if (!install("fspath", signature({patharg}), fspath) ||
        !install("stat", signature({patharg}), stat_entry, stat_capture) ||
        !install("lstat", signature({patharg}), stat_entry, stat_capture) ||
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
        !install("symlink", signature({{U"src", only}, {U"dst", only}}), entry<Op::Symlink>) ||
        !install("getpid", {}, entry<Op::Pid>) || !install("getppid", {}, entry<Op::Ppid>) ||
        !install("getuid", {}, entry<Op::Uid>) || !install("getgid", {}, entry<Op::Gid>))
        return;
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), value);
    auto environment = roots.root(value.value());
    if (!environment_snapshot(c, environment, out))
        return;
    make_str(c, U"environ", name);
    dict_store(c, dict, roots.root(name.value()), environment, out);
    if (failed(out))
        return;
    const std::pair<const char *, int> constants[] = {
        {"O_RDONLY", O_RDONLY}, {"O_WRONLY", O_WRONLY},   {"O_RDWR", O_RDWR},
        {"O_CREAT", O_CREAT},   {"O_EXCL", O_EXCL},       {"O_TRUNC", O_TRUNC},
        {"O_APPEND", O_APPEND}, {"O_CLOEXEC", O_CLOEXEC}, {"SEEK_SET", SEEK_SET},
        {"SEEK_CUR", SEEK_CUR}, {"SEEK_END", SEEK_END}};
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
