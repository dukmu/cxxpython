// Adapted from CPython 3.12.14 Objects/typevarobject.c (PSF license).
#include "cxxpython/typevarobject.hpp"
#include "cxxpython/execution.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"

#include <array>

namespace cxxpython::core {
namespace {
void typing_call(Attachment &c, std::u32string name, CallArgsView args, ReturnSlot &out)
{
    RootScope roots(c);
    for (auto value : args.positional)
        roots.root(value);
    for (auto keyword : args.keywords) {
        roots.root(keyword.name);
        roots.root(keyword.value);
    }
    import_module(c, U"typing", out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto module = roots.root(out.value());
    ReturnSlot key(c);
    make_str(c, std::move(name), key);
    get_attribute(c, module, roots.root(key.value()), out);
    if (out.tag() == ResultTag::Ok)
        call(c, roots.root(out.value()), args, out);
}

bool valid_name(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<StrObject>(value) &&
        is_subtype(c, c.type_of(value), c.builtin(CoreObject::StrType)))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"name must be a str", out);
    return false;
}

void parameter_module(Attachment &c, Handle self, ReturnSlot &out)
{
    RootScope roots(c);
    self = roots.root(self.get(c));
    ReturnSlot module(c), key(c);
    module.ok(c.builtin(CoreObject::None));
    if (auto current = c.current_frame()) {
        auto frame = roots.root(*current);
        auto function = roots.root(c.payload<FrameObject>(frame).function);
        c.load(function.get(c), c.payload<FunctionObject>(function).module, module);
        if (module.tag() == ResultTag::Empty)
            module.ok(c.builtin(CoreObject::None));
    }
    make_str(c, U"__module__", key);
    set_attribute(c, self, roots.root(key.value()), roots.root(module.value()), out);
    if (out.tag() == ResultTag::Ok)
        out.ok(self.get(c));
}

bool variance(Attachment &c, std::span<const Value> args, std::array<bool, 3> &flags,
              ReturnSlot &out)
{
    for (std::size_t i = 0; i < flags.size(); ++i) {
        RootScope roots(c);
        truth(c, roots.root(args[i]), out);
        if (out.tag() == ResultTag::Raised)
            return false;
        flags[i] = out.value() == c.builtin(CoreObject::True);
    }
    if (flags[0] && flags[1]) {
        raise_error(c, CoreObject::ValueErrorType, U"Bivariant types are not supported.", out);
        return false;
    }
    if (flags[2] && (flags[0] || flags[1])) {
        raise_error(c, CoreObject::ValueErrorType,
                    U"Variance cannot be specified with infer_variance.", out);
        return false;
    }
    return true;
}

void check_bound(Attachment &c, Value bound, ReturnSlot &out)
{
    if (bound == c.builtin(CoreObject::None)) {
        out.ok(c.builtin(CoreObject::NoneType));
        return;
    }
    RootScope roots(c);
    roots.root(bound);
    ReturnSlot message(c);
    make_str(c, U"Bound must be a type.", message);
    const std::array args{bound, message.value()};
    typing_call(c, U"_type_check", {args, {}}, out);
}

template <bool ParamSpec>
void parameter_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto id = ParamSpec ? CoreObject::ParamSpecType : CoreObject::TypeVarType;
    if (args[0] != c.builtin(id) || !valid_name(c, args[1], out)) {
        if (args[0] != c.builtin(id))
            raise_error(c, CoreObject::TypeErrorType, U"invalid type parameter class", out);
        return;
    }
    RootScope roots(c);
    auto name = roots.root(args[1]);
    std::array<bool, 3> flags;
    if (!variance(c, args.subspan(3, 3), flags, out))
        return;
    ReturnSlot bound(c);
    bound.ok(c.builtin(CoreObject::None));
    if constexpr (ParamSpec) {
        check_bound(c, args[2], bound);
        if (bound.tag() == ResultTag::Raised) {
            out.raised(bound.exception());
            return;
        }
        c.allocate_instance<ParamSpecObject>(roots.root(c.builtin(id)), out, name.get(c),
                                             bound.value(), flags[0], flags[1], flags[2]);
    } else {
        if (args[2] != c.builtin(CoreObject::None)) {
            check_bound(c, args[2], bound);
            if (bound.tag() == ResultTag::Raised) {
                out.raised(bound.exception());
                return;
            }
        }
        const auto count = c.payload<TupleObject>(args[6]).items.size();
        if (count == 1 || (count && args[2] != c.builtin(CoreObject::None))) {
            raise_error(c, CoreObject::TypeErrorType,
                        count == 1 ? U"A single constraint is not allowed"
                                   : U"Constraints cannot be combined with bound=...",
                        out);
            return;
        }
        c.allocate_instance<TypeVarObject>(roots.root(c.builtin(id)), out, name.get(c),
                                           bound.value(), std::optional<Value>{}, args[6],
                                           std::optional<Value>{}, flags[0], flags[1], flags[2]);
    }
    parameter_module(c, roots.root(out.value()), out);
}

void tuple_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != c.builtin(CoreObject::TypeVarTupleType)) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid TypeVarTuple class", out);
        return;
    }
    if (!valid_name(c, args[1], out))
        return;
    RootScope roots(c);
    c.allocate_instance<TypeVarTupleObject>(roots.root(args[0]), out, args[1]);
    parameter_module(c, roots.root(out.value()), out);
}

template <class T> void parameter_name(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<T>(self).name);
}
template <class T, const bool T::*Field>
void parameter_flag(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.builtin(c.payload<T>(self).*Field ? CoreObject::True : CoreObject::False));
}
template <bool Constraints> void lazy_field(Attachment &c, Value value, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(value);
    auto &object = c.payload<TypeVarObject>(self);
    auto &edge = Constraints ? object.constraints : object.bound;
    c.load(self.get(c), edge, out);
    if (out.tag() == ResultTag::Ok)
        return;
    auto evaluate = Constraints ? object.evaluate_constraints : object.evaluate_bound;
    if (!evaluate) {
        out.ok(c.builtin(Constraints ? CoreObject::EmptyTuple : CoreObject::None));
        return;
    }
    call(c, roots.root(*evaluate), {}, out);
    if (out.tag() == ResultTag::Ok && !c.compare_exchange(self.get(c), edge, {}, out.value()))
        c.load(self.get(c), edge, out);
}
void paramspec_bound(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<ParamSpecObject>(self).bound);
}
template <bool Keywords> void paramspec_view(Attachment &c, Value self, ReturnSlot &out)
{
    RootScope roots(c);
    detail::make_paramspec_attribute(c, roots.root(self), Keywords, out);
}
template <class T>
void parameter_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto &object = c.payload<T>(args[0]);
    if constexpr (std::is_same_v<T, TypeVarTupleObject>)
        out.ok(object.name);
    else if (object.infer_variance)
        out.ok(object.name);
    else
        make_str(c,
                 std::u32string(1, object.covariant       ? U'+'
                                   : object.contravariant ? U'-'
                                                          : U'~') +
                     c.payload<StrObject>(object.name).value,
                 out);
}
template <class T>
void parameter_reduce(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    parameter_name<T>(c, args[0], out);
}
void reject_base(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, U"Cannot subclass a type parameter instance", out);
}
template <bool ParamSpec, bool Prepare = false>
void substitute(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    typing_call(c,
                Prepare     ? U"_paramspec_prepare_subst"
                : ParamSpec ? U"_paramspec_subst"
                            : U"_typevar_subst",
                {args, {}}, out);
}
void tuple_substitute(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, U"Substitution of bare TypeVarTuple is not supported",
                out);
}
void tuple_prepare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    typing_call(c, U"_typevartuple_prepare_subst", {args, {}}, out);
}
template <bool Reverse>
void parameter_union(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const std::array values{args[Reverse ? 1 : 0], args[Reverse ? 0 : 1]};
    typing_call(c, U"_make_union", {values, {}}, out);
}
void unpack(Attachment &c, Handle self, ReturnSlot &out)
{
    RootScope roots(c);
    self = roots.root(self.get(c));
    import_module(c, U"typing", out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto module = roots.root(out.value());
    ReturnSlot key(c);
    make_str(c, U"Unpack", key);
    get_attribute(c, module, roots.root(key.value()), out);
    if (out.tag() == ResultTag::Ok)
        get_item(c, roots.root(out.value()), self, out);
}
void tuple_iter(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    unpack(c, roots.root(args[0]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    const std::array values{out.value()};
    cxxpython::core::make_tuple(c, values, out);
    get_iter(c, roots.root(out.value()), out);
}
template <bool Initialize>
void generic_policy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Value> values{args[0]};
    for (auto value : c.payload<TupleObject>(args[1]).items)
        values.push_back(value);
    DictView keywords(c, args[2]);
    std::vector<KeywordArg> named;
    for (const auto &entry : keywords.entries())
        named.push_back({entry.key, entry.value});
    typing_call(c, Initialize ? U"_generic_init_subclass" : U"_generic_class_getitem",
                {values, named}, out);
}
} // namespace

namespace detail {
void make_typevar(Attachment &c, Handle name, std::optional<Handle> bound,
                  std::optional<Handle> constraints, ReturnSlot &out)
{
    if (!valid_name(c, name.get(c), out))
        return;
    for (const auto &evaluate : {bound, constraints})
        if (evaluate && (c.type_of(*evaluate) != c.builtin(CoreObject::FunctionType) ||
                         !c.has_layout<FunctionObject>(evaluate->get(c)))) {
            raise_error(c, CoreObject::TypeErrorType, U"type variable evaluator must be a function",
                        out);
            return;
        }
    RootScope roots(c);
    c.allocate_instance<TypeVarObject>(
        roots.root(c.builtin(CoreObject::TypeVarType)), out, name.get(c), std::optional<Value>{},
        bound ? std::optional{bound->get(c)} : std::nullopt, std::optional<Value>{},
        constraints ? std::optional{constraints->get(c)} : std::nullopt, false, false, true);
    parameter_module(c, roots.root(out.value()), out);
}
void make_paramspec(Attachment &c, Handle name, ReturnSlot &out)
{
    if (!valid_name(c, name.get(c), out))
        return;
    RootScope roots(c);
    c.allocate_instance<ParamSpecObject>(roots.root(c.builtin(CoreObject::ParamSpecType)), out,
                                         name.get(c), c.builtin(CoreObject::None), false, false,
                                         true);
    parameter_module(c, roots.root(out.value()), out);
}
void make_typevartuple(Attachment &c, Handle name, ReturnSlot &out)
{
    if (!valid_name(c, name.get(c), out))
        return;
    RootScope roots(c);
    c.allocate_instance<TypeVarTupleObject>(roots.root(c.builtin(CoreObject::TypeVarTupleType)),
                                            out, name.get(c));
    parameter_module(c, roots.root(out.value()), out);
}
void unpack_typevartuples(Attachment &c, Handle tuple, ReturnSlot &out)
{
    RootScope roots(c);
    tuple = roots.root(tuple.get(c));
    std::vector<Value> values;
    bool changed = false;
    for (auto value : c.payload<TupleObject>(tuple).items) {
        if (c.type_of(value) == c.builtin(CoreObject::TypeVarTupleType)) {
            unpack(c, roots.root(value), out);
            if (out.tag() == ResultTag::Raised)
                return;
            value = out.value();
            changed = true;
        }
        values.push_back(roots.root(value).get(c));
    }
    if (changed)
        cxxpython::core::make_tuple(c, values, out);
    else
        out.ok(tuple.get(c));
}
void make_generic_base(Attachment &c, Handle tuple, ReturnSlot &out)
{
    unpack_typevartuples(c, tuple, out);
    if (out.tag() == ResultTag::Raised)
        return;
    const std::array args{c.builtin(CoreObject::GenericType), out.value()};
    typing_call(c, U"_GenericAlias", {args, {}}, out);
}
void install_typevars(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(c);
    auto install = [&](Handle owner, const std::u32string &name, SignatureLayout sig,
                       NativeEntry entry, bool classmethod = false) {
        install_native_attribute(c, core, owner, name, std::move(sig), entry);
        if (name == U"__new__" || classmethod) {
            ReturnSlot temporary(c);
            auto dictionary = *TypeView(c, owner).dictionary();
            auto method = roots.root(*name_lookup(c, dictionary, name));
            if (classmethod)
                c.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), temporary,
                                                       method.get(c));
            else
                c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                        method.get(c));
            auto wrapper = roots.root(temporary.value());
            c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, name);
            name_store(c, dictionary, temporary.value(), wrapper.get(c));
        }
    };
    SignatureLayout unary, binary, ternary, constructor;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    binary = unary;
    binary.parameters.push_back({U"arg", ParameterKind::PositionalOnly});
    ternary = binary;
    ternary.parameters.push_back({U"args", ParameterKind::PositionalOnly});
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"name", ParameterKind::PositionalOrKeyword}};
    auto tv = at(CoreObject::TypeVarType), ps = at(CoreObject::ParamSpecType),
         ts = at(CoreObject::TypeVarTupleType);
    install(ts, U"__new__", constructor, tuple_new);
    constructor.parameters.push_back(
        {U"bound", ParameterKind::KeywordOnly, at(CoreObject::None).get(c)});
    for (auto name : {U"covariant", U"contravariant", U"infer_variance"})
        constructor.parameters.push_back(
            {name, ParameterKind::KeywordOnly, at(CoreObject::False).get(c)});
    auto typevar = constructor;
    typevar.varargs = U"constraints";
    install(tv, U"__new__", typevar, parameter_new<false>);
    install(ps, U"__new__", constructor, parameter_new<true>);
    auto fields = [&]<class T>(Handle owner) {
        install_native_field(c, core, owner, U"__name__", parameter_name<T>);
        install_native_field(c, core, owner, U"__covariant__", parameter_flag<T, &T::covariant>);
        install_native_field(c, core, owner, U"__contravariant__",
                             parameter_flag<T, &T::contravariant>);
        install_native_field(c, core, owner, U"__infer_variance__",
                             parameter_flag<T, &T::infer_variance>);
        install(owner, U"__repr__", unary, parameter_repr<T>);
        install(owner, U"__reduce__", unary, parameter_reduce<T>);
        install(owner, U"__mro_entries__", binary, reject_base);
        install(owner, U"__or__", binary, parameter_union<false>);
        install(owner, U"__ror__", binary, parameter_union<true>);
    };
    fields.template operator()<TypeVarObject>(tv);
    fields.template operator()<ParamSpecObject>(ps);
    install_native_field(c, core, tv, U"__bound__", lazy_field<false>);
    install_native_field(c, core, tv, U"__constraints__", lazy_field<true>);
    install_native_field(c, core, ps, U"__bound__", paramspec_bound);
    install_native_field(c, core, ps, U"args", paramspec_view<false>);
    install_native_field(c, core, ps, U"kwargs", paramspec_view<true>);
    install(tv, U"__typing_subst__", binary, substitute<false>);
    install(ps, U"__typing_subst__", binary, substitute<true>);
    install(ps, U"__typing_prepare_subst__", ternary, substitute<true, true>);
    install_native_field(c, core, ts, U"__name__", parameter_name<TypeVarTupleObject>);
    install(ts, U"__repr__", unary, parameter_repr<TypeVarTupleObject>);
    install(ts, U"__reduce__", unary, parameter_reduce<TypeVarTupleObject>);
    install(ts, U"__iter__", unary, tuple_iter);
    install(ts, U"__mro_entries__", binary, reject_base);
    install(ts, U"__typing_subst__", binary, tuple_substitute);
    install(ts, U"__typing_prepare_subst__", ternary, tuple_prepare);
    auto generic = unary;
    generic.varargs = U"args";
    generic.varkwargs = U"kwargs";
    install(at(CoreObject::GenericType), U"__init_subclass__", generic, generic_policy<true>, true);
    install(at(CoreObject::GenericType), U"__class_getitem__", generic, generic_policy<false>,
            true);
    install_paramspec_views(c, core);
}
} // namespace detail
} // namespace cxxpython::core
