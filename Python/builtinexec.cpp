#include "internal/builtinexec.hpp"
#include "cxxpython/compiler.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

std::u32string diagnostic_text(std::string_view text);

template <std::size_t Index>
void syntax_field(Attachment &context, Value exception, ReturnSlot &out)
{
    context.load(exception, context.instance_slot(exception, Index), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}

template <std::size_t Index>
void set_syntax_field(Attachment &context, Value exception, std::optional<Value> value,
                      ReturnSlot &out)
{
    context.store(exception, context.instance_slot(exception, Index), value);
    out.ok(context.builtin(CoreObject::None));
}

// CPython 3.12.14 Objects/exceptions.c SyntaxError_init; tail slots are sorted by name.
void syntax_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<ExceptionObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]),
                    context.builtin(CoreObject::SyntaxErrorType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"SyntaxError.__init__ requires SyntaxError", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto arguments = roots.root(args[1]);
    context.store(self.get(context), context.payload<ExceptionObject>(self).args,
                  arguments.get(context));
    const auto &items = context.payload<TupleObject>(arguments).items;
    const auto count = items.size();
    if (count)
        context.store(self.get(context), context.instance_slot(self.get(context), 4), items[0]);
    if (count == 2) {
        auto info = roots.root(items[1]);
        ReturnSlot converted(context);
        const std::array values{info.get(context)};
        call(context, roots.root(context.builtin(CoreObject::TupleType)), {values}, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        auto location = roots.root(converted.value());
        context.store(self.get(context), context.instance_slot(self.get(context), 0), {});
        context.store(self.get(context), context.instance_slot(self.get(context), 1), {});
        const auto &parts = context.payload<TupleObject>(location).items;
        if (parts.size() < 4 || parts.size() > 6) {
            const auto prefix = parts.size() < 4 ? "function takes at least 4 arguments ("
                                                 : "function takes at most 6 arguments (";
            raise_error(
                context, CoreObject::TypeErrorType,
                diagnostic_text(std::string(prefix) + std::to_string(parts.size()) + " given)"),
                out);
            return;
        }
        constexpr std::array<std::size_t, 6> slots{2, 3, 5, 7, 0, 1};
        for (std::size_t i = 0; i < parts.size(); ++i)
            context.store(self.get(context), context.instance_slot(self.get(context), slots[i]),
                          parts[i]);
        if (parts.size() == 5) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"end_offset must be provided when end_lineno is provided", out);
            return;
        }
    }
    out.ok(context.builtin(CoreObject::None));
}

void syntax_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<ExceptionObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]),
                    context.builtin(CoreObject::SyntaxErrorType))) {
        raise_error(context, CoreObject::TypeErrorType, U"SyntaxError.__str__ requires SyntaxError",
                    out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    ReturnSlot field(context);
    syntax_field<4>(context, self.get(context), field);
    auto message = roots.root(field.value());
    syntax_field<2>(context, self.get(context), field);
    auto filename = roots.root(field.value());
    syntax_field<3>(context, self.get(context), field);
    auto lineno = roots.root(field.value());

    const bool have_filename = context.has_layout<StrObject>(filename.get(context));
    const bool have_lineno = context.type_of(lineno) == context.builtin(CoreObject::IntType);
    std::u32string basename;
    if (have_filename) {
        basename = context.payload<StrObject>(filename).value;
#ifdef _WIN32
        const auto slash = basename.find_last_of(U'\\');
#else
        const auto slash = basename.find_last_of(U'/');
#endif
        if (slash != std::u32string::npos)
            basename.erase(0, slash + 1);
    }
    std::int64_t number = -1;
    if (have_lineno) {
        std::int64_t converted;
        if (int_fits_signed(context, lineno.get(context), converted) &&
            converted >= std::numeric_limits<long>::min() &&
            converted <= std::numeric_limits<long>::max())
            number = converted;
    }
    ReturnSlot rendered(context);
    str(context, message, rendered);
    if (failed(rendered)) {
        out.raised(rendered.exception());
        return;
    }
    if (!have_filename && !have_lineno) {
        out.ok(rendered.value());
        return;
    }
    auto result = context.payload<StrObject>(rendered.value()).value;
    result += U" (";
    if (have_filename)
        result += basename;
    if (have_lineno) {
        if (have_filename)
            result += U", ";
        result += U"line ";
        result += diagnostic_text(std::to_string(number));
    }
    result += U")";
    make_str(context, std::move(result), out);
}

std::u32string diagnostic_text(std::string_view text)
{
    std::u32string result;
    for (std::size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i++]);
        char32_t point = c;
        unsigned count = 0;
        if ((c & 0xe0) == 0xc0) {
            point = c & 0x1f;
            count = 1;
        } else if ((c & 0xf0) == 0xe0) {
            point = c & 0x0f;
            count = 2;
        } else if ((c & 0xf8) == 0xf0) {
            point = c & 0x07;
            count = 3;
        }
        while (count-- && i < text.size())
            point = (point << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
        result.push_back(point);
    }
    return result;
}

} // namespace
void detail::raise_source_error(Attachment &context, const SourceError &error, ReturnSlot &out)
{
    if (error.kind == SourceErrorKind::Capability) {
        raise_error(context, CoreObject::RuntimeErrorType, diagnostic_text(error.what()), out);
        return;
    }
    RootScope roots(context);
    ReturnSlot value(context);
    std::vector<Value> fields;
    auto text = [&](std::string_view s) {
        make_str(context, diagnostic_text(s), value);
        fields.push_back(roots.root(value.value()).get(context));
    };
    auto number = [&](std::int64_t n) {
        make_int(context, n, value);
        fields.push_back(roots.root(value.value()).get(context));
    };
    text(error.filename);
    number(error.position.line);
    number(error.offset);
    text(error.source_line);
    number(error.position.end_line);
    number(error.end_offset);
    cxxpython::core::make_tuple(context, fields, value);
    auto location = roots.root(value.value());
    make_str(context, diagnostic_text(error.what()), value);
    auto message = roots.root(value.value());
    const std::array args{message.get(context), location.get(context)};
    auto kind = error.kind == SourceErrorKind::Tab           ? CoreObject::TabErrorType
                : error.kind == SourceErrorKind::Indentation ? CoreObject::IndentationErrorType
                                                             : CoreObject::SyntaxErrorType;
    call(context, roots.root(context.builtin(kind)), {args}, value);
    if (failed(value))
        out.raised(value.exception());
    else
        out.raised(value.value());
}

namespace {
bool source_text(Attachment &context, Value source, std::string &text, ReturnSlot &out)
{
    if (context.has_layout<StrObject>(source))
        text = source_utf8(context.payload<StrObject>(source).value);
    else if (is_bytes(context, source))
        text = context.payload<BytesObject>(source).value;
    else {
        raise_error(context, CoreObject::TypeErrorType, U"source must be a string or bytes", out);
        return false;
    }
    if (text.find('\0') != std::string::npos) {
        RootScope roots(context);
        ReturnSlot value(context);
        make_str(context, U"source code string cannot contain null bytes", value);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        const auto message = roots.root(value.value());
        const std::array args{message.get(context)};
        call(context, roots.root(context.builtin(CoreObject::SyntaxErrorType)), {args}, value);
        if (failed(value))
            out.raised(value.exception());
        else
            out.raised(value.value());
        return false;
    }
    return true;
}

void freeze_source(Attachment &context, Value source, std::string filename, InputMode mode,
                   ReturnSlot &out, std::uint32_t flags)
{
    std::string text;
    if (!source_text(context, source, text, out))
        return;
    if (mode == InputMode::Eval) {
        const auto first = text.find_first_not_of(" \t");
        text.erase(0, first == std::string::npos ? text.size() : first);
    }
    try {
        std::optional<SourceUnit> unit;
        {
            NativeWait parked(context);
            unit.emplace(SourceBuffer(std::move(text), std::move(filename)), mode, flags);
        }
        unit->freeze(context, out);
    } catch (const SourceError &error) {
        detail::raise_source_error(context, error, out);
    }
}
std::uint32_t caller_future_flags(Attachment &context)
{
    auto frame = context.current_frame();
    return frame ? context.payload<CodeObject>(context.payload<FrameObject>(*frame).code)
                       .program.future_flags()
                 : 0;
}

bool integer_arg(Attachment &context, Value value, std::int64_t &number, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(value), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(context, converted.value(), number)) {
        raise_error(context, CoreObject::OverflowErrorType, U"integer argument is too large", out);
        return false;
    }
    return true;
}

void compile_builtin(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<StrObject>(args[1]) || !context.has_layout<StrObject>(args[2])) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"compile() filename and mode must be strings", out);
        return;
    }
    std::int64_t flags, optimize, feature;
    RootScope roots(context);
    ReturnSlot inherit(context);
    truth(context, roots.root(args[4]), inherit);
    if (failed(inherit)) {
        out.raised(inherit.exception());
        return;
    }
    if (!integer_arg(context, args[3], flags, out) ||
        !integer_arg(context, args[5], optimize, out) ||
        !integer_arg(context, args[6], feature, out))
        return;
    constexpr auto accepted_flags = supported_future_flags | compile_dont_imply_dedent |
                                    compile_allow_incomplete_input | 0x10ULL;
    if (flags < 0 || (static_cast<std::uint64_t>(flags) & ~accepted_flags)) {
        raise_error(context, CoreObject::ValueErrorType, U"compile(): unrecognised flags", out);
        return;
    }
    if (optimize < -1 || optimize > 2) {
        raise_error(context, CoreObject::ValueErrorType, U"compile(): invalid optimize value", out);
        return;
    }
    if (optimize > 0 || feature != -1) {
        raise_error(context, CoreObject::ValueErrorType,
                    U"compile(): requested option is not supported", out);
        return;
    }
    const auto &mode = context.payload<StrObject>(args[2]).value;
    if (mode != U"exec" && mode != U"eval" && mode != U"single") {
        raise_error(context, CoreObject::ValueErrorType,
                    U"compile(): mode must be exec, eval or single", out);
        return;
    }
    freeze_source(context, args[0], source_utf8(context.payload<StrObject>(args[1]).value),
                  mode == U"eval"     ? InputMode::Eval
                  : mode == U"single" ? InputMode::Single
                                      : InputMode::Exec,
                  out,
                  static_cast<std::uint32_t>(flags) |
                      (inherit.value() == context.builtin(CoreObject::False)
                           ? caller_future_flags(context)
                           : 0));
}

template <bool Eval>
void execute_builtin(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto source = roots.root(args[0]);
    const auto none = context.builtin(CoreObject::None);
    if constexpr (!Eval) {
        if (args[3] != none) {
            raise_error(context, CoreObject::TypeErrorType, U"exec() closure is not supported",
                        out);
            return;
        }
    }
    std::optional<Handle> globals, locals;
    ReturnSlot value(context);
    if (args[1] != none)
        globals = roots.root(args[1]);
    else {
        const auto frame = context.current_frame();
        if (!frame) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"no current frame for globals and locals", out);
            return;
        }
        auto caller = roots.root(*frame);
        globals = roots.root(
            context.payload<FunctionObject>(context.payload<FrameObject>(caller).function).globals);
        if (args[2] == none) {
            detail::frame_locals(context, caller, value);
            if (failed(value)) {
                out.raised(value.exception());
                return;
            }
            locals = roots.root(value.value());
        }
    }
    if (!context.has_layout<DictObject>(globals->get(context)) ||
        !is_subtype(context, context.type_of(*globals), context.builtin(CoreObject::DictType))) {
        raise_error(context, CoreObject::TypeErrorType, U"globals must be a dict", out);
        return;
    }
    if (args[2] != none)
        locals = roots.root(args[2]);
    if (!locals)
        locals = *globals;
    if (!context.has_layout<DictObject>(locals->get(context)) &&
        !detail::type_attribute(context, context.type_of(*locals), U"__getitem__")) {
        raise_error(context, CoreObject::TypeErrorType, U"locals must be a mapping", out);
        return;
    }
    if (context.has_layout<CodeObject>(source.get(context))) {
        if (!context.payload<CodeObject>(source).program.free_names().empty()) {
            raise_error(context, CoreObject::TypeErrorType, U"code object contains free variables",
                        out);
            return;
        }
        value.ok(source.get(context));
    } else {
        freeze_source(context, source.get(context), "<string>",
                      Eval ? InputMode::Eval : InputMode::Exec, value,
                      caller_future_flags(context));
        if (failed(value)) {
            out.raised(value.exception());
            return;
        }
    }
    auto code = roots.root(value.value());
    std::optional<Handle> builtins;
    if (const auto frame = context.current_frame()) {
        ReturnSlot key(context), existing(context);
        make_str(context, U"__builtins__", key);
        dict_lookup(context, *globals, roots.root(key.value()), existing);
        if (failed(existing)) {
            out.raised(existing.exception());
            return;
        }
        if (existing.tag() == ResultTag::Empty)
            builtins = roots.root(
                context.payload<FunctionObject>(context.payload<FrameObject>(*frame).function)
                    .builtins);
    }
    run(context, {code, *globals, builtins, *locals}, out);
    if constexpr (!Eval) {
        if (!failed(out))
            out.ok(none);
    }
}
void locals_builtin(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    const auto frame = context.current_frame();
    if (!frame) {
        raise_error(context, CoreObject::RuntimeErrorType, U"no current frame", out);
        return;
    }
    RootScope roots(context);
    detail::frame_locals(context, roots.root(*frame), out);
}

void globals_builtin(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    const auto frame = context.current_frame();
    if (!frame) {
        raise_error(context, CoreObject::RuntimeErrorType, U"no current frame", out);
        return;
    }
    const auto &data = context.payload<FrameObject>(*frame);
    out.ok(context.payload<FunctionObject>(data.function).globals);
}

void vars_builtin(Attachment &context, Handle self, std::span<const Value> args, ReturnSlot &out)
{
    const auto &arguments = context.payload<TupleObject>(args[0]).items;
    if (arguments.empty()) {
        locals_builtin(context, self, {}, out);
        return;
    }
    if (arguments.size() != 1) {
        raise_error(context, CoreObject::TypeErrorType, U"vars expected at most 1 argument", out);
        return;
    }
    RootScope roots(context);
    const auto object = roots.root(arguments[0]);
    ReturnSlot name(context);
    make_str(context, U"__dict__", name);
    get_attribute(context, object, roots.root(name.value()), out);
    if (out.tag() == ResultTag::Raised &&
        is_subtype(context, context.type_of(out.exception()),
                   context.builtin(CoreObject::AttributeErrorType)))
        raise_error(context, CoreObject::TypeErrorType,
                    U"vars() argument must have __dict__ attribute", out);
}

void dir_builtin(Attachment &context, Handle self, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto arguments = roots.root(args[0]);
    const auto &items = context.payload<TupleObject>(arguments).items;
    if (items.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"dir expected at most 1 argument", out);
        return;
    }
    ReturnSlot value(context), names(context), comparison(context), boolean(context);
    std::vector<Handle> values;
    auto collect_dict = [&](Value dictionary) {
        DictView view(context, dictionary);
        for (const auto &entry : view.entries())
            values.push_back(roots.root(entry.key));
    };
    if (items.empty()) {
        locals_builtin(context, self, {}, value);
        if (failed(value)) {
            out.raised(value.exception());
            return;
        }
        if (!context.has_layout<DictObject>(value.value())) {
            raise_error(context, CoreObject::TypeErrorType, U"locals is not a dictionary", out);
            return;
        }
        collect_dict(value.value());
    } else {
        auto object = roots.root(items[0]);
        auto method = detail::type_attribute(context, context.type_of(object), U"__dir__");
        if (method) {
            detail::invoke_special(context, object, roots.root(*method), {{}, {}}, value);
            if (failed(value)) {
                out.raised(value.exception());
                return;
            }
            get_iter(context, roots.root(value.value()), names);
            if (failed(names)) {
                out.raised(names.exception());
                return;
            }
            auto iterator = roots.root(names.value());
            for (;;) {
                auto status = iter_next(context, iterator, value);
                if (status == NextStatus::Raised) {
                    out.raised(value.exception());
                    return;
                }
                if (status == NextStatus::Done)
                    break;
                values.push_back(roots.root(value.value()));
            }
        } else {
            make_str(context, U"__dict__", names);
            get_attribute(context, object, roots.root(names.value()), value);
            if (!failed(value) && context.has_layout<DictObject>(value.value()))
                collect_dict(value.value());
            else if (failed(value) &&
                     !is_subtype(context, context.type_of(value.exception()),
                                 context.builtin(CoreObject::AttributeErrorType))) {
                out.raised(value.exception());
                return;
            }
            const auto type = context.has_layout<TypeObject>(object.get(context))
                                  ? object.get(context)
                                  : context.type_of(object);
            TypeView type_view(context, type);
            if (auto snapshot = type_view.snapshot()) {
                const auto mro = roots.root(context.payload<TypeSnapshot>(*snapshot).mro);
                for (auto base : context.payload<TupleObject>(mro).items) {
                    TypeView base_view(context, base);
                    if (auto dictionary = base_view.dictionary())
                        collect_dict(*dictionary);
                }
            }
            // Namespace-derived names are deduplicated; an explicit __dir__ result is not.
            std::vector<Handle> unique;
            std::unordered_set<std::u32string> strings;
            for (auto candidate : values) {
                if (!context.has_layout<StrObject>(candidate.get(context)) ||
                    strings.insert(context.payload<StrObject>(candidate).value).second)
                    unique.push_back(candidate);
            }
            values = std::move(unique);
        }
    }
    struct SortError {};
    try {
        std::stable_sort(values.begin(), values.end(), [&](Handle left, Handle right) {
            rich_compare(context, left, right, CompareOp::Lt, comparison);
            if (failed(comparison)) {
                out.raised(comparison.exception());
                throw SortError{};
            }
            RootScope comparison_roots(context);
            truth(context, comparison_roots.root(comparison.value()), boolean);
            if (failed(boolean)) {
                out.raised(boolean.exception());
                throw SortError{};
            }
            return boolean.value() == context.builtin(CoreObject::True);
        });
    } catch (const SortError &) {
        return;
    }
    std::vector<Value> result;
    for (auto item : values)
        result.push_back(item.get(context));
    make_list(context, result, out);
}
} // namespace

void detail::install_exec_builtins(Attachment &context, std::span<const Handle> core, Value mapping)
{
    RootScope roots(context);
    const auto dictionary = roots.root(mapping);
    ReturnSlot function(context), name(context);
    auto install = [&](const char *label, std::u32string key, SignatureLayout signature,
                       NativeEntry entry) {
        context.allocate_instance<NativeFunctionObject>(
            core[static_cast<std::size_t>(CoreObject::NativeFunctionType)], function, label,
            std::move(signature), entry, std::vector<Value>{});
        const auto rooted = roots.root(function.value());
        context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)],
                                             name, std::move(key));
        name_store(context, dictionary.get(context), name.value(), rooted.get(context));
    };
    install("locals", U"locals", {}, locals_builtin);
    install("globals", U"globals", {}, globals_builtin);
    SignatureLayout vars;
    vars.varargs = U"args";
    install("vars", U"vars", std::move(vars), vars_builtin);
    SignatureLayout dir;
    dir.varargs = U"args";
    install("dir", U"dir", std::move(dir), dir_builtin);
    ReturnSlot zero(context), minus_one(context);
    context.allocate_instance<IntObject>(core[static_cast<std::size_t>(CoreObject::IntType)], zero,
                                         0);
    const auto zero_value = roots.root(zero.value());
    context.allocate_instance<IntObject>(core[static_cast<std::size_t>(CoreObject::IntType)],
                                         minus_one, -1);
    const auto negative = roots.root(minus_one.value());
    SignatureLayout compile;
    compile.parameters = {
        {U"source", ParameterKind::PositionalOrKeyword},
        {U"filename", ParameterKind::PositionalOrKeyword},
        {U"mode", ParameterKind::PositionalOrKeyword},
        {U"flags", ParameterKind::PositionalOrKeyword, zero_value.get(context)},
        {U"dont_inherit", ParameterKind::PositionalOrKeyword, zero_value.get(context)},
        {U"optimize", ParameterKind::PositionalOrKeyword, negative.get(context)},
        {U"_feature_version", ParameterKind::KeywordOnly, negative.get(context)}};
    install("compile", U"compile", std::move(compile), compile_builtin);
    const auto none = core[static_cast<std::size_t>(CoreObject::None)].get(context);
    SignatureLayout eval;
    eval.parameters = {{U"source", ParameterKind::PositionalOnly},
                       {U"globals", ParameterKind::PositionalOnly, none},
                       {U"locals", ParameterKind::PositionalOnly, none}};
    install("eval", U"eval", eval, execute_builtin<true>);
    eval.parameters.push_back({U"closure", ParameterKind::KeywordOnly, none});
    install("exec", U"exec", std::move(eval), execute_builtin<false>);
    const auto syntax = core[static_cast<std::size_t>(CoreObject::SyntaxErrorType)];
    SignatureLayout syntax_signature;
    syntax_signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    syntax_signature.varargs = U"args";
    install_native_attribute(context, core, syntax, U"__init__", std::move(syntax_signature),
                             syntax_init);
    SignatureLayout syntax_unary;
    syntax_unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, syntax, U"__str__", std::move(syntax_unary),
                             syntax_str);
    install_native_field(context, core, syntax, U"end_lineno", syntax_field<0>,
                         set_syntax_field<0>);
    install_native_field(context, core, syntax, U"end_offset", syntax_field<1>,
                         set_syntax_field<1>);
    install_native_field(context, core, syntax, U"filename", syntax_field<2>, set_syntax_field<2>);
    install_native_field(context, core, syntax, U"lineno", syntax_field<3>, set_syntax_field<3>);
    install_native_field(context, core, syntax, U"msg", syntax_field<4>, set_syntax_field<4>);
    install_native_field(context, core, syntax, U"offset", syntax_field<5>, set_syntax_field<5>);
    install_native_field(context, core, syntax, U"print_file_and_line", syntax_field<6>,
                         set_syntax_field<6>);
    install_native_field(context, core, syntax, U"text", syntax_field<7>, set_syntax_field<7>);
}
} // namespace cxxpython::core
