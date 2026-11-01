#include "internal/bytesmethods.hpp"

#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/memoryobject.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

bool number(Attachment &c, Handle value, std::int64_t &result, ReturnSlot &out)
{
    if (value.get(c) == c.builtin(CoreObject::None))
        return true;
    ReturnSlot converted(c);
    index(c, value, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(c, converted.value(), result))
        result = int_sign(c, converted.value()) < 0 ? std::numeric_limits<std::int64_t>::min()
                                                    : std::numeric_limits<std::int64_t>::max();
    return true;
}

bool bytes_arg(Attachment &c, Handle value, std::string &data, ReturnSlot &out)
{
    if (detail::buffer_bytes(c, value, data, out))
        return true;
    if (!failed(out))
        raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
    return false;
}

template <CoreObject Type> bool receiver(Attachment &c, Handle self, ReturnSlot &out)
{
    if (is_subtype(c, c.type_of(self), c.builtin(Type)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"bytes descriptor requires a compatible receiver",
                out);
    return false;
}

template <CoreObject Type> void result_bytes(Attachment &c, std::string data, ReturnSlot &out)
{
    if constexpr (Type == CoreObject::BytesType)
        make_bytes(c, std::move(data), out);
    else
        make_bytearray(c, std::move(data), out);
}

// CPython 3.12.14 Objects/bytes_methods.c: only ASCII letters are case-mapped.
template <CoreObject Type, bool Upper>
void change_case(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!receiver<Type>(c, self, out))
        return;
    std::string data;
    if (!bytes_arg(c, self, data, out))
        return;
    if constexpr (Type == CoreObject::BytesType)
        if (data.empty() && c.type_of(self) == c.builtin(Type)) {
            out.ok(self.get(c));
            return;
        }
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (!(i & 4095))
            c.poll();
        auto &ch = data[i];
        if constexpr (Upper) {
            if (ch >= 'a' && ch <= 'z')
                ch -= 'a' - 'A';
        } else if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    }
    result_bytes<Type>(c, std::move(data), out);
}

template <CoreObject Type>
void translate(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]), mapping = roots.root(args[1]), removed = roots.root(args[2]);
    if (!receiver<Type>(c, self, out))
        return;
    std::string table, deleted, data;
    if (mapping.get(c) != c.builtin(CoreObject::None)) {
        if (!bytes_arg(c, mapping, table, out))
            return;
        if (table.size() != 256) {
            raise_error(c, CoreObject::ValueErrorType,
                        U"translation table must be 256 characters long", out);
            return;
        }
    }
    if (!bytes_arg(c, removed, deleted, out) || !bytes_arg(c, self, data, out))
        return;
    std::array<bool, 256> skip{};
    for (unsigned char value : deleted)
        skip[value] = true;
    std::string result;
    result.reserve(data.size());
    for (unsigned char value : data)
        if (!skip[value])
            result += table.empty() ? static_cast<char>(value) : table[value];
    if constexpr (Type == CoreObject::BytesType)
        if (result == data && c.type_of(self) == c.builtin(Type)) {
            out.ok(self.get(c));
            return;
        }
    result_bytes<Type>(c, std::move(result), out);
}
void make_translation(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string from, to;
    if (!bytes_arg(c, roots.root(args[0]), from, out) ||
        !bytes_arg(c, roots.root(args[1]), to, out))
        return;
    if (from.size() != to.size()) {
        raise_error(c, CoreObject::ValueErrorType, U"maketrans arguments must have same length",
                    out);
        return;
    }
    std::string table(256, '\0');
    for (unsigned i = 0; i < 256; ++i)
        table[i] = static_cast<char>(i);
    for (std::size_t i = 0; i < from.size(); ++i)
        table[static_cast<unsigned char>(from[i])] = to[i];
    make_bytes(c, std::move(table), out);
}

enum class Search { Find, RFind, Index, RIndex, Count };
template <CoreObject Type, Search Mode>
void search(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto needle = roots.root(args[1]);
    if (!receiver<Type>(c, self, out))
        return;
    std::string initial;
    if (!bytes_arg(c, self, initial, out))
        return;
    std::int64_t start = 0, end = std::numeric_limits<std::int64_t>::max();
    if (!number(c, roots.root(args[2]), start, out) || !number(c, roots.root(args[3]), end, out))
        return;
    std::string part;
    const bool buffer = is_bytes(c, needle.get(c)) || is_bytearray(c, needle.get(c)) ||
                        c.has_layout<MemoryViewObject>(needle.get(c));
    if (!buffer &&
        (is_int(c, needle.get(c)) || detail::type_attribute(c, c.type_of(needle), U"__index__"))) {
        ReturnSlot converted(c);
        index(c, needle, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        std::int64_t byte;
        if (!int_fits_signed(c, converted.value(), byte) || byte < 0 || byte > 255) {
            raise_error(c, CoreObject::ValueErrorType, U"byte must be in range(0, 256)", out);
            return;
        }
        part.push_back(static_cast<char>(byte));
    } else if (!bytes_arg(c, needle, part, out))
        return;
    std::string data;
    if (!bytes_arg(c, self, data, out))
        return;
    if (data.size() > initial.size())
        data.resize(initial.size());
    const auto length = static_cast<std::int64_t>(data.size());
    if (start < 0)
        start = std::max<std::int64_t>(0, start + length);
    if (end < 0)
        end = std::max<std::int64_t>(0, end + length);
    else
        end = std::min(end, length);
    std::int64_t answer = -1;
    if (start <= end && start <= length) {
        const auto first = static_cast<std::size_t>(start);
        const auto last = static_cast<std::size_t>(end);
        if constexpr (Mode == Search::Count) {
            answer = 0;
            if (part.empty())
                answer = end - start + 1;
            else {
                auto pos = first;
                while (pos <= last && part.size() <= last - pos) {
                    auto found = data.find(part, pos);
                    if (found == std::string::npos || found > last - part.size())
                        break;
                    ++answer;
                    pos = found + part.size();
                }
            }
        } else if (part.size() <= last - first) {
            auto found = (Mode == Search::RFind || Mode == Search::RIndex)
                             ? data.rfind(part, last - part.size())
                             : data.find(part, first);
            if (found != std::string::npos && found >= first && found <= last - part.size())
                answer = static_cast<std::int64_t>(found);
        }
    } else if constexpr (Mode == Search::Count)
        answer = 0;
    if constexpr (Mode == Search::Index || Mode == Search::RIndex) {
        if (answer < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"subsection not found", out);
            return;
        }
    }
    make_int(c, answer, out);
}

template <CoreObject Type, bool Reverse>
void partition(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!receiver<Type>(c, self, out))
        return;
    std::string separator, data;
    if (!bytes_arg(c, roots.root(args[1]), separator, out))
        return;
    if (separator.empty()) {
        raise_error(c, CoreObject::ValueErrorType, U"empty separator", out);
        return;
    }
    if (!bytes_arg(c, self, data, out))
        return;
    const auto pos = Reverse ? data.rfind(separator) : data.find(separator);
    std::array<std::string, 3> pieces;
    if (pos == std::string::npos)
        pieces[Reverse ? 2 : 0] = std::move(data);
    else {
        pieces[0] = data.substr(0, pos);
        pieces[1] = separator;
        pieces[2] = data.substr(pos + separator.size());
    }
    std::vector<Value> values;
    ReturnSlot item(c);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        result_bytes<Type>(c, std::move(pieces[i]), item);
        values.push_back(roots.root(item.value()).get(c));
    }
    cxxpython::core::make_tuple(c, values, out);
}

bool whitespace(unsigned char ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}
template <CoreObject Type, bool Left, bool Right>
void strip(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!receiver<Type>(c, self, out))
        return;
    std::string chars, data;
    const bool default_chars = args[1] == c.builtin(CoreObject::None);
    if (!default_chars && !bytes_arg(c, roots.root(args[1]), chars, out))
        return;
    if (!bytes_arg(c, self, data, out))
        return;
    auto match = [&](char ch) {
        return default_chars ? whitespace(static_cast<unsigned char>(ch))
                             : chars.find(ch) != std::string::npos;
    };
    std::size_t first = 0, last = data.size();
    if constexpr (Left)
        while (first < last && match(data[first]))
            ++first;
    if constexpr (Right)
        while (last > first && match(data[last - 1]))
            --last;
    result_bytes<Type>(c, data.substr(first, last - first), out);
}

template <CoreObject Type, bool Reverse>
void split(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!receiver<Type>(c, self, out))
        return;
    auto separator_value = roots.root(args[1]);
    ReturnSlot converted(c);
    index(c, roots.root(args[2]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    std::int64_t limit;
    if (!int_fits_signed(c, converted.value(), limit)) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return;
    }
    const bool spaces = separator_value.get(c) == c.builtin(CoreObject::None);
    std::string separator, data;
    if (!spaces && !bytes_arg(c, separator_value, separator, out))
        return;
    if (!spaces && separator.empty()) {
        raise_error(c, CoreObject::ValueErrorType, U"empty separator", out);
        return;
    }
    if (!bytes_arg(c, self, data, out))
        return;
    std::vector<std::string> pieces;
    std::size_t first = 0, last = data.size();
    std::int64_t count = 0;
    if (spaces) {
        if constexpr (Reverse) {
            while (last > first && whitespace(static_cast<unsigned char>(data[last - 1])))
                --last;
            while (last > first && (limit < 0 || count < limit)) {
                auto end = last;
                while (last > first && !whitespace(static_cast<unsigned char>(data[last - 1])))
                    --last;
                pieces.push_back(data.substr(last, end - last));
                ++count;
                while (last > first && whitespace(static_cast<unsigned char>(data[last - 1])))
                    --last;
            }
            if (last > first)
                pieces.push_back(data.substr(first, last - first));
        } else {
            while (first < last && whitespace(static_cast<unsigned char>(data[first])))
                ++first;
            while (first < last && (limit < 0 || count < limit)) {
                auto begin = first;
                while (first < last && !whitespace(static_cast<unsigned char>(data[first])))
                    ++first;
                pieces.push_back(data.substr(begin, first - begin));
                ++count;
                while (first < last && whitespace(static_cast<unsigned char>(data[first])))
                    ++first;
            }
            if (first < last)
                pieces.push_back(data.substr(first, last - first));
        }
    } else if constexpr (Reverse) {
        while (limit < 0 || count < limit) {
            if (last < separator.size())
                break;
            auto pos = data.rfind(separator, last - separator.size());
            if (pos == std::string::npos)
                break;
            pieces.push_back(data.substr(pos + separator.size(), last - pos - separator.size()));
            last = pos;
            ++count;
        }
        pieces.push_back(data.substr(0, last));
    } else {
        while (limit < 0 || count < limit) {
            auto pos = data.find(separator, first);
            if (pos == std::string::npos)
                break;
            pieces.push_back(data.substr(first, pos - first));
            first = pos + separator.size();
            ++count;
        }
        pieces.push_back(data.substr(first));
    }
    if constexpr (Reverse)
        std::reverse(pieces.begin(), pieces.end());
    std::vector<Value> values;
    ReturnSlot item(c);
    for (auto &piece : pieces) {
        result_bytes<Type>(c, std::move(piece), item);
        values.push_back(roots.root(item.value()).get(c));
    }
    make_list(c, values, out);
}
} // namespace

void detail::install_bytes_methods(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto install = [&](CoreObject owner, const char32_t *name, SignatureLayout signature,
                       NativeEntry entry) {
        install_native_attribute(c, core, at(owner), name, std::move(signature), entry);
    };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install(CoreObject::BytesType, U"upper", unary, change_case<CoreObject::BytesType, true>);
    install(CoreObject::BytesType, U"lower", unary, change_case<CoreObject::BytesType, false>);
    install(CoreObject::ByteArrayType, U"upper", unary,
            change_case<CoreObject::ByteArrayType, true>);
    install(CoreObject::ByteArrayType, U"lower", unary,
            change_case<CoreObject::ByteArrayType, false>);
    SignatureLayout search_sig;
    search_sig.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"sub", ParameterKind::PositionalOnly},
        {U"start", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)},
        {U"end", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)}};
    SignatureLayout pair;
    pair.parameters = {{U"self", ParameterKind::PositionalOnly},
                       {U"sep", ParameterKind::PositionalOnly}};
    SignatureLayout strip_sig;
    strip_sig.parameters = {{U"self", ParameterKind::PositionalOnly},
                            {U"chars", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)}};
    SignatureLayout split_sig;
    ReturnSlot empty(c);
    c.allocate_instance<BytesObject>(at(CoreObject::BytesType), empty, std::string{});
    auto translation = pair;
    translation.parameters[1].name = U"table";
    translation.parameters.push_back(
        {U"delete", ParameterKind::PositionalOrKeyword, empty.value()});
    install(CoreObject::BytesType, U"translate", translation, translate<CoreObject::BytesType>);
    install(CoreObject::ByteArrayType, U"translate", translation,
            translate<CoreObject::ByteArrayType>);
    SignatureLayout mapping;
    mapping.parameters = {{U"from", ParameterKind::PositionalOnly},
                          {U"to", ParameterKind::PositionalOnly}};
    ReturnSlot function(c), wrapper(c), key(c);
    c.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType), function,
                                              "maketrans", mapping, make_translation,
                                              std::vector<Value>{});
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"maketrans");
    for (auto type : {CoreObject::BytesType, CoreObject::ByteArrayType})
        name_store(c, *TypeView(c, at(type)).dictionary(), key.value(), wrapper.value());
    ReturnSlot minus_one(c);
    c.allocate_instance<IntObject>(at(CoreObject::IntType), minus_one, -1);
    roots.root(minus_one.value());
    split_sig.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"sep", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)},
        {U"maxsplit", ParameterKind::PositionalOrKeyword, minus_one.value()}};
#define INSTALL(Type)                                                                              \
    install(Type, U"find", search_sig, search<Type, Search::Find>);                                \
    install(Type, U"rfind", search_sig, search<Type, Search::RFind>);                              \
    install(Type, U"index", search_sig, search<Type, Search::Index>);                              \
    install(Type, U"rindex", search_sig, search<Type, Search::RIndex>);                            \
    install(Type, U"count", search_sig, search<Type, Search::Count>);                              \
    install(Type, U"partition", pair, partition<Type, false>);                                     \
    install(Type, U"rpartition", pair, partition<Type, true>);                                     \
    install(Type, U"strip", strip_sig, strip<Type, true, true>);                                   \
    install(Type, U"lstrip", strip_sig, strip<Type, true, false>);                                 \
    install(Type, U"rstrip", strip_sig, strip<Type, false, true>);                                 \
    install(Type, U"split", split_sig, split<Type, false>);                                        \
    install(Type, U"rsplit", split_sig, split<Type, true>)
    INSTALL(CoreObject::BytesType);
    INSTALL(CoreObject::ByteArrayType);
#undef INSTALL
}
} // namespace cxxpython::core
