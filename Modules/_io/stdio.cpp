#include "internal/stdio.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"
#include "internal/errors.hpp"
#include "internal/iomodule.hpp"
#include "internal/sysmodule.hpp"
#ifdef CXXPYTHON_HAVE_READLINE
#include "internal/readlinemodule.hpp"
#endif

#include <cerrno>
#include <fcntl.h>
#include <tuple>
#include <unistd.h>

namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool is_text(Attachment &ctx, Value value)
{
    return ctx.has_layout<StrObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::StrType));
}
bool attribute(Attachment &ctx, Handle object, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, name, key);
    get_attribute(ctx, object, roots.root(key.value()), out);
    return !failed(out);
}
bool valid_descriptor(Attachment &ctx, int fd)
{
    NativeWait parked(ctx);
    int status;
    do {
        status = ::fcntl(fd, F_GETFD);
    } while (status < 0 && errno == EINTR);
    return status >= 0;
}
// CPython 3.12.14 pylifecycle.c create_stdio, using this runtime's native streams.
void standard_stream(Attachment &ctx, Handle io, int fd, const char32_t *label, ReturnSlot &out)
{
    if (!valid_descriptor(ctx, fd)) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    RootScope roots(ctx);
    ReturnSlot function(ctx), descriptor(ctx), mode(ctx), size(ctx), buffer(ctx), raw(ctx),
        value(ctx), encoding(ctx), errors(ctx), newline(ctx), stream(ctx);
    if (!attribute(ctx, io, U"open", function)) {
        out.raised(function.exception());
        return;
    }
    auto open = roots.root(function.value());
    make_int(ctx, fd, descriptor);
    make_str(ctx, fd == 0 ? U"rb" : U"wb", mode);
    make_int(ctx, -1, size);
    const auto none = ctx.builtin(CoreObject::None);
    const Value args[] = {descriptor.value(),
                          mode.value(),
                          size.value(),
                          none,
                          none,
                          none,
                          ctx.builtin(CoreObject::False)};
    call(ctx, open, {args}, buffer);
    if (failed(buffer)) {
        if (is_subtype(ctx, ctx.type_of(buffer.exception()),
                       ctx.builtin(CoreObject::OSErrorType)) &&
            !valid_descriptor(ctx, fd))
            out.ok(none);
        else
            out.raised(buffer.exception());
        return;
    }
    const auto binary = roots.root(buffer.value());
    if (!attribute(ctx, binary, U"raw", raw)) {
        out.raised(raw.exception());
        return;
    }
    const auto file = roots.root(raw.value());
    ReturnSlot key(ctx);
    make_str(ctx, U"name", key);
    const auto name = roots.root(key.value());
    make_str(ctx, label, value);
    set_attribute(ctx, file, name, roots.root(value.value()), out);
    if (failed(out))
        return;
    if (!detail::io_call(ctx, file, U"isatty", {}, value)) {
        out.raised(value.exception());
        return;
    }
    truth(ctx, roots.root(value.value()), out);
    if (failed(out))
        return;
    const bool line_buffering = fd == 2 || out.value() == ctx.builtin(CoreObject::True);
    if (!attribute(ctx, io, U"TextIOWrapper", function)) {
        out.raised(function.exception());
        return;
    }
    const auto wrapper = roots.root(function.value());
    make_str(ctx, U"utf-8", encoding);
    make_str(ctx, fd == 2 ? U"backslashreplace" : U"surrogateescape", errors);
    make_str(ctx, U"\n", newline);
    const Value text_args[] = {binary.get(ctx), encoding.value(), errors.value(), newline.value(),
                               ctx.builtin(line_buffering ? CoreObject::True : CoreObject::False)};
    call(ctx, wrapper, {text_args}, stream);
    if (failed(stream)) {
        out.raised(stream.exception());
        return;
    }
    const auto text = roots.root(stream.value());
    make_str(ctx, U"mode", key);
    make_str(ctx, fd == 0 ? U"r" : U"w", value);
    set_attribute(ctx, text, roots.root(key.value()), roots.root(value.value()), out);
    if (!failed(out))
        out.ok(text.get(ctx));
}
bool write_object(Attachment &ctx, Handle file, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx), method(ctx), text(ctx);
    make_str(ctx, U"write", key);
    get_attribute(ctx, file, roots.root(key.value()), method);
    if (failed(method)) {
        out.raised(method.exception());
        return false;
    }
    // PyFile_WriteObject resolves write before converting the value to text.
    str(ctx, value, text);
    if (failed(text)) {
        out.raised(text.exception());
        return false;
    }
    const Value argument = text.value();
    call(ctx, roots.root(method.value()), {{&argument, 1}, {}}, out);
    return !failed(out);
}
} // namespace
namespace detail {
bool read_terminal_input(Attachment &ctx, Handle input, Handle output, std::optional<Handle> prompt,
                         ReturnSlot &out)
{
#ifdef CXXPYTHON_HAVE_READLINE
    RootScope roots(ctx);
    ReturnSlot original(ctx), descriptor(ctx), converted(ctx), text(ctx);
    for (auto [name, stream, expected] :
         {std::tuple{U"__stdin__", input, 0}, std::tuple{U"__stdout__", output, 1}}) {
        get_sys_attribute(ctx, name, original);
        if (failed(original) || original.value() != stream.get(ctx))
            return false;
        if (!io_call(ctx, stream, U"fileno", {}, descriptor))
            return false;
        std::int64_t fd;
        if (!index_as_int64(ctx, descriptor.value(), fd, converted, std::nullopt)) {
            out.raised(converted.exception());
            return true;
        }
        if (fd != expected)
            return false;
    }
    {
        NativeWait wait(ctx);
        if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO))
            return false;
    }
    if (prompt)
        str(ctx, *prompt, text);
    else
        make_str(ctx, U"", text);
    if (failed(text)) {
        out.raised(text.exception());
        return true;
    }
    const auto message = roots.root(text.value());
    // Match ordinary input's best-effort output flush before native reading.
    io_call(ctx, output, U"flush", {}, converted);
    read_terminal_line(ctx, STDIN_FILENO, STDOUT_FILENO, message, out);
    return true;
#else
    (void)ctx;
    (void)input;
    (void)output;
    (void)prompt;
    (void)out;
    return false;
#endif
}
void initialize_stdio(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    module = roots.root(module.get(ctx));
    ReturnSlot io(ctx), input(ctx), output(ctx), error(ctx), key(ctx);
    import_module(ctx, U"io", io);
    if (failed(io)) {
        out.raised(io.exception());
        return;
    }
    const auto implementation = roots.root(io.value());
    const std::array streams{&input, &output, &error};
    const char32_t *names[] = {U"stdin", U"stdout", U"stderr"};
    const char32_t *originals[] = {U"__stdin__", U"__stdout__", U"__stderr__"};
    for (int fd = 0; fd != 3; ++fd) {
        const char32_t *labels[] = {U"<stdin>", U"<stdout>", U"<stderr>"};
        standard_stream(ctx, implementation, fd, labels[fd], *streams[fd]);
        if (failed(*streams[fd])) {
            out.raised(streams[fd]->exception());
            return;
        }
    }
    for (int fd = 0; fd != 3; ++fd) {
        const auto stream = roots.root(streams[fd]->value());
        for (auto name : {names[fd], originals[fd]}) {
            make_str(ctx, name, key);
            set_attribute(ctx, module, roots.root(key.value()), stream, out);
            if (failed(out))
                return;
        }
    }
    out.ok(ctx.builtin(CoreObject::None));
}
void print_builtin(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto none = ctx.builtin(CoreObject::None);
    ReturnSlot temp(ctx), file_slot(ctx);
    // CPython's generated argument wrapper converts flush before entering print.
    truth(ctx, roots.root(args[3]), temp);
    if (failed(temp)) {
        out.raised(temp.exception());
        return;
    }
    const bool flush = temp.value() == ctx.builtin(CoreObject::True);
    Handle file = roots.root(args[2]);
    if (args[2] == none) {
        get_sys_attribute(ctx, U"stdout", file_slot);
        if (failed(file_slot)) {
            out.raised(file_slot.exception());
            return;
        }
        file = roots.root(file_slot.value());
        if (file.get(ctx) == none) {
            out.ok(none);
            return;
        }
    }
    if ((args[0] != none && !is_text(ctx, args[0])) ||
        (args[1] != none && !is_text(ctx, args[1]))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"sep and end must be None or strings", out);
        return;
    }
    make_str(ctx, U" ", temp);
    auto sep = roots.root(args[0] == none ? temp.value() : args[0]);
    make_str(ctx, U"\n", temp);
    auto end = roots.root(args[1] == none ? temp.value() : args[1]);
    auto tuple = roots.root(args[4]);
    const auto &items = ctx.payload<TupleObject>(tuple).items;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i && !write_object(ctx, file, sep, out))
            return;
        if (!write_object(ctx, file, roots.root(items[i]), out))
            return;
    }
    if (!write_object(ctx, file, end, out))
        return;
    if (flush && !io_call(ctx, file, U"flush", {}, out))
        return;
    out.ok(none);
}
bool flush_stdio(Attachment &ctx, StdioFlush phase)
{
    RootScope roots(ctx);
    ReturnSlot dictionary(ctx), key(ctx), value(ctx), closed(ctx), result(ctx);
    ctx.load_sys_dictionary(dictionary);
    if (dictionary.tag() != ResultTag::Ok)
        return true;
    const auto sys = roots.root(dictionary.value());
    bool success = true;
    const auto order = phase == StdioFlush::Finalizing ? std::array{U"stdout", U"stderr"}
                                                       : std::array{U"stderr", U"stdout"};
    for (const auto name : order) {
        make_str(ctx, name, key);
        dict_lookup(ctx, sys, roots.root(key.value()), value);
        if (value.tag() == ResultTag::Empty)
            continue;
        if (failed(value)) {
            success = false;
            continue;
        }
        const auto stream = roots.root(value.value());
        if (stream.get(ctx) == ctx.builtin(CoreObject::None))
            continue;
        if (attribute(ctx, stream, U"closed", closed)) {
            truth(ctx, roots.root(closed.value()), result);
            if (!failed(result) && result.value() == ctx.builtin(CoreObject::True))
                continue;
        }
        if (!io_call(ctx, stream, U"flush", {}, result)) {
            success = false;
            if (phase == StdioFlush::Finalizing && std::u32string_view(name) == U"stdout")
                write_unraisable(ctx, roots.root(result.exception()), stream);
        }
    }
    return success;
}
} // namespace detail
} // namespace cxxpython::core
