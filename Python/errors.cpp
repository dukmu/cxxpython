// Adapted from CPython 3.12.14 Python/errors.c and Python/sysmodule.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/errors.hpp"

#include "cxxpython/protocol.hpp"
#include "internal/pythonrun.hpp"
#include "internal/sysmodule.hpp"

#include <array>
#include <cstdio>
#include <new>
#include <unordered_set>
#include <vector>

namespace cxxpython::core::detail {
std::u32string diagnostic_type_name(Attachment &ctx, Handle type)
{
    TypeView view(ctx, type);
    const auto stored = view.name();
    auto name = std::holds_alternative<Value>(stored)
                    ? ctx.payload<StrObject>(std::get<Value>(stored)).value
                    : std::get<std::u32string>(stored);
    const auto &module = ctx.payload<TypeObject>(type).declared_module;
    if (module && *module != U"builtins" && !module->empty())
        name = *module + U"." + name;
    return name;
}
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void attribute(Attachment &ctx, Handle object, std::u32string_view name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, std::u32string(name), key);
    if (failed(key)) {
        out.raised(key.exception());
        return;
    }
    get_attribute(ctx, object, roots.root(key.value()), out);
}

void put(Attachment &ctx, Handle object, std::u32string_view name, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, std::u32string(name), key);
    if (failed(key)) {
        out.raised(key.exception());
        return;
    }
    set_attribute(ctx, object, roots.root(key.value()), value, out);
}

template <std::size_t I> void field(Attachment &ctx, Value self, ReturnSlot &out)
{
    out.ok(ctx.payload<TupleObject>(self).items[I]);
}
void args_repr(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    if (!ctx.enter_repr(self)) {
        make_str(ctx, U"UnraisableHookArgs(...)", out);
        return;
    }
    try {
        constexpr std::array names{U"exc_type=", U"exc_value=", U"exc_traceback=", U"err_msg=",
                                   U"object="};
        std::u32string text = U"UnraisableHookArgs(";
        ReturnSlot part(ctx);
        const auto &items = ctx.payload<TupleObject>(self).items;
        for (std::size_t i = 0; i < names.size(); ++i) {
            repr(ctx, roots.root(items[i]), part);
            if (failed(part)) {
                out.raised(part.exception());
                ctx.leave_repr(self.get(ctx));
                return;
            }
            if (i)
                text += U", ";
            text += names[i];
            text += ctx.payload<StrObject>(part.value()).value;
        }
        make_str(ctx, text + U")", out);
    } catch (...) {
        ctx.leave_repr(self.get(ctx));
        throw;
    }
    ctx.leave_repr(self.get(ctx));
}

void args_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    if (cls.get(ctx) != ctx.builtin(CoreObject::UnraisableHookArgsType)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid UnraisableHookArgs type", out);
        return;
    }
    auto source = roots.root(args[1]);
    ReturnSlot iterator(ctx), item(ctx);
    get_iter(ctx, source, iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    auto iter = roots.root(iterator.value());
    std::vector<Handle> values;
    for (;;) {
        auto status = iter_next(ctx, iter, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        values.push_back(roots.root(item.value()));
    }
    if (values.size() != 5) {
        raise_error(ctx, CoreObject::TypeErrorType, U"UnraisableHookArgs() takes a 5-sequence",
                    out);
        return;
    }
    std::vector<Value> items;
    for (auto value : values)
        items.push_back(value.get(ctx));
    ctx.allocate_instance<TupleObject>(cls, out, std::move(items));
}

bool write(Attachment &ctx, Handle stream, std::u32string text, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot method(ctx), value(ctx);
    attribute(ctx, stream, U"write", method);
    if (failed(method)) {
        out.raised(method.exception());
        return false;
    }
    auto writer = roots.root(method.value());
    make_str(ctx, std::move(text), value);
    if (failed(value)) {
        out.raised(value.exception());
        return false;
    }
    const Value argument = value.value();
    call(ctx, writer, {{&argument, 1}, {}}, out);
    return !failed(out);
}

std::u32string decimal(std::int64_t number)
{
    const auto s = std::to_string(number);
    return {s.begin(), s.end()};
}

// The traceback nodes and their frames remain rooted throughout each allocation.
bool print_trace(Attachment &ctx, Handle stream, Handle traceback, ReturnSlot &out)
{
    if (traceback.get(ctx) == ctx.builtin(CoreObject::None))
        return true;
    RootScope roots(ctx);
    auto current = roots.root(traceback.get(ctx));
    if (!write(ctx, stream, U"Traceback (most recent call last):\n", out))
        return false;
    std::unordered_set<ObjectHeader *> seen;
    while (current.get(ctx) != ctx.builtin(CoreObject::None) &&
           seen.insert(current.get(ctx).object()).second) {
        const auto &node = ctx.payload<TracebackObject>(current);
        const auto &frame = ctx.payload<FrameObject>(node.frame);
        const auto &code = ctx.payload<CodeObject>(frame.code);
        auto line = U"  File \"" + ctx.payload<StrObject>(code.filename).value + U"\", line " +
                    decimal(node.lineno) + U", in " + ctx.payload<StrObject>(code.name).value +
                    U"\n";
        if (!write(ctx, stream, std::move(line), out))
            return false;
        ReturnSlot next(ctx);
        ctx.load(current.get(ctx), ctx.payload<TracebackObject>(current).next, next);
        if (next.tag() == ResultTag::Empty)
            break;
        current = roots.root(next.value());
    }
    return true;
}

bool print_one_exception(Attachment &ctx, Handle stream, Handle exception, Handle traceback,
                         ReturnSlot &out)
{
    RootScope roots(ctx);
    if (!print_trace(ctx, stream, traceback, out))
        return false;
    auto type = roots.root(ctx.type_of(exception));
    const auto name = TypeView(ctx, type).qualname();
    std::u32string label = std::holds_alternative<Value>(name)
                               ? ctx.payload<StrObject>(std::get<Value>(name)).value
                               : std::get<std::u32string>(name);
    ReturnSlot module(ctx), text(ctx);
    attribute(ctx, type, U"__module__", module);
    if (!failed(module) && ctx.has_layout<StrObject>(module.value())) {
        const auto &prefix = ctx.payload<StrObject>(module.value()).value;
        if (prefix != U"builtins" && prefix != U"__main__")
            label = prefix + U"." + label;
    }
    str(ctx, exception, text);
    if (failed(text))
        label += U": <exception str() failed>";
    else if (!ctx.payload<StrObject>(text.value()).value.empty())
        label += U": " + ctx.payload<StrObject>(text.value()).value;
    label += U"\n";
    return write(ctx, stream, std::move(label), out);
}

Handle traceback_of(Attachment &ctx, Handle exception, RootScope &roots)
{
    ReturnSlot trace(ctx);
    ctx.load(exception.get(ctx), ctx.payload<ExceptionObject>(exception).traceback, trace);
    return roots.root(trace.tag() == ResultTag::Ok ? trace.value() : ctx.builtin(CoreObject::None));
}

bool print_exception(Attachment &ctx, Handle stream, Handle exception, Handle traceback,
                     ReturnSlot &out)
{
    RootScope roots(ctx);
    enum class Relation { Context, Cause };
    std::vector<std::pair<Handle, Relation>> chain;
    std::unordered_set<ObjectHeader *> seen;
    auto current = roots.root(exception.get(ctx));
    ReturnSlot link(ctx);
    while (seen.insert(current.get(ctx).object()).second) {
        auto relation = Relation::Context;
        const auto &data = ctx.payload<ExceptionObject>(current);
        ctx.load(current.get(ctx), data.cause, link);
        if (link.tag() == ResultTag::Ok)
            relation = Relation::Cause;
        else if (!data.suppress_context.load(std::memory_order_acquire))
            ctx.load(current.get(ctx), data.context, link);
        chain.push_back({current, relation});
        if (link.tag() != ResultTag::Ok)
            break;
        current = roots.root(link.value());
    }
    for (auto i = chain.size(); i-- > 0;) {
        const auto [owner, relation] = chain[i];
        if (i + 1 < chain.size()) {
            const auto separator =
                relation == Relation::Cause
                    ? U"\nThe above exception was the direct cause of the following exception:\n\n"
                    : U"\nDuring handling of the above exception, another exception occurred:\n\n";
            if (!write(ctx, stream, separator, out))
                return false;
        }
        auto trace = i == 0 ? roots.root(traceback.get(ctx)) : traceback_of(ctx, owner, roots);
        if (!print_one_exception(ctx, stream, owner, trace, out))
            return false;
    }
    return true;
}

void raw_fallback(Attachment &ctx, Handle exception) noexcept
{
    try {
        report_exception_fallback(ctx, exception.get(ctx));
    } catch (...) {
        NativeWait parked(ctx);
        std::fputs("Exception ignored (diagnostic unavailable)\n", stderr);
    }
}

bool stream_for(Attachment &ctx, ReturnSlot &out)
{
    get_sys_attribute(ctx, U"stderr", out);
    return !failed(out);
}

bool unraisable_default(Attachment &ctx, Handle exception, Handle trace, Handle message,
                        Handle object, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot stream(ctx), text(ctx);
    if (!stream_for(ctx, stream)) {
        out.raised(stream.exception());
        return false;
    }
    if (stream.value() == ctx.builtin(CoreObject::None)) {
        out.ok(ctx.builtin(CoreObject::None));
        return true;
    }
    auto file = roots.root(stream.value());
    const bool has_object = object.get(ctx) != ctx.builtin(CoreObject::None);
    const bool has_message = message.get(ctx) != ctx.builtin(CoreObject::None);
    if (has_message) {
        str(ctx, message, text);
        if (failed(text)) {
            out.raised(text.exception());
            return false;
        }
        if (!write(ctx, file,
                   ctx.payload<StrObject>(text.value()).value + (has_object ? U": " : U":\n"), out))
            return false;
    } else if (has_object && !write(ctx, file, U"Exception ignored in: ", out))
        return false;
    if (has_object) {
        repr(ctx, object, text);
        auto repr_text = failed(text) ? std::u32string(U"<object repr() failed>")
                                      : ctx.payload<StrObject>(text.value()).value;
        if (!write(ctx, file, repr_text + U"\n", out))
            return false;
    }
    if (!print_exception(ctx, file, exception, trace, out))
        return false;
    attribute(ctx, file, U"flush", text);
    if (failed(text)) {
        out.raised(text.exception());
        return false;
    }
    call(ctx, roots.root(text.value()), {}, out);
    if (failed(out))
        return false;
    out.ok(ctx.builtin(CoreObject::None));
    return true;
}

void default_unraisable(Attachment &ctx, Handle fn, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto value = roots.root(args[0]);
    const auto &captures = ctx.payload<NativeFunctionObject>(fn).captures;
    if (ctx.type_of(value) != captures[0]) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"sys.unraisablehook argument type must be UnraisableHookArgs", out);
        return;
    }
    const auto fields = ctx.payload<TupleObject>(value).items;
    unraisable_default(ctx, roots.root(fields[1]), roots.root(fields[2]), roots.root(fields[3]),
                       roots.root(fields[4]), out);
}

void default_excepthook(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto exception = roots.root(args[1]);
    auto trace = roots.root(args[2]);
    if (!ctx.has_layout<ExceptionObject>(exception.get(ctx)) ||
        (trace.get(ctx) != ctx.builtin(CoreObject::None) &&
         !ctx.has_layout<TracebackObject>(trace.get(ctx)))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid exception or traceback", out);
        return;
    }
    ReturnSlot stream(ctx);
    if (!stream_for(ctx, stream)) {
        out.raised(stream.exception());
        return;
    }
    if (stream.value() == ctx.builtin(CoreObject::None)) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    if (print_exception(ctx, roots.root(stream.value()), exception, trace, out))
        out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

void install_error_types(Attachment &ctx, std::span<const Handle> core)
{
    RootScope roots(ctx);
    ReturnSlot out(ctx);
    auto type = core[static_cast<std::size_t>(CoreObject::UnraisableHookArgsType)];
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"sequence", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, type, U"__new__", constructor, args_new);
    auto dictionary = roots.root(*TypeView(ctx, type).dictionary());
    auto function = roots.root(*name_lookup(ctx, dictionary.get(ctx), U"__new__"));
    ctx.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], out, function.get(ctx));
    auto wrapper = roots.root(out.value());
    ctx.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], out,
                                     U"__new__");
    name_store(ctx, dictionary.get(ctx), out.value(), wrapper.get(ctx));
    install_native_field(ctx, core, type, U"exc_type", field<0>);
    install_native_field(ctx, core, type, U"exc_value", field<1>);
    install_native_field(ctx, core, type, U"exc_traceback", field<2>);
    install_native_field(ctx, core, type, U"err_msg", field<3>);
    install_native_field(ctx, core, type, U"object", field<4>);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, type, U"__repr__", unary, args_repr);
    for (const auto &[name, count] : {std::pair{U"n_fields", 5}, std::pair{U"n_sequence_fields", 5},
                                      std::pair{U"n_unnamed_fields", 0}}) {
        ctx.allocate_instance<IntObject>(core[static_cast<std::size_t>(CoreObject::IntType)], out,
                                         count);
        auto value = roots.root(out.value());
        ctx.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], out,
                                         name);
        name_store(ctx, dictionary.get(ctx), out.value(), value.get(ctx));
    }
}
void install_error_hooks(Attachment &ctx, Handle sys, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(ctx.builtin(CoreObject::UnraisableHookArgsType));
    SignatureLayout one;
    one.parameters = {{U"unraisable", ParameterKind::PositionalOnly}};
    const std::array captures{type.get(ctx)};
    make_native_function(ctx, "unraisablehook", one, default_unraisable, captures, out,
                         NativeFunctionBinding::Function);
    if (failed(out))
        return;
    auto hook = roots.root(out.value());
    put(ctx, sys, U"unraisablehook", hook, out);
    if (failed(out))
        return;
    put(ctx, sys, U"__unraisablehook__", hook, out);
    if (failed(out))
        return;
    SignatureLayout three;
    three.parameters = {{U"exctype", ParameterKind::PositionalOnly},
                        {U"value", ParameterKind::PositionalOnly},
                        {U"traceback", ParameterKind::PositionalOnly}};
    make_native_function(ctx, "excepthook", three, default_excepthook, {}, out,
                         NativeFunctionBinding::Function);
    if (failed(out))
        return;
    hook = roots.root(out.value());
    put(ctx, sys, U"excepthook", hook, out);
    if (failed(out))
        return;
    put(ctx, sys, U"__excepthook__", hook, out);
}

void write_unraisable(Attachment &ctx, Handle exception, std::optional<Handle> object,
                      std::optional<Handle> err_msg)
{
    try {
        RootScope roots(ctx);
        auto error = roots.root(exception.get(ctx));
        auto obj = roots.root(object ? object->get(ctx) : ctx.builtin(CoreObject::None));
        auto msg = roots.root(err_msg ? err_msg->get(ctx) : ctx.builtin(CoreObject::None));
        auto trace = traceback_of(ctx, error, roots);
        ReturnSlot hook(ctx), result(ctx);
        std::vector<Value> fields{ctx.type_of(error), error.get(ctx), trace.get(ctx), msg.get(ctx),
                                  obj.get(ctx)};
        ctx.allocate_instance<TupleObject>(
            roots.root(ctx.builtin(CoreObject::UnraisableHookArgsType)), result, std::move(fields));
        if (failed(result)) {
            raw_fallback(ctx, error);
            return;
        }
        auto arguments = roots.root(result.value());
        get_sys_attribute(ctx, U"unraisablehook", hook);
        if (failed(hook) || hook.value() == ctx.builtin(CoreObject::None)) {
            unraisable_default(ctx, error, trace, msg, obj, result);
            return;
        }
        auto callable = roots.root(hook.value());
        const Value arg = arguments.get(ctx);
        call(ctx, callable, {{&arg, 1}, {}}, result);
        if (!failed(result))
            return;
        auto hook_error = roots.root(result.exception());
        auto hook_trace = traceback_of(ctx, hook_error, roots);
        make_str(ctx, U"Exception ignored in sys.unraisablehook", result);
        auto message = roots.root(failed(result) ? ctx.builtin(CoreObject::None) : result.value());
        unraisable_default(ctx, hook_error, hook_trace, message, callable, result);
    } catch (const std::bad_alloc &) {
        raw_fallback(ctx, exception);
    }
}

void display_exception(Attachment &ctx, Handle exception)
{
    RootScope roots(ctx);
    auto error = roots.root(exception.get(ctx));
    auto trace = traceback_of(ctx, error, roots);
    ReturnSlot hook(ctx), result(ctx);
    get_sys_attribute(ctx, U"excepthook", hook);
    if (!failed(hook)) {
        const std::array args{ctx.type_of(error), error.get(ctx), trace.get(ctx)};
        call(ctx, roots.root(hook.value()), {args, {}}, result);
        if (!failed(result))
            return;
        auto hook_error = roots.root(result.exception());
        auto hook_trace = traceback_of(ctx, hook_error, roots);
        ReturnSlot stream(ctx), output(ctx);
        if (stream_for(ctx, stream) && stream.value() != ctx.builtin(CoreObject::None)) {
            auto file = roots.root(stream.value());
            if (write(ctx, file, U"Error in sys.excepthook:\n", output) &&
                print_exception(ctx, file, hook_error, hook_trace, output) &&
                write(ctx, file, U"\nOriginal exception was:\n", output) &&
                print_exception(ctx, file, error, trace, output))
                return;
        }
        report_exception_fallback(ctx, hook_error.get(ctx));
    }
    report_exception_fallback(ctx, error.get(ctx));
}
} // namespace cxxpython::core::detail
