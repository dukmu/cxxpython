// Based on CPython 3.12.14 Objects/typevarobject.c (PSF license).
#include "cxxpython/execution.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/typevarobject.hpp"

namespace cxxpython::core {
namespace {
template <bool Keywords>
constexpr CoreObject view_type =
    Keywords ? CoreObject::ParamSpecKwargsType : CoreObject::ParamSpecArgsType;

template <bool Keywords> bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.type_of(value) == c.builtin(view_type<Keywords>) &&
        c.has_layout<ParamSpecAttributeObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"invalid ParamSpec view descriptor receiver", out);
    return false;
}

template <bool Keywords>
void view_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != c.builtin(view_type<Keywords>)) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid ParamSpec view type", out);
        return;
    }
    RootScope roots(c);
    detail::make_paramspec_attribute(c, roots.root(args[1]), Keywords, out);
}

template <bool Keywords> void view_origin(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver<Keywords>(c, self, out))
        out.ok(c.payload<ParamSpecAttributeObject>(self).origin);
}

template <bool Keywords>
void view_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Keywords>(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto origin = roots.root(c.payload<ParamSpecAttributeObject>(self).origin);
    ReturnSlot rendered(c);
    if (c.type_of(origin) == c.builtin(CoreObject::ParamSpecType))
        rendered.ok(c.payload<ParamSpecObject>(origin).name);
    else {
        repr(c, origin, rendered);
        if (rendered.tag() == ResultTag::Raised) {
            out.raised(rendered.exception());
            return;
        }
    }
    std::u32string text = c.payload<StrObject>(rendered.value()).value;
    text += Keywords ? U".kwargs" : U".args";
    make_str(c, text, out);
}

template <bool Keywords, CompareOp Op>
void view_compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Keywords>(c, args[0], out))
        return;
    if (c.type_of(args[1]) != c.builtin(view_type<Keywords>)) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    auto a = roots.root(c.payload<ParamSpecAttributeObject>(left).origin);
    auto b = roots.root(c.payload<ParamSpecAttributeObject>(right).origin);
    rich_compare(c, a, b, Op, out);
}

template <bool Keywords>
void view_mro_entries(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver<Keywords>(c, args[0], out))
        return;
    raise_error(c, CoreObject::TypeErrorType,
                Keywords ? U"Cannot subclass an instance of ParamSpecKwargs"
                         : U"Cannot subclass an instance of ParamSpecArgs",
                out);
}

template <bool Keywords> void install_view(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(view_type<Keywords>);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"origin", ParameterKind::PositionalOrKeyword}};
    detail::install_native_attribute(c, core, owner, U"__new__", constructor, view_new<Keywords>);
    RootScope roots(c);
    ReturnSlot temporary(c);
    auto dictionary = *TypeView(c, owner).dictionary();
    auto function = roots.root(*detail::name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                            function.get(c));
    auto wrapper = roots.root(temporary.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    detail::name_store(c, dictionary, temporary.value(), wrapper.get(c));
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    detail::install_native_attribute(c, core, owner, U"__repr__", unary, view_repr<Keywords>);
    detail::install_native_attribute(c, core, owner, U"__eq__", pair,
                                     view_compare<Keywords, CompareOp::Eq>);
    detail::install_native_attribute(c, core, owner, U"__ne__", pair,
                                     view_compare<Keywords, CompareOp::Ne>);
    detail::install_native_attribute(c, core, owner, U"__mro_entries__", pair,
                                     view_mro_entries<Keywords>);
    detail::install_native_field(c, core, owner, U"__origin__", view_origin<Keywords>);
    c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__hash__");
    detail::name_store(c, dictionary, temporary.value(), at(CoreObject::None).get(c));
}
} // namespace

namespace detail {
void make_paramspec_attribute(Attachment &c, Handle origin, bool keywords, ReturnSlot &out)
{
    RootScope roots(c);
    auto value = roots.root(origin.get(c));
    auto type = roots.root(
        c.builtin(keywords ? CoreObject::ParamSpecKwargsType : CoreObject::ParamSpecArgsType));
    c.allocate_instance<ParamSpecAttributeObject>(type, out, value.get(c));
}

void install_paramspec_views(Attachment &c, std::span<const Handle> core)
{
    install_view<false>(c, core);
    install_view<true>(c, core);
}
} // namespace detail
} // namespace cxxpython::core
