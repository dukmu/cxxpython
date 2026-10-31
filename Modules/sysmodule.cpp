// Adapted from CPython 3.12.14 Python/sysmodule.c and Objects/exceptions.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/sysmodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/namespaceobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"
#include "internal/import.hpp"

#include <bit>
#include <cfloat>
#include <limits>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
constexpr const char32_t *version_names[] = {U"major", U"minor", U"micro", U"releaselevel",
                                             U"serial"};
constexpr const char32_t *float_names[] = {U"max",     U"max_exp",    U"max_10_exp", U"min",
                                           U"min_exp", U"min_10_exp", U"dig",        U"mant_dig",
                                           U"epsilon", U"radix",      U"rounds"};
template <std::size_t I> void info_field(Attachment &ctx, Value self, ReturnSlot &out)
{
    out.ok(ctx.payload<TupleObject>(self).items[I]);
}
template <bool Float>
void info_repr(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    const auto names = Float ? std::span<const char32_t *const>(float_names)
                             : std::span<const char32_t *const>(version_names);
    std::u32string text = Float ? U"sys.float_info(" : U"sys.version_info(";
    ReturnSlot item(ctx);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i)
            text += U", ";
        text += names[i];
        text += U"=";
        repr(ctx, roots.root(ctx.payload<TupleObject>(self).items[i]), item);
        if (failed(item)) {
            out.raised(item.exception());
            return;
        }
        text += ctx.payload<StrObject>(item.value()).value;
    }
    text += U")";
    make_str(ctx, std::move(text), out);
}
void version_new(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::TypeErrorType, U"cannot create 'sys.version_info' instances", out);
}
void float_info_new(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    if (type.get(ctx) != ctx.payload<NativeFunctionObject>(function).captures[0]) {
        raise_error(ctx, CoreObject::TypeErrorType, U"invalid sys.float_info type", out);
        return;
    }
    if (!ctx.has_layout<DictObject>(args[2])) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"sys.float_info() takes a dict as second arg, if any", out);
        return;
    }
    ReturnSlot sequence(ctx);
    const std::array input{args[1]};
    call(ctx, roots.root(ctx.builtin(CoreObject::TupleType)), {input}, sequence);
    if (failed(sequence)) {
        out.raised(sequence.exception());
        return;
    }
    const auto &items = ctx.payload<TupleObject>(sequence.value()).items;
    if (items.size() != 11) {
        auto count = std::to_string(items.size());
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"sys.float_info() takes a 11-sequence (" +
                        std::u32string(count.begin(), count.end()) + U"-sequence given)",
                    out);
        return;
    }
    ctx.allocate_instance<TupleObject>(type, out, items);
}
void make_info_type(Attachment &ctx, bool floating, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    const Handle bases[] = {roots.root(ctx.builtin(CoreObject::TupleType))};
    TypeSpec spec{floating ? U"float_info" : U"version_info", bases};
    spec.module = U"sys";
    spec.slots = std::vector<std::u32string>{};
    ReturnSlot temporary(ctx);
    create_native_type(ctx, NativeTypeSpec{spec, LayoutDescriptor::of<TupleObject>()}, temporary);
    if (failed(temporary)) {
        out.raised(temporary.exception());
        return;
    }
    auto type = roots.root(temporary.value());
    const auto names = floating ? std::span<const char32_t *const>(float_names)
                                : std::span<const char32_t *const>(version_names);
    const NativeGetter getters[] = {info_field<0>, info_field<1>, info_field<2>, info_field<3>,
                                    info_field<4>, info_field<5>, info_field<6>, info_field<7>,
                                    info_field<8>, info_field<9>, info_field<10>};
    for (std::size_t i = 0; i < names.size(); ++i)
        install_native_field(ctx, core, type, names[i], getters[i]);
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, type, U"__repr__", signature,
                             floating ? info_repr<true> : info_repr<false>);
    signature.parameters[0].name = U"cls";
    if (floating) {
        ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), temporary);
        auto defaults = roots.root(temporary.value());
        signature.parameters.push_back({U"sequence"});
        signature.parameters.push_back(
            {U"dict", ParameterKind::PositionalOrKeyword, defaults.get(ctx)});
    } else {
        signature.varargs = U"args";
        signature.varkwargs = U"kwargs";
    }
    install_native_attribute(ctx, core, type, U"__new__", signature,
                             floating ? float_info_new : version_new, {type.get(ctx)});
    auto dictionary = roots.root(*TypeView(ctx, type).dictionary());
    auto constructor = roots.root(*name_lookup(ctx, dictionary.get(ctx), U"__new__"));
    ctx.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], temporary,
        constructor.get(ctx));
    auto function = roots.root(temporary.value());
    make_str(ctx, U"__new__", temporary);
    name_store(ctx, dictionary.get(ctx), temporary.value(), function.get(ctx));
    for (const auto &[name, number] : {std::pair{U"n_fields", static_cast<int>(names.size())},
                                       {U"n_sequence_fields", static_cast<int>(names.size())},
                                       {U"n_unnamed_fields", 0}}) {
        make_int(ctx, number, temporary);
        auto value = roots.root(temporary.value());
        make_str(ctx, name, temporary);
        name_store(ctx, dictionary.get(ctx), temporary.value(), value.get(ctx));
    }
    ctx.payload<TypeObject>(type).subclassable = false;
    ctx.payload<TypeObject>(type).immutable = true;
    out.ok(type.get(ctx));
}
void make_version_info(Attachment &ctx, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot temporary(ctx);
    make_info_type(ctx, false, temporary);
    if (failed(temporary)) {
        out.raised(temporary.exception());
        return;
    }
    auto type = roots.root(temporary.value());
    std::vector<Value> items;
    for (auto number : {3, 12, 14}) {
        make_int(ctx, number, temporary);
        items.push_back(roots.root(temporary.value()).get(ctx));
    }
    make_str(ctx, U"final", temporary);
    items.push_back(roots.root(temporary.value()).get(ctx));
    make_int(ctx, 0, temporary);
    items.push_back(roots.root(temporary.value()).get(ctx));
    ctx.allocate_instance<TupleObject>(type, out, std::move(items));
}
void make_float_info(Attachment &ctx, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot temporary(ctx);
    make_info_type(ctx, true, temporary);
    if (failed(temporary)) {
        out.raised(temporary.exception());
        return;
    }
    auto type = roots.root(temporary.value());
    std::vector<Value> items;
    for (std::size_t i = 0; i < 11; ++i) {
        if (i == 0 || i == 3 || i == 8)
            make_float(ctx, i == 0 ? DBL_MAX : i == 3 ? DBL_MIN : DBL_EPSILON, temporary);
        else {
            const int numbers[] = {0,           DBL_MAX_EXP,    DBL_MAX_10_EXP, 0,
                                   DBL_MIN_EXP, DBL_MIN_10_EXP, DBL_DIG,        DBL_MANT_DIG,
                                   0,           FLT_RADIX,      FLT_ROUNDS};
            make_int(ctx, numbers[i], temporary);
        }
        items.push_back(roots.root(temporary.value()).get(ctx));
    }
    ctx.allocate_instance<TupleObject>(type, out, std::move(items));
}
template <bool Errors>
void filesystem_encoding(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    make_str(ctx, Errors ? U"surrogateescape" : U"utf-8", out);
}

void sys_exception(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(ctx.handled_exception().value_or(ctx.builtin(CoreObject::None)));
}
void recursion_limit(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    make_int(ctx, static_cast<std::int64_t>(ctx.call_depth_limit()), out);
}

void sys_exc_info(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto none = ctx.builtin(CoreObject::None);
    const auto exception = roots.root(ctx.handled_exception().value_or(none));
    const bool active = exception.get(ctx) != none;
    const auto type = roots.root(active ? ctx.type_of(exception) : none);
    ReturnSlot traceback(ctx);
    if (active) {
        ctx.load(exception.get(ctx), ctx.payload<ExceptionObject>(exception).traceback, traceback);
        if (failed(traceback)) {
            out.raised(traceback.exception());
            return;
        }
    }
    const auto trace = roots.root(traceback.tag() == ResultTag::Ok ? traceback.value() : none);
    const std::array values{type.get(ctx), exception.get(ctx), trace.get(ctx)};
    cxxpython::core::make_tuple(ctx, values, out);
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

// CPython 3.12.14 Python/sysmodule.c: only exact str objects may be interned.
void sys_intern(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto value = roots.root(args[0]);
    auto type = roots.root(ctx.type_of(value));
    if (type.get(ctx) != ctx.builtin(CoreObject::StrType)) {
        TypeView view(ctx, type);
        const auto name = view.name();
        const auto text = std::holds_alternative<std::u32string>(name)
                              ? std::get<std::u32string>(name)
                              : ctx.payload<StrObject>(std::get<Value>(name)).value;
        const bool string_subclass =
            is_subtype(ctx, type.get(ctx), ctx.builtin(CoreObject::StrType));
        raise_error(ctx, CoreObject::TypeErrorType,
                    string_subclass ? U"can't intern " + text
                                    : U"intern() argument must be str, not " + text,
                    out);
        return;
    }
    ctx.intern(value, out);
}

// Match _PySys_GetSizeOf: use the type's special method, not an instance attribute.
void sys_getsizeof(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto object = roots.root(args[0]);
    auto fallback = roots.root(args[1]);
    const bool has_default =
        fallback.get(ctx) != ctx.payload<NativeFunctionObject>(function).captures[0];
    ReturnSlot result(ctx);
    special(ctx, object.get(ctx), U"__sizeof__", {}, result);
    if (!failed(result)) {
        if (!is_int(ctx, result.value()))
            raise_error(ctx, CoreObject::TypeErrorType, U"an integer is required", result);
        else {
            std::int64_t size;
            if (!int_fits_signed(ctx, result.value(), size) ||
                size > std::numeric_limits<std::ptrdiff_t>::max() ||
                size < std::numeric_limits<std::ptrdiff_t>::min())
                raise_error(ctx, CoreObject::OverflowErrorType,
                            U"Python int too large to convert to C ssize_t", result);
            else if (size < 0)
                raise_error(ctx, CoreObject::ValueErrorType, U"__sizeof__() should return >= 0",
                            result);
            else {
                const auto total = static_cast<std::size_t>(size) + ctx.gc_metadata_size(object);
                make_int_decimal(ctx, std::to_string(total), out);
                return;
            }
        }
    }
    if (has_default &&
        is_subtype(ctx, ctx.type_of(result.exception()), ctx.builtin(CoreObject::TypeErrorType)))
        out.ok(fallback.get(ctx));
    else
        out.raised(result.exception());
}

bool frame_depth(Attachment &ctx, Value value, int &depth, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    index(ctx, roots.root(value), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    std::int64_t number;
    if (!int_fits_signed(ctx, converted.value(), number) ||
        number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    depth = static_cast<int>(number);
    return true;
}

// Walk only this attachment's active chain, rooting each atomic back-edge load.
void find_frame(Attachment &ctx, int depth, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto current = ctx.current_frame();
    if (!current)
        return;
    auto frame = roots.root(*current);
    ReturnSlot previous(ctx);
    while (depth > 0) {
        ctx.load(frame.get(ctx), ctx.payload<FrameObject>(frame).back, previous);
        if (previous.tag() == ResultTag::Empty)
            return;
        frame = roots.root(previous.value());
        --depth;
    }
    out.ok(frame.get(ctx));
}

void sys_getframe(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    int depth;
    if (!frame_depth(ctx, args[0], depth, out))
        return;
    ReturnSlot frame(ctx);
    find_frame(ctx, depth, frame);
    if (frame.tag() == ResultTag::Empty)
        raise_error(ctx, CoreObject::ValueErrorType, U"call stack is not deep enough", out);
    else
        out.ok(frame.value());
}

void sys_getframemodulename(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    int depth;
    if (!frame_depth(ctx, args[0], depth, out))
        return;
    ReturnSlot frame(ctx);
    find_frame(ctx, depth, frame);
    if (frame.tag() == ResultTag::Empty) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    RootScope roots(ctx);
    auto owner = roots.root(frame.value());
    auto function = roots.root(ctx.payload<FrameObject>(owner).function);
    ctx.load(function.get(ctx), ctx.payload<FunctionObject>(function).module, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(ctx.builtin(CoreObject::None));
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
    ReturnSlot stream(ctx), method(ctx), text(ctx), result(ctx);
    if (!set_last(ctx, roots.root(ctx.builtin(CoreObject::None)), out))
        return;
    get_sys_attribute(ctx, U"stdout", stream);
    if (failed(stream)) {
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

void get_sys_attribute(Attachment &ctx, std::u32string_view name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot dictionary(ctx), key(ctx);
    ctx.load_sys_dictionary(dictionary);
    if (dictionary.tag() == ResultTag::Empty) {
        initialize_imports(ctx, out);
        if (failed(out))
            return;
        ctx.load_sys_dictionary(dictionary);
    }
    if (failed(dictionary)) {
        out.raised(dictionary.exception());
        return;
    }
    if (dictionary.tag() != ResultTag::Empty) {
        const auto sysdict = roots.root(dictionary.value());
        make_str(ctx, std::u32string(name), key);
        if (failed(key)) {
            out.raised(key.exception());
            return;
        }
        dict_lookup(ctx, sysdict, roots.root(key.value()), out);
        if (out.tag() != ResultTag::Empty)
            return;
    }
    raise_error(ctx, CoreObject::RuntimeErrorType, U"lost sys." + std::u32string(name), out);
}

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
    std::vector<Value> builtin_modules;
    for (auto &name : builtin_names(ctx)) {
        make_str(ctx, std::move(name), function);
        builtin_modules.push_back(roots.root(function.value()).get(ctx));
    }
    cxxpython::core::make_tuple(ctx, builtin_modules, function);
    if (!store(U"builtin_module_names"))
        return;
    make_native_function(ctx, "getfilesystemencoding", {}, filesystem_encoding<false>, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"getfilesystemencoding"))
        return;
    make_native_function(ctx, "getfilesystemencodeerrors", {}, filesystem_encoding<true>, {},
                         function, NativeFunctionBinding::Function);
    if (!store(U"getfilesystemencodeerrors"))
        return;
    make_native_function(ctx, "exception", {}, sys_exception, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"exception"))
        return;
    make_native_function(ctx, "exc_info", {}, sys_exc_info, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"exc_info"))
        return;
    make_native_function(ctx, "getrecursionlimit", {}, recursion_limit, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"getrecursionlimit"))
        return;
    SignatureLayout exit_signature;
    exit_signature.parameters = {
        {U"status", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::None)}};
    make_native_function(ctx, "exit", exit_signature, sys_exit, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"exit"))
        return;
    SignatureLayout display_signature;
    display_signature.parameters = {{U"value", ParameterKind::PositionalOnly}};
    make_native_function(ctx, "displayhook", display_signature, sys_displayhook, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"displayhook") || !store(U"__displayhook__"))
        return;
    SignatureLayout intern_signature;
    intern_signature.parameters = {{U"string", ParameterKind::PositionalOnly}};
    make_native_function(ctx, "intern", intern_signature, sys_intern, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"intern"))
        return;
    make_list(ctx, {}, function);
    auto missing = roots.root(function.value());
    SignatureLayout sizeof_signature;
    sizeof_signature.parameters = {
        {U"object", ParameterKind::PositionalOrKeyword},
        {U"default", ParameterKind::PositionalOrKeyword, missing.get(ctx)}};
    const Value missing_value = missing.get(ctx);
    make_native_function(ctx, "getsizeof", sizeof_signature, sys_getsizeof, {&missing_value, 1},
                         function, NativeFunctionBinding::Function);
    if (!store(U"getsizeof"))
        return;
    ReturnSlot zero(ctx);
    make_int(ctx, 0, zero);
    SignatureLayout frame_signature;
    frame_signature.parameters = {{U"depth", ParameterKind::PositionalOnly, zero.value()}};
    make_native_function(ctx, "_getframe", frame_signature, sys_getframe, {}, function,
                         NativeFunctionBinding::Function);
    if (!store(U"_getframe"))
        return;
    frame_signature.parameters[0].kind = ParameterKind::PositionalOrKeyword;
    make_native_function(ctx, "_getframemodulename", frame_signature, sys_getframemodulename, {},
                         function, NativeFunctionBinding::Function);
    if (!store(U"_getframemodulename"))
        return;
    make_str(ctx, U"", function);
    if (!store(U"executable"))
        return;
    make_str(ctx, std::endian::native == std::endian::little ? U"little" : U"big", function);
    if (!store(U"byteorder"))
        return;
#if defined(__linux__)
    make_str(ctx, U"linux", function);
#elif defined(__APPLE__)
    make_str(ctx, U"darwin", function);
#else
    make_str(ctx, U"unknown", function);
#endif
    if (!store(U"platform"))
        return;
    make_int(ctx, std::numeric_limits<std::ptrdiff_t>::max(), function);
    if (!store(U"maxsize"))
        return;
    make_int(ctx, 0x10ffff, function);
    if (!store(U"maxunicode"))
        return;
    make_version_info(ctx, function);
    if (failed(function)) {
        out.raised(function.exception());
        return;
    }
    if (!store(U"version_info"))
        return;
    make_float_info(ctx, function);
    if (failed(function)) {
        out.raised(function.exception());
        return;
    }
    if (!store(U"float_info"))
        return;
    make_str(ctx, U"3.12.14 (cxxpython)", function);
    if (!store(U"version"))
        return;
    make_int(ctx, 0x030c0ef0, function);
    if (!store(U"hexversion"))
        return;
    make_list(ctx, {}, function);
    if (!store(U"warnoptions"))
        return;
    call(ctx, roots.root(ctx.builtin(CoreObject::SimpleNamespaceType)), {}, function);
    if (failed(function)) {
        out.raised(function.exception());
        return;
    }
    auto implementation = roots.root(function.value());
    if (!store(U"implementation"))
        return;
    ReturnSlot implementation_dict(ctx), value(ctx);
    ctx.load(implementation.get(ctx), *ctx.instance_dictionary(implementation.get(ctx)),
             implementation_dict);
    auto metadata = roots.root(implementation_dict.value());
    make_str(ctx, U"name", key);
    make_str(ctx, U"cxxpython", value);
    dict_store(ctx, metadata, roots.root(key.value()), roots.root(value.value()), out);
    if (failed(out))
        return;
    make_str(ctx, U"cache_tag", key);
    dict_store(ctx, metadata, roots.root(key.value()), roots.root(ctx.builtin(CoreObject::None)),
               out);
    if (failed(out))
        return;
    install_error_hooks(ctx, sys, out);
}
void display_value(Attachment &ctx, Handle value, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot hook(ctx);
    get_sys_attribute(ctx, U"displayhook", hook);
    if (failed(hook)) {
        out.raised(hook.exception());
        return;
    }
    const Value argument = value.get(ctx);
    call(ctx, roots.root(hook.value()), {{&argument, 1}, {}}, out);
}
} // namespace cxxpython::core::detail
