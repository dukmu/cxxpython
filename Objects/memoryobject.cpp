#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"

#include <bit>
#include <charconv>
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
                 char format, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<MemoryViewObject>(roots.root(c.builtin(CoreObject::MemoryViewType)), out,
                                          owner.get(c), std::move(token), offset, stride, length,
                                          readonly, format);
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
        create_view(c, source, std::make_shared<MemoryViewExport>(nullptr), 0, 1, n, true, 'B',
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
        create_view(c, source, std::move(token), 0, 1, n, false, 'B', out);
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
    data.resize(static_cast<std::size_t>(v.length));
    auto copy = [&](const std::string &source) {
        auto p = v.offset;
        for (std::int64_t i = 0; i < v.length; ++i) {
            data[static_cast<std::size_t>(i)] = source[static_cast<std::size_t>(p)];
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
            if (data.size() != static_cast<std::size_t>(s.length)) {
                raise_error(c, CoreObject::ValueErrorType,
                            U"memoryview assignment: lvalue and rvalue have different structures",
                            out);
                return;
            }
            locked(c, *token->storage, [&] {
                auto p = s.length ? v.offset + s.start * v.stride : v.offset;
                for (std::int64_t i = 0; i < s.length; ++i) {
                    token->storage->bytes[static_cast<std::size_t>(p)] =
                        data[static_cast<std::size_t>(i)];
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
        unsigned char b;
        if (token->storage)
            b = locked(c, *token->storage,
                       [&] { return static_cast<unsigned char>(token->storage->bytes[p]); });
        else {
            ReturnSlot owner(c);
            if (!load_owner(c, self, v, owner, out))
                return;
            b = static_cast<unsigned char>(c.payload<BytesObject>(owner.value()).value[p]);
        }
        if (v.format == 'c')
            make_bytes(c, std::string(1, static_cast<char>(b)), out);
        else
            make_int(c, v.format == 'b' && b >= 128 ? static_cast<int>(b) - 256 : b, out);
    } else {
        std::int64_t b;
        if (v.format == 'c') {
            std::string data;
            if (!detail::buffer_bytes(c, roots.root(args[2]), data, out) || data.size() != 1) {
                if (out.tag() != ResultTag::Raised)
                    raise_error(c, CoreObject::TypeErrorType, U"expected a byte", out);
                return;
            }
            b = static_cast<unsigned char>(data[0]);
        } else if (!integer(c, roots.root(args[2]), b, out))
            return;
        if ((v.format == 'b' && (b < -128 || b > 127)) || (v.format != 'b' && (b < 0 || b > 255))) {
            raise_error(c, CoreObject::ValueErrorType, U"memoryview: invalid value for format",
                        out);
            return;
        }
        locked(c, *token->storage, [&] {
            token->storage->bytes[p] = static_cast<char>(b);
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
        for (unsigned char b : data) {
            ReturnSlot value(c);
            if (format == 'c')
                make_bytes(c, std::string(1, static_cast<char>(b)), value);
            else
                make_int(c, format == 'b' && b >= 128 ? static_cast<int>(b) - 256 : b, value);
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
    if (fmt.size() != 1 || (fmt[0] != U'B' && fmt[0] != U'b' && fmt[0] != U'c')) {
        raise_error(c, CoreObject::ValueErrorType, U"unsupported memoryview format", out);
        return;
    }
    if (v.stride != 1 && v.length > 1) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"memoryview: casts are restricted to C-contiguous views", out);
        return;
    }
    ReturnSlot owner(c);
    if (load_owner(c, self, v, owner, out))
        create_view(c, roots.root(owner.value()), token, v.offset, 1, v.length, v.readonly,
                    static_cast<char>(fmt[0]), out);
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
    char right_format = 'B';
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
    bool equal = data.size() == right.size();
    if (!data.empty() && (left.format == 'c') != (right_format == 'c'))
        equal = false;
    for (std::size_t i = 0; equal && i < data.size(); ++i) {
        auto a = static_cast<unsigned char>(data[i]);
        auto b = static_cast<unsigned char>(right[i]);
        int av = left.format == 'b' && a >= 128 ? static_cast<int>(a) - 256 : a;
        int bv = right_format == 'b' && b >= 128 ? static_cast<int>(b) - 256 : b;
        equal = av == bv;
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
        make_str(c, std::u32string(1, v.format), out);
    else if constexpr (field == Field::Itemsize || field == Field::Ndim)
        make_int(c, 1, out);
    else if constexpr (field == Field::Nbytes)
        make_int(c, v.length, out);
    else if constexpr (field == Field::Readonly)
        out.ok(c.builtin(v.readonly ? CoreObject::True : CoreObject::False));
    else if constexpr (field == Field::Obj) {
        ReturnSlot owner(c);
        if (load_owner(c, self, v, owner, out))
            out.ok(owner.value());
    } else if constexpr (field == Field::C || field == Field::F || field == Field::Contiguous)
        out.ok(c.builtin(v.length <= 1 || v.stride == 1 ? CoreObject::True : CoreObject::False));
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
        if (!v.readonly && token->storage && (v.length <= 1 || v.stride == 1)) {
            if (!load_owner(c, source, v, result.owner, out))
                return false;
            result.offset = static_cast<std::size_t>(v.offset);
            result.length = static_cast<std::size_t>(v.length);
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
        if (contiguous && v.length > 1 && v.stride != 1) {
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
