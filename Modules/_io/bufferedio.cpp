// Buffered stream algorithms follow CPython 3.12.14 Modules/_io/bufferedio.c
// (PSF license). Managed edges and synchronization are specific to cxxpython.
#include "internal/bufferedio.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/abstract.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace cxxpython::core {
namespace {
enum class Kind { Reader, Writer, Random, Pair };

class Claim {
    Attachment &c_;
    BufferedIOObject &s_;
    bool held_ = false;

  public:
    Claim(Attachment &c, BufferedIOObject &s, ReturnSlot &out) : c_(c), s_(s)
    {
        for (;;) {
            std::unique_lock lock(s_.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                NativeWait wait(c_);
                std::unique_lock parked(s_.mutex);
                continue;
            }
            if (!s_.active) {
                s_.active = true;
                s_.owner = std::this_thread::get_id();
                held_ = true;
                return;
            }
            if (s_.owner == std::this_thread::get_id()) {
                lock.unlock();
                raise_error(c_, CoreObject::RuntimeErrorType,
                            U"reentrant call inside buffered stream", out);
                return;
            }
            lock.unlock();
            {
                NativeWait wait(c_);
                std::unique_lock parked(s_.mutex);
                s_.ready.wait(parked, [&] { return !s_.active; });
            }
        }
    }
    ~Claim()
    {
        if (!held_)
            return;
        for (;;) {
            std::unique_lock lock(s_.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                s_.active = false;
                s_.owner = {};
                s_.ready.notify_one();
                return;
            }
            NativeWait wait(c_);
            std::unique_lock parked(s_.mutex);
        }
    }
    explicit operator bool() const
    {
        return held_;
    }
};

bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<BufferedIOObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected buffered stream", out);
    return false;
}
bool usable(Attachment &c, BufferedIOObject &s, ReturnSlot &out)
{
    if (!s.initialized || s.detached) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"raw stream has been detached or is uninitialized", out);
        return false;
    }
    if (s.closed) {
        detail::io_closed_error(c, out);
        return false;
    }
    return true;
}
void account(Attachment &c, Handle self, BufferedIOObject &s)
{
    c.account_external(self, s.input.capacity() + s.output.capacity());
}
Handle raw(Attachment &c, Handle self, AtomicGcEdge &edge, RootScope &roots)
{
    ReturnSlot value(c);
    c.load(self.get(c), edge, value);
    return roots.root(value.value());
}
bool raw_seek(Attachment &c, Handle object, std::int64_t offset, int whence, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot off(c), origin(c);
    make_int(c, offset, off);
    auto rooted = roots.root(off.value());
    make_int(c, whence, origin);
    std::array<Value, 2> args{rooted.get(c), origin.value()};
    return detail::io_call(c, object, U"seek", args, out);
}
void blocking_error(Attachment &c, std::size_t written, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot code(c), message(c), count(c), exception(c);
    make_int(c, 0, code);
    auto error_code = roots.root(code.value());
    make_str(c, U"write could not complete without blocking", message);
    auto text = roots.root(message.value());
    make_int(c, static_cast<std::int64_t>(written), count);
    std::array<Value, 3> args{error_code.get(c), text.get(c), count.value()};
    call(c, roots.root(c.builtin(CoreObject::BlockingIOErrorType)), {args, {}}, exception);
    if (exception.tag() == ResultTag::Raised)
        out.raised(exception.exception());
    else
        out.raised(exception.value());
}
// -2 means would block, -1 means a Python exception, otherwise bytes written.
std::int64_t raw_write(Attachment &c, Handle stream, std::string_view data, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot bytes(c), result(c);
    make_bytes(c, std::string(data), bytes);
    if (bytes.tag() == ResultTag::Raised) {
        out.raised(bytes.exception());
        return -1;
    }
    auto arg = roots.root(bytes.value());
    Value value = arg.get(c);
    if (!detail::io_call(c, stream, U"write", {&value, 1}, result)) {
        out.raised(result.exception());
        return -1;
    }
    if (result.value() == c.builtin(CoreObject::None))
        return -2;
    std::int64_t count;
    if (!detail::index_as_int64(c, result.value(), count, out))
        return -1;
    if (count <= 0 || static_cast<std::uint64_t>(count) > data.size()) {
        raise_error(c, CoreObject::OSErrorType, U"raw write() returned invalid length", out);
        return -1;
    }
    return count;
}
bool flush_output(Attachment &c, Handle self, BufferedIOObject &s, ReturnSlot &out)
{
    RootScope roots(c);
    auto stream = raw(c, self, s.writer, roots);
    while (!s.output.empty()) {
        auto count = raw_write(
            c, stream, {reinterpret_cast<const char *>(s.output.data()), s.output.size()}, out);
        if (count == -2) {
            blocking_error(c, 0, out);
            return false;
        }
        if (count < 0)
            return false;
        s.output.erase(s.output.begin(), s.output.begin() + count);
    }
    return true;
}
bool rewind_input(Attachment &c, Handle self, BufferedIOObject &s, ReturnSlot &out)
{
    auto remaining = s.input.size() - s.position;
    if (remaining) {
        RootScope roots(c);
        auto stream = raw(c, self, s.reader, roots);
        if (!raw_seek(c, stream, -static_cast<std::int64_t>(remaining), 1, out))
            return false;
    }
    s.input.clear();
    s.position = 0;
    return true;
}
// Returns -1 on error, -2 for would-block, 0 for EOF, otherwise bytes read.
std::int64_t fill(Attachment &c, Handle self, BufferedIOObject &s, ReturnSlot &out)
{
    RootScope roots(c);
    auto stream = raw(c, self, s.reader, roots);
    ReturnSlot target(c), result(c);
    make_bytearray(c, std::string(s.capacity, '\0'), target);
    if (target.tag() == ResultTag::Raised) {
        out.raised(target.exception());
        return -1;
    }
    auto buffer = roots.root(target.value());
    Value arg = buffer.get(c);
    if (!detail::io_call(c, stream, U"readinto", {&arg, 1}, result)) {
        out.raised(result.exception());
        return -1;
    }
    if (result.value() == c.builtin(CoreObject::None))
        return -2;
    std::int64_t count;
    if (!detail::index_as_int64(c, result.value(), count, out))
        return -1;
    if (count < 0 || static_cast<std::uint64_t>(count) > s.capacity) {
        raise_error(c, CoreObject::OSErrorType, U"raw readinto() returned invalid length", out);
        return -1;
    }
    std::string data;
    if (!detail::buffer_bytes(c, buffer, data, out))
        return -1;
    s.input.assign(data.begin(), data.begin() + count);
    s.position = 0;
    account(c, self, s);
    return count;
}
template <Kind kind> void create(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<BufferedIOObject>(roots.root(a[0]), out, c.builtin(CoreObject::None));
}
template <Kind kind>
void initialize(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    std::int64_t size;
    constexpr bool pair = kind == Kind::Pair;
    if (!detail::index_as_int64(c, a[pair ? 3 : 2], size, out))
        return;
    if (size <= 0) {
        raise_error(c, CoreObject::ValueErrorType, U"buffer size must be strictly positive", out);
        return;
    }
    auto reader = roots.root(a[1]);
    auto writer = roots.root(pair ? a[2] : a[1]);
    constexpr bool readable = kind != Kind::Writer;
    constexpr bool writable = kind != Kind::Reader;
    for (auto [enabled, object, name] :
         {std::tuple{readable, reader, U"readable"}, std::tuple{writable, writer, U"writable"}}) {
        if (!enabled)
            continue;
        ReturnSlot result(c), is_true(c);
        if (!detail::io_call(c, object, name, {}, result)) {
            out.raised(result.exception());
            return;
        }
        truth(c, roots.root(result.value()), is_true);
        if (is_true.tag() == ResultTag::Raised) {
            out.raised(is_true.exception());
            return;
        }
        if (is_true.value() == c.builtin(CoreObject::False)) {
            detail::io_unsupported(c,
                                   readable && name == std::u32string_view(U"readable")
                                       ? U"File or stream is not readable."
                                       : U"File or stream is not writable.",
                                   out);
            return;
        }
    }
    if constexpr (kind == Kind::Random) {
        ReturnSlot result(c), ok(c);
        if (!detail::io_call(c, reader, U"seekable", {}, result)) {
            out.raised(result.exception());
            return;
        }
        truth(c, roots.root(result.value()), ok);
        if (ok.tag() == ResultTag::Raised) {
            out.raised(ok.exception());
            return;
        }
        if (ok.value() == c.builtin(CoreObject::False)) {
            detail::io_unsupported(c, U"File or stream is not seekable.", out);
            return;
        }
    }
    c.store(self.get(c), s.reader, reader.get(c));
    c.store(self.get(c), s.writer, writer.get(c));
    s.capacity = static_cast<std::size_t>(size);
    s.input.clear();
    s.output.clear();
    s.position = 0;
    s.can_read = readable;
    s.can_write = writable;
    s.pair = pair;
    s.initialized = true;
    s.detached = false;
    s.closed = false;
    account(c, self, s);
    out.ok(c.builtin(CoreObject::None));
}

enum class ReadMode { All, One, Line, Peek };
template <ReadMode mode> void read(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::int64_t size = -1;
    if (a[1] != c.builtin(CoreObject::None) && !detail::index_as_int64(c, a[1], size, out))
        return;
    if constexpr (mode == ReadMode::Peek)
        size = -1;
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.can_read) {
        detail::io_unsupported(c, U"read", out);
        return;
    }
    if (!s.pair && !flush_output(c, self, s, out))
        return;
    std::string result;
    bool filled = false;
    while (size < 0 || result.size() < static_cast<std::uint64_t>(size)) {
        if (s.position == s.input.size()) {
            if constexpr (mode == ReadMode::One)
                if (filled || !result.empty())
                    break;
            auto count = fill(c, self, s, out);
            filled = true;
            if (count == -1)
                return;
            if (count == -2) {
                if constexpr (mode == ReadMode::All)
                    if (result.empty()) {
                        out.ok(c.builtin(CoreObject::None));
                        return;
                    }
                break;
            }
            if (!count)
                break;
        }
        auto available = s.input.size() - s.position;
        auto count = size < 0 ? available
                              : std::min(available, static_cast<std::size_t>(size) - result.size());
        if constexpr (mode == ReadMode::Peek)
            count = available;
        if constexpr (mode == ReadMode::Line) {
            auto start = s.input.begin() + s.position;
            auto end = std::find(start, start + count, '\n');
            if (end != start + count)
                count = static_cast<std::size_t>(end - start) + 1;
        }
        result.append(reinterpret_cast<const char *>(s.input.data() + s.position), count);
        if constexpr (mode != ReadMode::Peek)
            s.position += count;
        if constexpr (mode == ReadMode::Peek)
            break;
        if constexpr (mode == ReadMode::Line)
            if (!result.empty() && result.back() == '\n')
                break;
    }
    if constexpr (mode == ReadMode::Peek) {
        if (result.empty() && s.position < s.input.size())
            result.assign(s.input.begin() + s.position, s.input.end());
    }
    make_bytes(c, std::move(result), out);
}
void write(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::string data;
    if (!detail::buffer_bytes(c, roots.root(a[1]), data, out)) {
        if (out.tag() != ResultTag::Raised)
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.can_write) {
        detail::io_unsupported(c, U"write", out);
        return;
    }
    if (!s.pair && !rewind_input(c, self, s, out))
        return;
    if (data.size() <= s.capacity - s.output.size()) {
        s.output.insert(s.output.end(), data.begin(), data.end());
        account(c, self, s);
        make_int(c, static_cast<std::int64_t>(data.size()), out);
        return;
    }
    ReturnSlot flushed(c);
    if (!flush_output(c, self, s, flushed)) {
        if (!is_subtype(c, c.type_of(flushed.exception()),
                        c.builtin(CoreObject::BlockingIOErrorType))) {
            out.raised(flushed.exception());
            return;
        }
        auto accepted = std::min(s.capacity - s.output.size(), data.size());
        s.output.insert(s.output.end(), data.begin(), data.begin() + accepted);
        account(c, self, s);
        if (accepted == data.size())
            make_int(c, static_cast<std::int64_t>(accepted), out);
        else
            blocking_error(c, accepted, out);
        return;
    }
    auto stream = raw(c, self, s.writer, roots);
    std::size_t position = 0;
    while (data.size() - position > s.capacity) {
        auto count = raw_write(c, stream, std::string_view(data).substr(position), out);
        if (count == -1)
            return;
        if (count == -2) {
            s.output.insert(s.output.end(), data.begin() + position,
                            data.begin() + position + s.capacity);
            position += s.capacity;
            account(c, self, s);
            blocking_error(c, position, out);
            return;
        }
        position += static_cast<std::size_t>(count);
    }
    s.output.insert(s.output.end(), data.begin() + position, data.end());
    account(c, self, s);
    make_int(c, static_cast<std::int64_t>(data.size()), out);
}
template <bool One> void readinto(Attachment &c, Value h, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto target = roots.root(a[1]);
    detail::BufferExport buffer(c);
    if (!detail::writable_buffer(c, target, buffer, out))
        return;
    ReturnSlot length(c), result(c);
    make_int(c, static_cast<std::int64_t>(buffer.length), length);
    std::array<Value, 2> args{self.get(c), length.value()};
    if constexpr (One)
        read<ReadMode::One>(c, h, args, result);
    else
        read<ReadMode::All>(c, h, args, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    if (result.value() == c.builtin(CoreObject::None)) {
        out.ok(result.value());
        return;
    }
    std::string data;
    if (!detail::buffer_bytes(c, roots.root(result.value()), data, out))
        return;
    detail::write_buffer(c, buffer, data);
    make_int(c, static_cast<std::int64_t>(data.size()), out);
}
void flush(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!flush_output(c, self, s, out))
        return;
    if (s.can_read && s.can_write && !s.pair && !rewind_input(c, self, s, out))
        return;
    out.ok(c.builtin(CoreObject::None));
}
void tell(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    auto stream = raw(c, self, s.can_read ? s.reader : s.writer, roots);
    ReturnSlot result(c);
    if (!detail::io_call(c, stream, U"tell", {}, result)) {
        out.raised(result.exception());
        return;
    }
    std::int64_t pos;
    if (!detail::index_as_int64(c, result.value(), pos, out))
        return;
    make_int(c,
             pos - static_cast<std::int64_t>(s.input.size() - s.position) +
                 static_cast<std::int64_t>(s.output.size()),
             out);
}
void seek(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::int64_t offset, whence;
    if (!detail::index_as_int64(c, a[1], offset, out) ||
        !detail::index_as_int64(c, a[2], whence, out))
        return;
    if (whence < 0 || whence > 2) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid whence", out);
        return;
    }
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!flush_output(c, self, s, out))
        return;
    if (whence == 1)
        offset -= static_cast<std::int64_t>(s.input.size() - s.position);
    auto stream = raw(c, self, s.can_read ? s.reader : s.writer, roots);
    if (!raw_seek(c, stream, offset, static_cast<int>(whence), out))
        return;
    s.input.clear();
    s.position = 0;
}
void detach(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!flush_output(c, self, s, out))
        return;
    if (s.can_read && s.can_write && !rewind_input(c, self, s, out))
        return;
    auto stream = raw(c, self, s.reader, roots);
    s.detached = true;
    c.store(self.get(c), s.reader, c.builtin(CoreObject::None));
    c.store(self.get(c), s.writer, c.builtin(CoreObject::None));
    out.ok(stream.get(c));
}
void truncate(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto arg = roots.root(a[1]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!flush_output(c, self, s, out))
        return;
    if (s.can_read && !rewind_input(c, self, s, out))
        return;
    auto stream = raw(c, self, s.writer, roots);
    Value value = arg.get(c);
    detail::io_call(c, stream, U"truncate", {&value, 1}, out);
}
void close(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    bool pair;
    {
        Claim claim(c, s, out);
        if (!claim)
            return;
        if (s.closed) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
        if (!usable(c, s, out))
            return;
        pair = s.pair;
    }
    ReturnSlot flush_result(c), result(c);
    bool flushed;
    if (pair) {
        Claim claim(c, s, out);
        if (!claim)
            return;
        flushed = flush_output(c, self, s, flush_result);
    } else {
        flushed = detail::io_call(c, self, U"flush", {}, flush_result);
    }
    Claim claim(c, s, out);
    if (!claim)
        return;
    auto writer = raw(c, self, s.writer, roots);
    auto reader = raw(c, self, s.reader, roots);
    bool ok = detail::io_call(c, writer, U"close", {}, result);
    if (!ok && !flushed)
        c.store(result.exception(), c.payload<ExceptionObject>(result.exception()).context,
                flush_result.exception());
    if (s.pair) {
        ReturnSlot second(c);
        if (!detail::io_call(c, reader, U"close", {}, second)) {
            if (!ok)
                c.store(second.exception(), c.payload<ExceptionObject>(second.exception()).context,
                        result.exception());
            else if (!flushed)
                c.store(second.exception(), c.payload<ExceptionObject>(second.exception()).context,
                        flush_result.exception());
            out.raised(second.exception());
            return;
        }
    }
    s.closed = ok;
    if (!ok)
        out.raised(result.exception());
    else if (!flushed)
        out.raised(flush_result.exception());
    else
        out.ok(c.builtin(CoreObject::None));
}
void closed_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    if (!s.initialized || s.detached) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"raw stream has been detached or is uninitialized", out);
        return;
    }
    if (s.closed) {
        out.ok(c.builtin(CoreObject::True));
        return;
    }
    auto stream = raw(c, self, s.pair ? s.writer : s.reader, roots);
    ReturnSlot name(c);
    make_str(c, U"closed", name);
    get_attribute(c, stream, roots.root(name.value()), out);
}
void raw_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    if (!s.initialized || s.detached) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"raw stream has been detached or is uninitialized", out);
        return;
    }
    c.load(self.get(c), s.reader, out);
}
template <bool Mode> void raw_attribute_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &s = c.payload<BufferedIOObject>(self);
    ReturnSlot stream(c);
    {
        Claim claim(c, s, out);
        if (!claim)
            return;
        if (!s.initialized || s.detached) {
            raise_error(c, CoreObject::ValueErrorType,
                        U"raw stream has been detached or is uninitialized", out);
            return;
        }
        c.load(self.get(c), s.reader, stream);
    }
    auto raw_stream = roots.root(stream.value());
    ReturnSlot name(c);
    make_str(c, Mode ? U"mode" : U"name", name);
    get_attribute(c, raw_stream, roots.root(name.value()), out);
}
template <bool Read>
void capability(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    out.ok(c.builtin((Read ? s.can_read : s.can_write) ? CoreObject::True : CoreObject::False));
}
void delegate(Attachment &c, std::span<const Value> a, ReturnSlot &out, const char32_t *name)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<BufferedIOObject>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    auto stream = raw(c, self, s.can_read ? s.reader : s.writer, roots);
    detail::io_call(c, stream, name, {}, out);
}
void fileno(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    delegate(c, a, out, U"fileno");
}
void seekable(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    delegate(c, a, out, U"seekable");
}
void isatty(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    delegate(c, a, out, U"isatty");
}

} // namespace
namespace detail {
void initialize_bufferedio(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    ReturnSlot default_size(c);
    make_int(c, 8192, default_size);
    auto default_root = roots.root(default_size.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto optional = unary;
    optional.parameters.push_back(
        {U"size", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
    auto binary = unary;
    binary.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto seek_sig = binary;
    seek_sig.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    auto create_sig = unary;
    create_sig.varargs = U"args";
    create_sig.varkwargs = U"kwargs";
    auto add = [&](const char *name, const char32_t *export_name, Kind kind, NativeEntry creator,
                   NativeEntry init) {
        std::array<Handle, 1> bases{at(CoreObject::BufferedIOBaseType)};
        NativeTypeSpec spec{{name, bases}, LayoutDescriptor::of<BufferedIOObject>()};
        spec.type.module = U"_io";
        ReturnSlot type_slot(c);
        create_native_type(c, spec, type_slot);
        if (type_slot.tag() == ResultTag::Raised) {
            out.raised(type_slot.exception());
            return false;
        }
        auto type = roots.root(type_slot.value());
        auto install = [&](const char32_t *n, const SignatureLayout &sig, NativeEntry entry) {
            install_native_attribute(c, core, type, n, sig, entry);
        };
        install(U"__new__", create_sig, creator);
        auto dict = *TypeView(c, type).dictionary();
        auto function = roots.root(*name_lookup(c, dict, U"__new__"));
        ReturnSlot wrapper(c), key(c);
        c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                function.get(c));
        make_str(c, U"__new__", key);
        name_store(c, dict, key.value(), wrapper.value());
        auto init_sig = unary;
        init_sig.parameters.push_back(
            {kind == Kind::Pair ? U"reader" : U"raw", ParameterKind::PositionalOrKeyword});
        if (kind == Kind::Pair)
            init_sig.parameters.push_back({U"writer", ParameterKind::PositionalOrKeyword});
        init_sig.parameters.push_back(
            {U"buffer_size", ParameterKind::PositionalOrKeyword, default_root.get(c)});
        install(U"__init__", init_sig, init);
        if (kind != Kind::Writer) {
            install(U"read", optional, read<ReadMode::All>);
            install(U"read1", optional, read<ReadMode::One>);
            install(U"readline", optional, read<ReadMode::Line>);
            install(U"peek", optional, read<ReadMode::Peek>);
            install(U"readinto", binary, readinto<false>);
            install(U"readinto1", binary, readinto<true>);
        }
        if (kind != Kind::Reader)
            install(U"write", binary, write);
        install(U"flush", unary, flush);
        install(U"close", unary, close);
        if (kind != Kind::Pair) {
            install(U"tell", unary, tell);
            install(U"seek", seek_sig, seek);
            install(U"detach", unary, detach);
            install(U"truncate", optional, truncate);
            install_native_field(c, core, type, U"raw", raw_get);
            install_native_field(c, core, type, U"name", raw_attribute_get<false>);
            install_native_field(c, core, type, U"mode", raw_attribute_get<true>);
        }
        install(U"readable", unary, capability<true>);
        install(U"writable", unary, capability<false>);
        install(U"seekable", unary, seekable);
        install(U"fileno", unary, fileno);
        install(U"isatty", unary, isatty);
        install_native_field(c, core, type, U"closed", closed_get);
        make_str(c, export_name, key);
        name_store(c, module_dict(c, module.get(c)), key.value(), type.get(c));
        return true;
    };
    if (!add("BufferedReader", U"BufferedReader", Kind::Reader, create<Kind::Reader>,
             initialize<Kind::Reader>) ||
        !add("BufferedWriter", U"BufferedWriter", Kind::Writer, create<Kind::Writer>,
             initialize<Kind::Writer>) ||
        !add("BufferedRandom", U"BufferedRandom", Kind::Random, create<Kind::Random>,
             initialize<Kind::Random>) ||
        !add("BufferedRWPair", U"BufferedRWPair", Kind::Pair, create<Kind::Pair>,
             initialize<Kind::Pair>))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace detail
} // namespace cxxpython::core
