#include "internal/memoryio.hpp"

#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/type.hpp"
#include "internal/abstract.hpp"

#include <algorithm>
#include <limits>

namespace cxxpython::core {
namespace {
template <class F> auto locked(Attachment &c, BytesIOObject &stream, F &&function)
{
    for (;;) {
        std::unique_lock lock(stream.storage->mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return function();
        }
        NativeWait wait(c);
        stream.storage->mutex.lock();
        stream.storage->mutex.unlock();
    }
}

bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<BytesIOObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected BytesIO object", out);
    return false;
}

void exported_error(Attachment &c, ReturnSlot &out)
{
    raise_error(c, CoreObject::BufferErrorType,
                U"Existing exports of data: object cannot be re-sized", out);
}

bool input_bytes(Attachment &c, Handle source, std::string &data, ReturnSlot &out)
{
    if (detail::buffer_bytes(c, source, data, out))
        return true;
    if (out.tag() != ResultTag::Raised)
        raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
    return false;
}

void create(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::BytesIOType))) {
        raise_error(c, CoreObject::TypeErrorType, U"BytesIO.__new__ requires a subtype", out);
        return;
    }
    ReturnSlot buffer(c);
    make_bytearray(c, {}, buffer);
    if (buffer.tag() == ResultTag::Raised) {
        out.raised(buffer.exception());
        return;
    }
    auto owner = roots.root(buffer.value());
    auto storage = c.payload<ByteArrayObject>(owner).contents;
    c.allocate_instance<BytesIOObject>(roots.root(args[0]), out, owner.get(c), std::move(storage));
}

void initialize(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::string data;
    if (args[1] != c.builtin(CoreObject::None) && !input_bytes(c, roots.root(args[1]), data, out))
        return;
    auto &s = c.payload<BytesIOObject>(self);
    ReturnSlot owner(c);
    c.load(self.get(c), s.buffer, owner);
    auto buffer = roots.root(owner.value());
    bool exported = locked(c, s, [&] {
        if (s.storage->exports.load())
            return true;
        s.storage->bytes.swap(data);
        ++s.storage->version;
        c.account_external(buffer,
                           sizeof(ByteArrayObject::Storage) + s.storage->bytes.capacity() + 1);
        s.position = 0;
        s.closed = false;
        return false;
    });
    if (exported)
        exported_error(c, out);
    else
        out.ok(c.builtin(CoreObject::None));
}

enum class ReadKind { Read, Line, Value };
template <ReadKind kind>
void read(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t size = -1;
    if constexpr (kind != ReadKind::Value) {
        if (args[1] != c.builtin(CoreObject::None) &&
            !detail::index_as_int64(c, args[1], size, out))
            return;
    }
    auto &s = c.payload<BytesIOObject>(self);
    std::string data;
    std::size_t needed = 0;
    int status;
    for (;;) {
        status = locked(c, s, [&] {
            if (s.closed)
                return 1;
            auto &bytes = s.storage->bytes;
            if constexpr (kind == ReadKind::Value) {
                needed = bytes.size();
                if (data.capacity() < needed)
                    return 2;
                data.assign(bytes.data(), needed);
            } else {
                auto start =
                    std::min<std::size_t>(static_cast<std::size_t>(s.position), bytes.size());
                auto count = bytes.size() - start;
                if (size >= 0)
                    count = std::min(count, static_cast<std::size_t>(size));
                if constexpr (kind == ReadKind::Line) {
                    auto end = bytes.find('\n', start);
                    if (end != std::string::npos)
                        count = std::min(count, end - start + 1);
                }
                needed = count;
                if (data.capacity() < needed)
                    return 2;
                data.assign(bytes.data() + start, count);
                s.position += static_cast<std::int64_t>(count);
            }
            return 0;
        });
        if (status != 2)
            break;
        data.reserve(needed);
        c.poll();
    }
    if (status == 1)
        detail::io_closed_error(c, out);
    else
        make_bytes(c, std::move(data), out);
}

void write(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &s = c.payload<BytesIOObject>(self);
    if (locked(c, s, [&] { return s.closed; })) {
        detail::io_closed_error(c, out);
        return;
    }
    std::string data;
    if (!input_bytes(c, roots.root(args[1]), data, out))
        return;
    ReturnSlot owner(c);
    c.load(self.get(c), s.buffer, owner);
    auto buffer = roots.root(owner.value());
    std::string prepared;
    std::size_t needed = 0;
    int status;
    for (;;) {
        status = locked(c, s, [&] {
            if (s.closed)
                return 1;
            if (s.storage->exports.load())
                return 2;
            auto position = static_cast<std::size_t>(s.position);
            if (position > s.storage->bytes.max_size() - data.size() ||
                data.size() >
                    static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() - s.position))
                return 3;
            auto end = position + data.size();
            if (!data.empty() && end > s.storage->bytes.capacity()) {
                needed = std::max(
                    end, s.storage->bytes.capacity() +
                             std::min(s.storage->bytes.max_size() - s.storage->bytes.capacity(),
                                      s.storage->bytes.capacity() / 2 + 4));
                if (prepared.capacity() < needed)
                    return 4;
                prepared.assign(s.storage->bytes.data(), s.storage->bytes.size());
                s.storage->bytes.swap(prepared);
            }
            if (!data.empty() && end > s.storage->bytes.size())
                s.storage->bytes.resize(end, '\0');
            if (!data.empty())
                s.storage->bytes.replace(position, data.size(), data);
            s.position += static_cast<std::int64_t>(data.size());
            ++s.storage->version;
            c.account_external(buffer,
                               sizeof(ByteArrayObject::Storage) + s.storage->bytes.capacity() + 1);
            return 0;
        });
        if (status != 4)
            break;
        prepared.reserve(needed);
        c.poll();
    }
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        exported_error(c, out);
    else if (status == 3)
        raise_error(c, CoreObject::OverflowErrorType, U"new position too large", out);
    else
        make_int(c, static_cast<std::int64_t>(data.size()), out);
}

void readinto(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto target = roots.root(args[1]);
    detail::BufferExport buffer(c);
    if (!detail::writable_buffer(c, target, buffer, out))
        return;
    auto &s = c.payload<BytesIOObject>(self);
    std::string data;
    std::size_t needed = 0;
    int status;
    for (;;) {
        status = locked(c, s, [&] {
            if (s.closed)
                return 1;
            auto &bytes = s.storage->bytes;
            auto start = std::min<std::size_t>(static_cast<std::size_t>(s.position), bytes.size());
            needed = std::min(buffer.length, bytes.size() - start);
            if (data.capacity() < needed)
                return 2;
            data.assign(bytes.data() + start, needed);
            s.position += static_cast<std::int64_t>(needed);
            return 0;
        });
        if (status != 2)
            break;
        data.reserve(needed);
        c.poll();
    }
    if (status == 1) {
        detail::io_closed_error(c, out);
        return;
    }
    detail::write_buffer(c, buffer, data);
    make_int(c, static_cast<std::int64_t>(data.size()), out);
}

void seek(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t offset, whence;
    if (!detail::index_as_int64(c, args[1], offset, out) ||
        !detail::index_as_int64(c, args[2], whence, out))
        return;
    auto &s = c.payload<BytesIOObject>(self);
    std::int64_t position = 0;
    int status = locked(c, s, [&] {
        if (s.closed)
            return 1;
        if (whence < 0 || whence > 2 || (whence == 0 && offset < 0))
            return 2;
        auto base = whence == 0   ? std::int64_t{0}
                    : whence == 1 ? s.position
                                  : static_cast<std::int64_t>(s.storage->bytes.size());
        if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset)
            return 3;
        position = offset < -base ? 0 : base + offset;
        s.position = position;
        return 0;
    });
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        raise_error(c, CoreObject::ValueErrorType, U"invalid seek", out);
    else if (status == 3)
        raise_error(c, CoreObject::OverflowErrorType, U"new position too large", out);
    else
        make_int(c, position, out);
}

void tell(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<BytesIOObject>(roots.root(args[0]));
    std::int64_t position;
    bool closed = locked(c, s, [&] {
        position = s.position;
        return s.closed;
    });
    if (closed)
        detail::io_closed_error(c, out);
    else
        make_int(c, position, out);
}

void truncate(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t size = -1;
    bool current = args[1] == c.builtin(CoreObject::None);
    if (!current && !detail::index_as_int64(c, args[1], size, out))
        return;
    auto &s = c.payload<BytesIOObject>(self);
    int status = locked(c, s, [&] {
        if (s.closed)
            return 1;
        if (s.storage->exports.load())
            return 2;
        if (current)
            size = s.position;
        if (size < 0)
            return 3;
        if (static_cast<std::uint64_t>(size) < s.storage->bytes.size()) {
            s.storage->bytes.resize(static_cast<std::size_t>(size));
            ++s.storage->version;
        }
        return 0;
    });
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        exported_error(c, out);
    else if (status == 3)
        raise_error(c, CoreObject::ValueErrorType, U"negative size value", out);
    else
        make_int(c, size, out);
}

void close(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<BytesIOObject>(roots.root(args[0]));
    bool exported = locked(c, s, [&] {
        if (!s.closed && s.storage->exports.load())
            return true;
        s.closed = true;
        return false;
    });
    if (exported)
        exported_error(c, out);
    else
        out.ok(c.builtin(CoreObject::None));
}

void closed_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto &s = c.payload<BytesIOObject>(roots.root(value));
    bool closed = locked(c, s, [&] { return s.closed; });
    out.ok(c.builtin(closed ? CoreObject::True : CoreObject::False));
}

void capability(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<BytesIOObject>(roots.root(args[0]));
    bool closed = locked(c, s, [&] { return s.closed; });
    if (closed)
        detail::io_closed_error(c, out);
    else
        out.ok(c.builtin(CoreObject::True));
}

void getbuffer(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &s = c.payload<BytesIOObject>(self);
    auto token = std::make_shared<MemoryViewExport>(nullptr);
    std::int64_t length = 0;
    bool closed = locked(c, s, [&] {
        if (s.closed)
            return true;
        token->storage = s.storage;
        ++s.storage->exports;
        length = static_cast<std::int64_t>(s.storage->bytes.size());
        return false;
    });
    if (closed) {
        detail::io_closed_error(c, out);
        return;
    }
    c.allocate_instance<MemoryViewObject>(roots.root(c.builtin(CoreObject::MemoryViewType)), out,
                                          self.get(c), std::move(token), 0, 1, length, false, "B");
}

void install_bytesio(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto type = at(CoreObject::BytesIOType);
    auto install = [&](std::u32string name, SignatureLayout signature, NativeEntry entry) {
        detail::install_native_attribute(c, core, type, std::move(name), std::move(signature),
                                         entry);
    };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto optional = unary;
    optional.parameters.push_back(
        {U"value", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install(U"__new__", variadic, create);
    ReturnSlot wrapper(c), key(c);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*detail::name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    detail::name_store(c, dictionary, key.value(), wrapper.value());
    auto init = unary;
    init.parameters.push_back(
        {U"initial_bytes", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    install(U"__init__", init, initialize);
    install(U"read", optional, read<ReadKind::Read>);
    install(U"read1", optional, read<ReadKind::Read>);
    install(U"readinto", pair, readinto);
    install(U"readinto1", pair, readinto);
    install(U"readline", optional, read<ReadKind::Line>);
    install(U"getvalue", unary, read<ReadKind::Value>);
    install(U"write", pair, write);
    install(U"truncate", optional, truncate);
    install(U"tell", unary, tell);
    install(U"close", unary, close);
    install(U"getbuffer", unary, getbuffer);
    install(U"readable", unary, capability);
    install(U"writable", unary, capability);
    install(U"seekable", unary, capability);
    auto seek_sig = pair;
    seek_sig.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    install(U"seek", seek_sig, seek);
    detail::install_native_field(c, core, type, U"closed", closed_get);
}
} // namespace

void detail::install_memoryio(Attachment &c, std::span<const Handle> core)
{
    install_bytesio(c, core);
    install_stringio(c, core);
}

void detail::initialize_memoryio(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot name(c);
    for (auto [text, id] : {std::pair{U"BytesIO", CoreObject::BytesIOType},
                            {U"StringIO", CoreObject::StringIOType}}) {
        make_str(c, text, name);
        name_store(c, module_dict(c, module.get(c)), name.value(), c.builtin(id));
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core
