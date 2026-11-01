// Factory semantics follow CPython 3.12.14 Modules/_sre/sre.c (PSF license).
#include "cxxpython/sreobject.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "engine.h"
#include "internal/unicodeobject.hpp"
#include "sre_constants.h"

#include <limits>

using namespace cxxpython::core;

extern "C" {
uint32_t cxx_sre_unicode_lower(uint32_t c)
{
    return detail::unicode_simple_lower(c);
}
uint32_t cxx_sre_unicode_upper(uint32_t c)
{
    return detail::unicode_simple_upper(c);
}
int cxx_sre_unicode_decimal(uint32_t c)
{
    return detail::unicode_decimal(c);
}
int cxx_sre_unicode_space(uint32_t c)
{
    return detail::unicode_space(c);
}
int cxx_sre_unicode_linebreak(uint32_t c)
{
    return detail::unicode_linebreak(c);
}
int cxx_sre_unicode_alnum(uint32_t c)
{
    return detail::unicode_alnum(c);
}
}

namespace cxxpython::core {
namespace {
template <class T> bool typed(Attachment &c, Value v, CoreObject type)
{
    return c.has_layout<T>(v) && is_subtype(c, c.type_of(v), c.builtin(type));
}

bool number(Attachment &c, Handle value, std::int64_t min, std::int64_t max, std::int64_t &n,
            ReturnSlot &out)
{
    ReturnSlot converted(c);
    index(c, value, converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(c, converted.value(), n) || n < min || n > max) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return false;
    }
    return true;
}

void compile_pattern(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> held;
    for (auto v : args)
        held.push_back(roots.root(v));
    if (!typed<ListObject>(c, args[2], CoreObject::ListType) ||
        !typed<DictObject>(c, args[4], CoreObject::DictType) ||
        !typed<TupleObject>(c, args[5], CoreObject::TupleType)) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"code, groupindex and indexgroup must be list, dict and tuple", out);
        return;
    }
    std::int64_t flags, groups;
    if (!number(c, held[1], INT32_MIN, INT32_MAX, flags, out) ||
        !number(c, held[3], std::numeric_limits<std::ptrdiff_t>::min(),
                std::numeric_limits<std::ptrdiff_t>::max(), groups, out))
        return;
    std::vector<Value> values;
    detail::list_snapshot(c, held[2], roots, values);
    std::vector<std::uint32_t> code;
    code.reserve(values.size());
    for (auto value : values) {
        std::int64_t n;
        if (!number(c, roots.root(value), 0, UINT32_MAX, n, out))
            return;
        code.push_back(static_cast<std::uint32_t>(n));
    }
    SreStringKind kind = SreStringKind::Any;
    if (held[0].get(c) != c.builtin(CoreObject::None)) {
        if (typed<StrObject>(c, held[0].get(c), CoreObject::StrType))
            kind = SreStringKind::Unicode;
        else {
            std::string bytes;
            ReturnSlot error(c);
            if (!detail::buffer_bytes(c, held[0], bytes, error)) {
                if (error.tag() == ResultTag::Raised)
                    out.raised(error.exception());
                else
                    raise_error(c, CoreObject::TypeErrorType,
                                U"expected string or bytes-like object", out);
                return;
            }
            kind = SreStringKind::Bytes;
        }
    }
    if (groups < 0 ||
        !cxx_sre_validate(code.data(), code.size(), static_cast<std::size_t>(groups))) {
        raise_error(c, CoreObject::RuntimeErrorType, U"invalid SRE code", out);
        return;
    }
    c.allocate_instance<SrePatternObject>(roots.root(c.builtin(CoreObject::SrePatternType)), out,
                                          held[0].get(c), held[4].get(c), held[5].get(c),
                                          static_cast<int>(flags), static_cast<std::size_t>(groups),
                                          kind, std::move(code));
}

void make_template(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto list = roots.root(args[1]);
    if (!typed<ListObject>(c, list.get(c), CoreObject::ListType)) {
        raise_error(c, CoreObject::TypeErrorType, U"template must be a list", out);
        return;
    }
    std::vector<Value> values;
    detail::list_snapshot(c, list, roots, values);
    if (values.empty() || values.size() % 2 == 0) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid template", out);
        return;
    }
    std::vector<Handle> items;
    for (auto value : values)
        items.push_back(roots.root(value));
    for (std::size_t i = 1; i < items.size(); i += 2) {
        ReturnSlot converted(c);
        index(c, items[i], converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return;
        }
        std::int64_t n;
        if (!int_fits_signed(c, converted.value(), n) ||
            n > std::numeric_limits<std::ptrdiff_t>::max()) {
            raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
            return;
        }
        if (n < 0) {
            raise_error(c, CoreObject::TypeErrorType, U"invalid template", out);
            return;
        }
        items[i] = roots.root(converted.value());
    }
    values.clear();
    for (auto item : items)
        values.push_back(item.get(c));
    ReturnSlot tuple(c);
    cxxpython::core::make_tuple(c, std::span<const Value>(values), tuple);
    if (tuple.tag() == ResultTag::Raised) {
        out.raised(tuple.exception());
        return;
    }
    auto owned = roots.root(tuple.value());
    c.allocate_instance<SreTemplateObject>(roots.root(c.builtin(CoreObject::SreTemplateType)), out,
                                           owned.get(c));
}

void getcodesize(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    make_int(c, sizeof(std::uint32_t), out);
}
template <int mode>
void character(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::int64_t n;
    if (!number(c, roots.root(args[0]), INT32_MIN, INT32_MAX, n, out))
        return;
    auto ch = static_cast<std::uint32_t>(n);
    auto lower = mode < 2 ? (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch) : cxx_sre_unicode_lower(ch);
    if constexpr (mode == 0 || mode == 2) {
        auto upper =
            mode == 0 ? (ch >= 'a' && ch <= 'z' ? ch - 32 : ch) : cxx_sre_unicode_upper(ch);
        out.ok(c.builtin(lower != upper ? CoreObject::True : CoreObject::False));
    } else
        make_int(c, static_cast<std::int32_t>(lower), out);
}
} // namespace

void detail::initialize_sre(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c), value(c), result(c);
    auto store = [&](const char32_t *name) {
        auto v = roots.root(value.value());
        make_str(c, name, key);
        dict_store(c, dict, roots.root(key.value()), v, result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    auto function = [&](const char *name, const char32_t *keyname, NativeEntry entry,
                        std::initializer_list<const char32_t *> names) {
        SignatureLayout sig;
        for (auto n : names)
            sig.parameters.push_back({n, ParameterKind::PositionalOnly});
        make_native_function(c, name, std::move(sig), entry, {}, value,
                             NativeFunctionBinding::Function);
        return store(keyname);
    };
    if (!function("compile", U"compile", compile_pattern,
                  {U"pattern", U"flags", U"code", U"groups", U"groupindex", U"indexgroup"}) ||
        !function("template", U"template", make_template, {U"pattern", U"template"}) ||
        !function("getcodesize", U"getcodesize", getcodesize, {}) ||
        !function("ascii_iscased", U"ascii_iscased", character<0>, {U"character"}) ||
        !function("ascii_tolower", U"ascii_tolower", character<1>, {U"character"}) ||
        !function("unicode_iscased", U"unicode_iscased", character<2>, {U"character"}) ||
        !function("unicode_tolower", U"unicode_tolower", character<3>, {U"character"}))
        return;
    for (auto [name, n] : {std::pair{U"MAGIC", std::int64_t{SRE_MAGIC}},
                           {U"CODESIZE", std::int64_t{sizeof(std::uint32_t)}},
                           {U"MAXREPEAT", std::int64_t{UINT32_MAX}},
                           {U"MAXGROUPS", std::int64_t{INT32_MAX / 2}}}) {
        make_int(c, n, value);
        if (!store(name))
            return;
    }
    make_str(c, U" SRE 2.2.2 Copyright (c) 1997-2002 by Secret Labs AB ", value);
    if (!store(U"copyright"))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core
