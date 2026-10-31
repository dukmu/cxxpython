// Adapted from CPython 3.12.14 Modules/_sre/sre.c (PSF license).
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/sreobject.hpp"

#include <array>

namespace cxxpython::core {
namespace {
bool group_index(Attachment &c, Handle self, Handle key, std::size_t &result, ReturnSlot &out)
{
    RootScope roots(c);
    self = roots.root(self.get(c));
    key = roots.root(key.get(c));
    auto slot = resolve_protocol(c, roots.root(c.type_of(key)), Protocol::Index, roots);
    ReturnSlot number(c);
    if (slot.state != SlotState::Missing && slot.state != SlotState::Disabled) {
        index(c, key, number);
        if (number.tag() == ResultTag::Raised) {
            out.raised(number.exception());
            return false;
        }
    } else {
        auto pattern = roots.root(c.payload<SreMatchObject>(self).pattern);
        auto names = roots.root(c.payload<SrePatternObject>(pattern).groupindex);
        if (names.get(c) != c.builtin(CoreObject::None)) {
            dict_lookup(c, names, key, number);
            if (number.tag() == ResultTag::Raised) {
                out.raised(number.exception());
                return false;
            }
        }
    }
    std::int64_t i;
    if (number.tag() == ResultTag::Ok && is_int(c, number.value()) &&
        int_fits_signed(c, number.value(), i) && i >= 0 &&
        static_cast<std::size_t>(i) < c.payload<SreMatchObject>(self).marks.size()) {
        result = static_cast<std::size_t>(i);
        return true;
    }
    raise_error(c, CoreObject::IndexErrorType, U"no such group", out);
    return false;
}

void pair(Attachment &c, std::pair<std::ptrdiff_t, std::ptrdiff_t> mark, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot a(c), b(c);
    make_int(c, mark.first, a);
    auto first = roots.root(a.value());
    make_int(c, mark.second, b);
    const std::array values{first.get(c), b.value()};
    cxxpython::core::make_tuple(c, values, out);
}

void group(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto groups = roots.root(args[1]);
    const auto &items = c.payload<TupleObject>(groups).items;
    if (items.empty()) {
        detail::sre_group_slice(c, self, 0, roots.root(c.builtin(CoreObject::None)), out);
        return;
    }
    std::vector<Value> values;
    for (auto item : items) {
        std::size_t i;
        if (!group_index(c, self, roots.root(item), i, out))
            return;
        detail::sre_group_slice(c, self, i, roots.root(c.builtin(CoreObject::None)), out);
        if (out.tag() == ResultTag::Raised)
            return;
        values.push_back(roots.root(out.value()).get(c));
    }
    if (values.size() == 1)
        out.ok(values[0]);
    else
        cxxpython::core::make_tuple(c, values, out);
}
void getitem(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::size_t i;
    if (group_index(c, self, roots.root(args[1]), i, out))
        detail::sre_group_slice(c, self, i, roots.root(c.builtin(CoreObject::None)), out);
}
void groups(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]), fallback = roots.root(args[1]);
    std::vector<Value> values;
    auto count = c.payload<SreMatchObject>(self).marks.size();
    for (std::size_t i = 1; i < count; ++i) {
        detail::sre_group_slice(c, self, i, fallback, out);
        if (out.tag() == ResultTag::Raised)
            return;
        values.push_back(roots.root(out.value()).get(c));
    }
    cxxpython::core::make_tuple(c, values, out);
}
void groupdict(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]), fallback = roots.root(args[1]);
    auto pattern = roots.root(c.payload<SreMatchObject>(self).pattern);
    auto names = roots.root(c.payload<SrePatternObject>(pattern).groupindex);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), out);
    auto result = roots.root(out.value());
    if (names.get(c) != c.builtin(CoreObject::None)) {
        DictView view(c, names.get(c));
        for (const auto &entry : view.entries()) {
            auto key = roots.root(entry.key);
            std::size_t i;
            if (!group_index(c, self, key, i, out))
                return;
            detail::sre_group_slice(c, self, i, fallback, out);
            if (out.tag() == ResultTag::Raised)
                return;
            dict_store(c, result, key, roots.root(out.value()), out);
            if (out.tag() == ResultTag::Raised)
                return;
        }
    }
    out.ok(result.get(c));
}
template <int Which>
void position(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::size_t i;
    if (!group_index(c, self, roots.root(args[1]), i, out))
        return;
    auto mark = c.payload<SreMatchObject>(self).marks[i];
    if constexpr (Which == 2)
        pair(c, mark, out);
    else
        make_int(c, Which == 0 ? mark.first : mark.second, out);
}
void expand(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    detail::sre_expand(c, roots.root(args[0]), roots.root(args[1]), out);
}
void copy(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void class_getitem(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    make_generic_alias(c, roots.root(args[0]), roots.root(args[1]), out);
}
template <int Field> void field(Attachment &c, Value self, ReturnSlot &out)
{
    const auto &m = c.payload<SreMatchObject>(self);
    if constexpr (Field == 0)
        out.ok(m.pattern);
    if constexpr (Field == 1)
        out.ok(m.string);
    if constexpr (Field == 2)
        make_int(c, m.pos, out);
    if constexpr (Field == 3)
        make_int(c, m.endpos, out);
    if constexpr (Field == 4) {
        if (m.lastindex < 0)
            out.ok(c.builtin(CoreObject::None));
        else
            make_int(c, m.lastindex, out);
    }
    if constexpr (Field == 5) {
        auto &p = c.payload<SrePatternObject>(m.pattern);
        if (m.lastindex >= 0 && c.has_layout<TupleObject>(p.indexgroup) &&
            static_cast<std::size_t>(m.lastindex) <
                c.payload<TupleObject>(p.indexgroup).items.size())
            out.ok(c.payload<TupleObject>(p.indexgroup).items[m.lastindex]);
        else
            out.ok(c.builtin(CoreObject::None));
    }
}
void regs(Attachment &c, Value value, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(value);
    auto &m = c.payload<SreMatchObject>(self);
    c.load(self.get(c), m.regs, out);
    if (out.tag() == ResultTag::Ok)
        return;
    std::vector<Value> values;
    for (auto mark : m.marks) {
        pair(c, mark, out);
        values.push_back(roots.root(out.value()).get(c));
    }
    cxxpython::core::make_tuple(c, values, out);
    if (!c.compare_exchange(self.get(c), m.regs, {}, out.value()))
        c.load(self.get(c), m.regs, out);
}
void match_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto mark = c.payload<SreMatchObject>(self).marks[0];
    detail::sre_group_slice(c, self, 0, roots.root(c.builtin(CoreObject::None)), out);
    if (out.tag() == ResultTag::Raised)
        return;
    repr(c, roots.root(out.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto text = c.payload<StrObject>(out.value()).value;
    if (text.size() > 50)
        text.resize(50);
    auto convert = [](std::ptrdiff_t n) {
        auto s = std::to_string(n);
        return std::u32string(s.begin(), s.end());
    };
    make_str(c,
             U"<re.Match object; span=(" + convert(mark.first) + U", " + convert(mark.second) +
                 U"), match=" + text + U">",
             out);
}
} // namespace

namespace detail {
void sre_group_slice(Attachment &c, Handle match, std::size_t group, Handle fallback,
                     ReturnSlot &out)
{
    RootScope roots(c);
    match = roots.root(match.get(c));
    fallback = roots.root(fallback.get(c));
    const auto &m = c.payload<SreMatchObject>(match);
    auto mark = m.marks[group];
    if (mark.first < 0 || m.string == c.builtin(CoreObject::None)) {
        out.ok(fallback.get(c));
        return;
    }
    sre_slice(c, roots.root(m.string), mark.first, mark.second, out);
}
void install_sre_match(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::SreMatchType);
    SignatureLayout unary, binary, optional, variadic;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    binary = unary;
    binary.parameters.push_back({U"arg", ParameterKind::PositionalOnly});
    optional = unary;
    optional.parameters.push_back(
        {U"default", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    variadic = unary;
    variadic.varargs = U"groups";
    install_native_attribute(c, core, owner, U"group", variadic, group);
    install_native_attribute(c, core, owner, U"__getitem__", binary, getitem);
    install_native_attribute(c, core, owner, U"groups", optional, groups);
    install_native_attribute(c, core, owner, U"groupdict", optional, groupdict);
    auto pos = unary;
    pos.parameters.push_back(
        {U"group", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    install_native_attribute(c, core, owner, U"start", pos, position<0>);
    install_native_attribute(c, core, owner, U"end", pos, position<1>);
    install_native_attribute(c, core, owner, U"span", pos, position<2>);
    install_native_attribute(c, core, owner, U"expand", binary, expand);
    install_native_attribute(c, core, owner, U"__copy__", unary, copy);
    install_native_attribute(c, core, owner, U"__deepcopy__", binary, copy);
    install_native_attribute(c, core, owner, U"__repr__", unary, match_repr);
    install_native_attribute(c, core, owner, U"__class_getitem__", binary, class_getitem);
    RootScope roots(c);
    auto dictionary = *TypeView(c, owner).dictionary();
    auto method = roots.root(*name_lookup(c, dictionary, U"__class_getitem__"));
    ReturnSlot temporary(c);
    c.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), temporary,
                                           method.get(c));
    auto wrapper = roots.root(temporary.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__class_getitem__");
    name_store(c, dictionary, temporary.value(), wrapper.get(c));
    install_native_field(c, core, owner, U"re", field<0>);
    install_native_field(c, core, owner, U"string", field<1>);
    install_native_field(c, core, owner, U"pos", field<2>);
    install_native_field(c, core, owner, U"endpos", field<3>);
    install_native_field(c, core, owner, U"lastindex", field<4>);
    install_native_field(c, core, owner, U"lastgroup", field<5>);
    install_native_field(c, core, owner, U"regs", regs);
}
} // namespace detail
} // namespace cxxpython::core
