// Unicode database and normalization adapted from CPython 3.12.14
// Modules/unicodedata.c. CPython is PSF licensed; see
// doc/licenses/python-3.12.14.txt.
#include "internal/unicodedata.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/parser.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace cxxpython::core::detail {
namespace {
using Py_UCS4 = char32_t;
constexpr std::size_t NAME_MAXLEN = 256;
struct _PyUnicode_DatabaseRecord {
    unsigned char category, combining, bidirectional, mirrored, east_asian_width,
        normalization_quick_check;
};
struct change_record {
    unsigned char bidir_changed, category_changed, decimal_changed, mirrored_changed,
        east_asian_width_changed;
    double numeric_changed;
};
#include "internal/unicodedata_db.hpp"
#undef SHIFT
namespace types {
struct _PyUnicode_TypeRecord {
    int upper, lower, title;
    unsigned char decimal, digit;
    unsigned short flags;
};
#include "internal/unicodedata_type_db.hpp"
const _PyUnicode_TypeRecord &record(char32_t ch)
{
    auto i = index1[ch >> SHIFT];
    return _PyUnicode_TypeRecords[index2[(i << SHIFT) + (ch & ((1 << SHIFT) - 1))]];
}
} // namespace types
#undef SHIFT

const _PyUnicode_DatabaseRecord &record(char32_t ch)
{
    auto i = index1[ch >> 7];
    return _PyUnicode_Database_Records[index2[(i << 7) + (ch & 127)]];
}

bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

std::u32string type_name(Attachment &c, Handle value)
{
    auto name = TypeView(c, c.type_of(value.get(c))).name();
    return std::holds_alternative<std::u32string>(name)
               ? std::get<std::u32string>(name)
               : c.payload<StrObject>(std::get<Value>(name)).value;
}

bool character(Attachment &c, Handle value, std::u32string_view function, bool numbered,
               char32_t &ch, ReturnSlot &out)
{
    if (!c.has_layout<StrObject>(value.get(c)) ||
        !is_subtype(c, c.type_of(value.get(c)), c.builtin(CoreObject::StrType)) ||
        c.payload<StrObject>(value).value.size() != 1) {
        raise_error(c, CoreObject::TypeErrorType,
                    std::u32string(function) +
                        (numbered ? U"() argument 1 must be a unicode character, not "
                                  : U"() argument must be a unicode character, not ") +
                        type_name(c, value).substr(0, 100),
                    out);
        return false;
    }
    ch = c.payload<StrObject>(value).value[0];
    return true;
}

void ascii(Attachment &c, std::string_view value, ReturnSlot &out)
{
    make_str(c, std::u32string(value.begin(), value.end()), out);
}

void surrogate_error(Attachment &c, Handle input, std::size_t start, std::size_t end,
                     ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot value(c);
    make_str(c, U"utf-8", value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto encoding = roots.root(value.value());
    make_int(c, static_cast<std::int64_t>(start), value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto first = roots.root(value.value());
    make_int(c, static_cast<std::int64_t>(end), value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto last = roots.root(value.value());
    make_str(c, U"surrogates not allowed", value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto reason = roots.root(value.value());
    std::array fields{encoding.get(c), input.get(c), first.get(c), last.get(c), reason.get(c)};
    cxxpython::core::make_tuple(c, std::span<const Value>(fields), value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto arguments = roots.root(value.value());
    auto type = roots.root(c.builtin(CoreObject::UnicodeEncodeErrorType));
    c.allocate_instance<ExceptionObject>(type, value, arguments.get(c));
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto error = roots.root(value.value());
    for (std::size_t i = 0; i < fields.size(); ++i)
        c.store(error.get(c), c.instance_slot(error.get(c), i), fields[i]);
    link_exception_context(c, error.get(c));
    out.raised(error.get(c));
}

enum class Property {
    Decimal,
    Digit,
    Numeric,
    Category,
    Bidi,
    Combining,
    Mirrored,
    Width,
    Decomp,
    Name
};
struct NamedSequence {
    std::string_view name;
    std::u32string_view value;
};
#include "internal/unicodedata_sequences.hpp"

template <Property P, bool Old = false>
void property(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    constexpr std::size_t offset = Old ? 1 : 0;
    auto input = roots.root(args[offset]);
    char32_t ch;
    constexpr auto name = [] {
        if constexpr (P == Property::Decimal)
            return U"decimal";
        if constexpr (P == Property::Digit)
            return U"digit";
        if constexpr (P == Property::Numeric)
            return U"numeric";
        if constexpr (P == Property::Category)
            return U"category";
        if constexpr (P == Property::Bidi)
            return U"bidirectional";
        if constexpr (P == Property::Combining)
            return U"combining";
        if constexpr (P == Property::Mirrored)
            return U"mirrored";
        if constexpr (P == Property::Width)
            return U"east_asian_width";
        if constexpr (P == Property::Decomp)
            return U"decomposition";
        return U"name";
    }();
    if constexpr (P == Property::Decimal || P == Property::Digit || P == Property::Numeric ||
                  P == Property::Name) {
        auto defaults = roots.root(args[offset + 1]);
        auto count = c.payload<TupleObject>(defaults).items.size();
        if (count > 1) {
            auto number = std::to_string(count + 1);
            raise_error(c, CoreObject::TypeErrorType,
                        std::u32string(name) + U" expected at most 2 arguments, got " +
                            std::u32string(number.begin(), number.end()),
                        out);
            return;
        }
    }
    if (!character(c, input, name,
                   P == Property::Decimal || P == Property::Digit || P == Property::Numeric ||
                       P == Property::Name,
                   ch, out))
        return;
    const auto &r = record(ch);
    const auto *old = Old ? get_change_3_2_0(ch) : nullptr;
    if constexpr (P == Property::Decimal || P == Property::Digit || P == Property::Numeric ||
                  P == Property::Name) {
        auto defaults = roots.root(args[offset + 1]);
        const auto &items = c.payload<TupleObject>(defaults).items;
        const auto &t = types::record(ch);
        bool present;
        if constexpr (P == Property::Name)
            present = !(ch >= 0xf0000 && ch < 0xf01d9) && !(ch >= 0xf0200 && ch < 0xf03cd) &&
                      syntax::unicode_character_name(ch).has_value();
        else if constexpr (P == Property::Decimal)
            present = (t.flags & 2) != 0;
        else if constexpr (P == Property::Digit)
            present = (t.flags & 4) != 0;
        else
            present = (t.flags & 0x800) != 0;
        if constexpr (Old) {
            if constexpr (P == Property::Name || P == Property::Decimal || P == Property::Numeric)
                if (old->category_changed == 0)
                    present = false;
            if constexpr (P == Property::Decimal || P == Property::Numeric)
                if (old->category_changed != 0 && old->decimal_changed != 0xff)
                    present = true;
        }
        if (!present) {
            if (!items.empty())
                out.ok(items[0]);
            else if constexpr (P == Property::Name)
                raise_error(c, CoreObject::ValueErrorType, U"no such name", out);
            else if constexpr (P == Property::Decimal)
                raise_error(c, CoreObject::ValueErrorType, U"not a decimal", out);
            else if constexpr (P == Property::Digit)
                raise_error(c, CoreObject::ValueErrorType, U"not a digit", out);
            else
                raise_error(c, CoreObject::ValueErrorType, U"not a numeric character", out);
            return;
        }
        if constexpr (P == Property::Name)
            ascii(c, *syntax::unicode_character_name(ch), out);
        else if constexpr (P == Property::Decimal)
            make_int(c, Old && old->decimal_changed != 0xff ? old->decimal_changed : t.decimal,
                     out);
        else if constexpr (P == Property::Digit)
            make_int(c, t.digit, out);
        else
            make_float(c,
                       Old && old->decimal_changed != 0xff ? old->decimal_changed
                                                           : types::_PyUnicode_ToNumeric(ch),
                       out);
    } else if constexpr (P == Property::Category)
        ascii(c,
              _PyUnicode_CategoryNames[Old && old->category_changed != 0xff ? old->category_changed
                                                                            : r.category],
              out);
    else if constexpr (P == Property::Bidi)
        ascii(c,
              _PyUnicode_BidirectionalNames[Old && old->category_changed == 0   ? 0
                                            : Old && old->bidir_changed != 0xff ? old->bidir_changed
                                                                                : r.bidirectional],
              out);
    else if constexpr (P == Property::Width)
        ascii(c,
              _PyUnicode_EastAsianWidthNames[Old && old->category_changed == 0 ? 0
                                             : Old && old->east_asian_width_changed != 0xff
                                                 ? old->east_asian_width_changed
                                                 : r.east_asian_width],
              out);
    else if constexpr (P == Property::Combining)
        make_int(c, Old && old->category_changed == 0 ? 0 : r.combining, out);
    else if constexpr (P == Property::Mirrored)
        make_int(c,
                 Old && old->category_changed == 0      ? 0
                 : Old && old->mirrored_changed != 0xff ? old->mirrored_changed
                                                        : r.mirrored,
                 out);
    else {
        if constexpr (Old) {
            if (old->category_changed == 0) {
                make_str(c, U"", out);
                return;
            }
        }
        auto i = decomp_index1[ch >> DECOMP_SHIFT];
        auto index = decomp_index2[(i << DECOMP_SHIFT) + (ch & ((1 << DECOMP_SHIFT) - 1))];
        auto count = decomp_data[index] >> 8;
        std::string text = decomp_prefix[decomp_data[index] & 255];
        while (count--) {
            char buf[12];
            std::snprintf(buf, sizeof(buf), "%04X", decomp_data[++index]);
            if (!text.empty())
                text += ' ';
            text += buf;
        }
        ascii(c, text, out);
    }
}

int nfc_index(const reindex *table, char32_t ch)
{
    for (; table->start; ++table) {
        if (ch < static_cast<char32_t>(table->start))
            return -1;
        if (ch <= static_cast<char32_t>(table->start + table->count))
            return table->index + static_cast<int>(ch) - table->start;
    }
    return -1;
}

template <typename Poll>
std::u32string normalized(std::u32string_view input, bool compatibility, bool compose, bool old,
                          Poll &&poll_gc)
{
    constexpr char32_t S = 0xac00, L = 0x1100, V = 0x1161, T = 0x11a7;
    constexpr int LC = 19, VC = 21, TC = 28, NC = VC * TC, SC = LC * NC;
    std::u32string result;
    std::vector<char32_t> stack;
    std::size_t steps = 0;
    auto poll = [&] {
        if (++steps % 4096 == 0)
            poll_gc();
    };
    for (auto ch : input) {
        stack.push_back(ch);
        while (!stack.empty()) {
            poll();
            ch = stack.back();
            stack.pop_back();
            if (ch >= S && ch < S + SC) {
                auto n = ch - S;
                result.push_back(L + n / NC);
                result.push_back(V + (n % NC) / TC);
                if (n % TC)
                    result.push_back(T + n % TC);
                continue;
            }
            if (old) {
                auto replacement = normalization_3_2_0(ch);
                if (replacement) {
                    stack.push_back(replacement);
                    continue;
                }
                if (get_change_3_2_0(ch)->category_changed == 0) {
                    result.push_back(ch);
                    continue;
                }
            }
            auto i = decomp_index1[ch >> DECOMP_SHIFT];
            auto index = decomp_index2[(i << DECOMP_SHIFT) + (ch & ((1 << DECOMP_SHIFT) - 1))];
            auto count = decomp_data[index] >> 8;
            if (!count || ((decomp_data[index] & 255) && !compatibility))
                result.push_back(ch);
            else
                while (count)
                    stack.push_back(decomp_data[index + count--]);
        }
    }
    // Stable counting sort bounds long combining runs to linear time.
    for (std::size_t start = 0; start < result.size();) {
        poll();
        if (!record(result[start]).combining) {
            ++start;
            continue;
        }
        auto end = start + 1;
        while (end < result.size() && record(result[end]).combining) {
            ++end;
            poll();
        }
        std::array<std::size_t, 256> counts{};
        for (auto i = start; i < end; ++i) {
            ++counts[record(result[i]).combining];
            poll();
        }
        std::size_t sum = 0;
        for (auto &count : counts) {
            auto n = count;
            count = sum;
            sum += n;
        }
        std::u32string sorted(end - start, U'\0');
        for (auto i = start; i < end; ++i) {
            sorted[counts[record(result[i]).combining]++] = result[i];
            poll();
        }
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            result[start + i] = sorted[i];
            poll();
        }
        start = end;
    }
    if (!compose)
        return result;
    std::u32string output;
    std::size_t starter = 0;
    int previous = 0;
    for (auto ch : result) {
        poll();
        int cc = record(ch).combining;
        char32_t composite = 0;
        if (!output.empty() && (previous < cc || previous == 0)) {
            auto base = output[starter];
            if (base >= L && base < L + LC && ch >= V && ch < V + VC)
                composite = S + ((base - L) * VC + ch - V) * TC;
            else if (base >= S && base < S + SC && (base - S) % TC == 0 && ch > T && ch < T + TC)
                composite = base + ch - T;
            else {
                int f = nfc_index(nfc_first, base), l = nfc_index(nfc_last, ch);
                if (f >= 0 && l >= 0) {
                    int index = f * TOTAL_LAST + l;
                    composite = comp_data[(comp_index[index >> COMP_SHIFT] << COMP_SHIFT) +
                                          (index & ((1 << COMP_SHIFT) - 1))];
                }
            }
        }
        if (composite)
            output[starter] = composite;
        else {
            if (!cc)
                starter = output.size();
            output.push_back(ch);
            previous = cc;
        }
    }
    return output;
}

template <bool Check, bool Old = false>
void normalize(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    constexpr std::size_t offset = Old ? 1 : 0;
    auto form = roots.root(args[offset]), input = roots.root(args[offset + 1]);
    for (auto [value, index] : {std::pair{form, U"1"}, std::pair{input, U"2"}})
        if (!c.has_layout<StrObject>(value.get(c))) {
            raise_error(c, CoreObject::TypeErrorType,
                        std::u32string(Check ? U"is_normalized" : U"normalize") + U"() argument " +
                            index + U" must be str, not " + type_name(c, value).substr(0, 100),
                        out);
            return;
        }
    const auto &f = c.payload<StrObject>(form).value;
    const auto &text = c.payload<StrObject>(input).value;
    if (text.empty()) {
        out.ok(Check ? c.builtin(CoreObject::True) : input.get(c));
        return;
    }
    bool compat = f == U"NFKC" || f == U"NFKD", compose = f == U"NFC" || f == U"NFKC";
    if (!compose && f != U"NFD" && f != U"NFKD") {
        raise_error(c, CoreObject::ValueErrorType, U"invalid normalization form", out);
        return;
    }
    auto result = normalized(text, compat, compose, Old, [&] { c.poll(); });
    bool same = result.size() == text.size();
    for (std::size_t i = 0; same && i < text.size(); ++i) {
        if (i % 4096 == 0)
            c.poll();
        same = result[i] == text[i];
    }
    if constexpr (Check)
        out.ok(c.builtin(same ? CoreObject::True : CoreObject::False));
    else if (same)
        out.ok(input.get(c));
    else
        make_str(c, std::move(result), out);
}

template <bool Old = false>
void lookup(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto input = roots.root(args[Old ? 1 : 0]);
    std::u32string original;
    bool binary = false;
    if (c.has_layout<StrObject>(input.get(c))) {
        const auto &value = c.payload<StrObject>(input).value;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i % 4096 == 0)
                c.poll();
            if (value[i] >= 0xd800 && value[i] <= 0xdfff) {
                auto end = i + 1;
                while (end < value.size() && value[end] >= 0xd800 && value[end] <= 0xdfff) {
                    if (end % 4096 == 0)
                        c.poll();
                    ++end;
                }
                surrogate_error(c, input, i, end, out);
                return;
            }
        }
        original.assign(value.begin(),
                        value.begin() + std::min(value.size(), std::size_t(NAME_MAXLEN + 1)));
    } else {
        binary = true;
        if (c.has_layout<ByteArrayObject>(input.get(c)) ||
            (c.has_layout<MemoryViewObject>(input.get(c)) &&
             !c.payload<MemoryViewObject>(input).readonly)) {
            raise_error(c, CoreObject::TypeErrorType,
                        U"lookup() argument must be read-only bytes-like object, not " +
                            type_name(c, input).substr(0, 100),
                        out);
            return;
        }
        if ((c.has_layout<BytesObject>(input.get(c)) &&
             c.payload<BytesObject>(input).value.size() > NAME_MAXLEN) ||
            (c.has_layout<MemoryViewObject>(input.get(c)) &&
             c.payload<MemoryViewObject>(input).length > static_cast<std::int64_t>(NAME_MAXLEN))) {
            raise_error(c, CoreObject::KeyErrorType, U"name too long", out);
            return;
        }
        std::string bytes;
        if (!buffer_bytes(c, input, bytes, out)) {
            if (!failed(out))
                raise_error(c, CoreObject::TypeErrorType,
                            U"a bytes-like object is required, not '" +
                                type_name(c, input).substr(0, 100) + U"'",
                            out);
            return;
        }
        for (unsigned char ch : bytes)
            original.push_back(ch);
    }
    // CPython's lookup consumes a UTF-8 byte string and applies NAME_MAXLEN to bytes.
    std::size_t length = 0;
    for (auto ch : original) {
        length += binary || ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
        if (length > NAME_MAXLEN) {
            raise_error(c, CoreObject::KeyErrorType, U"name too long", out);
            return;
        }
    }
    auto name = original;
    for (auto &ch : name)
        if (ch >= U'a' && ch <= U'z')
            ch -= 32;
    auto code = syntax::unicode_name(original);
    if constexpr (Old) {
        if (code && !original.starts_with(U"CJK UNIFIED IDEOGRAPH-") &&
            !original.starts_with(U"HANGUL SYLLABLE ")) {
            auto canonical = syntax::unicode_character_name(*code);
            if (get_change_3_2_0(*code)->category_changed == 0 || !canonical ||
                std::u32string(canonical->begin(), canonical->end()) != name)
                code.reset();
        }
    } else if (!code) {
        std::string key;
        for (auto ch : name) {
            if (ch >= 128)
                break;
            key.push_back(static_cast<char>(ch));
        }
        if (key.size() == name.size()) {
            auto found =
                std::lower_bound(std::begin(named_sequences), std::end(named_sequences), key,
                                 [](const NamedSequence &entry, std::string_view value) {
                                     return entry.name < value;
                                 });
            if (found != std::end(named_sequences) && found->name == key) {
                make_str(c, std::u32string(found->value), out);
                return;
            }
        }
    }
    if (!code) {
        auto end = original.find(U'\0');
        if (binary) {
            std::string bytes;
            for (auto ch : original.substr(0, end))
                bytes.push_back(static_cast<char>(ch));
            ReturnSlot value(c), errors(c), decoded(c);
            make_bytes(c, std::move(bytes), value);
            if (failed(value)) {
                out.raised(value.exception());
                return;
            }
            auto bytes_value = roots.root(value.value());
            make_str(c, U"replace", errors);
            if (failed(errors)) {
                out.raised(errors.exception());
                return;
            }
            decode_primitive(c, bytes_value, TextEncoding::Utf8, roots.root(errors.value()), true,
                             decoded);
            if (failed(decoded)) {
                out.raised(decoded.exception());
                return;
            }
            auto tuple = roots.root(decoded.value());
            original = c.payload<StrObject>(c.payload<TupleObject>(tuple).items[0]).value;
            end = original.size();
        }
        raise_error(c, CoreObject::KeyErrorType,
                    U"undefined character name '" + original.substr(0, end) + U"'", out);
        return;
    }
    make_str(c, std::u32string(1, *code), out);
}

struct UCD {
    ObjectHeader header;
    explicit UCD(InitialType type) : header(type) {}
    void trace(Tracer &) const noexcept {}
};

void ucd_new(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, U"cannot create 'unicodedata.UCD' instances", out);
}

void ucd_version(Attachment &c, Value, ReturnSlot &out)
{
    make_str(c, U"3.2.0", out);
}
} // namespace

void initialize_unicodedata(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    struct Entry {
        const char *name;
        NativeEntry entry;
        NativeEntry old_entry;
        bool optional;
        bool binary;
    };
    const Entry entries[] = {
        {"decimal", property<Property::Decimal>, property<Property::Decimal, true>, true, false},
        {"digit", property<Property::Digit>, property<Property::Digit, true>, true, false},
        {"numeric", property<Property::Numeric>, property<Property::Numeric, true>, true, false},
        {"name", property<Property::Name>, property<Property::Name, true>, true, false},
        {"category", property<Property::Category>, property<Property::Category, true>, false,
         false},
        {"bidirectional", property<Property::Bidi>, property<Property::Bidi, true>, false, false},
        {"combining", property<Property::Combining>, property<Property::Combining, true>, false,
         false},
        {"mirrored", property<Property::Mirrored>, property<Property::Mirrored, true>, false,
         false},
        {"east_asian_width", property<Property::Width>, property<Property::Width, true>, false,
         false},
        {"decomposition", property<Property::Decomp>, property<Property::Decomp, true>, false,
         false},
        {"lookup", lookup<false>, lookup<true>, false, false},
        {"normalize", normalize<false>, normalize<false, true>, false, true},
        {"is_normalized", normalize<true>, normalize<true, true>, false, true}};

    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"UCD"}, LayoutDescriptor::of<UCD>()};
    spec.type.module = U"unicodedata";
    spec.type.slots = std::vector<std::u32string>{};
    spec.type.immutable = true;
    spec.type.subclassable = false;
    ReturnSlot type_slot(c);
    create_native_type(c, spec, type_slot);
    if (failed(type_slot)) {
        out.raised(type_slot.exception());
        return;
    }
    auto type = roots.root(type_slot.value());
    SignatureLayout constructor;
    constructor.parameters = {{U"self", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    install_native_attribute(c, core, type, U"__new__", constructor, ucd_new);
    auto type_dict = roots.root(*TypeView(c, type).dictionary());
    auto new_function = roots.root(*name_lookup(c, type_dict.get(c), U"__new__"));
    ReturnSlot wrapper(c), wrapper_key(c);
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            wrapper, new_function.get(c));
    if (failed(wrapper)) {
        out.raised(wrapper.exception());
        return;
    }
    auto rooted_wrapper = roots.root(wrapper.value());
    make_str(c, U"__new__", wrapper_key);
    if (failed(wrapper_key)) {
        out.raised(wrapper_key.exception());
        return;
    }
    dict_store(c, type_dict, roots.root(wrapper_key.value()), rooted_wrapper, out);
    if (failed(out))
        return;
    install_native_field(c, core, type, U"unidata_version", ucd_version, nullptr);
    for (const auto &entry : entries) {
        SignatureLayout sig;
        sig.parameters.push_back({U"arg", ParameterKind::PositionalOnly});
        if (entry.binary)
            sig.parameters.push_back({U"text", ParameterKind::PositionalOnly});
        if (entry.optional)
            sig.varargs = U"default";
        auto method_sig = sig;
        method_sig.parameters.insert(method_sig.parameters.begin(),
                                     {U"self", ParameterKind::PositionalOnly});
        auto method_name =
            std::u32string(entry.name, entry.name + std::char_traits<char>::length(entry.name));
        install_native_attribute(c, core, type, method_name, method_sig, entry.old_entry);
        ReturnSlot function(c), key(c);
        make_native_function(c, entry.name, sig, entry.entry, {}, function,
                             NativeFunctionBinding::Function);
        if (failed(function)) {
            out.raised(function.exception());
            return;
        }
        auto rooted = roots.root(function.value());
        ascii(c, entry.name, key);
        if (failed(key)) {
            out.raised(key.exception());
            return;
        }
        dict_store(c, dict, roots.root(key.value()), rooted, out);
        if (failed(out))
            return;
    }
    ReturnSlot key(c), version(c);
    ReturnSlot instance(c);
    c.allocate_instance<UCD>(type, instance);
    if (failed(instance)) {
        out.raised(instance.exception());
        return;
    }
    auto old = roots.root(instance.value());
    make_str(c, U"ucd_3_2_0", key);
    if (failed(key)) {
        out.raised(key.exception());
        return;
    }
    dict_store(c, dict, roots.root(key.value()), old, out);
    if (failed(out))
        return;
    make_str(c, U"UCD", key);
    if (failed(key)) {
        out.raised(key.exception());
        return;
    }
    dict_store(c, dict, roots.root(key.value()), type, out);
    if (failed(out))
        return;
    make_str(c, U"unidata_version", key);
    if (failed(key)) {
        out.raised(key.exception());
        return;
    }
    auto rooted_key = roots.root(key.value());
    make_str(c, U"15.0.0", version);
    if (failed(version)) {
        out.raised(version.exception());
        return;
    }
    dict_store(c, dict, rooted_key, roots.root(version.value()), out);
    if (!failed(out))
        out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
