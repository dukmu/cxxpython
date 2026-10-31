// Based on CPython 3.12.14 Objects/typevarobject.c (PSF license).
#include "cxxpython/typealiasobject.hpp"
#include "cxxpython/execution.hpp"
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/typevarobject.hpp"
#include "cxxpython/unionobject.hpp"

namespace cxxpython::core {
namespace {
bool receiver(Attachment &c, Value self, ReturnSlot &out)
{
    if (c.has_layout<TypeAliasObject>(self) &&
        is_subtype(c, c.type_of(self), c.builtin(CoreObject::TypeAliasType)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a TypeAliasType", out);
    return false;
}

std::optional<Value> parameters(Attachment &c, Value value)
{
    if (value == c.builtin(CoreObject::None) ||
        (c.has_layout<TupleObject>(value) && c.payload<TupleObject>(value).items.empty()))
        return {};
    return value;
}

void alias_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(args[0]), name = roots.root(args[1]), value = roots.root(args[2]);
    auto params = roots.root(args[3]);
    if (type.get(c) != c.builtin(CoreObject::TypeAliasType)) {
        raise_error(c, CoreObject::TypeErrorType, U"TypeAliasType.__new__ requires TypeAliasType",
                    out);
        return;
    }
    if (!c.has_layout<StrObject>(name.get(c)) ||
        !is_subtype(c, c.type_of(name.get(c)), c.builtin(CoreObject::StrType))) {
        raise_error(c, CoreObject::TypeErrorType, U"name must be a str", out);
        return;
    }
    if (!c.has_layout<TupleObject>(params.get(c)) ||
        !is_subtype(c, c.type_of(params.get(c)), c.builtin(CoreObject::TupleType))) {
        raise_error(c, CoreObject::TypeErrorType, U"type_params must be a tuple", out);
        return;
    }
    ReturnSlot module(c);
    module.ok(c.builtin(CoreObject::None));
    if (auto frame = c.current_frame()) {
        auto owner = roots.root(*frame);
        auto function = roots.root(c.payload<FrameObject>(owner).function);
        c.load(function.get(c), c.payload<FunctionObject>(function).module, module);
        if (module.tag() == ResultTag::Empty)
            module.ok(c.builtin(CoreObject::None));
    }
    c.allocate_instance<TypeAliasObject>(type, out, name.get(c), parameters(c, params.get(c)),
                                         std::optional<Value>{}, value.get(c), module.value());
}

void alias_name(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver(c, self, out))
        out.ok(c.payload<TypeAliasObject>(self).name);
}

void alias_params(Attachment &c, Value self, ReturnSlot &out)
{
    if (receiver(c, self, out))
        out.ok(c.payload<TypeAliasObject>(self).type_params.value_or(
            c.builtin(CoreObject::EmptyTuple)));
}
void alias_parameters(Attachment &c, Value value, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(value);
    alias_params(c, self.get(c), out);
    if (out.tag() == ResultTag::Ok)
        detail::unpack_typevartuples(c, roots.root(out.value()), out);
}

void alias_module(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    const auto &alias = c.payload<TypeAliasObject>(self);
    if (alias.module) {
        out.ok(*alias.module);
        return;
    }
    if (alias.compute_value) {
        auto function = roots.root(*alias.compute_value);
        c.load(function.get(c), c.payload<FunctionObject>(function).module, out);
        if (out.tag() == ResultTag::Ok)
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}

void alias_value(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &alias = c.payload<TypeAliasObject>(self);
    c.load(self.get(c), alias.value, out);
    if (out.tag() == ResultTag::Ok)
        return;
    auto compute = roots.root(*alias.compute_value);
    call(c, compute, {{}, {}}, out);
    if (out.tag() == ResultTag::Raised)
        return;
    if (!c.compare_exchange(self.get(c), alias.value, {}, out.value()))
        c.load(self.get(c), alias.value, out);
}

void alias_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    alias_name(c, args[0], out);
}

void alias_getitem(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), item = roots.root(args[1]);
    if (!c.payload<TypeAliasObject>(self).type_params) {
        raise_error(c, CoreObject::TypeErrorType, U"Only generic type aliases are subscriptable",
                    out);
        return;
    }
    make_generic_alias(c, self, item, out);
}

template <bool Reverse>
void alias_union(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    make_union(c, roots.root(args[Reverse ? 1 : 0]), roots.root(args[Reverse ? 0 : 1]), out);
}
} // namespace

namespace detail {
void make_type_alias(Attachment &c, Handle name, Handle params, Handle compute, ReturnSlot &out)
{
    RootScope roots(c);
    name = roots.root(name.get(c));
    params = roots.root(params.get(c));
    compute = roots.root(compute.get(c));
    if (!c.has_layout<StrObject>(name.get(c)) ||
        !is_subtype(c, c.type_of(name), c.builtin(CoreObject::StrType)) ||
        (params.get(c) != c.builtin(CoreObject::None) &&
         (!c.has_layout<TupleObject>(params.get(c)) ||
          !is_subtype(c, c.type_of(params), c.builtin(CoreObject::TupleType))))) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid type alias name or parameters", out);
        return;
    }
    if (c.type_of(compute) != c.builtin(CoreObject::FunctionType) ||
        !c.has_layout<FunctionObject>(compute.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"type alias evaluator must be a function", out);
        return;
    }
    auto type = roots.root(c.builtin(CoreObject::TypeAliasType));
    c.allocate_instance<TypeAliasObject>(type, out, name.get(c), parameters(c, params.get(c)),
                                         compute.get(c), std::optional<Value>{},
                                         std::optional<Value>{});
}

void install_typealias(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::TypeAliasType);
    SignatureLayout constructor;
    constructor.parameters = {
        {U"cls", ParameterKind::PositionalOnly},
        {U"name", ParameterKind::PositionalOrKeyword},
        {U"value", ParameterKind::PositionalOrKeyword},
        {U"type_params", ParameterKind::KeywordOnly, at(CoreObject::EmptyTuple).get(c)}};
    install_native_attribute(c, core, owner, U"__new__", constructor, alias_new);
    RootScope roots(c);
    ReturnSlot temporary(c);
    auto dictionary = *TypeView(c, owner).dictionary();
    auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                            function.get(c));
    auto wrapper = roots.root(temporary.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    name_store(c, dictionary, temporary.value(), wrapper.get(c));
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(c, core, owner, U"__repr__", unary, alias_repr);
    install_native_attribute(c, core, owner, U"__reduce__", unary, alias_repr);
    install_native_attribute(c, core, owner, U"__getitem__", pair, alias_getitem);
    install_native_attribute(c, core, owner, U"__or__", pair, alias_union<false>);
    install_native_attribute(c, core, owner, U"__ror__", pair, alias_union<true>);
    install_native_field(c, core, owner, U"__name__", alias_name);
    install_native_field(c, core, owner, U"__type_params__", alias_params);
    install_native_field(c, core, owner, U"__parameters__", alias_parameters);
    install_native_field(c, core, owner, U"__module__", alias_module);
    install_native_field(c, core, owner, U"__value__", alias_value);
}
} // namespace detail
} // namespace cxxpython::core
