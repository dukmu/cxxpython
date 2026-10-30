#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace cxxpython::core {
bool is_bytes(Attachment &context, Value value)
{
    return context.has_layout<BytesObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::BytesType));
}
void make_bytes(Attachment &context, std::string data, ReturnSlot &out)
{
    RootScope roots(context);
    context.allocate_instance<BytesObject>(roots.root(context.builtin(CoreObject::BytesType)), out,
                                           std::move(data));
}
namespace {
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (is_bytes(c, value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"bytes descriptor requires bytes", out);
    return false;
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool parse_arguments(Attachment &c, Value positional, Value named,
                     std::span<const std::u32string_view> names,
                     std::span<std::optional<Handle>> values, RootScope &roots, ReturnSlot &out)
{
    const auto &args = c.payload<TupleObject>(positional).items;
    if (args.size() > names.size()) {
        raise_error(c, CoreObject::TypeErrorType, U"too many bytes arguments", out);
        return false;
    }
    for (std::size_t i = 0; i < args.size(); ++i)
        values[i] = roots.root(args[i]);
    DictView keywords(c, named);
    for (auto item : keywords.entries()) {
        const auto &name = c.payload<StrObject>(item.key).value;
        std::size_t i = 0;
        while (i < names.size() && name != names[i])
            ++i;
        if (i == names.size() || values[i]) {
            raise_error(c, CoreObject::TypeErrorType, U"invalid or repeated bytes argument", out);
            return false;
        }
        values[i] = roots.root(item.value);
    }
    return true;
}
bool text_argument(Attachment &c, Handle value, ReturnSlot &out)
{
    if (c.has_layout<StrObject>(value.get(c)) &&
        is_subtype(c, c.type_of(value), c.builtin(CoreObject::StrType)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"encoding and errors must be strings", out);
    return false;
}
// CPython bytes_new_impl: __bytes__ precedes index/iterable conversion, and
// subtype construction copies into the requested immutable native layout.
void create(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto type = roots.root(args[0]);
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::BytesType))) {
        raise_error(c, CoreObject::TypeErrorType, U"bytes.__new__ requires bytes subtype", out);
        return;
    }
    const std::array<std::u32string_view, 3> names{U"source", U"encoding", U"errors"};
    std::array<std::optional<Handle>, 3> values;
    if (!parse_arguments(c, args[1], args[2], names, values, roots, out))
        return;
    const bool exact = type.get(c) == c.builtin(CoreObject::BytesType);
    auto finish = [&](std::string data) {
        c.allocate_instance<BytesObject>(type, out, std::move(data));
    };
    if (values[1] && !text_argument(c, *values[1], out))
        return;
    if (values[2] && !text_argument(c, *values[2], out))
        return;
    if (!values[0]) {
        if (values[1] || values[2]) {
            raise_error(c, CoreObject::TypeErrorType, U"encoding or errors without a string", out);
            return;
        }
        finish({});
        return;
    }
    const auto source = *values[0];
    if (values[1]) {
        if (!text_argument(c, source, out))
            return;
        detail::encode_text(c, source, *values[1],
                            values[2].value_or(roots.root(c.builtin(CoreObject::None))), out);
        if (!failed(out) && !exact)
            finish(c.payload<BytesObject>(out.value()).value);
        return;
    }
    if (values[2]) {
        raise_error(c, CoreObject::TypeErrorType, U"errors without an encoding", out);
        return;
    }
    auto method = detail::type_attribute(c, c.type_of(source), U"__bytes__");
    if (method) {
        detail::invoke_special(c, source, roots.root(*method), {}, out);
        if (failed(out))
            return;
        if (!is_bytes(c, out.value())) {
            raise_error(c, CoreObject::TypeErrorType, U"__bytes__ returned non-bytes", out);
            return;
        }
        if (!exact)
            finish(c.payload<BytesObject>(out.value()).value);
        return;
    }
    if (is_bytearray(c, source.get(c))) {
        finish(detail::bytearray_snapshot(c, source));
        return;
    }
    if (c.has_layout<StrObject>(source.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"string argument without an encoding", out);
        return;
    }
    const auto slot = resolve_protocol(c, roots.root(c.type_of(source)), Protocol::Index, roots);
    if (slot.state != SlotState::Missing) {
        index(c, source, out);
        if (!failed(out)) {
            std::int64_t count;
            if (!int_fits_signed(c, out.value(), count)) {
                raise_error(c, CoreObject::OverflowErrorType,
                            U"bytes count does not fit index-sized integer", out);
                return;
            }
            if (count < 0) {
                raise_error(c, CoreObject::ValueErrorType, U"negative count", out);
                return;
            }
            if (static_cast<std::uint64_t>(count) >
                std::numeric_limits<std::ptrdiff_t>::max() - sizeof(BytesObject)) {
                raise_error(c, CoreObject::OverflowErrorType, U"byte string is too large", out);
                return;
            }
            if (static_cast<std::uint64_t>(count) > std::string{}.max_size()) {
                out.raised(c.builtin(CoreObject::MemoryError));
                return;
            }
            finish(std::string(static_cast<std::size_t>(count), '\0'));
            return;
        }
        if (!is_subtype(c, c.type_of(out.exception()), c.builtin(CoreObject::TypeErrorType)))
            return;
    }
    get_iter(c, source, out);
    if (failed(out))
        return;
    const auto iterator = roots.root(out.value());
    std::string data;
    ReturnSlot item(c), number(c);
    for (;;) {
        const auto status = iter_next(c, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        RootScope iteration(c);
        index(c, iteration.root(item.value()), number);
        if (failed(number)) {
            out.raised(number.exception());
            return;
        }
        std::int64_t value;
        if (!int_fits_signed(c, number.value(), value) || value < 0 || value > 255) {
            raise_error(c, CoreObject::ValueErrorType, U"bytes must be in range(0, 256)", out);
            return;
        }
        data.push_back(static_cast<char>(value));
    }
    finish(std::move(data));
}
void bytes_value(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (c.type_of(args[0]) == c.builtin(CoreObject::BytesType))
        out.ok(args[0]);
    else
        make_bytes(c, c.payload<BytesObject>(args[0]).value, out);
}
void representation(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    const auto &data = c.payload<BytesObject>(args[0]).value;
    const char quote =
        data.find('\'') != std::string::npos && data.find('"') == std::string::npos ? '"' : '\'';
    constexpr char hex[] = "0123456789abcdef";
    std::u32string text{U'b', static_cast<char32_t>(quote)};
    for (unsigned char byte : data) {
        if (byte == '\\' || byte == quote) {
            text += U'\\';
            text += byte;
        } else if (byte == '\t')
            text += U"\\t";
        else if (byte == '\n')
            text += U"\\n";
        else if (byte == '\r')
            text += U"\\r";
        else if (byte < 32 || byte >= 127) {
            text += U"\\x";
            text += hex[byte >> 4];
            text += hex[byte & 15];
        } else
            text += byte;
    }
    text += quote;
    make_str(c, std::move(text), out);
}
void size(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (receiver(c, args[0], out))
        make_int(c, c.payload<BytesObject>(args[0]).value.size(), out);
}
void hash_value(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    const auto &data = c.payload<BytesObject>(args[0]).value;
    const auto word = data.empty()
                          ? std::uint64_t{0}
                          : c.keyed_hash(std::as_bytes(std::span(data.data(), data.size())));
    const auto value = std::bit_cast<std::int64_t>(word);
    make_int(c, value == -1 ? -2 : value, out);
}
template <CompareOp Op>
void compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (!is_bytes(c, args[1])) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    const auto order =
        c.payload<BytesObject>(args[0]).value.compare(c.payload<BytesObject>(args[1]).value);
    const bool result = Op == CompareOp::Eq   ? order == 0
                        : Op == CompareOp::Ne ? order != 0
                        : Op == CompareOp::Lt ? order < 0
                        : Op == CompareOp::Le ? order <= 0
                        : Op == CompareOp::Gt ? order > 0
                                              : order >= 0;
    out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
}
void item(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto &data = c.payload<BytesObject>(self).value;
    if (c.type_of(args[1]) == c.builtin(CoreObject::SliceType)) {
        detail::SliceBounds bounds;
        if (!detail::unpack_slice(c, roots.root(args[1]), bounds, out))
            return;
        const auto slice = bounds.adjust(data.size());
        if (slice.step == 1 && slice.start == 0 &&
            static_cast<std::size_t>(slice.length) == data.size() &&
            c.type_of(self) == c.builtin(CoreObject::BytesType)) {
            out.ok(self.get(c));
            return;
        }
        std::string result;
        result.reserve(static_cast<std::size_t>(slice.length));
        auto position = slice.start;
        for (std::int64_t i = 0; i < slice.length; ++i) {
            result += data[position];
            if (i + 1 < slice.length)
                position += slice.step;
        }
        make_bytes(c, std::move(result), out);
        return;
    }
    index(c, roots.root(args[1]), out);
    if (failed(out))
        return;
    std::int64_t position;
    if (!int_fits_signed(c, out.value(), position)) {
        raise_error(c, CoreObject::IndexErrorType, U"index out of range", out);
        return;
    }
    if (position < 0)
        position += static_cast<std::int64_t>(data.size());
    if (position < 0 || static_cast<std::size_t>(position) >= data.size()) {
        raise_error(c, CoreObject::IndexErrorType, U"index out of range", out);
        return;
    }
    make_int(c, static_cast<unsigned char>(data[position]), out);
}
void add(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (!is_bytes(c, args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"can only concatenate bytes-like objects", out);
        return;
    }
    make_bytes(c, c.payload<BytesObject>(args[0]).value + c.payload<BytesObject>(args[1]).value,
               out);
}
void repeat(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    index(c, roots.root(args[1]), out);
    if (failed(out))
        return;
    std::int64_t count;
    if (!int_fits_signed(c, out.value(), count)) {
        raise_error(c, CoreObject::OverflowErrorType, U"repeat count too large", out);
        return;
    }
    const auto &data = c.payload<BytesObject>(self).value;
    if (count == 1 && c.type_of(self) == c.builtin(CoreObject::BytesType)) {
        out.ok(self.get(c));
        return;
    }
    std::string result;
    if (count > 0 && !data.empty()) {
        if (static_cast<std::uint64_t>(count) > result.max_size() / data.size()) {
            out.raised(c.builtin(CoreObject::MemoryError));
            return;
        }
        result.reserve(data.size() * count);
        for (std::int64_t i = 0; i < count; ++i)
            result += data;
    }
    make_bytes(c, std::move(result), out);
}
void contains(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]), value = roots.root(args[1]);
    const auto &data = c.payload<BytesObject>(self).value;
    bool found;
    if (is_bytes(c, args[1]))
        found = data.find(c.payload<BytesObject>(value).value) != std::string::npos;
    else {
        index(c, value, out);
        if (failed(out))
            return;
        std::int64_t byte;
        if (!int_fits_signed(c, out.value(), byte) || byte < 0 || byte > 255) {
            raise_error(c, CoreObject::ValueErrorType, U"byte must be in range(0, 256)", out);
            return;
        }
        found = data.find(static_cast<char>(byte)) != std::string::npos;
    }
    out.ok(c.builtin(found ? CoreObject::True : CoreObject::False));
}
void iterator(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    c.allocate_instance<SequenceIteratorObject>(
        roots.root(c.builtin(CoreObject::BytesIteratorType)), out, args[0]);
}
void next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<SequenceIteratorObject>(args[0]) ||
        c.type_of(args[0]) != c.builtin(CoreObject::BytesIteratorType)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected bytes iterator", out);
        return;
    }
    auto &cursor = c.payload<SequenceIteratorObject>(args[0]);
    ReturnSlot owner(c), value(c);
    for (;;) {
        c.load(args[0], cursor.owner, owner);
        if (owner.tag() == ResultTag::Empty) {
            detail::stop_iteration(c, out);
            return;
        }
        const auto &data = c.payload<BytesObject>(owner.value()).value;
        auto position = cursor.index.load(std::memory_order_relaxed);
        if (position >= data.size()) {
            c.store(args[0], cursor.owner, {});
            detail::stop_iteration(c, out);
            return;
        }
        make_int(c, static_cast<unsigned char>(data[position]), value);
        if (cursor.index.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
            out.ok(value.value());
            return;
        }
        c.poll();
    }
}
void iterator_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void decode(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    if (!text_argument(c, roots.root(args[1]), out) || !text_argument(c, roots.root(args[2]), out))
        return;
    detail::decode_bytes(c, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void join(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto separator = roots.root(args[0]);
    get_iter(c, roots.root(args[1]), out);
    if (failed(out))
        return;
    const auto input = roots.root(out.value());
    std::string result;
    ReturnSlot value(c);
    bool first = true;
    for (;;) {
        const auto status = iter_next(c, input, value);
        if (status == NextStatus::Raised) {
            out.raised(value.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        if (!is_bytes(c, value.value())) {
            raise_error(c, CoreObject::TypeErrorType, U"join requires bytes-like values", out);
            return;
        }
        if (!first)
            result += c.payload<BytesObject>(separator).value;
        first = false;
        result += c.payload<BytesObject>(value.value()).value;
    }
    make_bytes(c, std::move(result), out);
}
void fromhex(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto cls = roots.root(args[0]);
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::BytesType))) {
        raise_error(c, CoreObject::TypeErrorType, U"fromhex requires bytes type", out);
        return;
    }
    if (!text_argument(c, roots.root(args[1]), out))
        return;
    const auto &text = c.payload<StrObject>(args[1]).value;
    auto digit = [](char32_t ch) {
        return ch >= U'0' && ch <= U'9'   ? int(ch - U'0')
               : ch >= U'a' && ch <= U'f' ? int(ch - U'a' + 10)
               : ch >= U'A' && ch <= U'F' ? int(ch - U'A' + 10)
                                          : -1;
    };
    auto invalid = [&](std::size_t position) {
        std::u32string message = U"non-hexadecimal number found in fromhex() arg at position ";
        for (auto ch : std::to_string(position))
            message += static_cast<char32_t>(ch);
        raise_error(c, CoreObject::ValueErrorType, std::move(message), out);
    };
    // Upstream rejects the first non-ASCII character before parsing any pair.
    const auto non_ascii =
        std::find_if(text.begin(), text.end(), [](char32_t ch) { return ch >= 128; });
    if (non_ascii != text.end()) {
        invalid(static_cast<std::size_t>(non_ascii - text.begin()));
        return;
    }
    std::string data;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == U' ' || (text[i] >= U'\t' && text[i] <= U'\r')) {
            ++i;
            continue;
        }
        const auto hi = digit(text[i]);
        if (hi < 0) {
            invalid(i);
            return;
        }
        ++i;
        const auto lo = i < text.size() ? digit(text[i]) : -1;
        if (lo < 0) {
            invalid(i);
            return;
        }
        ++i;
        data += static_cast<char>(hi * 16 + lo);
    }
    c.allocate_instance<BytesObject>(cls, out, std::move(data));
}
void hex(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const std::array<std::u32string_view, 2> names{U"sep", U"bytes_per_sep"};
    std::array<std::optional<Handle>, 2> values;
    if (!parse_arguments(c, args[1], args[2], names, values, roots, out))
        return;
    char32_t separator = 0;
    if (values[0]) {
        const auto sep = values[0]->get(c);
        if (is_bytes(c, sep) && c.payload<BytesObject>(sep).value.size() == 1)
            separator = static_cast<unsigned char>(c.payload<BytesObject>(sep).value[0]);
        else if (c.has_layout<StrObject>(sep) && c.payload<StrObject>(sep).value.size() == 1)
            separator = c.payload<StrObject>(sep).value[0];
        else {
            raise_error(c,
                        c.has_layout<StrObject>(sep) || is_bytes(c, sep)
                            ? CoreObject::ValueErrorType
                            : CoreObject::TypeErrorType,
                        U"sep must be length 1 str or bytes", out);
            return;
        }
        if (separator > 127) {
            raise_error(c, CoreObject::ValueErrorType, U"sep must be ASCII", out);
            return;
        }
    }
    std::int64_t group = 1;
    if (values[1]) {
        index(c, *values[1], out);
        if (failed(out))
            return;
        if (!int_fits_signed(c, out.value(), group) || group < INT32_MIN || group > INT32_MAX) {
            raise_error(c, CoreObject::OverflowErrorType, U"bytes_per_sep does not fit int", out);
            return;
        }
    }
    const auto &data = c.payload<BytesObject>(args[0]).value;
    constexpr char digits[] = "0123456789abcdef";
    std::u32string result;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto count = group < 0 ? -group : group;
        if (i && values[0] && count &&
            (group > 0 ? (data.size() - i) % count == 0 : i % count == 0))
            result += separator;
        const auto byte = static_cast<unsigned char>(data[i]);
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    make_str(c, std::move(result), out);
}
} // namespace
void detail::install_bytes(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    auto install = [&](Handle owner, std::u32string name, SignatureLayout signature,
                       NativeEntry entry, NativeSlotKind kind = NativeSlotKind::Ordinary) {
        install_native_attribute(c, core, owner, std::move(name), std::move(signature), entry, {},
                                 kind);
    };
    install(at(CoreObject::BytesType), U"__new__", variadic, create);
    ReturnSlot wrapper(c), key(c);
    auto function = roots.root(
        *name_lookup(c, *TypeView(c, at(CoreObject::BytesType)).dictionary(), U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(c, *TypeView(c, at(CoreObject::BytesType)).dictionary(), key.value(),
               wrapper.value());
    for (auto [name, entry] : {std::pair{U"__bytes__", bytes_value},
                               {U"__repr__", representation},
                               {U"__str__", representation},
                               {U"__len__", size},
                               {U"__hash__", hash_value},
                               {U"__iter__", iterator}})
        install(at(CoreObject::BytesType), name, unary, entry);
    for (auto [name, entry] : {std::pair{U"__eq__", compare<CompareOp::Eq>},
                               {U"__ne__", compare<CompareOp::Ne>},
                               {U"__lt__", compare<CompareOp::Lt>},
                               {U"__le__", compare<CompareOp::Le>},
                               {U"__gt__", compare<CompareOp::Gt>},
                               {U"__ge__", compare<CompareOp::Ge>},
                               {U"__getitem__", item},
                               {U"__contains__", contains},
                               {U"join", join}})
        install(at(CoreObject::BytesType), name, pair, entry);
    install(at(CoreObject::BytesType), U"__add__", pair, add, NativeSlotKind::Sequence);
    install(at(CoreObject::BytesType), U"__mul__", pair, repeat, NativeSlotKind::Sequence);
    install(at(CoreObject::BytesType), U"__rmul__", pair, repeat, NativeSlotKind::Sequence);
    install(at(CoreObject::BytesType), U"hex", variadic, hex);
    install(at(CoreObject::BytesType), U"fromhex", pair, fromhex);
    function = roots.root(
        *name_lookup(c, *TypeView(c, at(CoreObject::BytesType)).dictionary(), U"fromhex"));
    c.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), wrapper,
                                           function.get(c));
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"fromhex");
    name_store(c, *TypeView(c, at(CoreObject::BytesType)).dictionary(), key.value(),
               wrapper.value());
    ReturnSlot encoding(c), errors(c);
    c.allocate_instance<StrObject>(at(CoreObject::StrType), encoding, U"utf-8");
    c.allocate_instance<StrObject>(at(CoreObject::StrType), errors, U"strict");
    auto decoded = unary;
    decoded.parameters.push_back(
        {U"encoding", ParameterKind::PositionalOrKeyword, encoding.value()});
    decoded.parameters.push_back({U"errors", ParameterKind::PositionalOrKeyword, errors.value()});
    install(at(CoreObject::BytesType), U"decode", decoded, decode);
    install(at(CoreObject::BytesIteratorType), U"__iter__", unary, iterator_self);
    install(at(CoreObject::BytesIteratorType), U"__next__", unary, next);
}
} // namespace cxxpython::core
