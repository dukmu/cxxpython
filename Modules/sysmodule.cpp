// Adapted from CPython 3.12.14 Python/sysmodule.c and Objects/exceptions.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/sysmodule.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void system_exit_init(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!ctx.has_layout<ExceptionObject>(args[0]) ||
        !is_subtype(ctx, ctx.type_of(args[0]), ctx.builtin(CoreObject::SystemExitType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"SystemExit.__init__ requires SystemExit",
                    out);
        return;
    }
    ctx.store(args[0], ctx.payload<ExceptionObject>(args[0]).args, args[1]);
    const auto &items = ctx.payload<TupleObject>(args[1]).items;
    // CPython 3.12.14 Objects/exceptions.c: an empty reinitialization leaves code alone.
    if (!items.empty())
        ctx.store(args[0], ctx.instance_slot(args[0], 0), items.size() == 1 ? items[0] : args[1]);
    out.ok(ctx.builtin(CoreObject::None));
}
void code_get(Attachment &ctx, Value self, ReturnSlot &out)
{
    ctx.load(self, ctx.instance_slot(self, 0), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(ctx.builtin(CoreObject::None));
}
void code_set(Attachment &ctx, Value self, std::optional<Value> value, ReturnSlot &out)
{
    ctx.store(self, ctx.instance_slot(self, 0), value);
    out.ok(ctx.builtin(CoreObject::None));
}
void sys_exit(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto code = roots.root(args[0]);
    ReturnSlot arguments(ctx), result(ctx);
    // PyErr_SetObject uses a tuple (including a subclass) as the exception's
    // argument container, preserving its identity rather than reboxing it.
    if (code.get(ctx) == ctx.builtin(CoreObject::None))
        arguments.ok(ctx.builtin(CoreObject::EmptyTuple));
    else if (ctx.has_layout<TupleObject>(code.get(ctx)) &&
             is_subtype(ctx, ctx.type_of(code), ctx.builtin(CoreObject::TupleType)))
        arguments.ok(code.get(ctx));
    else {
        const std::array values{code.get(ctx)};
        cxxpython::core::make_tuple(ctx, values, arguments);
    }
    auto tuple = roots.root(arguments.value());
    ctx.allocate_instance<ExceptionObject>(roots.root(ctx.builtin(CoreObject::SystemExitType)),
                                           result, tuple.get(ctx));
    auto error = roots.root(result.value());
    const auto &items = ctx.payload<TupleObject>(tuple).items;
    if (!items.empty())
        ctx.store(error.get(ctx), ctx.instance_slot(error.get(ctx), 0),
                  items.size() == 1 ? items[0] : tuple.get(ctx));
    link_exception_context(ctx, error.get(ctx));
    out.raised(error.get(ctx));
}
bool attribute(Attachment &ctx, Handle object, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, name, key);
    get_attribute(ctx, object, roots.root(key.value()), out);
    return !failed(out);
}
bool set_last(Attachment &ctx, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, U"_", key);
    dict_store(ctx, roots.root(ctx.builtin(CoreObject::Builtins)), roots.root(key.value()), value,
               out);
    return !failed(out);
}
bool write_value(Attachment &ctx, Handle stream, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot method(ctx);
    if (!attribute(ctx, stream, U"write", method)) {
        out.raised(method.exception());
        return false;
    }
    const Value argument = value.get(ctx);
    call(ctx, roots.root(method.value()), {{&argument, 1}, {}}, out);
    return !failed(out);
}
// Adapt CPython's sys_displayhook_unencodable without borrowing an unrooted buffer.
bool display_unencodable(Attachment &ctx, Handle stream, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot encoding(ctx), text(ctx), errors(ctx), encoded(ctx), buffer(ctx);
    if (!attribute(ctx, stream, U"encoding", encoding)) {
        out.raised(encoding.exception());
        return false;
    }
    repr(ctx, value, text);
    if (failed(text)) {
        out.raised(text.exception());
        return false;
    }
    make_str(ctx, U"backslashreplace", errors);
    encode_text(ctx, roots.root(text.value()), roots.root(encoding.value()),
                roots.root(errors.value()), encoded);
    if (failed(encoded)) {
        out.raised(encoded.exception());
        return false;
    }
    if (attribute(ctx, stream, U"buffer", buffer))
        return write_value(ctx, roots.root(buffer.value()), roots.root(encoded.value()), out);
    if (!is_subtype(ctx, ctx.type_of(buffer.exception()),
                    ctx.builtin(CoreObject::AttributeErrorType))) {
        out.raised(buffer.exception());
        return false;
    }
    make_str(ctx, U"strict", errors);
    decode_bytes(ctx, roots.root(encoded.value()), roots.root(encoding.value()),
                 roots.root(errors.value()), text);
    if (failed(text)) {
        out.raised(text.exception());
        return false;
    }
    return write_value(ctx, stream, roots.root(text.value()), out);
}
// CPython 3.12.14 Python/sysmodule.c sys_displayhook: clear _, repr, write twice, restore _.
void sys_displayhook(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] == ctx.builtin(CoreObject::None)) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    RootScope roots(ctx);
    auto value = roots.root(args[0]);
    ReturnSlot sys(ctx), stream(ctx), method(ctx), text(ctx), result(ctx);
    if (!set_last(ctx, roots.root(ctx.builtin(CoreObject::None)), out))
        return;
    import_module(ctx, U"sys", sys);
    if (failed(sys)) {
        out.raised(sys.exception());
        return;
    }
    if (!attribute(ctx, roots.root(sys.value()), U"stdout", stream)) {
        out.raised(stream.exception());
        return;
    }
    if (stream.value() == ctx.builtin(CoreObject::None)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"lost sys.stdout", out);
        return;
    }
    if (!attribute(ctx, roots.root(stream.value()), U"write", method)) {
        out.raised(method.exception());
        return;
    }
    auto writer = roots.root(method.value());
    repr(ctx, value, text);
    if (failed(text)) {
        out.raised(text.exception());
        return;
    }
    Value argument = text.value();
    call(ctx, writer, {{&argument, 1}, {}}, result);
    if (failed(result)) {
        if (!is_subtype(ctx, ctx.type_of(result.exception()),
                        ctx.builtin(CoreObject::UnicodeEncodeErrorType)) ||
            !display_unencodable(ctx, roots.root(stream.value()), value, out)) {
            if (!failed(out))
                out.raised(result.exception());
            return;
        }
    }
    make_str(ctx, U"\n", text);
    if (!attribute(ctx, roots.root(stream.value()), U"write", method)) {
        out.raised(method.exception());
        return;
    }
    argument = text.value();
    call(ctx, roots.root(method.value()), {{&argument, 1}, {}}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    if (set_last(ctx, value, out))
        out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

void install_system_exit(Attachment &ctx, std::span<const Handle> core)
{
    auto owner = core[static_cast<std::size_t>(CoreObject::SystemExitType)];
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    signature.varargs = U"args";
    install_native_attribute(ctx, core, owner, U"__init__", signature, system_exit_init);
    install_native_field(ctx, core, owner, U"code", code_get, code_set);
}
void initialize_sys_hooks(Attachment &ctx, Handle sys, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot function(ctx), key(ctx);
    auto dict = roots.root(module_dict(ctx, sys.get(ctx)));
    auto store = [&](const char32_t *name) {
        make_str(ctx, name, key);
        dict_store(ctx, dict, roots.root(key.value()), roots.root(function.value()), out);
        return !failed(out);
    };
    SignatureLayout exit_signature;
    exit_signature.parameters = {
        {U"status", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::None)}};
    make_native_function(ctx, "exit", exit_signature, sys_exit, {}, function);
    if (!store(U"exit"))
        return;
    SignatureLayout display_signature;
    display_signature.parameters = {{U"value", ParameterKind::PositionalOnly}};
    make_native_function(ctx, "displayhook", display_signature, sys_displayhook, {}, function);
    if (!store(U"displayhook") || !store(U"__displayhook__"))
        return;
    make_str(ctx, U"", function);
    if (!store(U"executable"))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
void display_value(Attachment &ctx, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot sys(ctx), hook(ctx);
    import_module(ctx, U"sys", sys);
    if (failed(sys)) {
        out.raised(sys.exception());
        return;
    }
    if (!attribute(ctx, roots.root(sys.value()), U"displayhook", hook)) {
        out.raised(hook.exception());
        return;
    }
    const Value argument = value.get(ctx);
    call(ctx, roots.root(hook.value()), {{&argument, 1}, {}}, out);
}
} // namespace cxxpython::core::detail
