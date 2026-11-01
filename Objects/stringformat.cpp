// Adapted from CPython 3.12.14 Objects/unicodeobject.c and
// Objects/stringlib/unicode_format.h; see doc/licenses/python-3.12.14.txt (PSF).
#include "internal/stringformat.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <charconv>
#include <climits>
#include <limits>
#include <optional>
#include <stdexcept>

namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void error(Attachment &ctx, CoreObject type, std::u32string message, ReturnSlot &out)
{
    raise_error(ctx, type, std::move(message), out);
}

bool convert(Attachment &ctx, Handle value, char32_t conversion, ReturnSlot &out)
{
    if (conversion == U's')
        str(ctx, value, out);
    else if (conversion == U'r')
        repr(ctx, value, out);
    else
        ascii(ctx, value, out);
    return !failed(out);
}

bool decimal(std::u32string_view text, std::size_t &value)
{
    if (text.empty())
        return false;
    value = 0;
    for (char32_t c : text) {
        const int digit = detail::decimal_digit(c);
        if (digit < 0 || value > (std::numeric_limits<std::size_t>::max() - 9) / 10)
            return false;
        value = value * 10 + static_cast<std::size_t>(digit);
    }
    return true;
}

struct Formatter {
    Attachment &ctx;
    RootScope &roots;
    Handle arguments;
    Handle keywords;
    ReturnSlot &out;
    std::size_t automatic = 0;
    bool used_auto = false;
    bool used_manual = false;

    bool render(std::u32string_view text, std::u32string &result, unsigned depth)
    {
        if (depth >= 2) {
            error(ctx, CoreObject::ValueErrorType, U"Max string recursion exceeded", out);
            return false;
        }
        for (std::size_t i = 0; i < text.size();) {
            char32_t c = text[i++];
            if (c != U'{' && c != U'}') {
                result.push_back(c);
                continue;
            }
            if (i < text.size() && text[i] == c) {
                result.push_back(c);
                ++i;
                continue;
            }
            if (c == U'}') {
                error(ctx, CoreObject::ValueErrorType, U"Single '}' encountered in format string",
                      out);
                return false;
            }
            if (i == text.size()) {
                error(ctx, CoreObject::ValueErrorType, U"Single '{' encountered in format string",
                      out);
                return false;
            }
            const auto start = i;
            unsigned nesting = 0;
            bool in_index = false;
            bool in_spec = false;
            std::size_t end = std::u32string_view::npos;
            while (i < text.size()) {
                if (!in_spec && text[i] == U'[')
                    in_index = true;
                else if (!in_spec && text[i] == U']')
                    in_index = false;
                else if (!in_spec && !in_index && (text[i] == U'!' || text[i] == U':')) {
                    if (end == text.npos)
                        end = i - start;
                    if (text[i] == U':')
                        in_spec = true;
                } else if (!in_spec && !in_index && text[i] == U'{') {
                    error(ctx, CoreObject::ValueErrorType, U"unexpected '{' in field name", out);
                    return false;
                } else if (in_spec && text[i] == U'{')
                    ++nesting;
                else if (!in_index && text[i] == U'}') {
                    if (!nesting)
                        break;
                    --nesting;
                }
                ++i;
            }
            if (i == text.size()) {
                error(ctx, CoreObject::ValueErrorType,
                      in_spec ? U"unmatched '{' in format spec"
                              : U"expected '}' before end of string",
                      out);
                return false;
            }
            auto field = text.substr(start, i++ - start);
            auto name = field.substr(0, end);
            auto rest = end == field.npos ? std::u32string_view{} : field.substr(end);
            char32_t conversion = 0;
            if (!rest.empty() && rest.front() == U'!') {
                if (rest.size() < 2) {
                    error(ctx, CoreObject::ValueErrorType,
                          U"end of string while looking for conversion specifier", out);
                    return false;
                }
                conversion = rest[1];
                rest.remove_prefix(2);
            }
            if (!rest.empty() && rest.front() != U':') {
                error(ctx, CoreObject::ValueErrorType, U"expected ':' after conversion specifier",
                      out);
                return false;
            }
            auto split = name.find_first_of(U".[");
            auto first = name.substr(0, split);
            std::size_t index = 0;
            bool numeric = decimal(first, index);
            ReturnSlot item(ctx);
            if (first.empty() || numeric) {
                if (first.empty()) {
                    if (used_manual) {
                        error(ctx, CoreObject::ValueErrorType,
                              U"cannot switch from manual field specification to automatic field "
                              U"numbering",
                              out);
                        return false;
                    }
                    used_auto = true;
                    index = automatic++;
                } else {
                    if (used_auto) {
                        error(ctx, CoreObject::ValueErrorType,
                              U"cannot switch from automatic field numbering to manual field "
                              U"specification",
                              out);
                        return false;
                    }
                    used_manual = true;
                }
                const auto &items = ctx.payload<TupleObject>(arguments).items;
                if (index >= items.size()) {
                    auto n = std::to_string(index);
                    error(ctx, CoreObject::IndexErrorType,
                          U"Replacement index " + std::u32string(n.begin(), n.end()) +
                              U" out of range for positional args tuple",
                          out);
                    return false;
                }
                item.ok(items[index]);
            } else {
                make_str(ctx, std::u32string(first), item);
                if (failed(item)) {
                    out.raised(item.exception());
                    return false;
                }
                auto key = roots.root(item.value());
                get_item(ctx, keywords, key, item);
                if (failed(item)) {
                    out.raised(item.exception());
                    return false;
                }
            }
            auto value = roots.root(item.value());
            std::size_t pos = split == name.npos ? name.size() : split;
            while (pos < name.size()) {
                if (name[pos] != U'.' && name[pos] != U'[') {
                    error(ctx, CoreObject::ValueErrorType,
                          U"Only '.' or '[' may follow ']' in format field specifier", out);
                    return false;
                }
                const bool attribute = name[pos++] == U'.';
                auto stop = attribute ? name.find_first_of(U".[", pos) : name.find(U']', pos);
                if (stop == name.npos)
                    stop = name.size();
                if (stop == pos || (!attribute && stop == name.size())) {
                    error(ctx, CoreObject::ValueErrorType,
                          U"Empty attribute or invalid index in format string", out);
                    return false;
                }
                auto part = name.substr(pos, stop - pos);
                std::size_t number;
                if (!attribute && decimal(part, number) &&
                    number <= static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                    make_int(ctx, static_cast<std::int64_t>(number), item);
                else
                    make_str(ctx, std::u32string(part), item);
                if (failed(item)) {
                    out.raised(item.exception());
                    return false;
                }
                auto key = roots.root(item.value());
                if (attribute)
                    get_attribute(ctx, value, key, item);
                else
                    get_item(ctx, value, key, item);
                if (failed(item)) {
                    out.raised(item.exception());
                    return false;
                }
                value = roots.root(item.value());
                pos = stop + (attribute ? 0 : 1);
            }
            if (conversion) {
                if (std::u32string_view(U"sra").find(conversion) == std::u32string_view::npos) {
                    std::u32string message = U"Unknown conversion specifier ";
                    if (conversion > 32 && conversion < 127)
                        message.push_back(conversion);
                    else {
                        char buffer[16];
                        auto converted = std::to_chars(buffer, buffer + sizeof(buffer),
                                                       static_cast<unsigned>(conversion), 16);
                        message += U"\\x";
                        message.append(buffer, converted.ptr);
                    }
                    error(ctx, CoreObject::ValueErrorType, std::move(message), out);
                    return false;
                }
                if (!convert(ctx, value, conversion, item)) {
                    out.raised(item.exception());
                    return false;
                }
                value = roots.root(item.value());
            }
            std::u32string spec;
            if (!rest.empty()) {
                auto original_spec = rest.substr(1);
                if (original_spec.find(U'{') != original_spec.npos) {
                    if (!render(original_spec, spec, depth + 1))
                        return false;
                } else
                    spec.assign(original_spec);
            }
            make_str(ctx, std::move(spec), item);
            if (failed(item)) {
                out.raised(item.exception());
                return false;
            }
            auto spec_value = roots.root(item.value());
            format(ctx, value, spec_value, item);
            if (failed(item)) {
                out.raised(item.exception());
                return false;
            }
            result += ctx.payload<StrObject>(item.value()).value;
        }
        return true;
    }
};

void format_method(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    try {
        RootScope roots(ctx);
        auto self = roots.root(args[0]);
        if (!ctx.has_layout<StrObject>(self.get(ctx)) ||
            !is_subtype(ctx, ctx.type_of(self), ctx.builtin(CoreObject::StrType))) {
            error(ctx, CoreObject::TypeErrorType, U"str.format requires a str receiver", out);
            return;
        }
        auto arguments = roots.root(args[1]);
        auto keywords = roots.root(args[2]);
        const auto source = ctx.payload<StrObject>(self).value;
        Formatter formatter{ctx, roots, arguments, keywords, out};
        std::u32string result;
        if (formatter.render(source, result, 0))
            make_str(ctx, std::move(result), out);
    } catch (const std::bad_alloc &) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
    } catch (const std::length_error &) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
    }
}

std::u32string type_name(Attachment &ctx, Handle value)
{
    TypeView type(ctx, ctx.type_of(value));
    return ctx.payload<StrObject>(std::get<Value>(type.name())).value.substr(0, 200);
}

bool type_error_result(Attachment &ctx, ReturnSlot &out)
{
    return failed(out) &&
           is_subtype(ctx, ctx.type_of(out.exception()), ctx.builtin(CoreObject::TypeErrorType));
}

std::u32string decimal_text(std::size_t number)
{
    auto text = std::to_string(number);
    return std::u32string(text.begin(), text.end());
}

struct PercentFormatter {
    Attachment &ctx;
    RootScope &roots;
    Handle operand;
    Handle current;
    ReturnSlot &out;
    bool tuple;
    bool mapping;
    bool single = false;
    std::size_t next = 0;

    bool argument(Handle &value)
    {
        if (single || !tuple) {
            if (next++) {
                error(ctx, CoreObject::TypeErrorType, U"not enough arguments for format string",
                      out);
                return false;
            }
            value = current;
            return true;
        }
        const auto &items = ctx.payload<TupleObject>(current).items;
        if (next >= items.size()) {
            error(ctx, CoreObject::TypeErrorType, U"not enough arguments for format string", out);
            return false;
        }
        value = roots.root(items[next++]);
        return true;
    }

    bool star(std::int64_t &number, bool precision)
    {
        Handle value;
        if (!argument(value))
            return false;
        if (!is_int(ctx, value.get(ctx))) {
            error(ctx, CoreObject::TypeErrorType, U"* wants int", out);
            return false;
        }
        if (!int_fits_signed(ctx, value.get(ctx), number) ||
            (precision && (number < INT_MIN || number > INT_MAX))) {
            error(ctx, CoreObject::OverflowErrorType,
                  precision ? U"Python int too large to convert to C int"
                            : U"Python int too large to convert to C ssize_t",
                  out);
            return false;
        }
        return true;
    }

    bool number(Handle value, char32_t code, bool alternate, std::optional<std::size_t> precision,
                std::u32string &rendered)
    {
        const bool integer = std::u32string_view(U"diuoxX").find(code) != std::u32string_view::npos;
        const bool base = code == U'o' || code == U'x' || code == U'X';
        ReturnSlot converted(ctx);
        if (integer) {
            if (is_int(ctx, value.get(ctx))) {
                copy_int(ctx, value.get(ctx), converted);
            } else if (base) {
                index(ctx, value, converted);
            } else {
                auto type = roots.root(ctx.type_of(value));
                auto integer_slot = resolve_protocol(ctx, type, Protocol::Int, roots);
                auto index_slot = resolve_protocol(ctx, type, Protocol::Index, roots);
                if (integer_slot.state == SlotState::Missing &&
                    index_slot.state == SlotState::Missing) {
                    error(ctx, CoreObject::TypeErrorType,
                          U"%" + std::u32string(1, code) +
                              U" format: a real number is required, not " + type_name(ctx, value),
                          out);
                    return false;
                }
                const std::array args{value.get(ctx)};
                call(ctx, roots.root(ctx.builtin(CoreObject::IntType)), {args, {}}, converted);
            }
            if (failed(converted)) {
                if (type_error_result(ctx, converted))
                    error(ctx, CoreObject::TypeErrorType,
                          U"%" + std::u32string(1, code) +
                              (base ? U" format: an integer is required, not "
                                    : U" format: a real number is required, not ") +
                              type_name(ctx, value),
                          out);
                else
                    out.raised(converted.exception());
                return false;
            }
            // Numeric formatting bypasses overrides on int subclasses.
            if (ctx.type_of(converted.value()) != ctx.builtin(CoreObject::IntType)) {
                auto original = roots.root(converted.value());
                copy_int(ctx, original.get(ctx), converted);
                if (failed(converted)) {
                    out.raised(converted.exception());
                    return false;
                }
            }
        } else {
            auto type = roots.root(ctx.type_of(value));
            auto index_slot = resolve_protocol(ctx, type, Protocol::Index, roots);
            auto float_slot = detail::type_attribute(ctx, type.get(ctx), U"__float__");
            if (!is_float(ctx, value.get(ctx)) && !is_int(ctx, value.get(ctx)) && !float_slot &&
                index_slot.state == SlotState::Missing) {
                error(ctx, CoreObject::TypeErrorType,
                      U"must be real number, not " + type_name(ctx, value), out);
                return false;
            }
            if (is_float(ctx, value.get(ctx)))
                make_float(ctx, ctx.payload<FloatObject>(value).value, converted);
            else {
                const std::array args{value.get(ctx)};
                call(ctx, roots.root(ctx.builtin(CoreObject::FloatType)), {args, {}}, converted);
            }
            if (failed(converted)) {
                out.raised(converted.exception());
                return false;
            }
        }
        auto numeric = roots.root(converted.value());
        std::u32string spec;
        if (alternate)
            spec.push_back(U'#');
        if (!integer) {
            spec.push_back(U'.');
            spec += decimal_text(precision.value_or(6));
        }
        spec.push_back(code == U'i' || code == U'u' ? U'd' : code);
        make_str(ctx, std::move(spec), converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return false;
        }
        auto format_spec = roots.root(converted.value());
        format(ctx, numeric, format_spec, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return false;
        }
        rendered = ctx.payload<StrObject>(converted.value()).value;
        if (integer && precision) {
            if (*precision > static_cast<std::size_t>(INT_MAX - 3)) {
                error(ctx, CoreObject::OverflowErrorType, U"precision too large", out);
                return false;
            }
            auto prefix = !rendered.empty() && rendered[0] == U'-' ? 1U : 0U;
            if (base && alternate)
                prefix += 2;
            const auto digits = rendered.size() - prefix;
            if (*precision > digits)
                rendered.insert(prefix, *precision - digits, U'0');
        }
        return true;
    }

    bool character(Handle value, std::u32string &rendered)
    {
        if (ctx.has_layout<StrObject>(value.get(ctx)) &&
            is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::StrType))) {
            const auto &text = ctx.payload<StrObject>(value).value;
            if (text.size() == 1) {
                rendered = text;
                return true;
            }
            error(ctx, CoreObject::TypeErrorType, U"%c requires int or char", out);
            return false;
        }
        ReturnSlot converted(ctx);
        index(ctx, value, converted);
        if (failed(converted)) {
            if (type_error_result(ctx, converted))
                error(ctx, CoreObject::TypeErrorType, U"%c requires int or char", out);
            else
                out.raised(converted.exception());
            return false;
        }
        std::int64_t ordinal;
        if (!int_fits_signed(ctx, converted.value(), ordinal) || ordinal < 0 ||
            ordinal > 0x10ffff) {
            error(ctx, CoreObject::OverflowErrorType, U"%c arg not in range(0x110000)", out);
            return false;
        }
        rendered.assign(1, static_cast<char32_t>(ordinal));
        return true;
    }

    bool render(std::u32string_view source, std::u32string &result)
    {
        ReturnSlot temp(ctx);
        for (std::size_t i = 0; i < source.size();) {
            if (source[i] != U'%') {
                result.push_back(source[i++]);
                continue;
            }
            ++i;
            if (i == source.size()) {
                error(ctx, CoreObject::ValueErrorType, U"incomplete format", out);
                return false;
            }
            if (source[i] == U'%') {
                result.push_back(U'%');
                ++i;
                continue;
            }
            if (source[i] == U'(') {
                if (!mapping) {
                    error(ctx, CoreObject::TypeErrorType, U"format requires a mapping", out);
                    return false;
                }
                const auto start = ++i;
                unsigned level = 1;
                while (i < source.size() && level) {
                    if (source[i] == U'(')
                        ++level;
                    if (source[i] == U')')
                        --level;
                    if (level)
                        ++i;
                }
                if (i == source.size()) {
                    error(ctx, CoreObject::ValueErrorType, U"incomplete format key", out);
                    return false;
                }
                make_str(ctx, std::u32string(source.substr(start, i - start)), temp);
                if (failed(temp)) {
                    out.raised(temp.exception());
                    return false;
                }
                auto key = roots.root(temp.value());
                get_item(ctx, operand, key, temp);
                if (failed(temp)) {
                    out.raised(temp.exception());
                    return false;
                }
                current = roots.root(temp.value());
                single = true;
                next = 0;
                ++i;
            }
            bool left = false, plus = false, space = false, alternate = false, zero = false;
            while (i < source.size()) {
                const auto flag = source[i];
                if (flag == U'-')
                    left = true;
                else if (flag == U'+')
                    plus = true;
                else if (flag == U' ')
                    space = true;
                else if (flag == U'#')
                    alternate = true;
                else if (flag == U'0')
                    zero = true;
                else
                    break;
                ++i;
            }
            std::size_t width = 0;
            if (i < source.size() && source[i] == U'*') {
                std::int64_t number;
                if (!star(number, false))
                    return false;
                if (number < 0) {
                    left = true;
                    if (number == std::numeric_limits<std::int64_t>::min()) {
                        error(ctx, CoreObject::OverflowErrorType,
                              U"Python int too large to convert to C ssize_t", out);
                        return false;
                    }
                    number = -number;
                }
                width = static_cast<std::size_t>(number);
                ++i;
            } else {
                const auto start = i;
                while (i < source.size() && source[i] >= U'0' && source[i] <= U'9')
                    ++i;
                if (i != start && (!decimal(source.substr(start, i - start), width) ||
                                   width > static_cast<std::size_t>(
                                               std::numeric_limits<std::ptrdiff_t>::max()))) {
                    error(ctx, CoreObject::ValueErrorType, U"width too big", out);
                    return false;
                }
            }
            std::optional<std::size_t> precision;
            if (i < source.size() && source[i] == U'.') {
                ++i;
                if (i < source.size() && source[i] == U'*') {
                    std::int64_t number;
                    if (!star(number, true))
                        return false;
                    precision = number < 0 ? 0 : static_cast<std::size_t>(number);
                    ++i;
                } else {
                    const auto start = i;
                    while (i < source.size() && source[i] >= U'0' && source[i] <= U'9')
                        ++i;
                    std::size_t number = 0;
                    if (i != start &&
                        (!decimal(source.substr(start, i - start), number) || number > INT_MAX)) {
                        error(ctx, CoreObject::ValueErrorType, U"precision too big", out);
                        return false;
                    }
                    precision = number;
                }
            }
            if (i < source.size() && std::u32string_view(U"hlL").find(source[i]) != source.npos)
                ++i;
            if (i == source.size()) {
                error(ctx, CoreObject::ValueErrorType, U"incomplete format", out);
                return false;
            }
            const auto code = source[i++];
            Handle value;
            if (!argument(value))
                return false;
            std::u32string rendered;
            const bool numeric = std::u32string_view(U"diuoxXeEfFgG").find(code) != source.npos;
            if (std::u32string_view(U"sra").find(code) != source.npos) {
                if (!convert(ctx, value, code, temp)) {
                    out.raised(temp.exception());
                    return false;
                }
                rendered = ctx.payload<StrObject>(temp.value()).value;
                if (precision && rendered.size() > *precision)
                    rendered.resize(*precision);
            } else if (numeric) {
                if (!number(value, code, alternate, precision, rendered))
                    return false;
            } else if (code == U'c') {
                if (!character(value, rendered))
                    return false;
            } else {
                char hex[16];
                auto converted =
                    std::to_chars(hex, hex + sizeof(hex), static_cast<unsigned>(code), 16);
                std::u32string message = U"unsupported format character '";
                message.push_back(code >= 31 && code <= 126 ? code : U'?');
                message += U"' (0x";
                message.append(hex, converted.ptr);
                message += U") at index " + decimal_text(i - 1);
                error(ctx, CoreObject::ValueErrorType, std::move(message), out);
                return false;
            }
            if (numeric && !rendered.empty() && rendered[0] != U'-' && rendered[0] != U'+') {
                if (plus)
                    rendered.insert(rendered.begin(), U'+');
                else if (space)
                    rendered.insert(rendered.begin(), U' ');
            }
            if (width > rendered.size()) {
                const auto count = width - rendered.size();
                if (left)
                    rendered.append(count, U' ');
                else if (numeric && zero) {
                    std::size_t prefix =
                        !rendered.empty() &&
                                (rendered[0] == U'-' || rendered[0] == U'+' || rendered[0] == U' ')
                            ? 1
                            : 0;
                    if (alternate && (code == U'o' || code == U'x' || code == U'X'))
                        prefix += 2;
                    rendered.insert(prefix, count, U'0');
                } else
                    rendered.insert(0, count, U' ');
            }
            result += rendered;
        }
        if ((!mapping && !single &&
             ((tuple && next < ctx.payload<TupleObject>(current).items.size()) ||
              (!tuple && next == 0))) ||
            (single && next == 0)) {
            error(ctx, CoreObject::TypeErrorType,
                  U"not all arguments converted during string formatting", out);
            return false;
        }
        return true;
    }
};

void mod_method(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    try {
        RootScope roots(ctx);
        auto self = roots.root(args[0]);
        if (!ctx.has_layout<StrObject>(self.get(ctx)) ||
            !is_subtype(ctx, ctx.type_of(self), ctx.builtin(CoreObject::StrType))) {
            error(ctx, CoreObject::TypeErrorType, U"str.__mod__ requires a str receiver", out);
            return;
        }
        auto operand = roots.root(args[1]);
        const auto source = ctx.payload<StrObject>(self).value;
        const bool tuple =
            ctx.has_layout<TupleObject>(operand.get(ctx)) &&
            is_subtype(ctx, ctx.type_of(operand), ctx.builtin(CoreObject::TupleType));
        const bool string = ctx.has_layout<StrObject>(operand.get(ctx)) &&
                            is_subtype(ctx, ctx.type_of(operand), ctx.builtin(CoreObject::StrType));
        auto slot =
            resolve_protocol(ctx, roots.root(ctx.type_of(operand)), Protocol::GetItem, roots);
        const bool mapping = !tuple && !string && slot.state != SlotState::Missing &&
                             slot.state != SlotState::Disabled;
        PercentFormatter formatter{ctx, roots, operand, operand, out, tuple, mapping};
        std::u32string result;
        if (formatter.render(source, result))
            make_str(ctx, std::move(result), out);
    } catch (const std::bad_alloc &) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
    } catch (const std::length_error &) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
    }
}
} // namespace

namespace detail {
void install_string_format(Attachment &ctx, std::span<const Handle> core)
{
    SignatureLayout mod;
    mod.parameters = {{U"self", ParameterKind::PositionalOnly},
                      {U"value", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, core[static_cast<std::size_t>(CoreObject::StrType)],
                             U"__mod__", mod, mod_method);
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    signature.varargs = U"args";
    signature.varkwargs = U"kwargs";
    install_native_attribute(ctx, core, core[static_cast<std::size_t>(CoreObject::StrType)],
                             U"format", signature, format_method);
}
} // namespace detail
} // namespace cxxpython::core
