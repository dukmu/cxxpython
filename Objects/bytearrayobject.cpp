#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

#include <algorithm>
#include <limits>

namespace cxxpython::core {
namespace {
template <class F> decltype(auto) locked(Attachment &c, ByteArrayObject::Storage &s, F &&f)
{
    for (;;) {
        std::unique_lock lock(s.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return f();
        }
        NativeWait wait(c);
        s.mutex.lock();
        s.mutex.unlock();
    }
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool receiver(Attachment &c, Value v, ReturnSlot &out)
{
    if (is_bytearray(c, v))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"bytearray descriptor requires bytearray", out);
    return false;
}
struct Snapshot {
    std::string data;
    std::size_t version;
};
Snapshot snapshot(Attachment &c, Handle self)
{
    auto &s = *c.payload<ByteArrayObject>(self).contents;
    Snapshot result;
    for (;;) {
        const auto size = locked(c, s, [&] { return s.bytes.size(); });
        result.data.reserve(size);
        if (locked(c, s, [&] {
                if (result.data.capacity() < s.bytes.size())
                    return false;
                result.data.assign(s.bytes.data(), s.bytes.size());
                result.version = s.version;
                return true;
            }))
            return result;
        c.poll();
    }
}
enum class Commit { Retry, Done, Exported };
Commit commit(Attachment &c, Handle self, Snapshot &old, std::string &replacement)
{
    auto &s = *c.payload<ByteArrayObject>(self).contents;
    return locked(c, s, [&] {
        if (s.version != old.version)
            return Commit::Retry;
        if (s.bytes.size() != replacement.size() && s.exports.load())
            return Commit::Exported;
        s.bytes.swap(replacement);
        ++s.version;
        c.account_external(self, sizeof(ByteArrayObject::Storage) + s.bytes.capacity() + 1);
        return Commit::Done;
    });
}
template <class F> bool mutate(Attachment &c, Handle self, F &&f, ReturnSlot &out)
{
    for (;;) {
        auto old = snapshot(c, self);
        auto data = old.data;
        f(data);
        auto result = commit(c, self, old, data);
        if (result == Commit::Done)
            return true;
        if (result == Commit::Exported) {
            raise_error(c, CoreObject::BufferErrorType,
                        U"Existing exports of data: object cannot be re-sized", out);
            return false;
        }
        c.poll();
    }
}
bool append_data(Attachment &c, Handle self, std::string_view extra, ReturnSlot &out)
{
    auto &s = *c.payload<ByteArrayObject>(self).contents;
    std::string prepared;
    for (;;) {
        std::size_t needed = 0;
        bool exported = false;
        if (locked(c, s, [&] {
                if (!extra.empty() && s.exports.load()) {
                    exported = true;
                    return true;
                }
                if (extra.size() > s.bytes.max_size() - s.bytes.size())
                    throw std::bad_alloc();
                needed = s.bytes.size() + extra.size();
                if (needed > s.bytes.capacity()) {
                    if (prepared.capacity() < needed) {
                        needed =
                            std::max(needed, s.bytes.capacity() +
                                                 std::min(s.bytes.max_size() - s.bytes.capacity(),
                                                          s.bytes.capacity() / 2 + 4));
                        return false;
                    }
                    prepared.assign(s.bytes.data(), s.bytes.size());
                    s.bytes.swap(prepared);
                }
                s.bytes.append(extra.data(), extra.size());
                ++s.version;
                c.account_external(self, sizeof(ByteArrayObject::Storage) + s.bytes.capacity() + 1);
                return true;
            })) {
            if (exported) {
                raise_error(c, CoreObject::BufferErrorType,
                            U"Existing exports of data: object cannot be re-sized", out);
                return false;
            }
            return true;
        }
        prepared.reserve(needed);
        c.poll();
    }
}
bool integer(Attachment &c, Handle value, std::int64_t &number, ReturnSlot &out,
             CoreObject overflow = CoreObject::OverflowErrorType, bool clip = false)
{
    ReturnSlot converted(c);
    index(c, value, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (int_fits_signed(c, converted.value(), number))
        return true;
    if (clip) {
        number = int_sign(c, converted.value()) < 0 ? std::numeric_limits<std::int64_t>::min()
                                                    : std::numeric_limits<std::int64_t>::max();
        return true;
    }
    raise_error(c, overflow, U"integer out of range", out);
    return false;
}
bool byte_value(Attachment &c, Handle value, char &byte, ReturnSlot &out)
{
    std::int64_t n;
    if (!integer(c, value, n, out, CoreObject::ValueErrorType))
        return false;
    if (n < 0 || n > 255) {
        raise_error(c, CoreObject::ValueErrorType, U"byte must be in range(0, 256)", out);
        return false;
    }
    byte = static_cast<char>(n);
    return true;
}
bool buffer(Attachment &c, Handle value, std::string &data, ReturnSlot &out)
{
    return detail::buffer_bytes(c, value, data, out, false);
}
bool iterable(Attachment &c, Handle source, std::string &data, ReturnSlot &out)
{
    if (buffer(c, source, data, out))
        return true;
    if (failed(out))
        return false;
    RootScope roots(c);
    ReturnSlot iter(c), item(c);
    get_iter(c, source, iter);
    if (failed(iter)) {
        out.raised(iter.exception());
        return false;
    }
    auto it = roots.root(iter.value());
    for (;;) {
        auto status = iter_next(c, it, item);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return false;
        }
        RootScope iteration(c);
        char byte;
        if (!byte_value(c, iteration.root(item.value()), byte, out))
            return false;
        data.push_back(byte);
    }
}
bool text_argument(Attachment &c, Handle h, ReturnSlot &out)
{
    if (c.has_layout<StrObject>(h.get(c)) &&
        is_subtype(c, c.type_of(h), c.builtin(CoreObject::StrType)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"encoding and errors must be strings", out);
    return false;
}
bool arguments(Attachment &c, Value positional, Value named, RootScope &roots,
               std::array<std::optional<Handle>, 3> &values, ReturnSlot &out)
{
    const auto &args = c.payload<TupleObject>(positional).items;
    if (args.size() > 3) {
        raise_error(c, CoreObject::TypeErrorType, U"too many bytearray arguments", out);
        return false;
    }
    for (std::size_t i = 0; i < args.size(); ++i)
        values[i] = roots.root(args[i]);
    const std::array<std::u32string_view, 3> names{U"source", U"encoding", U"errors"};
    DictView keywords(c, named);
    for (auto entry : keywords.entries()) {
        const auto &name = c.payload<StrObject>(entry.key).value;
        std::size_t i = 0;
        while (i < names.size() && name != names[i])
            ++i;
        if (i == names.size() || values[i]) {
            raise_error(c, CoreObject::TypeErrorType, U"invalid or repeated bytearray argument",
                        out);
            return false;
        }
        values[i] = roots.root(entry.value);
    }
    return true;
}
void create(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::ByteArrayType))) {
        raise_error(c, CoreObject::TypeErrorType, U"bytearray.__new__ requires a subtype", out);
        return;
    }
    c.allocate_instance<ByteArrayObject>(roots.root(args[0]), out);
}
void initialize(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::array<std::optional<Handle>, 3> values;
    if (!arguments(c, args[1], args[2], roots, values, out))
        return;
    if ((values[1] && !text_argument(c, *values[1], out)) ||
        (values[2] && !text_argument(c, *values[2], out)))
        return;
    if (!mutate(
            c, self, [](std::string &target) { target.clear(); }, out))
        return;
    std::string data;
    if (!values[0]) {
        if (values[1] || values[2]) {
            raise_error(c, CoreObject::TypeErrorType, U"encoding or errors without a string", out);
            return;
        }
    } else if (values[1]) {
        if (!text_argument(c, *values[0], out))
            return;
        detail::encode_text(c, *values[0], *values[1],
                            values[2].value_or(roots.root(c.builtin(CoreObject::None))), out);
        if (failed(out))
            return;
        data = c.payload<BytesObject>(out.value()).value;
    } else if (values[2]) {
        raise_error(c, CoreObject::TypeErrorType, U"errors without an encoding", out);
        return;
    } else {
        auto source = *values[0];
        if (c.has_layout<StrObject>(source.get(c))) {
            raise_error(c, CoreObject::TypeErrorType, U"string argument without an encoding", out);
            return;
        }
        auto slot = resolve_protocol(c, roots.root(c.type_of(source)), Protocol::Index, roots);
        if (slot.state != SlotState::Missing) {
            std::int64_t count;
            if (!integer(c, source, count, out))
                return;
            if (count < 0) {
                raise_error(c, CoreObject::ValueErrorType, U"negative count", out);
                return;
            }
            if (static_cast<std::uint64_t>(count) > data.max_size())
                throw std::bad_alloc();
            data.resize(static_cast<std::size_t>(count), '\0');
        } else if (!iterable(c, source, data, out))
            return;
    }
    if (!mutate(
            c, self, [&](std::string &target) { target = data; }, out))
        return;
    out.ok(c.builtin(CoreObject::None));
}
void representation(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto data = snapshot(c, roots.root(args[0])).data;
    char quote =
        data.find('\'') != std::string::npos && data.find('"') == std::string::npos ? '"' : '\'';
    constexpr char hex[] = "0123456789abcdef";
    std::u32string text = U"bytearray(b";
    text += quote;
    for (unsigned char b : data) {
        if (b == '\\' || b == quote) {
            text += U'\\';
            text += b;
        } else if (b == '\n')
            text += U"\\n";
        else if (b == '\r')
            text += U"\\r";
        else if (b == '\t')
            text += U"\\t";
        else if (b < 32 || b >= 127) {
            text += U"\\x";
            text += hex[b >> 4];
            text += hex[b & 15];
        } else
            text += b;
    }
    text += quote;
    text += U')';
    make_str(c, std::move(text), out);
}
void size(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = *c.payload<ByteArrayObject>(roots.root(args[0])).contents;
    auto n = locked(c, s, [&] { return s.bytes.size(); });
    make_int(c, n, out);
}
enum class ItemOp { Get, Set, Delete, Pop };
template <ItemOp Op> void item(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), key = roots.root(args[1]);
    if (c.type_of(key) == c.builtin(CoreObject::SliceType) && Op != ItemOp::Pop) {
        detail::SliceBounds bounds;
        if (!detail::unpack_slice(c, key, bounds, out))
            return;
        std::string replacement;
        if constexpr (Op == ItemOp::Set) {
            auto source = roots.root(args[2]);
            if (c.has_layout<StrObject>(source.get(c)) ||
                resolve_protocol(c, roots.root(c.type_of(source)), Protocol::Index, roots).state !=
                    SlotState::Missing) {
                raise_error(c, CoreObject::TypeErrorType,
                            U"can assign only bytes or an iterable of integers", out);
                return;
            }
            if (!iterable(c, source, replacement, out))
                return;
        }
        for (;;) {
            auto old = snapshot(c, self);
            auto selected = bounds.adjust(old.data.size());
            if constexpr (Op == ItemOp::Get) {
                std::string result;
                result.reserve(static_cast<std::size_t>(selected.length));
                auto pos = selected.start;
                for (std::int64_t i = 0; i < selected.length; ++i) {
                    result += old.data[static_cast<std::size_t>(pos)];
                    if (i + 1 < selected.length)
                        pos += selected.step;
                }
                make_bytearray(c, std::move(result), out);
                return;
            } else {
                auto data = old.data;
                if constexpr (Op == ItemOp::Set) {
                    if (selected.step != 1 && !replacement.empty() &&
                        replacement.size() != static_cast<std::size_t>(selected.length)) {
                        raise_error(c, CoreObject::ValueErrorType,
                                    U"attempt to assign bytes of different size to extended slice",
                                    out);
                        return;
                    }
                    if (selected.step == 1)
                        data.replace(static_cast<std::size_t>(selected.start),
                                     static_cast<std::size_t>(selected.length), replacement);
                    else if (!replacement.empty()) {
                        auto pos = selected.start;
                        for (std::size_t i = 0; i < replacement.size(); ++i) {
                            data[static_cast<std::size_t>(pos)] = replacement[i];
                            if (i + 1 < replacement.size())
                                pos += selected.step;
                        }
                    } else if (selected.length) {
                        auto pos = selected.step > 0
                                       ? selected.start
                                       : selected.start + (selected.length - 1) * selected.step;
                        auto step = selected.step > 0 ? selected.step : -selected.step;
                        auto remaining = selected.length;
                        std::size_t write = 0;
                        for (std::size_t read = 0; read < data.size(); ++read) {
                            if (remaining && read == static_cast<std::size_t>(pos)) {
                                if (--remaining)
                                    pos += step;
                            } else
                                data[write++] = data[read];
                        }
                        data.resize(write);
                    }
                } else {
                    if (selected.step == 1)
                        data.erase(static_cast<std::size_t>(selected.start),
                                   static_cast<std::size_t>(selected.length));
                    else if (selected.length) {
                        auto pos = selected.step > 0
                                       ? selected.start
                                       : selected.start + (selected.length - 1) * selected.step;
                        auto step = selected.step > 0 ? selected.step : -selected.step;
                        auto remaining = selected.length;
                        std::size_t write = 0;
                        for (std::size_t read = 0; read < data.size(); ++read) {
                            if (remaining && read == static_cast<std::size_t>(pos)) {
                                if (--remaining)
                                    pos += step;
                            } else
                                data[write++] = data[read];
                        }
                        data.resize(write);
                    }
                }
                auto result = commit(c, self, old, data);
                if (result == Commit::Exported) {
                    raise_error(c, CoreObject::BufferErrorType,
                                U"Existing exports of data: object cannot be re-sized", out);
                    return;
                }
                if (result == Commit::Done) {
                    out.ok(c.builtin(CoreObject::None));
                    return;
                }
                c.poll();
            }
        }
    }
    std::int64_t position;
    if (!integer(c, key, position, out, CoreObject::IndexErrorType))
        return;
    char byte = 0;
    if constexpr (Op == ItemOp::Set)
        if (!byte_value(c, roots.root(args[2]), byte, out))
            return;
    auto &storage = *c.payload<ByteArrayObject>(self).contents;
    for (;;) {
        std::size_t selected = 0, version = 0;
        unsigned char value = 0;
        bool exported = false;
        const bool valid = locked(c, storage, [&] {
            auto pos = position;
            if (pos < 0)
                pos += static_cast<std::int64_t>(storage.bytes.size());
            if (pos < 0 || static_cast<std::size_t>(pos) >= storage.bytes.size())
                return false;
            selected = static_cast<std::size_t>(pos);
            version = storage.version;
            value = static_cast<unsigned char>(storage.bytes[selected]);
            if constexpr (Op == ItemOp::Set) {
                storage.bytes[selected] = byte;
                ++storage.version;
            } else if constexpr (Op == ItemOp::Delete) {
                if (storage.exports.load()) {
                    exported = true;
                    return true;
                }
                storage.bytes.erase(selected, 1);
                ++storage.version;
            }
            return true;
        });
        if (exported) {
            raise_error(c, CoreObject::BufferErrorType,
                        U"Existing exports of data: object cannot be re-sized", out);
            return;
        }
        if (!valid) {
            raise_error(c, CoreObject::IndexErrorType, U"bytearray index out of range", out);
            return;
        }
        if constexpr (Op == ItemOp::Get || Op == ItemOp::Pop) {
            make_int(c, value, out);
            if constexpr (Op == ItemOp::Pop) {
                // Prepare the result before committing; popping the tail stays O(1).
                if (!locked(c, storage, [&] {
                        if (storage.version != version)
                            return false;
                        if (storage.exports.load()) {
                            exported = true;
                            return true;
                        }
                        storage.bytes.erase(selected, 1);
                        ++storage.version;
                        return true;
                    })) {
                    c.poll();
                    continue;
                }
                if (exported)
                    raise_error(c, CoreObject::BufferErrorType,
                                U"Existing exports of data: object cannot be re-sized", out);
            }
            return;
        } else {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
    }
}
void append(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    char b;
    if (!byte_value(c, roots.root(args[1]), b, out))
        return;
    if (!append_data(c, self, std::string_view(&b, 1), out))
        return;
    out.ok(c.builtin(CoreObject::None));
}
void extend(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string data;
    if (!iterable(c, roots.root(args[1]), data, out))
        return;
    if (!append_data(c, self, data, out))
        return;
    out.ok(c.builtin(CoreObject::None));
}
void insert(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t pos;
    char b;
    if (!integer(c, roots.root(args[1]), pos, out, CoreObject::OverflowErrorType, true) ||
        !byte_value(c, roots.root(args[2]), b, out))
        return;
    if (!mutate(
            c, self,
            [&](std::string &data) {
                auto at = pos;
                if (at < 0)
                    at = std::max<std::int64_t>(0, at + static_cast<std::int64_t>(data.size()));
                data.insert(std::min<std::size_t>(static_cast<std::size_t>(at), data.size()), 1, b);
            },
            out))
        return;
    out.ok(c.builtin(CoreObject::None));
}
void remove(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    char b;
    if (!byte_value(c, roots.root(args[1]), b, out))
        return;
    for (;;) {
        auto old = snapshot(c, self);
        auto data = old.data;
        auto pos = data.find(b);
        if (pos == std::string::npos) {
            raise_error(c, CoreObject::ValueErrorType, U"value not found in bytearray", out);
            return;
        }
        data.erase(pos, 1);
        auto result = commit(c, self, old, data);
        if (result == Commit::Exported) {
            raise_error(c, CoreObject::BufferErrorType,
                        U"Existing exports of data: object cannot be re-sized", out);
            return;
        }
        if (result == Commit::Done) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
        c.poll();
    }
}
template <int Operation>
void simple(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if constexpr (Operation == 2)
        make_bytearray(c, snapshot(c, self).data, out);
    else if constexpr (Operation == 0) {
        auto &storage = *c.payload<ByteArrayObject>(self).contents;
        std::string old;
        bool exported = locked(c, storage, [&] {
            if (!storage.bytes.empty() && storage.exports.load())
                return true;
            old.swap(storage.bytes);
            ++storage.version;
            c.account_external(self,
                               sizeof(ByteArrayObject::Storage) + storage.bytes.capacity() + 1);
            return false;
        });
        if (exported) {
            raise_error(c, CoreObject::BufferErrorType,
                        U"Existing exports of data: object cannot be re-sized", out);
            return;
        }
        out.ok(c.builtin(CoreObject::None));
    } else {
        if (!mutate(
                c, self,
                [](std::string &data) {
                    if constexpr (Operation == 0)
                        data.clear();
                    else
                        std::reverse(data.begin(), data.end());
                },
                out))
            return;
        out.ok(c.builtin(CoreObject::None));
    }
}
template <bool Inplace>
void add(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string other;
    if (!buffer(c, roots.root(args[1]), other, out)) {
        if (failed(out))
            return;
        raise_error(c, CoreObject::TypeErrorType,
                    U"can't concat non-bytes-like object to bytearray", out);
        return;
    }
    if constexpr (Inplace) {
        if (!append_data(c, self, other, out))
            return;
        out.ok(self.get(c));
    } else {
        auto data = snapshot(c, self).data;
        data += other;
        make_bytearray(c, std::move(data), out);
    }
}
template <bool Inplace>
void repeat(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t n;
    if (!integer(c, roots.root(args[1]), n, out))
        return;
    auto operation = [&](std::string &data) {
        if (n <= 0) {
            data.clear();
            return;
        }
        auto source = data;
        if (!source.empty() && static_cast<std::uint64_t>(n) > data.max_size() / source.size())
            throw std::bad_alloc();
        data.reserve(source.size() * static_cast<std::size_t>(n));
        for (std::int64_t i = 1; i < n && !source.empty(); ++i)
            data += source;
    };
    if constexpr (Inplace) {
        if (!mutate(c, self, operation, out))
            return;
        out.ok(self.get(c));
    } else {
        auto data = snapshot(c, self).data;
        operation(data);
        make_bytearray(c, std::move(data), out);
    }
}
void contains(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), value = roots.root(args[1]);
    std::string needle;
    if (!buffer(c, value, needle, out)) {
        if (failed(out))
            return;
        char b;
        if (!byte_value(c, value, b, out))
            return;
        needle += b;
    }
    bool found = snapshot(c, self).data.find(needle) != std::string::npos;
    out.ok(c.builtin(found ? CoreObject::True : CoreObject::False));
}
template <CompareOp Op>
void compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string other;
    if (!buffer(c, roots.root(args[1]), other, out)) {
        if (failed(out))
            return;
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    auto order = snapshot(c, self).data.compare(other);
    bool result = Op == CompareOp::Eq   ? order == 0
                  : Op == CompareOp::Ne ? order != 0
                  : Op == CompareOp::Lt ? order < 0
                  : Op == CompareOp::Le ? order <= 0
                  : Op == CompareOp::Gt ? order > 0
                                        : order >= 0;
    out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
}
void iterator(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    c.allocate_instance<SequenceIteratorObject>(
        roots.root(c.builtin(CoreObject::ByteArrayIteratorType)), out, args[0]);
}
void iterator_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<SequenceIteratorObject>(args[0]) ||
        c.type_of(args[0]) != c.builtin(CoreObject::ByteArrayIteratorType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected bytearray iterator", out);
        return;
    }
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &cursor = c.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(c), current(c);
    for (;;) {
        c.load(self.get(c), cursor.owner, owner);
        if (owner.tag() == ResultTag::Empty) {
            detail::stop_iteration(c, out);
            return;
        }
        auto h = roots.root(owner.value());
        auto &s = *c.payload<ByteArrayObject>(h).contents;
        unsigned char byte = 0;
        bool done = locked(c, s, [&] {
            c.load(self.get(c), cursor.owner, current);
            if (current.tag() == ResultTag::Empty)
                return true;
            auto pos = cursor.index.load(std::memory_order_relaxed);
            if (pos >= s.bytes.size()) {
                c.store(self.get(c), cursor.owner, {});
                return true;
            }
            byte = static_cast<unsigned char>(s.bytes[pos]);
            cursor.index.store(pos + 1, std::memory_order_relaxed);
            return false;
        });
        if (done)
            detail::stop_iteration(c, out);
        else
            make_int(c, byte, out);
        return;
    }
}
void decode(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), encoding = roots.root(args[1]), errors = roots.root(args[2]);
    if (!text_argument(c, encoding, out) || !text_argument(c, errors, out))
        return;
    ReturnSlot bytes(c);
    make_bytes(c, snapshot(c, self).data, bytes);
    detail::decode_bytes(c, roots.root(bytes.value()), encoding, errors, out);
}
} // namespace

bool is_bytearray(Attachment &c, Value value)
{
    return c.has_layout<ByteArrayObject>(value) &&
           is_subtype(c, c.type_of(value), c.builtin(CoreObject::ByteArrayType));
}
void make_bytearray(Attachment &c, std::string data, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<ByteArrayObject>(roots.root(c.builtin(CoreObject::ByteArrayType)), out,
                                         std::move(data));
}
std::string detail::bytearray_snapshot(Attachment &c, Handle self)
{
    return snapshot(c, self).data;
}
void detail::install_bytearrays(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto install = [&](Handle owner, std::u32string name, SignatureLayout sig, NativeEntry entry,
                       NativeSlotKind kind = NativeSlotKind::Ordinary) {
        install_native_attribute(c, core, owner, std::move(name), std::move(sig), entry, {}, kind);
    };
    auto type = at(CoreObject::ByteArrayType);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto three = pair;
    three.parameters.push_back({U"value2", ParameterKind::PositionalOnly});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install(type, U"__new__", variadic, create);
    ReturnSlot wrapper(c), key(c), default_index(c), encoding(c), errors(c);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(c, dictionary, key.value(), wrapper.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
    name_store(c, dictionary, key.value(), at(CoreObject::None).get(c));
    install(type, U"__init__", variadic, initialize);
    for (auto [name, entry] : {std::pair{U"__repr__", representation},
                               {U"__str__", representation},
                               {U"__len__", size},
                               {U"__iter__", iterator},
                               {U"clear", simple<0>},
                               {U"reverse", simple<1>},
                               {U"copy", simple<2>}})
        install(type, name, unary, entry);
    for (auto [name, entry] : {std::pair{U"__getitem__", item<ItemOp::Get>},
                               {U"__delitem__", item<ItemOp::Delete>},
                               {U"__contains__", contains},
                               {U"append", append},
                               {U"extend", extend},
                               {U"remove", remove},
                               {U"__eq__", compare<CompareOp::Eq>},
                               {U"__ne__", compare<CompareOp::Ne>},
                               {U"__lt__", compare<CompareOp::Lt>},
                               {U"__le__", compare<CompareOp::Le>},
                               {U"__gt__", compare<CompareOp::Gt>},
                               {U"__ge__", compare<CompareOp::Ge>}})
        install(type, name, pair, entry);
    install(type, U"__setitem__", three, item<ItemOp::Set>);
    install(type, U"insert", three, insert);
    install(type, U"__add__", pair, add<false>, NativeSlotKind::Sequence);
    install(type, U"__iadd__", pair, add<true>, NativeSlotKind::Sequence);
    install(type, U"__mul__", pair, repeat<false>, NativeSlotKind::Sequence);
    install(type, U"__rmul__", pair, repeat<false>, NativeSlotKind::Sequence);
    install(type, U"__imul__", pair, repeat<true>, NativeSlotKind::Sequence);
    c.allocate_instance<IntObject>(at(CoreObject::IntType), default_index, -1);
    auto pop = unary;
    pop.parameters.push_back({U"index", ParameterKind::PositionalOnly, default_index.value()});
    install(type, U"pop", pop, item<ItemOp::Pop>);
    c.allocate_instance<StrObject>(at(CoreObject::StrType), encoding, U"utf-8");
    c.allocate_instance<StrObject>(at(CoreObject::StrType), errors, U"strict");
    auto decoded = unary;
    decoded.parameters.push_back(
        {U"encoding", ParameterKind::PositionalOrKeyword, encoding.value()});
    decoded.parameters.push_back({U"errors", ParameterKind::PositionalOrKeyword, errors.value()});
    install(type, U"decode", decoded, decode);
    install(at(CoreObject::ByteArrayIteratorType), U"__iter__", unary, iterator_self);
    install(at(CoreObject::ByteArrayIteratorType), U"__next__", unary, next);
}
} // namespace cxxpython::core
