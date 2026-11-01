// Stream construction follows CPython 3.12.14 Modules/_io/_iomodule.c (PSF license).
#include "internal/ioopen.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"
#include "internal/iomodule.hpp"

#include <algorithm>
#include <array>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void cleanup(Attachment &c, Handle stream, ReturnSlot &out)
{
    RootScope roots(c);
    const auto original = roots.root(out.exception());
    ReturnSlot closed(c);
    if (!io_call(c, stream, U"close", {}, closed)) {
        c.store(closed.exception(), c.payload<ExceptionObject>(closed.exception()).context,
                original.get(c));
        out.raised(closed.exception());
    }
}
bool attribute(Attachment &c, Handle object, std::u32string_view name, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot key(c);
    make_str(c, std::u32string(name), key);
    get_attribute(c, object, roots.root(key.value()), out);
    return !failed(out);
}
void open_code(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<StrObject>(args[0]) ||
        !is_subtype(c, c.type_of(args[0]), c.builtin(CoreObject::StrType))) {
        raise_error(c, CoreObject::TypeErrorType, U"open_code() argument must be a str", out);
        return;
    }
    RootScope roots(c);
    ReturnSlot mode(c), buffering(c);
    make_str(c, U"rb", mode);
    auto binary = roots.root(mode.value());
    make_int(c, -1, buffering);
    const auto none = c.builtin(CoreObject::None);
    const Value values[] = {args[0], binary.get(c), buffering.value(),           none,
                            none,    none,          c.builtin(CoreObject::True), none};
    io_open(c, function, values, out);
}
void text_encoding(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] == c.builtin(CoreObject::None))
        make_str(c, U"utf-8", out);
    else
        out.ok(args[0]);
}
} // namespace

void io_open(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto file = roots.root(args[0]), mode = roots.root(args[1]);
    const auto &types = c.payload<NativeFunctionObject>(function).captures;
    ReturnSlot module(c), type(c), raw_result(c), value(c), result(c);
    if (types.empty()) {
        import_module(c, U"_io", module);
        if (failed(module)) {
            out.raised(module.exception());
            return;
        }
        auto implementation = name_lookup(c, module_dict(c, module.value()), U"_open_impl");
        if (!implementation || !c.has_layout<NativeFunctionObject>(*implementation) ||
            c.payload<NativeFunctionObject>(*implementation).entry != io_open ||
            c.payload<NativeFunctionObject>(*implementation).captures.size() != 5) {
            raise_error(c, CoreObject::RuntimeErrorType, U"_io open state is unavailable", out);
            return;
        }
        auto implementation_root = roots.root(*implementation);
        io_open(c, implementation_root.get(c), args, out);
        return;
    }
    if (!c.has_layout<StrObject>(mode.get(c)) ||
        !is_subtype(c, c.type_of(mode), c.builtin(CoreObject::StrType))) {
        raise_error(c, CoreObject::TypeErrorType, U"open() mode must be str", out);
        return;
    }
    const auto &text = c.payload<StrObject>(mode).value;
    std::u32string seen;
    char32_t access = 0;
    bool binary = false, explicit_text = false, updating = false, invalid = false;
    for (const auto ch : text) {
        if (seen.find(ch) != std::u32string::npos)
            invalid = true;
        seen.push_back(ch);
        if (ch == U'r' || ch == U'w' || ch == U'a' || ch == U'x') {
            invalid |= access != 0;
            access = ch;
        } else if (ch == U'+')
            updating = true;
        else if (ch == U'b')
            binary = true;
        else if (ch == U't')
            explicit_text = true;
        else
            invalid = true;
    }
    if (invalid || !access || (binary && explicit_text)) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid mode: " + text, out);
        return;
    }
    if (binary &&
        (args[3] != c.builtin(CoreObject::None) || args[4] != c.builtin(CoreObject::None) ||
         args[5] != c.builtin(CoreObject::None))) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"binary mode doesn't take encoding, errors or newline arguments", out);
        return;
    }
    std::int64_t buffering;
    if (!index_as_int64(c, args[2], buffering, out))
        return;
    type.ok(types[0]);
    std::u32string rawmode(1, access);
    if (updating)
        rawmode += U'+';
    make_str(c, std::move(rawmode), value);
    const Value raw_args[] = {file.get(c), value.value(), args[6], args[7]};
    call(c, roots.root(type.value()), {raw_args}, raw_result);
    if (failed(raw_result)) {
        out.raised(raw_result.exception());
        return;
    }
    auto stream = roots.root(raw_result.value());
    bool line_buffering = buffering == 1;
    if (buffering < 0) {
        if (!io_call(c, stream, U"isatty", {}, value)) {
            out.raised(value.exception());
            cleanup(c, stream, out);
            return;
        }
        truth(c, roots.root(value.value()), result);
        if (failed(result)) {
            out.raised(result.exception());
            cleanup(c, stream, out);
            return;
        }
        line_buffering = result.value() == c.builtin(CoreObject::True);
    }
    if (line_buffering)
        buffering = -1;
    if (buffering < 0) {
        if (!attribute(c, stream, U"_blksize", value)) {
            out.raised(value.exception());
            cleanup(c, stream, out);
            return;
        }
        if (!index_as_int64(c, value.value(), buffering, out)) {
            cleanup(c, stream, out);
            return;
        }
    }
    if (buffering < 0 || (!buffering && !binary)) {
        raise_error(c, CoreObject::ValueErrorType,
                    buffering < 0 ? U"invalid buffering size" : U"can't have unbuffered text I/O",
                    out);
        cleanup(c, stream, out);
        return;
    }
    if (!buffering) {
        out.ok(stream.get(c));
        return;
    }
    type.ok(types[updating ? 3 : access == U'r' ? 1 : 2]);
    make_int(c, buffering, value);
    const Value buffered_args[] = {stream.get(c), value.value()};
    call(c, roots.root(type.value()), {buffered_args}, result);
    if (failed(result)) {
        out.raised(result.exception());
        cleanup(c, stream, out);
        return;
    }
    stream = roots.root(result.value());
    if (binary) {
        out.ok(stream.get(c));
        return;
    }
    type.ok(types[4]);
    const Value text_args[] = {stream.get(c), args[3], args[4], args[5],
                               c.builtin(line_buffering ? CoreObject::True : CoreObject::False)};
    call(c, roots.root(type.value()), {text_args}, result);
    if (failed(result)) {
        out.raised(result.exception());
        cleanup(c, stream, out);
        return;
    }
    stream = roots.root(result.value());
    make_str(c, U"mode", value);
    set_attribute(c, stream, roots.root(value.value()), mode, out);
    if (failed(out))
        cleanup(c, stream, out);
    else
        out.ok(stream.get(c));
}

void install_open_builtin(Attachment &c, std::span<const Handle> core, Value dictionary)
{
    RootScope roots(c);
    ReturnSlot mode(c), buffering(c), function(c), key(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    c.allocate_instance<StrObject>(at(CoreObject::StrType), mode, U"r");
    c.allocate_instance<IntObject>(at(CoreObject::IntType), buffering, -1);
    SignatureLayout signature;
    signature.parameters = {
        {U"file", ParameterKind::PositionalOrKeyword},
        {U"mode", ParameterKind::PositionalOrKeyword, mode.value()},
        {U"buffering", ParameterKind::PositionalOrKeyword, buffering.value()},
        {U"encoding", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)},
        {U"errors", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)},
        {U"newline", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)},
        {U"closefd", ParameterKind::PositionalOrKeyword, at(CoreObject::True).get(c)},
        {U"opener", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)}};
    c.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType), function, "open",
                                              signature, io_open, std::vector<Value>{});
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"open");
    name_store(c, dictionary, key.value(), function.value());
}

void initialize_open(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot value(c), key(c);
    const auto open = roots.root(*name_lookup(c, c.builtin(CoreObject::Builtins), U"open"));
    const auto old = name_lookup(c, dictionary.get(c), U"_open_impl");
    if (!old || !c.has_layout<NativeFunctionObject>(*old) ||
        c.payload<NativeFunctionObject>(*old).entry != io_open) {
        std::vector<Value> types;
        for (auto name : {U"FileIO", U"BufferedReader", U"BufferedWriter", U"BufferedRandom",
                          U"TextIOWrapper"}) {
            auto type = *name_lookup(c, dictionary.get(c), name);
            types.push_back(roots.root(type).get(c));
        }
        make_native_function(c, "_open_impl", c.payload<NativeFunctionObject>(open).signature,
                             io_open, types, value, NativeFunctionBinding::Function);
        auto implementation = roots.root(value.value());
        make_str(c, U"_open_impl", key);
        dict_store(c, dictionary, roots.root(key.value()), implementation, out);
        if (failed(out))
            return;
    }
    make_str(c, U"open", key);
    dict_store(c, dictionary, roots.root(key.value()), open, out);
    if (failed(out))
        return;
    SignatureLayout path;
    path.parameters = {{U"path", ParameterKind::PositionalOnly}};
    make_native_function(c, "open_code", path, open_code, {}, value,
                         NativeFunctionBinding::Function);
    auto function = roots.root(value.value());
    make_str(c, U"open_code", key);
    dict_store(c, dictionary, roots.root(key.value()), function, out);
    if (failed(out))
        return;
    make_int(c, 2, value);
    SignatureLayout encoding;
    encoding.parameters = {
        {U"encoding", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)},
        {U"stacklevel", ParameterKind::PositionalOnly, value.value()}};
    make_native_function(c, "text_encoding", encoding, text_encoding, {}, value,
                         NativeFunctionBinding::Function);
    function = roots.root(value.value());
    make_str(c, U"text_encoding", key);
    dict_store(c, dictionary, roots.root(key.value()), function, out);
}
} // namespace cxxpython::core::detail
