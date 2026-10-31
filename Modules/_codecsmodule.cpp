// Registry contracts follow CPython 3.12.14 Python/codecs.c (PSF license).
#include "internal/codecregistry.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <tuple>

namespace cxxpython::core::detail {
namespace {
struct CodecRegistry {
    ObjectHeader header;
    struct Storage {
        std::mutex mutex;
        std::vector<Value> searches;
        std::map<std::u32string, Value> cache, errors;
        std::uint64_t generation = 0;
        std::size_t external_bytes() const noexcept
        {
            auto size = sizeof(Storage) + searches.capacity() * sizeof(Value);
            for (const auto *table : {&cache, &errors})
                for (const auto &[name, value] : *table) {
                    (void)value;
                    size += sizeof(std::pair<const std::u32string, Value>) + 3 * sizeof(void *) +
                            (name.capacity() + 1) * sizeof(char32_t);
                }
            return size;
        }
    };
    const NativeOwner<Storage> contents;
    explicit CodecRegistry(InitialType head) : header(head), contents(std::make_unique<Storage>())
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto value : contents->searches)
            tracer.visit(value);
        for (const auto &[name, value] : contents->cache) {
            (void)name;
            tracer.visit(value);
        }
        for (const auto &[name, value] : contents->errors) {
            (void)name;
            tracer.visit(value);
        }
    }
    std::size_t external_bytes() const noexcept
    {
        return contents->external_bytes();
    }
};
template <class F> decltype(auto) locked(Attachment &ctx, CodecRegistry::Storage &state, F &&body)
{
    for (;;) {
        std::unique_lock lock(state.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint commit(ctx);
            return body();
        }
        NativeWait wait(ctx);
        state.mutex.lock();
        state.mutex.unlock();
    }
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool text(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<StrObject>(value) &&
        is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::StrType)))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"encoding and error names must be str", out);
    return false;
}
bool callable(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (type_attribute(ctx, ctx.type_of(value), U"__call__"))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType,
                U"codec search functions and handlers must be callable", out);
    return false;
}
Handle registry(Attachment &ctx, Handle function, RootScope &roots)
{
    return roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
}
std::u32string normalized(std::u32string_view name)
{
    std::u32string result;
    bool separator = false;
    for (char32_t c : name) {
        if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') ||
            c == U'.') {
            if (separator && !result.empty())
                result += U'_';
            separator = false;
            result += c >= U'A' && c <= U'Z' ? c + 32 : c;
        } else
            separator = true;
    }
    return result;
}
void register_search(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!callable(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto owner = registry(ctx, function, roots);
    const auto callback = roots.root(args[0]);
    auto &state = *ctx.payload<CodecRegistry>(owner).contents;
    std::vector<Value> prepared;
    std::size_t needed = 0;
    while (!locked(ctx, state, [&] {
        needed = state.searches.size() + 1;
        if (prepared.capacity() < needed)
            return false;
        prepared.assign(state.searches.begin(), state.searches.end());
        prepared.push_back(callback.get(ctx));
        state.searches.swap(prepared);
        ++state.generation;
        ctx.account_external(owner, state.external_bytes());
        return true;
    }))
        prepared.reserve(needed);
    out.ok(ctx.builtin(CoreObject::None));
}
void unregister_search(Attachment &ctx, Handle function, std::span<const Value> args,
                       ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto owner = registry(ctx, function, roots);
    auto &state = *ctx.payload<CodecRegistry>(owner).contents;
    decltype(state.cache) discarded;
    locked(ctx, state, [&] {
        const auto found = std::find(state.searches.begin(), state.searches.end(), args[0]);
        if (found != state.searches.end()) {
            state.searches.erase(found);
            state.cache.swap(discarded);
            ++state.generation;
            ctx.account_external(owner, state.external_bytes());
        }
    });
    out.ok(ctx.builtin(CoreObject::None));
}
void lookup(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!text(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto input = roots.root(args[0]);
    const auto &original = ctx.payload<StrObject>(input).value;
    if (original.find(U'\0') != original.npos) {
        raise_error(ctx, CoreObject::ValueErrorType, U"embedded null character", out);
        return;
    }
    const auto name = normalized(original);
    const auto owner = registry(ctx, function, roots);
    auto &state = *ctx.payload<CodecRegistry>(owner).contents;
    if (locked(ctx, state, [&] {
            const auto found = state.cache.find(name);
            if (found == state.cache.end())
                return false;
            out.ok(found->second);
            return true;
        }))
        return;
    // Import policy stays in Python. No registry lock spans imports or callbacks.
    ReturnSlot imported(ctx), key(ctx), result(ctx);
    import_module(ctx, U"encodings", imported);
    if (failed(imported)) {
        out.raised(imported.exception());
        return;
    }
    make_str(ctx, name, key);
    const auto encoding = roots.root(key.value());
    std::deque<ReturnSlot> searches;
    std::size_t count = 0;
    std::uint64_t generation = 0;
    for (;;) {
        const auto wanted = locked(ctx, state, [&] { return state.searches.size(); });
        while (searches.size() < wanted)
            searches.emplace_back(ctx);
        if (locked(ctx, state, [&] {
                if (searches.size() < state.searches.size())
                    return false;
                count = state.searches.size();
                generation = state.generation;
                for (std::size_t i = 0; i < count; ++i)
                    searches[i].ok(state.searches[i]);
                return true;
            }))
            break;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Value argument[] = {encoding.get(ctx)};
        call(ctx, roots.root(searches[i].value()), {argument}, result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        if (result.value() == ctx.builtin(CoreObject::None))
            continue;
        if (!ctx.has_layout<TupleObject>(result.value()) ||
            !is_subtype(ctx, ctx.type_of(result.value()), ctx.builtin(CoreObject::TupleType)) ||
            ctx.payload<TupleObject>(result.value()).items.size() != 4) {
            raise_error(ctx, CoreObject::TypeErrorType,
                        U"codec search functions must return 4-tuples", out);
            return;
        }
        const auto found = roots.root(result.value());
        decltype(state.cache) prepared;
        prepared.emplace(name, found.get(ctx));
        auto node = prepared.extract(prepared.begin());
        locked(ctx, state, [&] {
            if (state.generation == generation) {
                const auto inserted = state.cache.insert(std::move(node));
                out.ok(inserted.position->second);
                ctx.account_external(owner, state.external_bytes());
            } else
                out.ok(found.get(ctx));
        });
        return;
    }
    raise_error(ctx, CoreObject::LookupErrorType, U"unknown encoding: " + original, out);
}
void register_error(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!text(ctx, args[0], out) || !callable(ctx, args[1], out))
        return;
    RootScope roots(ctx);
    const auto owner = registry(ctx, function, roots);
    const auto callback = roots.root(args[1]);
    auto &state = *ctx.payload<CodecRegistry>(owner).contents;
    decltype(state.errors) prepared;
    prepared.emplace(ctx.payload<StrObject>(args[0]).value, callback.get(ctx));
    auto node = prepared.extract(prepared.begin());
    decltype(prepared)::node_type removed;
    locked(ctx, state, [&] {
        removed = state.errors.extract(node.key());
        state.errors.insert(std::move(node));
        ctx.account_external(owner, state.external_bytes());
    });
    out.ok(ctx.builtin(CoreObject::None));
}
void lookup_error(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!text(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto name = roots.root(args[0]), owner = registry(ctx, function, roots);
    auto &state = *ctx.payload<CodecRegistry>(owner).contents;
    if (!locked(ctx, state, [&] {
            const auto found = state.errors.find(ctx.payload<StrObject>(name).value);
            if (found == state.errors.end())
                return false;
            out.ok(found->second);
            return true;
        }))
        raise_error(ctx, CoreObject::LookupErrorType,
                    U"unknown error handler name '" + ctx.payload<StrObject>(name).value + U"'",
                    out);
}
template <bool Encode>
void generic(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto input = roots.root(args[0]), encoding = roots.root(args[1]),
               errors = roots.root(args[2]);
    if (!text(ctx, errors.get(ctx), out))
        return;
    ReturnSlot entry(ctx);
    const Value name[] = {encoding.get(ctx)};
    lookup(ctx, function, name, entry);
    if (failed(entry)) {
        out.raised(entry.exception());
        return;
    }
    const auto codec = roots.root(entry.value());
    const auto method = roots.root(ctx.payload<TupleObject>(codec).items[Encode ? 0 : 1]);
    const Value values[] = {input.get(ctx), errors.get(ctx)};
    call(ctx, method, {values}, entry);
    if (failed(entry)) {
        out.raised(entry.exception());
        return;
    }
    if (!ctx.has_layout<TupleObject>(entry.value()) ||
        ctx.payload<TupleObject>(entry.value()).items.size() != 2) {
        raise_error(ctx, CoreObject::TypeErrorType, U"codec must return a tuple (object, integer)",
                    out);
        return;
    }
    out.ok(ctx.payload<TupleObject>(entry.value()).items[0]);
}
template <TextEncoding Encoding, bool Encode>
void primitive(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto input = roots.root(args[0]), errors = roots.root(args[1]);
    if constexpr (Encode)
        encode_primitive(ctx, input, Encoding, errors, out);
    else {
        ReturnSlot final(ctx);
        truth(ctx, roots.root(args[2]), final);
        if (failed(final)) {
            out.raised(final.exception());
            return;
        }
        decode_primitive(ctx, input, Encoding, errors,
                         final.value() == ctx.builtin(CoreObject::True), out);
    }
}
template <TextEncoding Encoding>
void extended(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot order(ctx), final(ctx);
    index(ctx, roots.root(args[2]), order);
    if (failed(order)) {
        out.raised(order.exception());
        return;
    }
    std::int64_t byteorder;
    if (!int_fits_signed(ctx, order.value(), byteorder) || byteorder < -1 || byteorder > 1) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"byte order is out of range", out);
        return;
    }
    truth(ctx, roots.root(args[3]), final);
    if (failed(final)) {
        out.raised(final.exception());
        return;
    }
    decode_extended(ctx, roots.root(args[0]), Encoding, roots.root(args[1]),
                    static_cast<int>(byteorder), final.value() == ctx.builtin(CoreObject::True),
                    out);
}
void error_handler(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto name = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    builtin_codec_error(ctx, ctx.payload<StrObject>(name).value, roots.root(args[0]), out);
}
template <bool Encode>
void charmap(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    if constexpr (Encode)
        encode_charmap(ctx, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
    else
        decode_charmap(ctx, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void charmap_build(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    build_charmap(ctx, roots.root(args[0]), out);
}
void registry_call(Attachment &ctx, Handle value, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot module(ctx), key(ctx), function(ctx);
    import_module(ctx, U"_codecs", module);
    if (failed(module)) {
        out.raised(module.exception());
        return;
    }
    make_str(ctx, name, key);
    get_attribute(ctx, roots.root(module.value()), roots.root(key.value()), function);
    if (failed(function)) {
        out.raised(function.exception());
        return;
    }
    const Value args[] = {value.get(ctx)};
    call(ctx, roots.root(function.value()), {args}, out);
}
} // namespace
void lookup_codec(Attachment &ctx, Handle encoding, ReturnSlot &out)
{
    registry_call(ctx, encoding, U"lookup", out);
}
void lookup_codec_error(Attachment &ctx, Handle name, ReturnSlot &out)
{
    registry_call(ctx, name, U"lookup_error", out);
}
void initialize_codecs(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot value(ctx), key(ctx), state(ctx);
    if (const auto previous = name_lookup(ctx, dictionary.get(ctx), U"lookup");
        previous && ctx.has_layout<NativeFunctionObject>(*previous) &&
        ctx.payload<NativeFunctionObject>(*previous).entry == lookup &&
        !ctx.payload<NativeFunctionObject>(*previous).captures.empty())
        state.ok(ctx.payload<NativeFunctionObject>(*previous).captures[0]);
    else
        ctx.allocate<CodecRegistry>(state);
    const auto owner = roots.root(state.value());
    auto store = [&](std::u32string_view name) {
        make_str(ctx, std::u32string(name), key);
        dict_store(ctx, dictionary, roots.root(key.value()), roots.root(value.value()), out);
        return !failed(out);
    };
    auto install = [&](const char *name, SignatureLayout signature, NativeEntry entry,
                       std::vector<Value> captures = {}) {
        make_native_function(ctx, name, std::move(signature), entry, std::move(captures), value,
                             NativeFunctionBinding::Function);
        return store(std::u32string(name, name + std::char_traits<char>::length(name)));
    };
    SignatureLayout unary;
    unary.parameters = {{U"object", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"handler", ParameterKind::PositionalOnly});
    for (auto [name, entry] : {std::pair{"register", &register_search},
                               {"unregister", unregister_search},
                               {"lookup", lookup},
                               {"lookup_error", lookup_error}})
        if (!install(name, unary, entry, {owner.get(ctx)}))
            return;
    if (!install("register_error", pair, register_error, {owner.get(ctx)}))
        return;
    make_str(ctx, U"utf-8", value);
    const auto utf8 = roots.root(value.value());
    make_str(ctx, U"strict", value);
    const auto strict = roots.root(value.value());
    SignatureLayout generic_signature;
    generic_signature.parameters = {
        {U"obj", ParameterKind::PositionalOnly},
        {U"encoding", ParameterKind::PositionalOrKeyword, utf8.get(ctx)},
        {U"errors", ParameterKind::PositionalOrKeyword, strict.get(ctx)}};
    if (!install("encode", generic_signature, generic<true>, {owner.get(ctx)}) ||
        !install("decode", generic_signature, generic<false>, {owner.get(ctx)}))
        return;
    auto encode_signature = unary;
    encode_signature.parameters.push_back(
        {U"errors", ParameterKind::PositionalOnly, strict.get(ctx)});
    auto decode_signature = encode_signature;
    decode_signature.parameters.push_back(
        {U"final", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::False)});
    const std::tuple<const char *, NativeEntry, NativeEntry> primitives[] = {
        {"utf_8", primitive<TextEncoding::Utf8, true>, primitive<TextEncoding::Utf8, false>},
        {"ascii", primitive<TextEncoding::Ascii, true>, primitive<TextEncoding::Ascii, false>},
        {"latin_1", primitive<TextEncoding::Latin1, true>, primitive<TextEncoding::Latin1, false>},
        {"utf_16", primitive<TextEncoding::Utf16, true>, primitive<TextEncoding::Utf16, false>},
        {"utf_16_le", primitive<TextEncoding::Utf16LE, true>,
         primitive<TextEncoding::Utf16LE, false>},
        {"utf_16_be", primitive<TextEncoding::Utf16BE, true>,
         primitive<TextEncoding::Utf16BE, false>},
        {"utf_32", primitive<TextEncoding::Utf32, true>, primitive<TextEncoding::Utf32, false>},
        {"utf_32_le", primitive<TextEncoding::Utf32LE, true>,
         primitive<TextEncoding::Utf32LE, false>},
        {"utf_32_be", primitive<TextEncoding::Utf32BE, true>,
         primitive<TextEncoding::Utf32BE, false>}};
    for (auto [name, encode, decode] : primitives) {
        if (!install((std::string(name) + "_encode").c_str(), encode_signature, encode) ||
            !install((std::string(name) + "_decode").c_str(), decode_signature, decode))
            return;
    }
    auto extended_signature = encode_signature;
    extended_signature.parameters.push_back(
        {U"byteorder", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::IntZero)});
    extended_signature.parameters.push_back(
        {U"final", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::False)});
    if (!install("utf_16_ex_decode", extended_signature, extended<TextEncoding::Utf16>) ||
        !install("utf_32_ex_decode", extended_signature, extended<TextEncoding::Utf32>))
        return;
    auto mapped = encode_signature;
    mapped.parameters.push_back(
        {U"mapping", ParameterKind::PositionalOnly, ctx.builtin(CoreObject::None)});
    if (!install("charmap_encode", mapped, charmap<true>) ||
        !install("charmap_decode", mapped, charmap<false>) ||
        !install("charmap_build", unary, charmap_build))
        return;
    auto &data = *ctx.payload<CodecRegistry>(owner).contents;
    for (const auto name :
         {U"strict", U"ignore", U"replace", U"xmlcharrefreplace", U"backslashreplace",
          U"namereplace", U"surrogatepass", U"surrogateescape"}) {
        make_str(ctx, name, key);
        const auto handler_name = roots.root(key.value());
        if (locked(ctx, data, [&] { return data.errors.contains(name); }))
            continue;
        const Value capture[] = {handler_name.get(ctx)};
        make_native_function(ctx, "codec_error_handler", unary, error_handler, capture, value,
                             NativeFunctionBinding::Function);
        decltype(data.errors) prepared;
        prepared.emplace(name, value.value());
        auto node = prepared.extract(prepared.begin());
        locked(ctx, data, [&] {
            data.errors.insert(std::move(node));
            ctx.account_external(owner, data.external_bytes());
        });
    }
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
