// Array storage and typecodes follow CPython 3.12.14 Modules/arraymodule.c (PSF license).
#include "internal/arraymodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/abstract.hpp"
#include "internal/long.hpp"
#include "internal/memoryformat.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>

namespace cxxpython::core::detail {
namespace {
using Storage = ByteArrayObject::Storage;

template <class F> decltype(auto) locked(Attachment &c, Storage &storage, F &&fn)
{
    for (;;) {
        std::unique_lock lock(storage.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return fn();
        }
        NativeWait wait(c);
        storage.mutex.lock();
        storage.mutex.unlock();
    }
}

struct Snapshot {
    std::string bytes;
    std::size_t version;
};
Snapshot snapshot(Attachment &c, Handle self)
{
    auto &storage = *c.payload<ArrayObject>(self).contents;
    Snapshot result;
    for (;;) {
        auto size = locked(c, storage, [&] { return storage.bytes.size(); });
        result.bytes.reserve(size);
        if (locked(c, storage, [&] {
                if (result.bytes.capacity() < storage.bytes.size())
                    return false;
                result.bytes.assign(storage.bytes.data(), storage.bytes.size());
                result.version = storage.version;
                return true;
            }))
            return result;
        c.poll();
    }
}
enum class Commit { Retry, Done, Exported };
Commit commit(Attachment &c, Handle self, Snapshot &old, std::string &bytes)
{
    auto &storage = *c.payload<ArrayObject>(self).contents;
    return locked(c, storage, [&] {
        if (storage.version != old.version)
            return Commit::Retry;
        if (storage.bytes.size() != bytes.size() && storage.exports.load())
            return Commit::Exported;
        storage.bytes.swap(bytes);
        ++storage.version;
        c.account_external(self, sizeof(Storage) + storage.bytes.capacity() + 1);
        return Commit::Done;
    });
}
bool append_data(Attachment &c, Handle self, std::string_view extra, ReturnSlot &out)
{
    auto &storage = *c.payload<ArrayObject>(self).contents;
    std::string prepared;
    for (;;) {
        std::size_t needed = 0;
        bool exported = false;
        if (locked(c, storage, [&] {
                if (!extra.empty() && storage.exports.load()) {
                    exported = true;
                    return true;
                }
                if (extra.size() > storage.bytes.max_size() - storage.bytes.size())
                    throw std::bad_alloc();
                needed = storage.bytes.size() + extra.size();
                if (needed > storage.bytes.capacity()) {
                    if (prepared.capacity() < needed) {
                        needed = std::max(needed, storage.bytes.capacity() +
                                                      std::min(storage.bytes.max_size() -
                                                                   storage.bytes.capacity(),
                                                               storage.bytes.capacity() / 2 + 4));
                        return false;
                    }
                    prepared.assign(storage.bytes.data(), storage.bytes.size());
                    storage.bytes.swap(prepared);
                }
                storage.bytes.append(extra.data(), extra.size());
                ++storage.version;
                c.account_external(self, sizeof(Storage) + storage.bytes.capacity() + 1);
                return true;
            })) {
            if (exported) {
                raise_error(c, CoreObject::BufferErrorType,
                            U"cannot resize an array that is exporting buffers", out);
                return false;
            }
            return true;
        }
        prepared.reserve(needed);
        c.poll();
    }
}
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<ArrayObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected array.array", out);
    return false;
}
std::size_t item_size(char code)
{
    if (code == 'u')
        return sizeof(wchar_t);
    if (std::string_view("bBhHiIlLqQfd").find(code) != std::string_view::npos)
        return memory_format_size(code);
    return 0;
}
template <class T>
bool pack_integer(Attachment &c, Handle object, std::string &bytes, ReturnSlot &out)
{
    ReturnSlot indexed(c);
    index(c, object, indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return false;
    }
    const auto value = integer_value(c.payload<IntObject>(indexed.value()));
    if (value < Integer(std::numeric_limits<T>::min()) ||
        value > Integer(std::numeric_limits<T>::max())) {
        raise_error(c, CoreObject::OverflowErrorType, U"array item out of range", out);
        return false;
    }
    const T number = value.convert_to<T>();
    bytes.assign(reinterpret_cast<const char *>(&number), sizeof(number));
    return true;
}
bool pack(Attachment &c, char code, Handle value, std::string &bytes, ReturnSlot &out)
{
    switch (code) {
    case 'b':
        return pack_integer<signed char>(c, value, bytes, out);
    case 'B':
        return pack_integer<unsigned char>(c, value, bytes, out);
    case 'h':
        return pack_integer<short>(c, value, bytes, out);
    case 'H':
        return pack_integer<unsigned short>(c, value, bytes, out);
    case 'i':
        return pack_integer<int>(c, value, bytes, out);
    case 'I':
        return pack_integer<unsigned int>(c, value, bytes, out);
    case 'l':
        return pack_integer<long>(c, value, bytes, out);
    case 'L':
        return pack_integer<unsigned long>(c, value, bytes, out);
    case 'q':
        return pack_integer<long long>(c, value, bytes, out);
    case 'Q':
        return pack_integer<unsigned long long>(c, value, bytes, out);
    case 'u': {
        if (!c.has_layout<StrObject>(value.get(c)) ||
            c.payload<StrObject>(value).value.size() != 1) {
            raise_error(c, CoreObject::TypeErrorType, U"array item must be a unicode character",
                        out);
            return false;
        }
        const auto ch = c.payload<StrObject>(value).value[0];
        if (static_cast<std::uint32_t>(ch) > static_cast<std::uint32_t>(WCHAR_MAX)) {
            raise_error(c, CoreObject::OverflowErrorType, U"character out of range", out);
            return false;
        }
        const wchar_t wc = static_cast<wchar_t>(ch);
        bytes.assign(reinterpret_cast<const char *>(&wc), sizeof(wc));
        return true;
    }
    default:
        return pack_memory_format(c, code, value, bytes, out);
    }
}
void unpack(Attachment &c, char code, std::string_view bytes, ReturnSlot &out)
{
    if (code != 'u') {
        unpack_memory_format(c, code, bytes, out);
        return;
    }
    wchar_t wc;
    std::memcpy(&wc, bytes.data(), sizeof(wc));
    if (static_cast<std::uint32_t>(wc) > 0x10ffff) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid Unicode character", out);
        return;
    }
    make_str(c, std::u32string(1, static_cast<char32_t>(wc)), out);
}
bool collect(Attachment &c, char code, Handle source, std::string &bytes, ReturnSlot &out)
{
    RootScope roots(c);
    if (c.has_layout<ArrayObject>(source.get(c))) {
        const auto &other = c.payload<ArrayObject>(source);
        if (other.typecode == code) {
            bytes = snapshot(c, source).bytes;
            return true;
        }
    }
    ReturnSlot iterator(c), item(c);
    get_iter(c, source, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return false;
    }
    auto it = roots.root(iterator.value());
    for (;;) {
        auto status = iter_next(c, it, item);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return false;
        }
        RootScope element(c);
        std::string encoded;
        if (!pack(c, code, element.root(item.value()), encoded, out))
            return false;
        bytes += encoded;
    }
}
void create(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(args[0]);
    auto arguments = roots.root(args[1]);
    auto keywords = roots.root(args[2]);
    if (!c.has_layout<TypeObject>(type.get(c)) ||
        c.payload<TypeObject>(type).instance_layout.native_family !=
            LayoutDescriptor::of<ArrayObject>().native_family) {
        raise_error(c, CoreObject::TypeErrorType, U"array.__new__ requires an array subtype", out);
        return;
    }
    const auto &items = c.payload<TupleObject>(arguments).items;
    if (items.empty() || items.size() > 2 || dictionary_size(c, keywords.get(c))) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"array() requires a typecode and optional initializer", out);
        return;
    }
    auto code_value = roots.root(items[0]);
    if (!c.has_layout<StrObject>(code_value.get(c)) ||
        c.payload<StrObject>(code_value).value.size() != 1) {
        raise_error(c, CoreObject::TypeErrorType, U"array() argument 1 must be a unicode character",
                    out);
        return;
    }
    const auto character = c.payload<StrObject>(code_value).value[0];
    const char code = character <= 127 ? static_cast<char>(character) : '\0';
    const auto size = item_size(code);
    if (!size) {
        raise_error(c, CoreObject::ValueErrorType, U"bad typecode", out);
        return;
    }
    std::string data;
    if (items.size() == 2) {
        auto source = roots.root(items[1]);
        if (is_bytes(c, source.get(c)) || is_bytearray(c, source.get(c))) {
            if (!buffer_bytes(c, source, data, out))
                return;
            if (data.size() % size) {
                raise_error(c, CoreObject::ValueErrorType,
                            U"bytes length not a multiple of item size", out);
                return;
            }
        } else if (!collect(c, code, source, data, out))
            return;
    }
    c.allocate_instance<ArrayObject>(type, out, code, size, std::move(data));
}
void initialize(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(c.builtin(CoreObject::None));
}
void length(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &a = c.payload<ArrayObject>(self);
    const auto size = locked(c, *a.contents, [&] { return a.contents->bytes.size() / a.itemsize; });
    make_int(c, size, out);
}
bool index_number(Attachment &c, Handle key, std::int64_t &number, ReturnSlot &out)
{
    ReturnSlot indexed(c);
    index(c, key, indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return false;
    }
    if (!int_fits_signed(c, indexed.value(), number)) {
        raise_error(c, CoreObject::IndexErrorType, U"array index out of range", out);
        return false;
    }
    return true;
}
enum class ItemOp { Get, Set, Delete };
template <ItemOp op> void item(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto key = roots.root(args[1]);
    const auto &a = c.payload<ArrayObject>(self);
    const auto size = a.itemsize;
    const auto code = a.typecode;
    const bool slice = c.type_of(key) == c.builtin(CoreObject::SliceType);
    SliceBounds bounds{};
    std::int64_t number = 0;
    if (slice ? !unpack_slice(c, key, bounds, out) : !index_number(c, key, number, out))
        return;
    std::string replacement;
    if constexpr (op == ItemOp::Set) {
        auto source = roots.root(args[2]);
        if (slice) {
            if (!c.has_layout<ArrayObject>(source.get(c)) ||
                c.payload<ArrayObject>(source).typecode != code) {
                raise_error(c, CoreObject::TypeErrorType, U"can only assign array of same kind",
                            out);
                return;
            }
            replacement = snapshot(c, source).bytes;
        } else if (!pack(c, code, source, replacement, out))
            return;
    }
    for (;;) {
        auto old = snapshot(c, self);
        auto count = old.bytes.size() / size;
        auto selected = slice ? bounds.adjust(count) : SliceIndices{};
        if (!slice) {
            auto pos = number;
            if (pos < 0)
                pos += static_cast<std::int64_t>(count);
            if (pos < 0 || static_cast<std::size_t>(pos) >= count) {
                raise_error(c, CoreObject::IndexErrorType, U"array index out of range", out);
                return;
            }
            selected = {pos, pos + 1, 1, 1};
        }
        if constexpr (op == ItemOp::Get) {
            std::string data;
            auto pos = selected.start;
            for (std::int64_t i = 0; i < selected.length; ++i, pos += selected.step)
                data.append(old.bytes, static_cast<std::size_t>(pos) * size, size);
            if (slice)
                c.allocate_instance<ArrayObject>(roots.root(c.type_of(self.get(c))), out, code,
                                                 size, std::move(data));
            else
                unpack(c, code, data, out);
            return;
        }
        auto data = old.bytes;
        if constexpr (op == ItemOp::Set) {
            if (selected.step == 1) {
                data.replace(static_cast<std::size_t>(selected.start) * size,
                             static_cast<std::size_t>(selected.length) * size, replacement);
            } else {
                if (replacement.size() / size != static_cast<std::size_t>(selected.length)) {
                    raise_error(c, CoreObject::ValueErrorType, U"extended slice size mismatch",
                                out);
                    return;
                }
                auto pos = selected.start;
                for (std::int64_t i = 0; i < selected.length; ++i, pos += selected.step)
                    data.replace(static_cast<std::size_t>(pos) * size, size, replacement,
                                 static_cast<std::size_t>(i) * size, size);
            }
        } else {
            std::string kept;
            kept.reserve(data.size());
            for (std::size_t i = 0; i < count; ++i) {
                auto delta = static_cast<std::int64_t>(i) - selected.start;
                bool remove = selected.length && delta % selected.step == 0 &&
                              delta / selected.step >= 0 && delta / selected.step < selected.length;
                if (!remove)
                    kept.append(data, i * size, size);
            }
            data.swap(kept);
        }
        auto result = commit(c, self, old, data);
        if (result == Commit::Retry) {
            c.poll();
            continue;
        }
        if (result == Commit::Exported) {
            raise_error(c, CoreObject::BufferErrorType,
                        U"cannot resize an array that is exporting buffers", out);
            return;
        }
        out.ok(c.builtin(CoreObject::None));
        return;
    }
}
void append(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string data;
    if (!pack(c, c.payload<ArrayObject>(self).typecode, roots.root(args[1]), data, out))
        return;
    if (append_data(c, self, data, out))
        out.ok(c.builtin(CoreObject::None));
}
void extend(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto source = roots.root(args[1]);
    const char code = c.payload<ArrayObject>(self).typecode;
    if (c.has_layout<ArrayObject>(source.get(c))) {
        if (c.payload<ArrayObject>(source).typecode != code) {
            raise_error(c, CoreObject::TypeErrorType, U"can only extend with array of same kind",
                        out);
            return;
        }
        auto data = snapshot(c, source).bytes;
        if (append_data(c, self, data, out))
            out.ok(c.builtin(CoreObject::None));
        return;
    }
    ReturnSlot iterator(c), item(c);
    get_iter(c, source, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return;
    }
    auto it = roots.root(iterator.value());
    for (;;) {
        const auto status = iter_next(c, it, item);
        if (status == NextStatus::Done) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        RootScope element(c);
        std::string data;
        if (!pack(c, code, element.root(item.value()), data, out) ||
            !append_data(c, self, data, out))
            return;
    }
}
void frombytes(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string data;
    if (!buffer_bytes(c, roots.root(args[1]), data, out)) {
        if (out.tag() != ResultTag::Raised)
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    if (data.size() % c.payload<ArrayObject>(self).itemsize) {
        raise_error(c, CoreObject::ValueErrorType, U"bytes length not a multiple of item size",
                    out);
        return;
    }
    if (append_data(c, self, data, out))
        out.ok(c.builtin(CoreObject::None));
}
void tobytes(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    make_bytes(c, snapshot(c, roots.root(args[0])).bytes, out);
}
void typecode(Attachment &c, Value self, ReturnSlot &out)
{
    if (!receiver(c, self, out))
        return;
    make_str(c, std::u32string(1, c.payload<ArrayObject>(self).typecode), out);
}
void itemsize(Attachment &c, Value self, ReturnSlot &out)
{
    if (!receiver(c, self, out))
        return;
    make_int(c, c.payload<ArrayObject>(self).itemsize, out);
}
void tounicode(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (c.payload<ArrayObject>(self).typecode != 'u') {
        raise_error(c, CoreObject::ValueErrorType,
                    U"tounicode() may only be called on unicode arrays", out);
        return;
    }
    const auto data = snapshot(c, self).bytes;
    std::u32string text;
    for (std::size_t i = 0; i < data.size(); i += sizeof(wchar_t)) {
        wchar_t ch;
        std::memcpy(&ch, data.data() + i, sizeof(ch));
        if (static_cast<std::uint32_t>(ch) > 0x10ffff) {
            raise_error(c, CoreObject::ValueErrorType, U"invalid Unicode character", out);
            return;
        }
        text.push_back(static_cast<char32_t>(ch));
    }
    make_str(c, std::move(text), out);
}

void make_array_list(Attachment &c, Handle self, ReturnSlot &out)
{
    RootScope roots(c);
    const auto &a = c.payload<ArrayObject>(self);
    const auto code = a.typecode;
    const auto size = a.itemsize;
    const auto data = snapshot(c, self).bytes;
    std::vector<Value> values;
    values.reserve(data.size() / size);
    ReturnSlot value(c);
    for (std::size_t pos = 0; pos < data.size(); pos += size) {
        unpack(c, code, std::string_view(data).substr(pos, size), value);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return;
        }
        values.push_back(roots.root(value.value()).get(c));
    }
    make_list(c, values, out);
}
void tolist(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    make_array_list(c, roots.root(args[0]), out);
}
void fromlist(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto source = roots.root(args[1]);
    if (!c.has_layout<ListObject>(source.get(c)) ||
        !is_subtype(c, c.type_of(source), c.builtin(CoreObject::ListType))) {
        raise_error(c, CoreObject::TypeErrorType, U"arg must be list", out);
        return;
    }
    std::vector<Value> values;
    list_snapshot(c, source, roots, values);
    const auto code = c.payload<ArrayObject>(self).typecode;
    std::string data;
    for (auto value : values) {
        RootScope element(c);
        std::string bytes;
        if (!pack(c, code, element.root(value), bytes, out))
            return;
        data += bytes;
    }
    if (append_data(c, self, data, out))
        out.ok(c.builtin(CoreObject::None));
}
void representation(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto code = c.payload<ArrayObject>(self).typecode;
    const auto data = snapshot(c, self).bytes;
    TypeView type(c, c.type_of(self));
    const auto name = type.name();
    std::u32string text = std::holds_alternative<Value>(name)
                              ? c.payload<StrObject>(std::get<Value>(name)).value
                              : std::get<std::u32string>(name);
    text += U"('";
    text += code;
    text += U"'";
    if (!data.empty()) {
        ReturnSlot value(c), rendered(c);
        if (code == 'u') {
            const std::array argv{self.get(c)};
            tounicode(c, c.builtin(CoreObject::None), argv, value);
        } else {
            make_array_list(c, self, value);
        }
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return;
        }
        repr(c, roots.root(value.value()), rendered);
        if (rendered.tag() == ResultTag::Raised) {
            out.raised(rendered.exception());
            return;
        }
        text += U", ";
        text += c.payload<StrObject>(rendered.value()).value;
    }
    text += U")";
    make_str(c, std::move(text), out);
}
template <CompareOp op>
void compare(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (!c.has_layout<ArrayObject>(args[1])) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    const auto left_code = c.payload<ArrayObject>(left).typecode;
    const auto right_code = c.payload<ArrayObject>(right).typecode;
    const auto left_size = c.payload<ArrayObject>(left).itemsize;
    const auto right_size = c.payload<ArrayObject>(right).itemsize;
    const auto a = snapshot(c, left).bytes;
    const auto b = snapshot(c, right).bytes;
    const auto an = a.size() / left_size, bn = b.size() / right_size;
    auto finish = [&](std::size_t x, std::size_t y) {
        const bool result = op == CompareOp::Eq   ? x == y
                            : op == CompareOp::Ne ? x != y
                            : op == CompareOp::Lt ? x < y
                            : op == CompareOp::Le ? x <= y
                            : op == CompareOp::Gt ? x > y
                                                  : x >= y;
        out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
    };
    if constexpr (op == CompareOp::Eq || op == CompareOp::Ne) {
        if (an != bn) {
            finish(an, bn);
            return;
        }
    }
    for (std::size_t i = 0; i < std::min(an, bn); ++i) {
        RootScope pair(c);
        ReturnSlot av(c), bv(c), equal(c);
        unpack(c, left_code, std::string_view(a).substr(i * left_size, left_size), av);
        if (av.tag() == ResultTag::Raised) {
            out.raised(av.exception());
            return;
        }
        auto ar = pair.root(av.value());
        unpack(c, right_code, std::string_view(b).substr(i * right_size, right_size), bv);
        if (bv.tag() == ResultTag::Raised) {
            out.raised(bv.exception());
            return;
        }
        auto br = pair.root(bv.value());
        container_equal_owned(c, ar, br, equal);
        if (equal.tag() == ResultTag::Raised) {
            out.raised(equal.exception());
            return;
        }
        if (equal.value() == c.builtin(CoreObject::True))
            continue;
        if constexpr (op == CompareOp::Eq || op == CompareOp::Ne)
            finish(0, 1);
        else
            rich_compare_owned(c, ar, br, op, out);
        return;
    }
    finish(an, bn);
}
void reduce_ex(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot converted(c), name(c), method(c), state(c), list(c), code(c), arguments(c);
    index(c, roots.root(args[1]), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    std::int64_t protocol;
    if (!int_fits_signed(c, converted.value(), protocol) ||
        protocol < std::numeric_limits<int>::min() || protocol > std::numeric_limits<int>::max()) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return;
    }
    make_str(c, U"__getstate__", name);
    get_attribute(c, self, roots.root(name.value()), method);
    if (method.tag() == ResultTag::Raised) {
        out.raised(method.exception());
        return;
    }
    call(c, roots.root(method.value()), {}, state);
    if (state.tag() == ResultTag::Raised) {
        out.raised(state.exception());
        return;
    }
    make_array_list(c, self, list);
    if (list.tag() == ResultTag::Raised) {
        out.raised(list.exception());
        return;
    }
    const auto list_root = roots.root(list.value());
    make_str(c, std::u32string(1, c.payload<ArrayObject>(self).typecode), code);
    const std::array parameters{code.value(), list_root.get(c)};
    cxxpython::core::make_tuple(c, parameters, arguments);
    const std::array result{c.type_of(self), arguments.value(), state.value()};
    cxxpython::core::make_tuple(c, result, out);
}
} // namespace

void initialize_array(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::Count); ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"array"}, LayoutDescriptor::of<ArrayObject>()};
    spec.type.module = U"array";
    spec.type.immutable = true;
    spec.type.subclassable = true;
    ReturnSlot value(c), wrapper(c), key(c), result(c);
    create_native_type(c, spec, value);
    if (value.tag() == ResultTag::Raised) {
        out.raised(value.exception());
        return;
    }
    auto type = roots.root(value.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto binary = unary;
    binary.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto ternary = binary;
    ternary.parameters.push_back({U"replacement", ParameterKind::PositionalOnly});
    auto constructor = unary;
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", constructor, create);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapper, function.get(c));
    c.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                   U"__new__");
    name_store(c, dictionary, key.value(), wrapper.value());
    install_native_attribute(c, core, type, U"__init__", constructor, initialize);
    install_native_attribute(c, core, type, U"__repr__", unary, representation);
    install_native_attribute(c, core, type, U"__str__", unary, representation);
    install_native_attribute(c, core, type, U"__eq__", binary, compare<CompareOp::Eq>);
    install_native_attribute(c, core, type, U"__ne__", binary, compare<CompareOp::Ne>);
    install_native_attribute(c, core, type, U"__lt__", binary, compare<CompareOp::Lt>);
    install_native_attribute(c, core, type, U"__le__", binary, compare<CompareOp::Le>);
    install_native_attribute(c, core, type, U"__gt__", binary, compare<CompareOp::Gt>);
    install_native_attribute(c, core, type, U"__ge__", binary, compare<CompareOp::Ge>);
    c.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                   U"__hash__");
    name_store(c, dictionary, key.value(), c.builtin(CoreObject::None));
    install_native_attribute(c, core, type, U"__len__", unary, length);
    install_native_attribute(c, core, type, U"__getitem__", binary, item<ItemOp::Get>);
    install_native_attribute(c, core, type, U"__setitem__", ternary, item<ItemOp::Set>);
    install_native_attribute(c, core, type, U"__delitem__", binary, item<ItemOp::Delete>);
    install_native_attribute(c, core, type, U"append", binary, append);
    install_native_attribute(c, core, type, U"extend", binary, extend);
    install_native_attribute(c, core, type, U"frombytes", binary, frombytes);
    install_native_attribute(c, core, type, U"tobytes", unary, tobytes);
    install_native_attribute(c, core, type, U"tounicode", unary, tounicode);
    install_native_attribute(c, core, type, U"tolist", unary, tolist);
    install_native_attribute(c, core, type, U"fromlist", binary, fromlist);
    install_native_attribute(c, core, type, U"__reduce_ex__", binary, reduce_ex);
    install_native_field(c, core, type, U"typecode", typecode);
    install_native_field(c, core, type, U"itemsize", itemsize);
    auto dict = roots.root(module_dict(c, module.get(c)));
    make_str(c, U"array", key);
    dict_store(c, dict, roots.root(key.value()), type, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    make_str(c, U"typecodes", key);
    make_str(c, U"bBuhHiIlLqQfd", value);
    dict_store(c, dict, roots.root(key.value()), roots.root(value.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
