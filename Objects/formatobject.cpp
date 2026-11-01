#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <iterator>
#include <limits>

// Independently implemented against Python 3.12.14's format mini-language.
// Semantic references: Python/formatter_unicode.c and Python/pystrtod.c in
// .cache/deps/Python-3.12.14. No CPython implementation code is incorporated.
namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool string_value(Attachment &context, Value value)
{
    return context.has_layout<StrObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType));
}
void error(Attachment &context, std::u32string message, ReturnSlot &out,
           CoreObject type = CoreObject::ValueErrorType)
{
    raise_error(context, type, std::move(message), out);
}
template <class F> ResultView guarded(Attachment &context, ReturnSlot &out, F &&function)
{
    out.check_caller(context);
    try {
        function();
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    } catch (const std::length_error &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
struct FormatSpec {
    char32_t fill = U' ';
    char32_t align;
    char32_t sign = 0;
    char32_t grouping = 0;
    char32_t type;
    std::size_t width = 0;
    std::optional<std::size_t> precision = {};
    bool alternate = false;
    bool no_negative_zero = false;
};
bool alignment(char32_t c)
{
    return c == U'<' || c == U'>' || c == U'=' || c == U'^';
}
bool read_decimal(Attachment &context, std::u32string_view text, std::size_t &position,
                  std::size_t &value, ReturnSlot &out)
{
    value = 0;
    constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    while (position < text.size()) {
        const int digit = detail::decimal_digit(text[position]);
        if (digit < 0)
            break;
        if (value > (limit - static_cast<unsigned>(digit)) / 10) {
            error(context, U"Too many decimal digits in format string", out);
            return false;
        }
        value = value * 10 + static_cast<unsigned>(digit);
        ++position;
    }
    return true;
}
bool parse_spec(Attachment &context, std::u32string_view text, FormatSpec &spec, ReturnSlot &out)
{
    std::size_t position = 0;
    bool explicit_fill = false, explicit_align = false;
    if (text.size() >= 2 && alignment(text[1])) {
        spec.fill = text[0];
        spec.align = text[1];
        explicit_fill = explicit_align = true;
        position = 2;
    } else if (!text.empty() && alignment(text[0])) {
        spec.align = text[0];
        explicit_align = true;
        position = 1;
    }
    auto take = [&](char32_t c) {
        if (position == text.size() || text[position] != c)
            return false;
        ++position;
        return true;
    };
    if (position < text.size() &&
        (text[position] == U'+' || text[position] == U'-' || text[position] == U' '))
        spec.sign = text[position++];
    spec.no_negative_zero = take(U'z');
    spec.alternate = take(U'#');
    if (!explicit_fill && take(U'0')) {
        spec.fill = U'0';
        if (!explicit_align && spec.align == U'>')
            spec.align = U'=';
    }
    if (!read_decimal(context, text, position, spec.width, out))
        return false;
    if (take(U','))
        spec.grouping = U',';
    if (take(U'_')) {
        if (spec.grouping) {
            error(context, U"Cannot specify both ',' and '_'.", out);
            return false;
        }
        spec.grouping = U'_';
    }
    if (spec.grouping == U'_' && position < text.size() && text[position] == U',') {
        error(context, U"Cannot specify both ',' and '_'.", out);
        return false;
    }
    if (take(U'.')) {
        const auto start = position;
        std::size_t precision;
        if (!read_decimal(context, text, position, precision, out))
            return false;
        if (start == position) {
            error(context, U"Format specifier missing precision", out);
            return false;
        }
        spec.precision = precision;
    }
    if (position < text.size())
        spec.type = text[position++];
    if (position != text.size()) {
        error(context, U"Invalid format specifier", out);
        return false;
    }
    if (spec.grouping) {
        const bool decimal = spec.type == 0 || std::u32string_view(U"deEfFgG%").find(spec.type) !=
                                                   std::u32string_view::npos;
        const bool base = std::u32string_view(U"boxX").find(spec.type) != std::u32string_view::npos;
        if (!decimal && !(base && spec.grouping == U'_')) {
            error(context, U"Cannot specify grouping with this format type", out);
            return false;
        }
    }
    return true;
}
std::u32string padded(std::u32string text, const FormatSpec &spec, std::size_t prefix = 0)
{
    if (text.size() >= spec.width)
        return text;
    const auto count = spec.width - text.size();
    if (spec.align == U'<')
        text.append(count, spec.fill);
    else if (spec.align == U'^') {
        text.insert(0, count / 2, spec.fill);
        text.append(count - count / 2, spec.fill);
    } else
        text.insert(spec.align == U'=' ? prefix : 0, count, spec.fill);
    return text;
}
std::u32string group_digits(Attachment &context, std::u32string_view digits, char32_t separator,
                            std::size_t interval, std::size_t minimum)
{
    auto length = digits.size();
    if (minimum > length) {
        // Invert n + floor((n - 1) / interval) >= minimum without a padding loop.
        const auto needed = minimum - minimum / (interval + 1);
        length = std::max(length, needed);
        if (length + (length - 1) / interval < minimum)
            ++length;
    }
    std::u32string result;
    result.reserve(length + (separator ? (length - 1) / interval : 0));
    for (std::size_t i = 0; i < length; ++i) {
        if ((i & 4095) == 0)
            context.poll();
        if (separator && i && (length - i) % interval == 0)
            result.push_back(separator);
        result.push_back(i < length - digits.size() ? U'0' : digits[i - (length - digits.size())]);
    }
    return result;
}
std::u32string number_field(Attachment &context, std::u32string digits, std::u32string remainder,
                            bool negative, std::u32string prefix, const FormatSpec &spec,
                            std::size_t interval = 3)
{
    if (negative)
        prefix.insert(prefix.begin(), U'-');
    else if (spec.sign == U'+' || spec.sign == U' ')
        prefix.insert(prefix.begin(), spec.sign);
    const auto prefix_length = prefix.size();
    if (!digits.empty()) {
        const auto occupied = prefix.size() + remainder.size();
        const auto minimum = spec.align == U'=' && spec.fill == U'0' && spec.width > occupied
                                 ? spec.width - occupied
                                 : 0;
        if (spec.grouping)
            digits = group_digits(context, digits, spec.grouping, interval, minimum);
        else if (digits.size() < minimum)
            digits.insert(0, minimum - digits.size(), U'0');
    }
    return padded(prefix + digits + remainder, spec, prefix_length);
}
void object_format(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[1])) {
        error(context, U"format_spec must be a string", out, CoreObject::TypeErrorType);
        return;
    }
    if (!context.payload<StrObject>(args[1]).value.empty()) {
        error(context, U"unsupported format string passed to object.__format__", out,
              CoreObject::TypeErrorType);
        return;
    }
    RootScope roots(context);
    str(context, roots.root(args[0]), out);
}
void string_format(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!string_value(context, args[0]) || !string_value(context, args[1])) {
        error(context, U"str.__format__ requires strings", out, CoreObject::TypeErrorType);
        return;
    }
    if (context.payload<StrObject>(args[1]).value.empty() &&
        context.type_of(args[0]) == context.builtin(CoreObject::StrType)) {
        out.ok(args[0]);
        return;
    }
    FormatSpec spec{.align = U'<', .type = U's'};
    if (!parse_spec(context, context.payload<StrObject>(args[1]).value, spec, out))
        return;
    if (spec.type != U's' || spec.sign || spec.alternate || spec.no_negative_zero ||
        spec.align == U'=') {
        error(context, U"Invalid format specifier for string", out);
        return;
    }
    auto text = context.payload<StrObject>(args[0]).value;
    if (spec.precision && text.size() > *spec.precision)
        text.resize(*spec.precision);
    make_str(context, padded(std::move(text), spec), out);
}
bool floating_type(char32_t type)
{
    return std::u32string_view(U"eEfFgG%").find(type) != std::u32string_view::npos;
}
bool float_digits(Attachment &context, double value, const FormatSpec &spec, std::string &text,
                  ReturnSlot &out)
{
    if (spec.precision && *spec.precision > static_cast<std::size_t>(INT_MAX)) {
        error(context, U"precision too big", out);
        return false;
    }
    if (std::isnan(value)) {
        text = "nan";
        return true;
    }
    if (std::isinf(value)) {
        text = "inf";
        return true;
    }
    const char32_t type = spec.type >= U'A' && spec.type <= U'Z' ? spec.type + 32 : spec.type;
    const bool shortest = type == 0 && !spec.precision;
    const bool fixed = type == U'f' || type == U'%';
    const auto precision = spec.precision.value_or(6);
    const auto significant = std::max<std::size_t>(1, precision);
    // Fixed binary64 output needs at most 309 integral digits, plus precision.
    std::string buffer((shortest ? 32 : precision) + 400, '\0');
    std::to_chars_result converted;
    if (shortest)
        converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                  std::chars_format::scientific);
    else
        converted =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                          fixed ? std::chars_format::fixed : std::chars_format::scientific,
                          static_cast<int>(fixed || type == U'e' ? precision : significant - 1));
    if (converted.ec != std::errc{}) {
        error(context, U"float formatting failed", out);
        return false;
    }
    buffer.resize(static_cast<std::size_t>(converted.ptr - buffer.data()));
    if (fixed) {
        text = std::move(buffer);
        if (spec.alternate && text.find('.') == std::string::npos)
            text.push_back('.');
        return true;
    }
    const auto epos = buffer.find('e');
    const char *exponent_start = buffer.data() + epos + 1;
    if (*exponent_start == '+')
        ++exponent_start;
    int exponent = 0;
    const auto parsed = std::from_chars(exponent_start, buffer.data() + buffer.size(), exponent);
    if (parsed.ec != std::errc{}) {
        error(context, U"float exponent conversion failed", out);
        return false;
    }
    auto digits = buffer.substr(0, epos);
    if (const auto dot = digits.find('.'); dot != std::string::npos)
        digits.erase(dot, 1);
    if (type != U'e' && !spec.alternate) {
        while (digits.size() > 1 && digits.back() == '0')
            digits.pop_back();
    }
    const auto cutoff = shortest ? 16 : static_cast<int>(significant) - (type == 0 ? 1 : 0);
    const bool scientific = type == U'e' || exponent < -4 || exponent >= cutoff;
    if (scientific) {
        text = digits.substr(0, 1);
        if (digits.size() > 1 || spec.alternate)
            text += "." + digits.substr(1);
        text += exponent < 0 ? "e-" : "e+";
        auto exp = std::to_string(exponent < 0 ? -exponent : exponent);
        if (exp.size() < 2)
            text.push_back('0');
        text += exp;
    } else {
        const int point = exponent + 1;
        if (point <= 0)
            text = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
        else if (static_cast<std::size_t>(point) >= digits.size())
            text = digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0');
        else
            text = digits.substr(0, point) + "." + digits.substr(point);
        if (text.find('.') == std::string::npos) {
            if (type == 0)
                text += ".0";
            else if (spec.alternate)
                text.push_back('.');
        }
    }
    return true;
}
void render_float(Attachment &context, double value, const FormatSpec &spec, ReturnSlot &out)
{
    if (spec.type != 0 && !floating_type(spec.type)) {
        error(context,
              spec.type == U'n' ? U"Locale-dependent formatting is not supported"
                                : U"Unknown format code for float",
              out);
        return;
    }
    if (spec.type == U'%')
        value *= 100.0;
    bool negative = std::signbit(value) && !std::isnan(value);
    std::string text;
    if (!float_digits(context, std::fabs(value), spec, text, out))
        return;
    const auto exponent = text.find('e');
    const auto mantissa = std::string_view(text).substr(0, exponent);
    if (spec.no_negative_zero && mantissa.find_first_of("123456789infna") == std::string_view::npos)
        negative = false;
    if (spec.type >= U'A' && spec.type <= U'Z') {
        for (auto &c : text) {
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
        }
    }
    if (spec.type == U'%')
        text.push_back('%');
    const auto digits_end = text.find_first_not_of("0123456789");
    const auto count = digits_end == std::string::npos ? text.size() : digits_end;
    const std::u32string digits(text.begin(), text.begin() + count);
    const std::u32string remainder(text.begin() + count, text.end());
    make_str(context, number_field(context, digits, remainder, negative, {}, spec), out);
}
void float_format(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_float(context, args[0]) || !string_value(context, args[1])) {
        error(context, U"float.__format__ requires a float and string", out,
              CoreObject::TypeErrorType);
        return;
    }
    const auto &text = context.payload<StrObject>(args[1]).value;
    if (text.empty()) {
        RootScope roots(context);
        str(context, roots.root(args[0]), out);
        return;
    }
    FormatSpec spec{.align = U'>', .type = 0};
    if (parse_spec(context, text, spec, out))
        render_float(context, context.payload<FloatObject>(args[0]).value, spec, out);
}
void complex_format(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_complex(context, args[0]) || !string_value(context, args[1])) {
        error(context, U"complex.__format__ requires a complex and string", out,
              CoreObject::TypeErrorType);
        return;
    }
    const auto &text = context.payload<StrObject>(args[1]).value;
    if (text.empty()) {
        RootScope roots(context);
        str(context, roots.root(args[0]), out);
        return;
    }
    FormatSpec spec{.align = U'>', .type = 0};
    if (!parse_spec(context, text, spec, out))
        return;
    if (spec.fill == U'0' || spec.align == U'=') {
        error(context, U"Zero padding or '=' alignment is not allowed for complex", out);
        return;
    }
    if (spec.type == U'%' || (spec.type && !floating_type(spec.type))) {
        error(context, U"Unknown format code for complex", out);
        return;
    }
    const auto &value = context.payload<ComplexObject>(args[0]);
    const bool real_hidden = spec.type == 0 && value.real == 0.0 && !std::signbit(value.real);
    FormatSpec component = spec;
    component.width = 0;
    component.align = U'>';
    if (component.type == 0 && component.precision)
        component.type = U'g';
    std::u32string result;
    ReturnSlot rendered(context);
    auto append = [&](double number) {
        render_float(context, number, component, rendered);
        if (rendered.tag() == ResultTag::Raised) {
            out.raised(rendered.exception());
            return false;
        }
        auto part = context.payload<StrObject>(rendered.value()).value;
        if (component.type == 0 && !component.alternate && part.ends_with(U".0"))
            part.resize(part.size() - 2);
        result += part;
        return true;
    };
    if (!real_hidden) {
        if (!append(value.real))
            return;
        component.sign = U'+';
    }
    if (!append(value.imag))
        return;
    result += U'j';
    if (spec.type == 0 && !real_hidden)
        result = U"(" + result + U")";
    make_str(context, padded(std::move(result), spec), out);
}
void integer_format(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_int(context, args[0]) || !string_value(context, args[1])) {
        error(context, U"int.__format__ requires an int and string", out,
              CoreObject::TypeErrorType);
        return;
    }
    const auto &text = context.payload<StrObject>(args[1]).value;
    RootScope roots(context);
    if (text.empty()) {
        str(context, roots.root(args[0]), out);
        return;
    }
    FormatSpec spec{.align = U'>', .type = U'd'};
    if (!parse_spec(context, text, spec, out))
        return;
    if (floating_type(spec.type)) {
        ReturnSlot converted(context);
        const std::array values{args[0]};
        call(context, roots.root(context.builtin(CoreObject::FloatType)), {values, {}}, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        render_float(context, context.payload<FloatObject>(converted.value()).value, spec, out);
        return;
    }
    if (spec.type == U'n') {
        error(context, U"Locale-dependent formatting is not supported", out);
        return;
    }
    if (spec.precision || spec.no_negative_zero) {
        error(context,
              spec.precision
                  ? U"Precision not allowed in integer format specifier"
                  : U"Negative zero coercion (z) not allowed in integer format specifier",
              out);
        return;
    }
    detail::Integer number = detail::integer_value(context.payload<IntObject>(args[0]));
    if (spec.type == U'c') {
        if (spec.sign || spec.alternate) {
            error(context, U"Sign or alternate form not allowed with integer format specifier 'c'",
                  out);
            return;
        }
        if (number < 0 || number > 0x10ffff) {
            error(context, U"%c arg not in range(0x110000)", out, CoreObject::OverflowErrorType);
            return;
        }
        make_str(context,
                 number_field(
                     context, {},
                     std::u32string(1, static_cast<char32_t>(number.convert_to<std::uint32_t>())),
                     false, {}, spec),
                 out);
        return;
    }
    if (std::u32string_view(U"dboxX").find(spec.type) == std::u32string_view::npos) {
        error(context, U"Unknown format code for int", out);
        return;
    }
    const bool negative = number < 0;
    if (negative)
        number = -number;
    std::u32string digits, prefix;
    if (spec.type == U'd') {
        const auto decimal = int_decimal(context, args[0]);
        digits.assign(decimal.begin() + (negative ? 1 : 0), decimal.end());
    } else {
        const unsigned chunk = spec.type == U'b' ? 1 : (spec.type == U'o' ? 3 : 4);
        std::vector<unsigned char> groups;
        boost::multiprecision::export_bits(number, std::back_inserter(groups), chunk, true);
        const auto alphabet = spec.type == U'X' ? U"0123456789ABCDEF" : U"0123456789abcdef";
        digits.reserve(groups.size());
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if ((i & 4095) == 0)
                context.poll();
            digits.push_back(alphabet[groups[i]]);
        }
        if (digits.empty())
            digits = U"0";
        if (spec.alternate) {
            prefix = U"0";
            prefix.push_back(spec.type);
        }
    }
    make_str(context,
             number_field(context, std::move(digits), {}, negative, std::move(prefix), spec,
                          spec.type == U'd' ? 3 : 4),
             out);
}
template <NativeEntry Entry>
void format_method(Attachment &context, Value function, std::span<const Value> args,
                   ReturnSlot &out)
{
    guarded(context, out, [&] { Entry(context, function, args, out); });
}
} // namespace

ResultView format(Attachment &context, Handle value, Handle spec, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        const auto format_spec = roots.root(spec.get(context));
        out.reset(context);
        if (!string_value(context, format_spec.get(context))) {
            error(context, U"format() argument 2 must be str", out, CoreObject::TypeErrorType);
            return;
        }
        const std::array args{format_spec.get(context)};
        detail::special(context, self.get(context), U"__format__", {args, {}}, out);
        if (!failed(out) && !string_value(context, out.value()))
            error(context, U"__format__ must return a str", out, CoreObject::TypeErrorType);
    });
}
ResultView ascii(Attachment &context, Handle value, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        out.reset(context);
        ReturnSlot representation(context);
        repr(context, self, representation);
        if (failed(representation)) {
            out.raised(representation.exception());
            return;
        }
        const auto source = roots.root(representation.value());
        const auto &text = context.payload<StrObject>(source).value;
        std::u32string result;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if ((i & 4095) == 0)
                context.poll();
            const auto c = text[i];
            if (c < 128) {
                result.push_back(c);
                continue;
            }
            result.push_back(U'\\');
            result.push_back(c <= 0xff ? U'x' : (c <= 0xffff ? U'u' : U'U'));
            const int width = c <= 0xff ? 2 : (c <= 0xffff ? 4 : 8);
            for (int digit = width - 1; digit >= 0; --digit)
                result.push_back(U"0123456789abcdef"[(c >> (digit * 4)) & 15]);
        }
        make_str(context, std::move(result), out);
    });
}
namespace detail {
void install_format(Attachment &context, std::span<const Handle> core)
{
    RootScope roots(context);
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly},
                            {U"format_spec", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, roots.root(context.object_type()), U"__format__",
                             signature, format_method<object_format>);
    for (const auto id : {CoreObject::StrType, CoreObject::IntType, CoreObject::BoolType,
                          CoreObject::FloatType, CoreObject::ComplexType}) {
        const auto entry = id == CoreObject::StrType       ? format_method<string_format>
                           : id == CoreObject::FloatType   ? format_method<float_format>
                           : id == CoreObject::ComplexType ? format_method<complex_format>
                                                           : format_method<integer_format>;
        install_native_attribute(context, core, core[static_cast<std::size_t>(id)], U"__format__",
                                 signature, entry);
    }
}
} // namespace detail
} // namespace cxxpython::core
