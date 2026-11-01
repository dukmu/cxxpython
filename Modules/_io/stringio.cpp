#include "internal/memoryio.hpp"

#include "cxxpython/type.hpp"
#include "internal/abstract.hpp"

#include <algorithm>
#include <limits>

namespace cxxpython::core {
namespace {
template <class F> auto locked(Attachment &c, StringIOObject &s, F &&function)
{
    for (;;) {
        std::unique_lock lock(s.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return function();
        }
        NativeWait wait(c);
        s.mutex.lock();
        s.mutex.unlock();
    }
}
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<StringIOObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected StringIO object", out);
    return false;
}
void create(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::StringIOType))) {
        raise_error(c, CoreObject::TypeErrorType, U"StringIO.__new__ requires a subtype", out);
        return;
    }
    c.allocate_instance<StringIOObject>(roots.root(args[0]), out);
}

// Modes: universal translated, universal preserved, LF, CR, CRLF.
std::u32string translate(std::u32string_view input, int mode, unsigned &seen)
{
    std::u32string result;
    for (std::size_t i = 0; i < input.size(); ++i) {
        char32_t ch = input[i];
        if (mode < 2 && ch == U'\r') {
            bool pair = i + 1 < input.size() && input[i + 1] == U'\n';
            seen |= pair ? 4 : 1;
            if (mode == 0)
                result += U'\n';
            else {
                result += U'\r';
                if (pair)
                    result += U'\n';
            }
            if (pair)
                ++i;
        } else if (ch == U'\n') {
            if (mode < 2)
                seen |= 2;
            if (mode == 3 || mode == 4)
                result += U'\r';
            if (mode != 3)
                result += U'\n';
        } else
            result += ch;
    }
    return result;
}

void initialize(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    int mode = 0;
    if (args[2] != c.builtin(CoreObject::None)) {
        if (!c.has_layout<StrObject>(args[2])) {
            raise_error(c, CoreObject::TypeErrorType, U"newline must be str or None", out);
            return;
        }
        auto &nl = c.payload<StrObject>(args[2]).value;
        if (nl.empty())
            mode = 1;
        else if (nl == U"\n")
            mode = 2;
        else if (nl == U"\r")
            mode = 3;
        else if (nl == U"\r\n")
            mode = 4;
        else {
            raise_error(c, CoreObject::ValueErrorType, U"illegal newline value", out);
            return;
        }
    }
    std::u32string text;
    unsigned seen = 0;
    if (args[1] != c.builtin(CoreObject::None)) {
        if (!c.has_layout<StrObject>(args[1])) {
            raise_error(c, CoreObject::TypeErrorType, U"initial_value must be str or None", out);
            return;
        }
        text = translate(c.payload<StrObject>(args[1]).value, mode, seen);
    }
    auto &s = c.payload<StringIOObject>(self);
    locked(c, s, [&] {
        s.contents.swap(text);
        s.position = 0;
        s.newline = mode;
        s.newlines = seen;
        s.closed = false;
        ++s.version;
        c.account_external(self, s.external_bytes());
        return 0;
    });
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
    auto &s = c.payload<StringIOObject>(self);
    std::u32string text;
    std::size_t needed = 0;
    int status;
    for (;;) {
        status = locked(c, s, [&] {
            if (s.closed)
                return 1;
            if constexpr (kind == ReadKind::Value) {
                needed = s.contents.size();
                if (text.capacity() < needed)
                    return 2;
                text.assign(s.contents.data(), needed);
            } else {
                auto start =
                    std::min<std::size_t>(static_cast<std::size_t>(s.position), s.contents.size());
                auto count = s.contents.size() - start;
                if (size >= 0)
                    count = std::min(count, static_cast<std::size_t>(size));
                if constexpr (kind == ReadKind::Line) {
                    for (std::size_t i = 0; i < count; ++i) {
                        auto ch = s.contents[start + i];
                        bool end = (s.newline <= 1 && (ch == U'\r' || ch == U'\n')) ||
                                   (s.newline == 2 && ch == U'\n') ||
                                   (s.newline == 3 && ch == U'\r') ||
                                   (s.newline == 4 && ch == U'\r' && i + 1 < count &&
                                    s.contents[start + i + 1] == U'\n');
                        if (end) {
                            count = i + 1;
                            if (ch == U'\r' && (s.newline <= 1 || s.newline == 4) &&
                                start + count < s.contents.size() &&
                                (size < 0 || count < static_cast<std::size_t>(size)) &&
                                s.contents[start + count] == U'\n')
                                ++count;
                            break;
                        }
                    }
                }
                needed = count;
                if (text.capacity() < needed)
                    return 2;
                text.assign(s.contents.data() + start, count);
                s.position += static_cast<std::int64_t>(count);
                ++s.version;
            }
            return 0;
        });
        if (status != 2)
            break;
        text.reserve(needed);
        c.poll();
    }
    if (status == 1)
        detail::io_closed_error(c, out);
    else
        make_str(c, std::move(text), out);
}

void write(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (!c.has_layout<StrObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"string argument expected", out);
        return;
    }
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto input = c.payload<StrObject>(args[1]).value;
    auto &s = c.payload<StringIOObject>(self);
    std::u32string prepared;
    std::size_t needed = 0;
    int status;
    for (;;) {
        int mode = 0;
        std::size_t version = 0;
        bool closed = locked(c, s, [&] {
            mode = s.newline;
            version = s.version;
            return s.closed;
        });
        if (closed) {
            status = 1;
            break;
        }
        unsigned seen = 0;
        auto text = translate(input, mode, seen);
        status = locked(c, s, [&] {
            if (s.closed)
                return 1;
            if (s.version != version)
                return 3;
            auto position = static_cast<std::size_t>(s.position);
            if (position > s.contents.max_size() - text.size() ||
                text.size() >
                    static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() - s.position))
                return 2;
            auto end = position + text.size();
            if (!text.empty() && end > s.contents.capacity()) {
                needed = std::max(end, s.contents.capacity() +
                                           std::min(s.contents.max_size() - s.contents.capacity(),
                                                    s.contents.capacity() / 2 + 4));
                if (prepared.capacity() < needed)
                    return 4;
                prepared.assign(s.contents.data(), s.contents.size());
                s.contents.swap(prepared);
            }
            if (!text.empty() && end > s.contents.size())
                s.contents.resize(position + text.size(), U'\0');
            if (!text.empty())
                s.contents.replace(position, text.size(), text);
            s.position += static_cast<std::int64_t>(text.size());
            s.newlines |= seen;
            ++s.version;
            c.account_external(self, s.external_bytes());
            return 0;
        });
        if (status != 3 && status != 4)
            break;
        if (status == 4)
            prepared.reserve(needed);
        c.poll();
    }
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        raise_error(c, CoreObject::OverflowErrorType, U"new position too large", out);
    else
        make_int(c, static_cast<std::int64_t>(input.size()), out);
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
    auto &s = c.payload<StringIOObject>(self);
    std::int64_t position = 0;
    int status = locked(c, s, [&] {
        if (s.closed)
            return 1;
        if (whence < 0 || whence > 2 || (whence == 0 && offset < 0))
            return 2;
        if (whence != 0 && offset != 0)
            return 3;
        position = whence == 0   ? offset
                   : whence == 1 ? s.position
                                 : static_cast<std::int64_t>(s.contents.size());
        s.position = position;
        ++s.version;
        return 0;
    });
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        raise_error(c, CoreObject::ValueErrorType, U"invalid seek", out);
    else if (status == 3)
        raise_error(c, CoreObject::OSErrorType, U"Can't do nonzero cur-relative seeks", out);
    else
        make_int(c, position, out);
}
void tell(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<StringIOObject>(roots.root(args[0]));
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
    auto &s = c.payload<StringIOObject>(self);
    int status = locked(c, s, [&] {
        if (s.closed)
            return 1;
        if (current)
            size = s.position;
        if (size < 0)
            return 2;
        if (static_cast<std::uint64_t>(size) < s.contents.size())
            s.contents.resize(static_cast<std::size_t>(size));
        ++s.version;
        return 0;
    });
    if (status == 1)
        detail::io_closed_error(c, out);
    else if (status == 2)
        raise_error(c, CoreObject::ValueErrorType, U"negative size value", out);
    else
        make_int(c, size, out);
}
void close(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<StringIOObject>(roots.root(args[0]));
    locked(c, s, [&] {
        s.closed = true;
        ++s.version;
        return 0;
    });
    out.ok(c.builtin(CoreObject::None));
}
void closed_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto &s = c.payload<StringIOObject>(roots.root(value));
    bool closed = locked(c, s, [&] { return s.closed; });
    out.ok(c.builtin(closed ? CoreObject::True : CoreObject::False));
}
void newlines_get(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto &s = c.payload<StringIOObject>(roots.root(value));
    unsigned seen;
    bool closed = locked(c, s, [&] {
        seen = s.newlines;
        return s.closed;
    });
    if (closed) {
        detail::io_closed_error(c, out);
        return;
    }
    std::vector<Handle> values;
    ReturnSlot text(c);
    for (auto [bit, name] : {std::pair{1u, U"\r"}, {2u, U"\n"}, {4u, U"\r\n"}}) {
        if (seen & bit) {
            make_str(c, name, text);
            values.push_back(roots.root(text.value()));
        }
    }
    if (values.empty())
        out.ok(c.builtin(CoreObject::None));
    else if (values.size() == 1)
        out.ok(values[0].get(c));
    else {
        std::vector<Value> items;
        for (auto item : values)
            items.push_back(item.get(c));
        cxxpython::core::make_tuple(c, items, out);
    }
}
void capability(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto &s = c.payload<StringIOObject>(roots.root(args[0]));
    bool closed = locked(c, s, [&] { return s.closed; });
    if (closed)
        detail::io_closed_error(c, out);
    else
        out.ok(c.builtin(CoreObject::True));
}
} // namespace

void detail::install_stringio(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto type = at(CoreObject::StringIOType);
    auto install = [&](std::u32string name, SignatureLayout signature, NativeEntry entry) {
        install_native_attribute(c, core, type, std::move(name), std::move(signature), entry);
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
    ReturnSlot wrapper(c), key(c), newline(c);
    auto dictionary = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(c, dictionary, key.value(), wrapper.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), newline, U"\n");
    auto init = unary;
    init.parameters.push_back(
        {U"initial_value", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    init.parameters.push_back({U"newline", ParameterKind::PositionalOrKeyword, newline.value()});
    install(U"__init__", init, initialize);
    install(U"read", optional, read<ReadKind::Read>);
    install(U"readline", optional, read<ReadKind::Line>);
    install(U"getvalue", unary, read<ReadKind::Value>);
    install(U"write", pair, write);
    install(U"truncate", optional, truncate);
    install(U"tell", unary, tell);
    install(U"close", unary, close);
    install(U"readable", unary, capability);
    install(U"writable", unary, capability);
    install(U"seekable", unary, capability);
    auto seek_sig = pair;
    seek_sig.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    install(U"seek", seek_sig, seek);
    install_native_field(c, core, type, U"closed", closed_get);
    install_native_field(c, core, type, U"newlines", newlines_get);
}
} // namespace cxxpython::core
