#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <bit>
#include <charconv>

namespace cxxpython::core {
namespace {
#include "unicode_nonprintable.inc"

bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void type_error(Attachment &context, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, U"descriptor received an incompatible value",
                out);
}
template <class T, CoreObject Type> bool instance_of(Attachment &context, Value value)
{
    return is_subtype(context, context.type_of(value), context.builtin(Type)) &&
           context.has_layout<T>(value);
}
bool string_value(Attachment &context, Value value)
{
    return instance_of<StrObject, CoreObject::StrType>(context, value);
}
std::int64_t signed_hash(std::uintptr_t word)
{
    const auto result = static_cast<std::int64_t>(std::bit_cast<std::intptr_t>(word));
    return result == -1 ? -2 : result;
}
std::u32string pointer_text(Value value)
{
    char buffer[2 * sizeof(std::uintptr_t)];
    auto converted = std::to_chars(std::begin(buffer), std::end(buffer),
                                   reinterpret_cast<std::uintptr_t>(value.object()), 16);
    return U"0x" + std::u32string(buffer, converted.ptr);
}
std::u32string type_name(Attachment &context, Value type, bool qualified)
{
    TypeView data(context, type);
    return context.payload<StrObject>(std::get<Value>(qualified ? data.qualname() : data.name()))
        .value;
}
std::u32string display_type(Attachment &context, Value type)
{
    TypeView data(context, type);
    DictView names(context, data.storage());
    auto module = names.find(U"__module__");
    if (module && string_value(context, *module)) {
        const auto &name = context.payload<StrObject>(*module).value;
        if (name != U"builtins")
            return name + U"." + context.payload<StrObject>(std::get<Value>(data.qualname())).value;
    }
    return context.payload<StrObject>(std::get<Value>(data.name())).value;
}
void object_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    make_int(context, signed_hash(std::rotr(reinterpret_cast<std::uintptr_t>(args[0].object()), 4)),
             out);
}
void object_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    make_str(context,
             U"<" + display_type(context, context.type_of(args[0])) + U" object at " +
                 pointer_text(args[0]) + U">",
             out);
}
void object_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    repr(context, roots.root(args[0]), out);
}
void type_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]), context.type_type())) {
        type_error(context, out);
        return;
    }
    make_str(context, U"<class '" + display_type(context, args[0]) + U"'>", out);
}
void singleton_repr(Attachment &context, Handle fn, std::span<const Value> args, ReturnSlot &out)
{
    const auto &captures = context.payload<NativeFunctionObject>(fn).captures;
    if (args[0] != captures[0]) {
        type_error(context, out);
        return;
    }
    out.ok(captures[1]);
}
void none_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != context.builtin(CoreObject::None)) {
        type_error(context, out);
        return;
    }
    make_int(context, static_cast<std::intptr_t>(0xfca86420U), out);
}
void string_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    const auto &text = context.payload<StrObject>(args[0]).value;
    make_int(context, detail::name_hash(context, text), out);
}
void string_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (context.type_of(args[0]) == context.builtin(CoreObject::StrType))
        out.ok(args[0]);
    else
        make_str(context, context.payload<StrObject>(args[0]).value, out);
}
void string_contains(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    for (auto value : args) {
        if (!context.has_layout<StrObject>(value) ||
            !is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType))) {
            raise_error(context, CoreObject::TypeErrorType, U"string containment requires a string",
                        out);
            return;
        }
    }
    const auto &text = context.payload<StrObject>(args[0]).value;
    const auto &part = context.payload<StrObject>(args[1]).value;
    out.ok(context.builtin(text.find(part) == text.npos ? CoreObject::False : CoreObject::True));
}
void string_rpartition(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0]) || !string_value(context, args[1])) {
        type_error(context, out);
        return;
    }
    const auto &text = context.payload<StrObject>(args[0]).value;
    const auto &separator = context.payload<StrObject>(args[1]).value;
    if (separator.empty()) {
        raise_error(context, CoreObject::ValueErrorType, U"empty separator", out);
        return;
    }
    const auto position = text.rfind(separator);
    const auto before = position == std::u32string::npos ? U"" : text.substr(0, position);
    const auto middle = position == std::u32string::npos ? U"" : separator;
    const auto after =
        position == std::u32string::npos ? text : text.substr(position + separator.size());
    RootScope roots(context);
    ReturnSlot part(context);
    make_str(context, before, part);
    const auto first = roots.root(part.value());
    make_str(context, middle, part);
    const auto second = roots.root(part.value());
    make_str(context, after, part);
    const auto third = roots.root(part.value());
    const std::array values{first.get(context), second.get(context), third.get(context)};
    cxxpython::core::make_tuple(context, values, out);
}
void string_join(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    const auto separator = roots.root(args[0]);
    ReturnSlot iterator(context), item(context);
    get_iter(context, roots.root(args[1]), iterator);
    if (raised(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    const auto it = roots.root(iterator.value());
    std::vector<Handle> items;
    for (;;) {
        const auto status = iter_next(context, it, item);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        items.push_back(roots.root(item.value()));
    }
    if (items.size() == 1 && context.type_of(items[0]) == context.builtin(CoreObject::StrType)) {
        out.ok(items[0].get(context));
        return;
    }
    std::u32string joined;
    const auto &separator_text = context.payload<StrObject>(separator).value;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto value = items[i].get(context);
        if (!string_value(context, value)) {
            raise_error(context, CoreObject::TypeErrorType, U"sequence item is not a string", out);
            return;
        }
        if (i)
            joined += separator_text;
        joined += context.payload<StrObject>(value).value;
    }
    make_str(context, std::move(joined), out);
}
bool printable(char32_t point)
{
    if (point == U' ')
        return true;
    auto next = std::upper_bound(std::begin(non_printable), std::end(non_printable), point,
                                 [](auto value, auto range) { return value < range.first; });
    return next == std::begin(non_printable) || point > (next - 1)->second;
}
void string_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    const auto &text = context.payload<StrObject>(args[0]).value;
    const auto quote =
        text.find(U'\'') != std::u32string::npos && text.find(U'"') == std::u32string::npos ? U'"'
                                                                                            : U'\'';
    std::u32string result(1, quote);
    constexpr char32_t digits[] = U"0123456789abcdef";
    for (auto point : text) {
        if (point == quote || point == U'\\') {
            result += U'\\';
            result += point;
        } else if (point == U'\n')
            result += U"\\n";
        else if (point == U'\r')
            result += U"\\r";
        else if (point == U'\t')
            result += U"\\t";
        else if (printable(point))
            result += point;
        else {
            const unsigned count = point <= 0xff ? 2 : point <= 0xffff ? 4 : 8;
            result += count == 2 ? U"\\x" : count == 4 ? U"\\u" : U"\\U";
            for (auto remaining = count; remaining; --remaining)
                result += digits[(point >> ((remaining - 1) * 4)) & 15];
        }
    }
    result += quote;
    make_str(context, std::move(result), out);
}
template <CompareOp Op> void order_result(Attachment &context, int order, ReturnSlot &out)
{
    bool result;
    if constexpr (Op == CompareOp::Eq)
        result = order == 0;
    if constexpr (Op == CompareOp::Ne)
        result = order != 0;
    if constexpr (Op == CompareOp::Lt)
        result = order < 0;
    if constexpr (Op == CompareOp::Le)
        result = order <= 0;
    if constexpr (Op == CompareOp::Gt)
        result = order > 0;
    if constexpr (Op == CompareOp::Ge)
        result = order >= 0;
    out.ok(context.builtin(result ? CoreObject::True : CoreObject::False));
}
template <CompareOp Op>
void string_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!string_value(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    order_result<Op>(context,
                     context.payload<StrObject>(args[0]).value.compare(
                         context.payload<StrObject>(args[1]).value),
                     out);
}
struct ReprGuard {
    Attachment &context;
    Value object;
    bool entered;
    ReprGuard(Attachment &c, Handle value)
        : context(c), object(value.get(c)), entered(c.enter_repr(value))
    {}
    ~ReprGuard()
    {
        if (entered)
            context.leave_repr(object);
    }
};
void tuple_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    ReprGuard recursion(context, roots.root(args[0]));
    if (!recursion.entered) {
        make_str(context, U"(...)", out);
        return;
    }
    const auto &items = context.payload<TupleObject>(args[0]).items;
    std::u32string result = U"(";
    for (std::size_t i = 0; i < items.size(); ++i) {
        RootScope item_roots(context);
        ReturnSlot text(context);
        repr(context, item_roots.root(items[i]), text);
        if (raised(text)) {
            out.raised(text.exception());
            return;
        }
        if (i)
            result += U", ";
        result += context.payload<StrObject>(text.value()).value;
    }
    if (items.size() == 1)
        result += U',';
    result += U')';
    make_str(context, std::move(result), out);
}
void list_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<ListObject, CoreObject::ListType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    ReprGuard recursion(context, self);
    if (!recursion.entered) {
        make_str(context, U"[...]", out);
        return;
    }
    std::u32string result = U"[";
    ReturnSlot item(context), text(context);
    for (std::size_t i = 0;; ++i) {
        detail::list_item(context, self, i, item);
        if (item.tag() == ResultTag::Empty)
            break;
        RootScope iteration(context);
        repr(context, iteration.root(item.value()), text);
        if (raised(text)) {
            out.raised(text.exception());
            return;
        }
        if (i)
            result += U", ";
        result += context.payload<StrObject>(text.value()).value;
    }
    result += U']';
    make_str(context, std::move(result), out);
}
void tuple_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    constexpr std::uintptr_t prime1 =
        sizeof(std::uintptr_t) == 8 ? 11400714785074694791ULL : 2654435761UL;
    constexpr std::uintptr_t prime2 =
        sizeof(std::uintptr_t) == 8 ? 14029467366897019727ULL : 2246822519UL;
    constexpr std::uintptr_t prime5 =
        sizeof(std::uintptr_t) == 8 ? 2870177450012600261ULL : 374761393UL;
    auto accumulator = prime5;
    const auto &items = context.payload<TupleObject>(args[0]).items;
    for (auto value : items) {
        RootScope roots(context);
        ReturnSlot hashed(context);
        cxxpython::core::hash(context, roots.root(value), hashed);
        if (raised(hashed)) {
            out.raised(hashed.exception());
            return;
        }
        std::int64_t lane = 0;
        (void)int_fits_signed(context, hashed.value(), lane);
        accumulator += static_cast<std::uintptr_t>(lane) * prime2;
        accumulator = std::rotl(accumulator, sizeof(std::uintptr_t) == 8 ? 31 : 13);
        accumulator *= prime1;
    }
    accumulator += items.size() ^ (prime5 ^ 3527539UL);
    make_int(context,
             accumulator == std::numeric_limits<std::uintptr_t>::max() ? 1546275796
                                                                       : signed_hash(accumulator),
             out);
}
void dictionary_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    const auto is_dict = context.has_layout<DictObject>(args[0]);
    const auto is_proxy = context.has_layout<MappingProxyObject>(args[0]);
    const auto is_view = context.has_layout<DictViewObject>(args[0]);
    if (!is_dict && !is_proxy && !is_view) {
        type_error(context, out);
        return;
    }
    ReprGuard recursion(context, self);
    if (!recursion.entered) {
        make_str(context, U"{...}", out);
        return;
    }
    auto dictionary = args[0];
    if (is_proxy)
        dictionary = context.payload<MappingProxyObject>(self).mapping;
    if (is_view)
        dictionary = context.payload<DictViewObject>(self).dictionary;
    DictView entries(context, dictionary);
    const auto kind = is_view ? context.payload<DictViewObject>(self).kind : DictViewKind::Items;
    std::u32string result = is_proxy                       ? U"mappingproxy({"
                            : !is_view                     ? U"{"
                            : kind == DictViewKind::Keys   ? U"dict_keys(["
                            : kind == DictViewKind::Values ? U"dict_values(["
                                                           : U"dict_items([";
    bool first = true;
    ReturnSlot text(context);
    for (const auto &entry : entries.entries()) {
        if (!first)
            result += U", ";
        first = false;
        if (is_view && kind == DictViewKind::Items)
            result += U'(';
        if (kind != DictViewKind::Values) {
            repr(context, roots.root(entry.key), text);
            if (raised(text)) {
                out.raised(text.exception());
                return;
            }
            result += context.payload<StrObject>(text.value()).value;
        }
        if (kind != DictViewKind::Keys) {
            if (kind == DictViewKind::Items)
                result += is_view ? U", " : U": ";
            repr(context, roots.root(entry.value), text);
            if (raised(text)) {
                out.raised(text.exception());
                return;
            }
            result += context.payload<StrObject>(text.value()).value;
        }
        if (is_view && kind == DictViewKind::Items)
            result += U')';
    }
    result += is_proxy ? U"})" : is_view ? U"])" : U"}";
    make_str(context, std::move(result), out);
}
template <CompareOp Op>
void tuple_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    const auto &left = context.payload<TupleObject>(args[0]).items;
    const auto &right = context.payload<TupleObject>(args[1]).items;
    for (std::size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
        RootScope roots(context);
        auto a = roots.root(left[i]);
        auto b = roots.root(right[i]);
        ReturnSlot equal(context);
        container_equal(context, a, b, equal);
        if (raised(equal)) {
            out.raised(equal.exception());
            return;
        }
        if (equal.value() == context.builtin(CoreObject::True))
            continue;
        if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne)
            order_result<Op>(context, 1, out);
        else
            rich_compare(context, a, b, Op, out);
        return;
    }
    order_result<Op>(context, (left.size() > right.size()) - (left.size() < right.size()), out);
}
template <CompareOp Op>
void cell_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<CellObject, CoreObject::CellType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!instance_of<CellObject, CoreObject::CellType>(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    ReturnSlot left(context), right(context);
    const bool has_left = read_cell(context, args[0], left),
               has_right = read_cell(context, args[1], right);
    if (has_left && has_right) {
        RootScope roots(context);
        rich_compare(context, roots.root(left.value()), roots.root(right.value()), Op, out);
    } else
        order_result<Op>(context, static_cast<int>(has_left) - static_cast<int>(has_right), out);
}
void cell_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<CellObject, CoreObject::CellType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    ReturnSlot contents(context);
    const bool present = read_cell(context, args[0], contents);
    auto result = U"<cell at " + pointer_text(args[0]) + U": ";
    result += present ? type_name(context, context.type_of(contents.value()), false).substr(0, 80) +
                            U" object at " + pointer_text(contents.value())
                      : U"empty";
    result += U'>';
    make_str(context, std::move(result), out);
}
void proxy_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<MappingProxyObject, CoreObject::MappingProxyType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    cxxpython::core::hash(context, roots.root(context.payload<MappingProxyObject>(args[0]).mapping),
                          out);
}
void string_add(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0])) {
        type_error(context, out);
        return;
    }
    if (!string_value(context, args[1])) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"can only concatenate a string to a string", out);
        return;
    }
    make_str(context,
             context.payload<StrObject>(args[0]).value + context.payload<StrObject>(args[1]).value,
             out);
}
void exception_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of<ExceptionObject, CoreObject::BaseExceptionType>(context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    ReturnSlot saved(context);
    context.load(args[0], context.payload<ExceptionObject>(args[0]).args, saved);
    auto arguments = roots.root(saved.value());
    const auto &values = context.payload<TupleObject>(arguments).items;
    auto name = type_name(context, context.type_of(args[0]), false);
    ReturnSlot text(context);
    if (values.size() == 1) {
        repr(context, roots.root(values[0]), text);
        if (!raised(text))
            name += U"(" + context.payload<StrObject>(text.value()).value + U")";
    } else {
        repr(context, arguments, text);
        if (!raised(text))
            name += context.payload<StrObject>(text.value()).value;
    }
    if (raised(text)) {
        out.raised(text.exception());
        return;
    }
    make_str(context, std::move(name), out);
}
template <bool KeyError>
void exception_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!instance_of < ExceptionObject,
        KeyError ? CoreObject::KeyErrorType : CoreObject::BaseExceptionType > (context, args[0])) {
        type_error(context, out);
        return;
    }
    RootScope roots(context);
    ReturnSlot saved(context);
    context.load(args[0], context.payload<ExceptionObject>(args[0]).args, saved);
    auto arguments = roots.root(saved.value());
    const auto &values = context.payload<TupleObject>(arguments).items;
    if (values.empty())
        make_str(context, U"", out);
    else if (KeyError && values.size() == 1)
        repr(context, roots.root(values[0]), out);
    else
        str(context, values.size() == 1 ? roots.root(values[0]) : arguments, out);
}
} // namespace
namespace detail {
void install_value_protocols(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    auto install = [&](Handle owner, std::u32string name, NativeEntry entry) {
        install_native_attribute(context, core, owner, std::move(name), unary, entry);
    };
    RootScope roots(context);
    auto object = roots.root(context.object_type());
    install(object, U"__hash__", object_hash);
    install(object, U"__repr__", object_repr);
    install(object, U"__str__", object_str);
    install(roots.root(context.type_type()), U"__repr__", type_repr);
    install(at(CoreObject::StrType), U"__hash__", string_hash);
    install(at(CoreObject::StrType), U"__str__", string_str);
    install(at(CoreObject::StrType), U"__repr__", string_repr);
    install_native_attribute(context, core, at(CoreObject::StrType), U"rpartition", pair,
                             string_rpartition);
    install_native_attribute(context, core, at(CoreObject::StrType), U"join", pair, string_join);
    install_native_attribute(context, core, at(CoreObject::StrType), U"__contains__", pair,
                             string_contains);
    install_native_attribute(context, core, at(CoreObject::StrType), U"__add__", pair, string_add,
                             {}, NativeSlotKind::Sequence);
    install(at(CoreObject::TupleType), U"__hash__", tuple_hash);
    install(at(CoreObject::TupleType), U"__repr__", tuple_repr);
    install(at(CoreObject::ListType), U"__repr__", list_repr);
    for (auto type : {CoreObject::DictType, CoreObject::MappingProxyType, CoreObject::DictKeysType,
                      CoreObject::DictValuesType, CoreObject::DictItemsType})
        install(at(type), U"__repr__", dictionary_repr);
    install(at(CoreObject::CellType), U"__repr__", cell_repr);
    install(at(CoreObject::NoneType), U"__hash__", none_hash);
    install(at(CoreObject::MappingProxyType), U"__hash__", proxy_hash);
    install(at(CoreObject::BaseExceptionType), U"__repr__", exception_repr);
    install(at(CoreObject::BaseExceptionType), U"__str__", exception_str<false>);
    install(at(CoreObject::KeyErrorType), U"__str__", exception_str<true>);
    for (auto [id, label] : {std::pair{CoreObject::None, U"None"},
                             {CoreObject::NotImplemented, U"NotImplemented"},
                             {CoreObject::Ellipsis, U"Ellipsis"}}) {
        ReturnSlot text(context);
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, label);
        auto name = roots.root(text.value());
        install_native_attribute(context, core, roots.root(context.type_of(at(id))), U"__repr__",
                                 unary, singleton_repr, {at(id).get(context), name.get(context)});
    }
    for (auto id : {CoreObject::CellType, CoreObject::DictType}) {
        ReturnSlot text(context);
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__hash__");
        name_store(context, *TypeView(context, at(id)).dictionary(), text.value(),
                   at(CoreObject::None).get(context));
    }
    const std::array<std::u32string, 6> names{U"__eq__", U"__ne__", U"__lt__",
                                              U"__le__", U"__gt__", U"__ge__"};
    const std::array string_entries{string_compare<CompareOp::Eq>, string_compare<CompareOp::Ne>,
                                    string_compare<CompareOp::Lt>, string_compare<CompareOp::Le>,
                                    string_compare<CompareOp::Gt>, string_compare<CompareOp::Ge>};
    const std::array tuple_entries{tuple_compare<CompareOp::Eq>, tuple_compare<CompareOp::Ne>,
                                   tuple_compare<CompareOp::Lt>, tuple_compare<CompareOp::Le>,
                                   tuple_compare<CompareOp::Gt>, tuple_compare<CompareOp::Ge>};
    const std::array cell_entries{cell_compare<CompareOp::Eq>, cell_compare<CompareOp::Ne>,
                                  cell_compare<CompareOp::Lt>, cell_compare<CompareOp::Le>,
                                  cell_compare<CompareOp::Gt>, cell_compare<CompareOp::Ge>};
    for (std::size_t i = 0; i < names.size(); ++i) {
        install_native_attribute(context, core, at(CoreObject::StrType), names[i], pair,
                                 string_entries[i]);
        install_native_attribute(context, core, at(CoreObject::TupleType), names[i], pair,
                                 tuple_entries[i]);
        install_native_attribute(context, core, at(CoreObject::CellType), names[i], pair,
                                 cell_entries[i]);
    }
}
} // namespace detail
} // namespace cxxpython::core
