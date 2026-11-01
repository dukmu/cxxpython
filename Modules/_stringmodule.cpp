// Parsing adapted from CPython 3.12.14 Objects/stringlib/unicode_format.h.
// CPython is PSF licensed; see doc/licenses/python-3.12.14.txt.
#include "internal/stringmodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/long.hpp"

#include <atomic>
#include <limits>

namespace cxxpython::core::detail {
namespace {
template <bool Field> struct Iterator {
    ObjectHeader header;
    const Value source;
    std::atomic<std::size_t> cursor;

    Iterator(InitialType type, Value text, std::size_t start)
        : header(type), source(text), cursor(start)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(source);
    }
};

using Markup = Iterator<false>;
using Fields = Iterator<true>;

bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

struct Scan {
    Attachment &ctx;
    std::size_t steps = 0;

    void step()
    {
        if (++steps % 4096 == 0)
            ctx.poll();
    }
};

struct Slice {
    std::size_t start = 0, end = 0;
};

// -1 means non-numeric, -2 means overflow, as in get_integer().
std::int64_t integer(std::u32string_view text, Slice part, Scan &scan)
{
    if (part.start == part.end)
        return -1;
    std::int64_t value = 0;
    for (auto i = part.start; i < part.end; ++i) {
        scan.step();
        int digit = decimal_digit(text[i]);
        if (digit < 0)
            return -1;
        if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10)
            return -2;
        value = value * 10 + digit;
    }
    return value;
}

struct MarkupResult {
    Slice literal, field, spec;
    char32_t conversion = 0;
    bool present = false, ended = false;
    const char32_t *error = nullptr;
};

MarkupResult markup_next(std::u32string_view text, std::size_t &pos, Scan &scan)
{
    MarkupResult result;
    if (pos >= text.size()) {
        result.ended = true;
        return result;
    }
    auto start = pos;
    char32_t c = 0;
    bool follows = false;
    while (pos < text.size()) {
        scan.step();
        c = text[pos++];
        if (c == U'{' || c == U'}') {
            follows = true;
            break;
        }
    }
    bool at_end = pos >= text.size();
    auto len = pos - start;
    if (c == U'}' && (at_end || c != text[pos])) {
        result.error = U"Single '}' encountered in format string";
        return result;
    }
    if (at_end && c == U'{') {
        result.error = U"Single '{' encountered in format string";
        return result;
    }
    if (!at_end) {
        if (c == text[pos]) {
            ++pos;
            follows = false;
        } else {
            --len;
        }
    }
    result.literal = {start, start + len};
    if (!follows)
        return result;
    result.present = true;
    result.field.start = pos;
    c = 0;
    while (pos < text.size()) {
        scan.step();
        c = text[pos++];
        if (c == U'{') {
            result.error = U"unexpected '{' in field name";
            return result;
        }
        if (c == U'[') {
            while (pos < text.size() && text[pos] != U']') {
                scan.step();
                ++pos;
            }
            continue;
        }
        if (c == U'}' || c == U':' || c == U'!')
            break;
    }
    result.field.end = pos - 1;
    if (c == U'!' || c == U':') {
        if (c == U'!') {
            if (pos >= text.size()) {
                result.error = U"end of string while looking for conversion specifier";
                return result;
            }
            result.conversion = text[pos++];
            if (pos < text.size()) {
                c = text[pos++];
                if (c == U'}')
                    return result;
                if (c != U':') {
                    result.error = U"expected ':' after conversion specifier";
                    return result;
                }
            }
        }
        result.spec.start = pos;
        std::size_t depth = 1;
        while (pos < text.size()) {
            scan.step();
            c = text[pos++];
            if (c == U'{')
                ++depth;
            else if (c == U'}' && --depth == 0) {
                result.spec.end = pos - 1;
                return result;
            }
        }
        result.error = U"unmatched '{' in format spec";
    } else if (c != U'}') {
        result.error = U"expected '}' before end of string";
    }
    return result;
}

struct FieldResult {
    Slice name;
    std::int64_t number = -1;
    bool attribute = false, ended = false;
    const char32_t *error = nullptr;
};

FieldResult field_next(std::u32string_view text, std::size_t &pos, Scan &scan)
{
    FieldResult result;
    if (pos >= text.size()) {
        result.ended = true;
        return result;
    }
    char32_t c = text[pos++];
    result.attribute = c == U'.';
    if (result.attribute) {
        result.name.start = pos;
        while (pos < text.size() && text[pos] != U'.' && text[pos] != U'[') {
            scan.step();
            ++pos;
        }
        result.name.end = pos;
    } else if (c == U'[') {
        result.name.start = pos;
        while (pos < text.size() && text[pos] != U']') {
            scan.step();
            ++pos;
        }
        if (pos == text.size()) {
            result.error = U"Missing ']' in format string";
            return result;
        }
        result.name.end = pos++;
        result.number = integer(text, result.name, scan);
        if (result.number == -2) {
            result.error = U"Too many decimal digits in format string";
            return result;
        }
    } else {
        result.error = U"Only '.' or '[' may follow ']' in format field specifier";
        return result;
    }
    if (result.name.start == result.name.end)
        result.error = U"Empty attribute in format string";
    return result;
}

template <class T> bool receiver(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<T>(value))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"invalid iterator receiver", out);
    return false;
}

template <class T> void iter(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (receiver<T>(ctx, args[0], out))
        out.ok(args[0]);
}

void no_new(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::TypeErrorType, U"cannot create formatter iterator instances", out);
}

void slice(Attachment &ctx, std::u32string_view text, Slice part, ReturnSlot &out)
{
    make_str(ctx, std::u32string(text.substr(part.start, part.end - part.start)), out);
}

void markup_iter_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Markup>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Markup>(self);
    auto source = roots.root(state.source);
    const auto &text = ctx.payload<StrObject>(source).value;
    MarkupResult parsed;
    auto start = state.cursor.load(std::memory_order_relaxed);
    for (;;) {
        auto end = start;
        Scan scan{ctx};
        parsed = markup_next(text, end, scan);
        if (state.cursor.compare_exchange_weak(start, end, std::memory_order_relaxed))
            break;
    }
    if (parsed.error) {
        raise_error(ctx, CoreObject::ValueErrorType, parsed.error, out);
        return;
    }
    if (parsed.ended) {
        stop_iteration(ctx, out);
        return;
    }
    ReturnSlot item(ctx);
    std::vector<Value> values;
    auto append = [&](Slice part) {
        slice(ctx, text, part, item);
        if (failed(item)) {
            out.raised(item.exception());
            return false;
        }
        values.push_back(roots.root(item.value()).get(ctx));
        return true;
    };
    if (!append(parsed.literal))
        return;
    if (parsed.present) {
        if (!append(parsed.field) || !append(parsed.spec))
            return;
    } else {
        values.push_back(ctx.builtin(CoreObject::None));
        values.push_back(ctx.builtin(CoreObject::None));
    }
    if (parsed.conversion) {
        make_str(ctx, std::u32string(1, parsed.conversion), item);
        if (failed(item)) {
            out.raised(item.exception());
            return;
        }
        values.push_back(roots.root(item.value()).get(ctx));
    } else {
        values.push_back(ctx.builtin(CoreObject::None));
    }
    cxxpython::core::make_tuple(ctx, values, out);
}

void field_iter_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Fields>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Fields>(self);
    auto source = roots.root(state.source);
    const auto &text = ctx.payload<StrObject>(source).value;
    FieldResult parsed;
    auto start = state.cursor.load(std::memory_order_relaxed);
    for (;;) {
        auto end = start;
        Scan scan{ctx};
        parsed = field_next(text, end, scan);
        if (state.cursor.compare_exchange_weak(start, end, std::memory_order_relaxed))
            break;
    }
    if (parsed.error) {
        raise_error(ctx, CoreObject::ValueErrorType, parsed.error, out);
        return;
    }
    if (parsed.ended) {
        stop_iteration(ctx, out);
        return;
    }
    ReturnSlot item(ctx);
    if (parsed.number >= 0)
        make_int(ctx, parsed.number, item);
    else
        slice(ctx, text, parsed.name, item);
    if (failed(item)) {
        out.raised(item.exception());
        return;
    }
    auto value = roots.root(item.value());
    const Value items[] = {ctx.builtin(parsed.attribute ? CoreObject::True : CoreObject::False),
                           value.get(ctx)};
    make_tuple(ctx, items, out);
}

bool check_text(Attachment &ctx, Handle value, ReturnSlot &out)
{
    if (ctx.has_layout<StrObject>(value.get(ctx)) &&
        is_subtype(ctx, ctx.type_of(value.get(ctx)), ctx.builtin(CoreObject::StrType)))
        return true;
    auto name = TypeView(ctx, ctx.type_of(value.get(ctx))).name();
    std::u32string type = std::holds_alternative<std::u32string>(name)
                              ? std::get<std::u32string>(name)
                              : ctx.payload<StrObject>(std::get<Value>(name)).value;
    raise_error(ctx, CoreObject::TypeErrorType, U"expected str, got " + type.substr(0, 100), out);
    return false;
}

template <bool Field>
void factory(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto source = roots.root(args[0]);
    if (!check_text(ctx, source, out))
        return;
    auto type = roots.root(ctx.payload<NativeFunctionObject>(function).captures[0]);
    const auto &text = ctx.payload<StrObject>(source).value;
    std::size_t end = 0;
    Scan scan{ctx};
    if constexpr (Field) {
        while (end < text.size() && text[end] != U'.' && text[end] != U'[') {
            scan.step();
            ++end;
        }
    }
    auto number = Field ? integer(text, {0, end}, scan) : -1;
    if (number == -2) {
        raise_error(ctx, CoreObject::ValueErrorType, U"Too many decimal digits in format string",
                    out);
        return;
    }
    ReturnSlot iterator(ctx);
    ctx.allocate_instance<Iterator<Field>>(type, iterator, source.get(ctx), end);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    if constexpr (!Field) {
        out.ok(iterator.value());
    } else {
        auto iter_root = roots.root(iterator.value());
        ReturnSlot first(ctx);
        if (number >= 0)
            make_int(ctx, number, first);
        else
            slice(ctx, text, {0, end}, first);
        if (failed(first)) {
            out.raised(first.exception());
            return;
        }
        auto first_root = roots.root(first.value());
        const Value items[] = {first_root.get(ctx), iter_root.get(ctx)};
        make_tuple(ctx, items, out);
    }
}
} // namespace

void initialize_string(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot value(ctx), key(ctx);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto install = [&](const char *type_name, const char *factory_name, const char32_t *key_name,
                       LayoutDescriptor layout, NativeEntry next, NativeEntry iter_entry,
                       NativeEntry create) {
        NativeTypeSpec spec{{type_name}, layout};
        spec.type.module = U"_string";
        spec.type.slots = std::vector<std::u32string>{};
        spec.type.subclassable = false;
        spec.type.immutable = true;
        create_native_type(ctx, spec, value);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        auto type = roots.root(value.value());
        install_native_attribute(ctx, core, type, U"__new__", unary, no_new);
        auto dict = *TypeView(ctx, type).dictionary();
        auto new_function = roots.root(*name_lookup(ctx, dict, U"__new__"));
        ctx.allocate_instance<StaticMethodObject>(
            core[static_cast<std::size_t>(CoreObject::StaticMethodType)], value,
            new_function.get(ctx));
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        make_str(ctx, U"__new__", key);
        if (failed(key)) {
            out.raised(key.exception());
            return false;
        }
        name_store(ctx, dict, key.value(), value.value());
        install_native_attribute(ctx, core, type, U"__iter__", unary, iter_entry);
        install_native_attribute(ctx, core, type, U"__next__", unary, next);
        const Value captures[] = {type.get(ctx)};
        make_native_function(ctx, factory_name, unary, create, captures, value,
                             NativeFunctionBinding::Function);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        auto function = roots.root(value.value());
        make_str(ctx, key_name, key);
        if (failed(key)) {
            out.raised(key.exception());
            return false;
        }
        dict_store(ctx, dictionary, roots.root(key.value()), function, out);
        return !failed(out);
    };
    if (!install("formatteriterator", "formatter_parser", U"formatter_parser",
                 LayoutDescriptor::of<Markup>(), markup_iter_next, iter<Markup>, factory<false>) ||
        !install("fieldnameiterator", "formatter_field_name_split", U"formatter_field_name_split",
                 LayoutDescriptor::of<Fields>(), field_iter_next, iter<Fields>, factory<true>))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
