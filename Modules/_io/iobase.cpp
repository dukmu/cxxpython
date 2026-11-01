// IOBase defaults follow CPython 3.12.14 Modules/_io/iobase.c (PSF license).
#include "internal/iomodule.hpp"

#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"

#include <algorithm>

namespace cxxpython::core::detail {
bool io_call(Attachment &c, Handle self, std::u32string_view name, std::span<const Value> args,
             ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot key(c), method(c);
    make_str(c, std::u32string(name), key);
    get_attribute(c, self, roots.root(key.value()), method);
    if (method.tag() == ResultTag::Raised) {
        out.raised(method.exception());
        return false;
    }
    call(c, roots.root(method.value()), {args, {}}, out);
    return out.tag() != ResultTag::Raised;
}
void io_closed_error(Attachment &c, ReturnSlot &out)
{
    raise_error(c, CoreObject::ValueErrorType, U"I/O operation on closed file", out);
}
void io_unsupported(Attachment &c, std::u32string message, ReturnSlot &out)
{
    raise_error(c, CoreObject::UnsupportedOperationType, std::move(message), out);
}
namespace {
bool receiver(Attachment &c, Value self, ReturnSlot &out)
{
    if (is_subtype(c, c.type_of(self), c.builtin(CoreObject::IOBaseType)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires an IOBase object", out);
    return false;
}
bool closed(Attachment &c, Handle self, ReturnSlot &out)
{
    if (!receiver(c, self.get(c), out))
        return true;
    RootScope roots(c);
    ReturnSlot name(c), result(c);
    make_str(c, U"closed", name);
    get_attribute(c, self, roots.root(name.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return true;
    }
    truth(c, roots.root(result.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return true;
    }
    if (result.value() == c.builtin(CoreObject::True)) {
        io_closed_error(c, out);
        return true;
    }
    return false;
}
void closed_get(Attachment &c, Value value, ReturnSlot &out)
{
    auto dictionary = c.instance_dictionary(value);
    ReturnSlot stored(c);
    if (dictionary)
        c.load(value, *dictionary, stored);
    const bool found = stored.tag() == ResultTag::Ok &&
                       name_lookup(c, stored.value(), U"_IOBase__closed").has_value();
    out.ok(c.builtin(found ? CoreObject::True : CoreObject::False));
}
template <bool Close>
void flush_close(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    if constexpr (!Close) {
        if (!closed(c, self, out))
            out.ok(c.builtin(CoreObject::None));
    } else {
        ReturnSlot status(c);
        closed_get(c, a[0], status);
        if (status.value() == c.builtin(CoreObject::True)) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
        io_call(c, self, U"flush", {}, out);
        ReturnSlot key(c), marked(c);
        make_str(c, U"_IOBase__closed", key);
        set_attribute(c, self, roots.root(key.value()), roots.root(c.builtin(CoreObject::True)),
                      marked);
        if (out.tag() != ResultTag::Raised) {
            if (marked.tag() == ResultTag::Raised)
                out.raised(marked.exception());
            else
                out.ok(c.builtin(CoreObject::None));
        }
    }
}
void finalize(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(a[0]);
    ReturnSlot name(c), status(c), changed(c);
    make_str(c, U"closed", name);
    get_attribute(c, self, roots.root(name.value()), status);
    if (status.tag() != ResultTag::Raised)
        truth(c, roots.root(status.value()), status);
    if (status.tag() == ResultTag::Raised || status.value() == c.builtin(CoreObject::True)) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    make_str(c, U"_finalizing", name);
    set_attribute(c, self, roots.root(name.value()), roots.root(c.builtin(CoreObject::True)),
                  changed);
    io_call(c, self, U"close", {}, out);
}
template <bool Enter> void context(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    if constexpr (Enter) {
        if (!closed(c, self, out))
            out.ok(a[0]);
    } else
        io_call(c, self, U"close", {}, out);
}
void iterator(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    if (!closed(c, roots.root(a[0]), out))
        out.ok(a[0]);
}
void next_line(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    if (!io_call(c, roots.root(a[0]), U"readline", {}, out))
        return;
    ReturnSlot status(c);
    truth(c, roots.root(out.value()), status);
    if (status.tag() == ResultTag::Raised)
        out.raised(status.exception());
    else if (status.value() == c.builtin(CoreObject::False))
        stop_iteration(c, out);
}
template <bool Tty>
void false_method(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    if constexpr (Tty) {
        if (closed(c, roots.root(a[0]), out))
            return;
    }
    out.ok(c.builtin(CoreObject::False));
}
void unsupported(Attachment &c, Handle function, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    auto name = c.payload<NativeFunctionObject>(function).name;
    io_unsupported(c, std::u32string(name.begin(), name.end()), out);
}
void tell(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    const Value values[] = {c.builtin(CoreObject::IntZero), c.builtin(CoreObject::IntOne)};
    io_call(c, roots.root(a[0]), U"seek", values, out);
}
void check_closed(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    if (!closed(c, roots.root(a[0]), out))
        out.ok(c.builtin(CoreObject::None));
}
template <bool Read>
void check_mode(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    ReturnSlot result(c);
    if (!io_call(c, roots.root(a[0]), Read ? U"readable" : U"writable", {}, result)) {
        out.raised(result.exception());
        return;
    }
    if (result.value() != c.builtin(CoreObject::True))
        io_unsupported(
            c, Read ? U"File or stream is not readable." : U"File or stream is not writable.", out);
    else
        out.ok(result.value());
}
void readlines(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    std::int64_t hint = -1;
    if (a[1] != c.builtin(CoreObject::None) && !index_as_int64(c, a[1], hint, out))
        return;
    ReturnSlot result(c), line(c), size(c), iterator(c);
    make_list(c, {}, result);
    auto list = roots.root(result.value());
    auto self = roots.root(a[0]);
    get_iter(c, self, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return;
    }
    auto sequence = roots.root(iterator.value());
    std::int64_t total = 0;
    for (;;) {
        auto status = iter_next(c, sequence, line);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(line.exception());
            return;
        }
        auto item = roots.root(line.value());
        if (hint > 0) {
            length(c, item, size);
            if (size.tag() == ResultTag::Raised) {
                out.raised(size.exception());
                return;
            }
            std::int64_t n;
            if (!int_fits_signed(c, size.value(), n)) {
                raise_error(c, CoreObject::OverflowErrorType, U"line length out of range", out);
                return;
            }
            total += std::min(n, INT64_MAX - total);
        }
        list_append(c, list, item, out);
        if (out.tag() == ResultTag::Raised)
            return;
        roots.release(item);
        if (hint > 0 && total > hint)
            break;
    }
    out.ok(list.get(c));
}
void writelines(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(a[0]);
    if (closed(c, self, out))
        return;
    ReturnSlot iter(c), item(c);
    get_iter(c, roots.root(a[1]), iter);
    if (iter.tag() == ResultTag::Raised) {
        out.raised(iter.exception());
        return;
    }
    auto sequence = roots.root(iter.value());
    for (;;) {
        auto status = iter_next(c, sequence, item);
        if (status == NextStatus::Done)
            break;
        if (item.tag() == ResultTag::Raised) {
            out.raised(item.exception());
            return;
        }
        const Value value = item.value();
        if (!io_call(c, self, U"write", {&value, 1}, out))
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}
void readline(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    std::int64_t limit = -1;
    if (a[1] != c.builtin(CoreObject::None) && !index_as_int64(c, a[1], limit, out))
        return;
    std::string result;
    ReturnSlot item(c);
    const Value one = c.builtin(CoreObject::IntOne);
    while (limit < 0 || result.size() < static_cast<std::uint64_t>(limit)) {
        if (!io_call(c, self, U"read", {&one, 1}, item)) {
            out.raised(item.exception());
            return;
        }
        if (!is_bytes(c, item.value())) {
            raise_error(c, CoreObject::OSErrorType, U"read() should have returned a bytes object",
                        out);
            return;
        }
        const auto &data = c.payload<BytesObject>(item.value()).value;
        result += data;
        if (data.empty() || result.back() == '\n')
            break;
    }
    make_bytes(c, std::move(result), out);
}
void raw_unimplemented(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (receiver(c, a[0], out))
        raise_error(c, CoreObject::NotImplementedErrorType, U"", out);
}
template <bool All> void raw_read(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    if constexpr (All) {
        std::string data;
        ReturnSlot size(c), chunk(c);
        make_int(c, 8192, size);
        const Value argument = size.value();
        for (;;) {
            if (!io_call(c, self, U"read", {&argument, 1}, chunk)) {
                out.raised(chunk.exception());
                return;
            }
            if (chunk.value() == c.builtin(CoreObject::None)) {
                if (data.empty()) {
                    out.ok(chunk.value());
                    return;
                }
                break;
            }
            if (!is_bytes(c, chunk.value())) {
                raise_error(c, CoreObject::TypeErrorType, U"read() should return bytes", out);
                return;
            }
            const auto &bytes = c.payload<BytesObject>(chunk.value()).value;
            if (bytes.empty())
                break;
            data += bytes;
        }
        make_bytes(c, std::move(data), out);
    } else {
        std::int64_t size = -1;
        if (a[1] != c.builtin(CoreObject::None) && !index_as_int64(c, a[1], size, out))
            return;
        if (size < 0) {
            io_call(c, self, U"readall", {}, out);
            return;
        }
        std::string bytes;
        if (static_cast<std::uint64_t>(size) > bytes.max_size())
            throw std::bad_alloc();
        bytes.resize(static_cast<std::size_t>(size), '\0');
        ReturnSlot buffer(c), count(c);
        make_bytearray(c, std::move(bytes), buffer);
        auto target = roots.root(buffer.value());
        const Value argument = target.get(c);
        if (!io_call(c, self, U"readinto", {&argument, 1}, count)) {
            out.raised(count.exception());
            return;
        }
        if (count.value() == c.builtin(CoreObject::None)) {
            out.ok(count.value());
            return;
        }
        std::int64_t n;
        if (!index_as_int64(c, count.value(), n, out))
            return;
        if (n < 0 || n > size) {
            raise_error(c, CoreObject::ValueErrorType, U"readinto() returned invalid length", out);
            return;
        }
        bytes = bytearray_snapshot(c, target);
        bytes.resize(static_cast<std::size_t>(n));
        make_bytes(c, std::move(bytes), out);
    }
}
} // namespace
void install_iobases(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto base = core[static_cast<std::size_t>(CoreObject::IOBaseType)];
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto binary = unary;
    binary.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto sized = unary;
    sized.parameters.push_back(
        {U"size", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    // IO defaults use -1, not the omission sentinel accepted by protocol calls.
    RootScope roots(c);
    ReturnSlot negative(c);
    c.allocate_instance<IntObject>(core[static_cast<std::size_t>(CoreObject::IntType)], negative,
                                   -1);
    sized.parameters[1].default_value = negative.value();
    auto seek = unary;
    seek.parameters.push_back({U"offset", ParameterKind::PositionalOnly});
    seek.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    auto exit = unary;
    for (auto name : {U"exc_type", U"exc_value", U"traceback"})
        exit.parameters.push_back({name, ParameterKind::PositionalOnly});
    install_native_field(c, core, base, U"closed", closed_get);
    install_native_attribute(c, core, base, U"flush", unary, flush_close<false>);
    install_native_attribute(c, core, base, U"close", unary, flush_close<true>);
    install_native_attribute(c, core, base, U"__del__", unary, finalize);
    install_native_attribute(c, core, base, U"__enter__", unary, context<true>);
    install_native_attribute(c, core, base, U"__exit__", exit, context<false>);
    install_native_attribute(c, core, base, U"__iter__", unary, iterator);
    install_native_attribute(c, core, base, U"__next__", unary, next_line);
    install_native_attribute(c, core, base, U"readable", unary, false_method<false>);
    install_native_attribute(c, core, base, U"writable", unary, false_method<false>);
    install_native_attribute(c, core, base, U"seekable", unary, false_method<false>);
    install_native_attribute(c, core, base, U"isatty", unary, false_method<true>);
    install_native_attribute(c, core, base, U"fileno", unary, unsupported);
    install_native_attribute(c, core, base, U"seek", seek, unsupported);
    install_native_attribute(c, core, base, U"truncate", sized, unsupported);
    install_native_attribute(c, core, base, U"tell", unary, tell);
    install_native_attribute(c, core, base, U"_checkClosed", unary, check_closed);
    install_native_attribute(c, core, base, U"_checkReadable", unary, check_mode<true>);
    install_native_attribute(c, core, base, U"_checkWritable", unary, check_mode<false>);
    install_native_attribute(c, core, base, U"readlines", sized, readlines);
    install_native_attribute(c, core, base, U"writelines", binary, writelines);
    install_native_attribute(c, core, base, U"readline", sized, readline);
    auto raw = at(CoreObject::RawIOBaseType);
    install_native_attribute(c, core, raw, U"read", sized, raw_read<false>);
    install_native_attribute(c, core, raw, U"readall", unary, raw_read<true>);
    install_native_attribute(c, core, raw, U"readinto", binary, raw_unimplemented);
    install_native_attribute(c, core, raw, U"write", binary, raw_unimplemented);
}
} // namespace cxxpython::core::detail
