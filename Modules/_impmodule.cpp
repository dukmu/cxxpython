#include "cxxpython/memoryobject.hpp"
#include "internal/builtinexec.hpp"
#include "internal/compiler.hpp"
#include "internal/import.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace cxxpython::core {
namespace {
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool string(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.has_layout<StrObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType)))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"expected a string", out);
    return false;
}

// CPython 3.12 Python/pyhash.c _Py_KeyedHash uses SipHash-1-3 with k1=0.
// CPython is distributed under the PSF license; see doc/licenses/python-3.12.14.txt.
std::uint64_t source_digest(std::uint64_t key, const std::string &data)
{
    std::uint64_t a = 0x736f6d6570736575ULL ^ key, b = 0x646f72616e646f6dULL;
    std::uint64_t c = 0x6c7967656e657261ULL ^ key, d = 0x7465646279746573ULL;
    auto round = [&] {
        a += b;
        b = std::rotl(b, 13) ^ a;
        a = std::rotl(a, 32);
        c += d;
        d = std::rotl(d, 16) ^ c;
        a += d;
        d = std::rotl(d, 21) ^ a;
        c += b;
        b = std::rotl(b, 17) ^ c;
        c = std::rotl(c, 32);
    };
    std::size_t position = 0;
    while (data.size() - position >= 8) {
        std::uint64_t word = 0;
        for (unsigned i = 0; i < 8; ++i)
            word |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[position + i]))
                    << (i * 8);
        d ^= word;
        round();
        a ^= word;
        position += 8;
    }
    std::uint64_t tail = static_cast<std::uint64_t>(data.size()) << 56;
    for (std::size_t i = 0; i < data.size() - position; ++i)
        tail |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[position + i]))
                << (i * 8);
    d ^= tail;
    round();
    a ^= tail;
    c ^= 0xff;
    round();
    round();
    round();
    return a ^ b ^ c ^ d;
}

void source_hash(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto key = roots.root(args[0]);
    auto source = roots.root(args[1]);
    ReturnSlot indexed(context);
    index(context, key, indexed);
    if (failed(indexed)) {
        out.raised(indexed.exception());
        return;
    }
    std::int64_t value;
    if (!int_fits_signed(context, indexed.value(), value)) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C long", out);
        return;
    }
    std::string data;
    if (!detail::buffer_bytes(context, source, data, out)) {
        if (!failed(out))
            raise_error(context, CoreObject::TypeErrorType, U"a bytes-like object is required",
                        out);
        return;
    }
    const auto digest = source_digest(static_cast<std::uint64_t>(value), data);
    std::string result(8, '\0');
    for (unsigned i = 0; i < 8; ++i)
        result[i] = static_cast<char>((digest >> (i * 8)) & 0xff);
    make_bytes(context, std::move(result), out);
}
std::optional<SourceBuffer> read_source(Attachment &context, const std::string &path,
                                        ReturnSlot &out)
{
    try {
        NativeWait parked(context);
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::ios_base::failure("cannot open module source");
        std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
            throw std::ios_base::failure("cannot read module source");
        if (text.starts_with("\xef\xbb\xbf"))
            text.erase(0, 3);
        return SourceBuffer(std::move(text), path);
    } catch (const SourceError &error) {
        detail::raise_source_error(context, error, out);
        return {};
    } catch (const std::ios_base::failure &) {
        raise_error(context, CoreObject::OSErrorType, U"cannot read module source", out);
        return {};
    }
}
struct LoaderBinding {
    ObjectHeader header;
    const std::u32string name;
    const std::shared_ptr<const detail::ModuleDefinition> definition;
    LoaderBinding(InitialType head, std::u32string name,
                  std::shared_ptr<const detail::ModuleDefinition> definition)
        : header(head), name(std::move(name)), definition(std::move(definition))
    {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return (name.capacity() + 1) * sizeof(char32_t);
    }
};
enum class LoaderOperation { Execute, Source, Code };
template <LoaderOperation Operation>
void loader_entry(Attachment &context, Value function, std::span<const Value> args, ReturnSlot &out)
{
    const auto capture = context.payload<NativeFunctionObject>(function).captures[0];
    const auto &binding = context.payload<LoaderBinding>(capture);
    RootScope roots(context);
    if constexpr (Operation == LoaderOperation::Execute) {
        if (!is_module(context, args[0])) {
            raise_error(context, CoreObject::TypeErrorType, U"loader execution requires a module",
                        out);
            return;
        }
        detail::execute_definition(context, binding.definition, roots.root(args[0]), out);
    } else {
        if (!string(context, args[0], out))
            return;
        if (context.payload<StrObject>(args[0]).value != binding.name) {
            raise_error(context, CoreObject::ImportErrorType,
                        U"loader cannot handle this module name", out);
            return;
        }
        const auto &unit = *std::get<std::shared_ptr<const SourceUnit>>(binding.definition->loader);
        if constexpr (Operation == LoaderOperation::Code)
            unit.freeze(context, out);
        else
            make_str(context,
                     detail::decode_source(unit.source(), {0, unit.source().text().size()}), out);
    }
}
void exec_code(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_module(context, args[1])) {
        raise_error(context, CoreObject::TypeErrorType, U"exec_code requires a module", out);
        return;
    }
    RootScope roots(context);
    run(context, {roots.root(args[0]), roots.root(module_dict(context, args[1]))}, out);
}
void source_to_code(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out) || !string(context, args[1], out))
        return;
    std::shared_ptr<const SourceUnit> unit;
    const auto text = source_utf8(context.payload<StrObject>(args[0]).value);
    const auto filename = source_utf8(context.payload<StrObject>(args[1]).value);
    try {
        {
            NativeWait parked(context);
            unit = std::make_shared<SourceUnit>(SourceBuffer(text, filename));
        }
        unit->freeze(context, out);
    } catch (const SourceError &error) {
        detail::raise_source_error(context, error, out);
    }
}
void file_source(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    const auto path = source_utf8(context.payload<StrObject>(args[0]).value);
    auto buffer = read_source(context, path, out);
    if (buffer)
        make_str(context, detail::decode_source(*buffer, {0, buffer->text().size()}), out);
}
void registered_data(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    const auto name = context.payload<StrObject>(args[0]).value;
    const auto definition = detail::find_registered_definition(context, name);
    if (definition)
        detail::loader_arguments(context, name, definition, out);
    else
        out.ok(context.builtin(CoreObject::None));
}
void extension_data(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out) || !string(context, args[1], out))
        return;
    const auto name = context.payload<StrObject>(args[0]).value;
    const auto path = source_utf8(context.payload<StrObject>(args[1]).value);
    out.reset(context);
    const auto native = detail::load_extension(context, path, name, out);
    if (failed(out))
        return;
    if (!native)
        return;
    const bool package = std::filesystem::path(path).filename() == "__init__.cxxpy.so";
    auto definition = std::make_shared<const detail::ModuleDefinition>(detail::ModuleDefinition{
        native, package, path,
        package ? std::vector<std::string>{std::filesystem::path(path).parent_path().string()}
                : std::vector<std::string>{}});
    detail::loader_arguments(context, name, definition, out);
}
void path_absolute(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    auto path = source_utf8(context.payload<StrObject>(args[0]).value);
    // CPython _path_abspath strips one leading './', not embedded '.' or '..'.
    if (path.starts_with("./"))
        path.erase(0, 2);
    std::string absolute;
    try {
        NativeWait parked(context);
        absolute =
            (path.empty() ? std::filesystem::current_path() : std::filesystem::absolute(path))
                .string();
    } catch (const std::filesystem::filesystem_error &error) {
        raise_error(context,
                    error.code() == std::errc::no_such_file_or_directory
                        ? CoreObject::FileNotFoundErrorType
                        : CoreObject::OSErrorType,
                    U"cannot resolve import path", out);
        return;
    }
    make_str(context, detail::decode_filename(SourceBuffer("", absolute)), out);
}
void is_directory(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    const auto path = source_utf8(context.payload<StrObject>(args[0]).value);
    bool found;
    {
        NativeWait parked(context);
        std::error_code error;
        found = std::filesystem::is_directory(path.empty() ? "." : path, error);
    }
    out.ok(context.builtin(found ? CoreObject::True : CoreObject::False));
}
void is_file(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    const auto path = source_utf8(context.payload<StrObject>(args[0]).value);
    bool found;
    {
        NativeWait parked(context);
        std::error_code error;
        found = std::filesystem::is_regular_file(path, error);
    }
    out.ok(context.builtin(found ? CoreObject::True : CoreObject::False));
}
void create_builtin(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    if (context.payload<StrObject>(args[0]).value != U"builtins") {
        out.ok(context.builtin(CoreObject::None));
        return;
    }
    RootScope roots(context);
    make_module(context, U"builtins", out, roots.root(context.builtin(CoreObject::Builtins)));
}
void gcd_import(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    RootScope roots(context);
    const auto name = roots.root(args[0]), package = roots.root(args[1]),
               level = roots.root(args[2]);
    ReturnSlot mapping(context), key(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          mapping);
    const auto globals = roots.root(mapping.value());
    make_str(context, U"__package__", key);
    dict_store(context, globals, roots.root(key.value()), package, out);
    if (!failed(out))
        import_name(context, name, globals, roots.root(context.builtin(CoreObject::EmptyTuple)),
                    level, out, true);
}
void reload(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[1], out))
        return;
    RootScope roots(context);
    reload_module(context, roots.root(args[0]), context.payload<StrObject>(args[1]).value, out);
}
void is_builtin(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string(context, args[0], out))
        return;
    const auto name = context.payload<StrObject>(args[0]).value;
    bool builtin = false;
    auto &state = context.imports();
    {
        NativeWait parked(context);
        std::lock_guard lock(state.mutex);
        const auto found = state.definitions.find(name);
        if (found != state.definitions.end()) {
            const auto &body = found->second->loader;
            builtin = std::holds_alternative<ModuleInitializer>(body);
            if (const auto native = std::get_if<std::shared_ptr<const detail::NativeModule>>(&body))
                builtin = !(*native)->library;
        }
    }
    make_int(context, builtin ? 1 : 0, out);
}
void suffixes(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(context);
    make_str(context, U".cxxpy.so", out);
    const std::array values{roots.root(out.value()).get(context)};
    make_list(context, values, out);
}
} // namespace
namespace detail {
std::shared_ptr<const SourceUnit>
definition_source(Attachment &context, const ModuleDefinition &definition, ReturnSlot &out)
{
    out.reset(context);
    if (const auto source = std::get_if<std::shared_ptr<const SourceUnit>>(&definition.loader))
        return *source;
    if (!std::holds_alternative<ModuleDefinition::File>(definition.loader))
        return {};
    auto buffer = read_source(context, definition.origin, out);
    if (!buffer)
        return {};
    try {
        NativeWait parked(context);
        return std::make_shared<SourceUnit>(std::move(*buffer));
    } catch (const SourceError &error) {
        detail::raise_source_error(context, error, out);
    }
    return {};
}
void execute_definition(Attachment &context,
                        const std::shared_ptr<const ModuleDefinition> &definition, Handle module,
                        ReturnSlot &out)
{
    const auto unit = definition_source(context, *definition, out);
    if (failed(out))
        return;
    if (unit) {
        RootScope roots(context);
        run(context, {unit.get(), roots.root(module_dict(context, module.get(context)))}, out);
        return;
    }
    out.reset(context);
    if (const auto native = std::get_if<std::shared_ptr<const NativeModule>>(&definition->loader))
        exec_extension(context, *native, module, out);
    else
        std::get<ModuleInitializer>(definition->loader)(context, module, out);
    if (out.tag() == ResultTag::Empty)
        raise_error(context, CoreObject::RuntimeErrorType,
                    U"module initializer did not publish a result", out);
    else if (out.tag() == ResultTag::Ok && out.value() != context.builtin(CoreObject::None))
        raise_error(context, CoreObject::TypeErrorType, U"module initializer must return None",
                    out);
    if (out.tag() != ResultTag::Ok)
        return;
    RootScope roots(context);
    const auto dictionary = roots.root(module_dict(context, module.get(context)));
    const auto name = roots.root(name_lookup(context, dictionary.get(context), U"__name__")
                                     .value_or(context.builtin(CoreObject::None)));
    DictView exports(context, dictionary.get(context));
    for (const auto &item : exports.entries()) {
        if (!context.has_layout<NativeFunctionObject>(item.value))
            continue;
        auto &function = context.payload<NativeFunctionObject>(item.value);
        if (function.binding != NativeFunctionBinding::Function || function.bound_self)
            continue;
        if (context.compare_exchange(item.value, function.module_owner, {}, module.get(context)))
            context.compare_exchange(item.value, function.module_name, {}, name.get(context));
    }
}
void loader_arguments(Attachment &context, const std::u32string &name,
                      const std::shared_ptr<const ModuleDefinition> &definition, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot temporary(context), binding(context);
    std::vector<Value> values;
    auto append = [&] { values.push_back(roots.root(temporary.value()).get(context)); };
    make_str(context, name, temporary);
    append();
    make_str(context, decode_filename(SourceBuffer("", definition->origin)), temporary);
    append();
    if (definition->package) {
        std::vector<Value> paths;
        for (const auto &path : definition->paths) {
            make_str(context, decode_filename(SourceBuffer("", path)), temporary);
            paths.push_back(roots.root(temporary.value()).get(context));
        }
        make_list(context, paths, temporary);
    } else
        temporary.ok(context.builtin(CoreObject::None));
    append();
    const bool file = std::holds_alternative<ModuleDefinition::File>(definition->loader);
    const bool memory =
        std::holds_alternative<std::shared_ptr<const SourceUnit>>(definition->loader);
    if (!file)
        context.allocate<LoaderBinding>(binding, name, definition);
    const std::array captures{!file ? binding.value() : context.builtin(CoreObject::None)};
    SignatureLayout signature;
    signature.parameters = {{U"value", ParameterKind::PositionalOnly}};
    for (auto entry :
         {loader_entry<LoaderOperation::Execute>, loader_entry<LoaderOperation::Source>,
          loader_entry<LoaderOperation::Code>}) {
        if (file || (!memory && entry != loader_entry<LoaderOperation::Execute>))
            temporary.ok(context.builtin(CoreObject::None));
        else
            make_native_function(context, "loader operation", signature, entry, captures, temporary,
                                 NativeFunctionBinding::Function);
        append();
    }
    make_int(context, file ? 0 : memory ? 1 : 2, temporary);
    append();
    temporary.ok(context.builtin(definition->origin != "built-in" && !definition->origin.empty()
                                     ? CoreObject::True
                                     : CoreObject::False));
    append();
    cxxpython::core::make_tuple(context, values, out);
}
void initialize_imp(Attachment &context, Handle module, ReturnSlot &out)
{
    RootScope roots(context);
    SignatureLayout pair;
    pair.parameters = {{U"value", ParameterKind::PositionalOnly},
                       {U"other", ParameterKind::PositionalOnly}};
    auto unary = pair;
    unary.parameters.pop_back();
    auto triple = pair;
    triple.parameters.push_back({U"level", ParameterKind::PositionalOnly});
    ReturnSlot function(context), key(context);
    auto install = [&](const char *name, const char32_t *label, SignatureLayout signature,
                       NativeEntry entry) {
        make_native_function(context, name, std::move(signature), entry, {}, function,
                             NativeFunctionBinding::Function);
        make_str(context, label, key);
        dict_store(context, roots.root(module_dict(context, module.get(context))),
                   roots.root(key.value()), roots.root(function.value()), out);
        return !failed(out);
    };
    if (!install("exec_code", U"exec_code", pair, exec_code) ||
        !install("source_hash", U"source_hash", pair, source_hash) ||
        !install("source_to_code", U"source_to_code", pair, source_to_code) ||
        !install("_get_source", U"_get_source", unary, file_source) ||
        !install("_registered_data", U"_registered_data", unary, registered_data) ||
        !install("_extension_data", U"_extension_data", pair, extension_data) ||
        !install("_path_absolute", U"_path_absolute", unary, path_absolute) ||
        !install("_is_directory", U"_is_directory", unary, is_directory) ||
        !install("_is_file", U"_is_file", unary, is_file) ||
        !install("_create_builtin", U"_create_builtin", unary, create_builtin) ||
        !install("_gcd_import", U"_gcd_import", triple, gcd_import) ||
        !install("_reload", U"_reload", pair, reload) ||
        !install("is_builtin", U"is_builtin", unary, is_builtin) ||
        !install("extension_suffixes", U"extension_suffixes", {}, suffixes))
        return;
    out.ok(context.builtin(CoreObject::None));
}
} // namespace detail
} // namespace cxxpython::core
