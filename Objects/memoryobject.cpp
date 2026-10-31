#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/memoryformat.hpp"

#include <bit>
#include <charconv>
#include <cstring>
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
bool view(Attachment &c, Value v, ReturnSlot &out)
{
    if (c.has_layout<MemoryViewObject>(v) && c.type_of(v) == c.builtin(CoreObject::MemoryViewType))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected memoryview", out);
    return false;
}
std::shared_ptr<MemoryViewExport> live(Attachment &c, MemoryViewObject &v, ReturnSlot &out)
{
    auto token = std::atomic_load_explicit(&v.exported, std::memory_order_acquire);
    if (!token)
        raise_error(c, CoreObject::ValueErrorType,
                    U"operation forbidden on released memoryview object", out);
    return token;
}
bool load_owner(Attachment &c, Handle self, MemoryViewObject &v, ReturnSlot &owner, ReturnSlot &out)
{
    c.load(self.get(c), v.owner, owner);
    if (owner.tag() != ResultTag::Empty)
        return true;
    raise_error(c, CoreObject::ValueErrorType, U"operation forbidden on released memoryview object",
                out);
    return false;
}
bool integer(Attachment &c, Handle h, std::int64_t &n, ReturnSlot &out)
{
    ReturnSlot value(c);
    index(c, h, value);
    if (value.tag() == ResultTag::Raised) {
        out.raised(value.exception());
        return false;
    }
    if (!int_fits_signed(c, value.value(), n)) {
        raise_error(c, CoreObject::IndexErrorType, U"index out of range", out);
        return false;
    }
    return true;
}
void create_view(Attachment &c, Handle owner, std::shared_ptr<MemoryViewExport> token,
                 std::int64_t offset, std::int64_t stride, std::int64_t length, bool readonly,
                 std::string format, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<MemoryViewObject>(roots.root(c.builtin(CoreObject::MemoryViewType)), out,
                                          owner.get(c), std::move(token), offset, stride, length,
                                          readonly, std::move(format));
}
void create(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto &pos = c.payload<TupleObject>(args[1]).items;
    DictView keywords(c, args[2]);
    if (pos.size() != 1 || !keywords.entries().empty()) {
        raise_error(c, CoreObject::TypeErrorType, U"memoryview requires one argument", out);
        return;
    }
    auto source = roots.root(pos[0]);
    if (c.has_layout<MemoryViewObject>(source.get(c)) &&
        c.type_of(source) == c.builtin(CoreObject::MemoryViewType)) {
        auto &v = c.payload<MemoryViewObject>(source);
        auto token = live(c, v, out);
        ReturnSlot owner(c);
        if (token && load_owner(c, source, v, owner, out))
            create_view(c, roots.root(owner.value()), token, v.offset, v.stride, v.length,
                        v.readonly, v.format, out);
        return;
    }
    if (is_bytes(c, source.get(c))) {
        auto n = c.payload<BytesObject>(source).value.size();
        create_view(c, source, std::make_shared<MemoryViewExport>(nullptr), 0, 1, n, true, "B",
                    out);
        return;
    }
    if (is_bytearray(c, source.get(c))) {
        auto storage = c.payload<ByteArrayObject>(source).contents;
        auto token = std::make_shared<MemoryViewExport>(nullptr);
        std::size_t n = locked(c, *storage, [&] {
            token->storage = storage;
            ++storage->exports;
            return storage->bytes.size();
        });
        create_view(c, source, std::move(token), 0, 1, n, false, "B", out);
        return;
    }
    raise_error(c, CoreObject::TypeErrorType, U"memoryview: a bytes-like object is required", out);
}
bool snapshot(Attachment &c, Handle self, std::string &data, ReturnSlot &out)
{
    auto &v = c.payload<MemoryViewObject>(self);
    auto token = live(c, v, out);
    if (!token)
        return false;
    const auto itemsize = detail::memory_format_size(v.format.back());
    data.resize(static_cast<std::size_t>(v.length) * itemsize);
    auto copy = [&](const std::string &source) {
        auto p = v.offset;
        for (std::int64_t i = 0; i < v.length; ++i) {
            std::memcpy(data.data() + static_cast<std::size_t>(i) * itemsize, source.data() + p,
                        itemsize);
            if (i + 1 < v.length)
                p += v.stride;
        }
    };
    if (token->storage)
        locked(c, *token->storage, [&] { copy(token->storage->bytes); });
    else {
        ReturnSlot owner(c);
        if (!load_owner(c, self, v, owner, out))
            return false;
        copy(c.payload<BytesObject>(owner.value()).value);
    }
    return true;
}
void memory_length(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    auto &v = c.payload<MemoryViewObject>(args[0]);
    if (live(c, v, out))
        make_int(c, v.length, out);
}
enum class Op { Get, Set };
template <Op op> void item(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), key = roots.root(args[1]);
    auto &v = c.payload<MemoryViewObject>(self);
    auto token = live(c, v, out);
    if (!token)
        return;
    if constexpr (op == Op::Set) {
        if (v.readonly) {
            raise_error(c, CoreObject::TypeErrorType, U"cannot modify read-only memory", out);
            return;
        }
    }
    if (c.type_of(key) == c.builtin(CoreObject::SliceType)) {
        detail::SliceBounds bounds;
        if (!detail::unpack_slice(c, key, bounds, out))
            return;
        auto s = bounds.adjust(static_cast<std::size_t>(v.length));
        if constexpr (op == Op::Get) {
            std::int64_t stride;
            if (__builtin_mul_overflow(v.stride, s.step, &stride))
                stride = (v.stride < 0) != (s.step < 0) ? std::numeric_limits<std::int64_t>::min()
                                                        : std::numeric_limits<std::int64_t>::max();
            auto offset = s.length ? v.offset + s.start * v.stride : v.offset;
            ReturnSlot owner(c);
            if (load_owner(c, self, v, owner, out))
                create_view(c, roots.root(owner.value()), token, offset, stride, s.length,
                            v.readonly, v.format, out);
        } else {
            std::string data;
            if (!detail::buffer_bytes(c, roots.root(args[2]), data, out, false)) {
                if (out.tag() != ResultTag::Raised)
                    raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required",
                                out);
                return;
            }
            const auto size = detail::memory_format_size(v.format.back());
            const auto rhs = roots.root(args[2]);
            std::string format = "B";
            if (c.has_layout<MemoryViewObject>(rhs.get(c)))
                format = c.payload<MemoryViewObject>(rhs).format;
            auto normalized = [](std::string_view f) {
                return f.starts_with('@') ? f.substr(1) : f;
            };
            if (data.size() != static_cast<std::size_t>(s.length) * size ||
                normalized(format) != normalized(v.format)) {
                raise_error(c, CoreObject::ValueErrorType,
                            U"memoryview assignment: lvalue and rvalue have different structures",
                            out);
                return;
            }
            locked(c, *token->storage, [&] {
                auto p = s.length ? v.offset + s.start * v.stride : v.offset;
                for (std::int64_t i = 0; i < s.length; ++i) {
                    std::memcpy(token->storage->bytes.data() + p,
                                data.data() + static_cast<std::size_t>(i) * size, size);
                    if (i + 1 < s.length)
                        p += s.step * v.stride;
                }
                ++token->storage->version;
            });
            out.ok(c.builtin(CoreObject::None));
        }
        return;
    }
    std::int64_t n;
    if (!integer(c, key, n, out))
        return;
    if (n < 0)
        n += v.length;
    if (n < 0 || n >= v.length) {
        raise_error(c, CoreObject::IndexErrorType, U"index out of bounds on dimension 1", out);
        return;
    }
    auto p = static_cast<std::size_t>(v.offset + n * v.stride);
    if constexpr (op == Op::Get) {
        const auto size = detail::memory_format_size(v.format.back());
        std::string data(size, '\0');
        if (token->storage)
            locked(c, *token->storage,
                   [&] { std::memcpy(data.data(), token->storage->bytes.data() + p, size); });
        else {
            ReturnSlot owner(c);
            if (!load_owner(c, self, v, owner, out))
                return;
            std::memcpy(data.data(), c.payload<BytesObject>(owner.value()).value.data() + p, size);
        }
        detail::unpack_memory_format(c, v.format.back(), data, out);
    } else {
        std::string data;
        if (!detail::pack_memory_format(c, v.format.back(), roots.root(args[2]), data, out))
            return;
        locked(c, *token->storage, [&] {
            std::memcpy(token->storage->bytes.data() + p, data.data(), data.size());
            ++token->storage->version;
        });
        out.ok(c.builtin(CoreObject::None));
    }
}
void release(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &v = c.payload<MemoryViewObject>(self);
    c.store(self.get(c), v.owner, {});
    std::atomic_exchange_explicit(&v.exported, std::shared_ptr<MemoryViewExport>{},
                                  std::memory_order_acq_rel);
    out.ok(c.builtin(CoreObject::None));
}
void enter(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (view(c, args[0], out) && live(c, c.payload<MemoryViewObject>(args[0]), out))
        out.ok(args[0]);
}
void readonly_view(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &v = c.payload<MemoryViewObject>(self);
    auto token = live(c, v, out);
    ReturnSlot owner(c);
    if (token && load_owner(c, self, v, owner, out))
        create_view(c, roots.root(owner.value()), token, v.offset, v.stride, v.length, true,
                    v.format, out);
}
template <int kind>
void convert(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string data;
    if (!snapshot(c, self, data, out))
        return;
    if constexpr (kind == 0)
        make_bytes(c, std::move(data), out);
    else if constexpr (kind == 1) {
        auto format = c.payload<MemoryViewObject>(self).format;
        std::vector<Value> values;
        const auto size = detail::memory_format_size(format.back());
        for (std::size_t offset = 0; offset < data.size(); offset += size) {
            ReturnSlot value(c);
            detail::unpack_memory_format(c, format.back(),
                                         std::string_view(data).substr(offset, size), value);
            values.push_back(roots.root(value.value()).get(c));
        }
        make_list(c, values, out);
    } else {
        constexpr char hex[] = "0123456789abcdef";
        std::u32string result;
        for (unsigned char b : data) {
            result += hex[b >> 4];
            result += hex[b & 15];
        }
        make_str(c, std::move(result), out);
    }
}
void cast(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &v = c.payload<MemoryViewObject>(self);
    auto token = live(c, v, out);
    if (!token)
        return;
    if (!c.has_layout<StrObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"cast() argument 'format' must be str", out);
        return;
    }
    const auto &fmt = c.payload<StrObject>(args[1]).value;
    if ((fmt.size() != 1 && !(fmt.size() == 2 && fmt[0] == '@')) || fmt.back() > 127 ||
        !detail::memory_format_size(static_cast<char>(fmt.back()))) {
        raise_error(c, CoreObject::ValueErrorType, U"unsupported memoryview format", out);
        return;
    }
    const auto oldsize = detail::memory_format_size(v.format.back());
    const auto size = detail::memory_format_size(static_cast<char>(fmt.back()));
    if (v.stride != static_cast<std::int64_t>(oldsize) && v.length > 1) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"memoryview: casts are restricted to C-contiguous views", out);
        return;
    }
    auto byte_format = [](char format) { return format == 'b' || format == 'B' || format == 'c'; };
    if (!byte_format(v.format.back()) && !byte_format(static_cast<char>(fmt.back()))) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"memoryview: cannot cast between two non-byte formats", out);
        return;
    }
    const auto bytes = static_cast<std::size_t>(v.length) * oldsize;
    if (bytes % size) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"memoryview: length is not a multiple of itemsize", out);
        return;
    }
    ReturnSlot owner(c);
    if (load_owner(c, self, v, owner, out))
        create_view(c, roots.root(owner.value()), token, v.offset, size, bytes / size, v.readonly,
                    std::string(fmt.begin(), fmt.end()), out);
}
template <bool negate>
void compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), other = roots.root(args[1]);
    auto &left = c.payload<MemoryViewObject>(self);
    auto left_token = std::atomic_load_explicit(&left.exported, std::memory_order_acquire);
    if (!left_token) {
        bool equal = args[0] == args[1];
        out.ok(c.builtin((equal != negate) ? CoreObject::True : CoreObject::False));
        return;
    }
    std::string right_format = "B";
    std::string right;
    if (c.has_layout<MemoryViewObject>(other.get(c)) &&
        c.type_of(other) == c.builtin(CoreObject::MemoryViewType)) {
        auto &v = c.payload<MemoryViewObject>(other);
        if (!std::atomic_load_explicit(&v.exported, std::memory_order_acquire)) {
            out.ok(c.builtin(negate ? CoreObject::True : CoreObject::False));
            return;
        }
        right_format = v.format;
        if (!snapshot(c, other, right, out))
            return;
    } else if (!detail::buffer_bytes(c, other, right, out, false)) {
        if (out.tag() != ResultTag::Raised)
            out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    std::string data;
    if (!snapshot(c, self, data, out))
        return;
    const auto a_size = detail::memory_format_size(left.format.back());
    const auto b_size = detail::memory_format_size(right_format.back());
    bool equal = data.size() / a_size == right.size() / b_size;
    for (std::size_t i = 0; equal && i < data.size() / a_size; ++i) {
        RootScope iteration(c);
        ReturnSlot a(c), b(c), result(c);
        detail::unpack_memory_format(c, left.format.back(),
                                     std::string_view(data).substr(i * a_size, a_size), a);
        detail::unpack_memory_format(c, right_format.back(),
                                     std::string_view(right).substr(i * b_size, b_size), b);
        container_equal(c, iteration.root(a.value()), iteration.root(b.value()), result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
        equal = result.value() == c.builtin(CoreObject::True);
    }
    out.ok(c.builtin((equal != negate) ? CoreObject::True : CoreObject::False));
}
void hash_value(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &v = c.payload<MemoryViewObject>(self);
    if (!live(c, v, out))
        return;
    if (!v.readonly) {
        raise_error(c, CoreObject::ValueErrorType, U"cannot hash writable memoryview object", out);
        return;
    }
    if (v.format.back() != 'b' && v.format.back() != 'B' && v.format.back() != 'c') {
        raise_error(c, CoreObject::ValueErrorType,
                    U"memoryview: hashing is restricted to formats 'B', 'b' or 'c'", out);
        return;
    }
    ReturnSlot owner(c), owner_hash(c);
    if (!load_owner(c, self, v, owner, out))
        return;
    hash(c, roots.root(owner.value()), owner_hash);
    if (owner_hash.tag() == ResultTag::Raised) {
        out.raised(owner_hash.exception());
        return;
    }
    std::string data;
    if (!snapshot(c, self, data, out))
        return;
    const auto word = data.empty()
                          ? std::uint64_t{0}
                          : c.keyed_hash(std::as_bytes(std::span(data.data(), data.size())));
    auto value = std::bit_cast<std::int64_t>(word);
    make_int(c, value == -1 ? -2 : value, out);
}
void representation(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out))
        return;
    char buffer[2 * sizeof(std::uintptr_t) + 1];
    auto result = std::to_chars(std::begin(buffer), std::end(buffer),
                                reinterpret_cast<std::uintptr_t>(args[0].object()), 16);
    std::u32string text = std::atomic_load_explicit(&c.payload<MemoryViewObject>(args[0]).exported,
                                                    std::memory_order_acquire)
                              ? U"<memory at 0x"
                              : U"<released memory at 0x";
    for (auto p = buffer; p != result.ptr; ++p)
        text += static_cast<char32_t>(*p);
    text += U'>';
    make_str(c, std::move(text), out);
}
void iter(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!view(c, args[0], out) || !live(c, c.payload<MemoryViewObject>(args[0]), out))
        return;
    RootScope roots(c);
    c.allocate_instance<SequenceIteratorObject>(
        roots.root(c.builtin(CoreObject::MemoryViewIteratorType)), out, args[0]);
}
void iter_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<SequenceIteratorObject>(args[0]) ||
        c.type_of(args[0]) != c.builtin(CoreObject::MemoryViewIteratorType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected memoryview iterator", out);
        return;
    }
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &cursor = c.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(c);
    c.load(self.get(c), cursor.owner, owner);
    if (owner.tag() == ResultTag::Empty) {
        detail::stop_iteration(c, out);
        return;
    }
    auto h = roots.root(owner.value());
    auto &v = c.payload<MemoryViewObject>(h);
    if (!live(c, v, out))
        return;
    auto n = cursor.index.fetch_add(1);
    if (n >= static_cast<std::size_t>(v.length)) {
        detail::stop_iteration(c, out);
        return;
    }
    ReturnSlot index_value(c);
    make_int(c, n, index_value);
    std::array<Value, 2> values{h.get(c), index_value.value()};
    item<Op::Get>(c, self, values, out);
}
enum class Field {
    Format,
    Itemsize,
    Ndim,
    Nbytes,
    Readonly,
    Obj,
    C,
    F,
    Contiguous,
    Shape,
    Strides,
    Suboffsets
};
template <Field field> void attribute(Attachment &c, Value value, ReturnSlot &out)
{
    if (!view(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &v = c.payload<MemoryViewObject>(self);
    if (!live(c, v, out))
        return;
    if constexpr (field == Field::Format)
        make_str(c, std::u32string(v.format.begin(), v.format.end()), out);
    else if constexpr (field == Field::Itemsize)
        make_int(c, detail::memory_format_size(v.format.back()), out);
    else if constexpr (field == Field::Ndim)
        make_int(c, 1, out);
    else if constexpr (field == Field::Nbytes)
        make_int(c, v.length * detail::memory_format_size(v.format.back()), out);
    else if constexpr (field == Field::Readonly)
        out.ok(c.builtin(v.readonly ? CoreObject::True : CoreObject::False));
    else if constexpr (field == Field::Obj) {
        ReturnSlot owner(c);
        if (load_owner(c, self, v, owner, out))
            out.ok(owner.value());
    } else if constexpr (field == Field::C || field == Field::F || field == Field::Contiguous)
        out.ok(
            c.builtin(v.length <= 1 || v.stride == static_cast<std::int64_t>(
                                                       detail::memory_format_size(v.format.back()))
                          ? CoreObject::True
                          : CoreObject::False));
    else if constexpr (field == Field::Suboffsets)
        cxxpython::core::make_tuple(c, {}, out);
    else {
        ReturnSlot element(c);
        make_int(c, field == Field::Shape ? v.length : v.stride, element);
        std::array<Value, 1> values{element.value()};
        cxxpython::core::make_tuple(c, values, out);
    }
}
} // namespace

bool detail::writable_buffer(Attachment &c, Handle source, WritableBuffer &result, ReturnSlot &out)
{
    if (is_bytearray(c, source.get(c))) {
        result.owner.ok(source.get(c));
        auto storage = c.payload<ByteArrayObject>(source).contents;
        auto token = std::make_shared<MemoryViewExport>(nullptr);
        result.length = locked(c, *storage, [&] {
            token->storage = storage;
            ++storage->exports;
            return storage->bytes.size();
        });
        result.offset = 0;
        result.lease = std::move(token);
        return true;
    }
    if (c.has_layout<MemoryViewObject>(source.get(c))) {
        auto &v = c.payload<MemoryViewObject>(source);
        auto token = live(c, v, out);
        if (!token)
            return false;
        if (!v.readonly && token->storage &&
            (v.length <= 1 ||
             v.stride == static_cast<std::int64_t>(memory_format_size(v.format.back())))) {
            if (!load_owner(c, source, v, result.owner, out))
                return false;
            result.offset = static_cast<std::size_t>(v.offset);
            result.length =
                static_cast<std::size_t>(v.length) * memory_format_size(v.format.back());
            result.lease = std::move(token);
            return true;
        }
    }
    raise_error(c, CoreObject::TypeErrorType, U"a contiguous writable buffer is required", out);
    return false;
}
void detail::write_buffer(Attachment &c, const WritableBuffer &target, std::string_view data)
{
    if (!target.lease || data.size() > target.length)
        throw std::logic_error("write exceeds the native buffer export");
    auto &storage = *target.lease->storage;
    locked(c, storage, [&] {
        std::copy(data.begin(), data.end(), storage.bytes.begin() + target.offset);
        ++storage.version;
    });
}
bool detail::buffer_bytes(Attachment &c, Handle source, std::string &out, ReturnSlot &error,
                          bool contiguous)
{
    if (is_bytes(c, source.get(c))) {
        out = c.payload<BytesObject>(source).value;
        return true;
    }
    if (is_bytearray(c, source.get(c))) {
        out = bytearray_snapshot(c, source);
        return true;
    }
    if (c.has_layout<MemoryViewObject>(source.get(c)) &&
        c.type_of(source) == c.builtin(CoreObject::MemoryViewType)) {
        auto &v = c.payload<MemoryViewObject>(source);
        if (!live(c, v, error))
            return false;
        if (contiguous && v.length > 1 &&
            v.stride != static_cast<std::int64_t>(memory_format_size(v.format.back()))) {
            raise_error(c, CoreObject::BufferErrorType, U"underlying buffer is not C-contiguous",
                        error);
            return false;
        }
        return snapshot(c, source, out, error);
    }
    return false;
}

void detail::install_memoryviews(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto type = at(CoreObject::MemoryViewType);
    auto install = [&](Handle owner, std::u32string name, SignatureLayout signature,
                       NativeEntry entry) {
        install_native_attribute(c, core, owner, std::move(name), std::move(signature), entry);
    };
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
    ReturnSlot wrapper(c), key(c);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(c, dictionary, key.value(), wrapper.value());
    for (auto [name, entry] : {std::pair{U"__len__", memory_length},
                               {U"__repr__", representation},
                               {U"__hash__", hash_value},
                               {U"__iter__", iter},
                               {U"__enter__", enter},
                               {U"release", release},
                               {U"toreadonly", readonly_view},
                               {U"tobytes", convert<0>},
                               {U"tolist", convert<1>},
                               {U"hex", convert<2>}})
        install(type, name, unary, entry);
    install(type, U"__getitem__", pair, item<Op::Get>);
    install(type, U"__eq__", pair, compare<false>);
    install(type, U"__ne__", pair, compare<true>);
    install(type, U"__setitem__", three, item<Op::Set>);
    install(type, U"cast", pair, cast);
    auto exit = three;
    exit.parameters.push_back({U"traceback", ParameterKind::PositionalOnly});
    install(type, U"__exit__", exit, release);
    install(at(CoreObject::MemoryViewIteratorType), U"__iter__", unary, iter_self);
    install(at(CoreObject::MemoryViewIteratorType), U"__next__", unary, next);
    install_native_field(c, core, type, U"format", attribute<Field::Format>);
    install_native_field(c, core, type, U"itemsize", attribute<Field::Itemsize>);
    install_native_field(c, core, type, U"ndim", attribute<Field::Ndim>);
    install_native_field(c, core, type, U"nbytes", attribute<Field::Nbytes>);
    install_native_field(c, core, type, U"readonly", attribute<Field::Readonly>);
    install_native_field(c, core, type, U"obj", attribute<Field::Obj>);
    install_native_field(c, core, type, U"c_contiguous", attribute<Field::C>);
    install_native_field(c, core, type, U"f_contiguous", attribute<Field::F>);
    install_native_field(c, core, type, U"contiguous", attribute<Field::Contiguous>);
    install_native_field(c, core, type, U"shape", attribute<Field::Shape>);
    install_native_field(c, core, type, U"strides", attribute<Field::Strides>);
    install_native_field(c, core, type, U"suboffsets", attribute<Field::Suboffsets>);
}
} // namespace cxxpython::core
