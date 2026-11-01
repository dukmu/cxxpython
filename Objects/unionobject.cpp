// Adapted from CPython 3.12.14 Objects/unionobject.c and the parameter
// substitution helpers in Objects/genericaliasobject.c (PSF license).
#include "cxxpython/unionobject.hpp"

#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "cxxpython/type.hpp"
#include "cxxpython/typealiasobject.hpp"

#include <array>

namespace cxxpython::core {
namespace {
bool failed(const ReturnSlot &result)
{
    return result.tag() == ResultTag::Raised;
}

bool is_union(Attachment &ctx, Value value)
{
    return ctx.has_layout<UnionObject>(value) &&
           ctx.type_of(value) == ctx.builtin(CoreObject::UnionType);
}

bool is_alias(Attachment &ctx, Value value)
{
    return ctx.has_layout<GenericAliasObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::GenericAliasType));
}

bool is_tuple(Attachment &ctx, Value value)
{
    return ctx.has_layout<TupleObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::TupleType));
}

bool receiver(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (is_union(ctx, value))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"descriptor requires a types.UnionType", out);
    return false;
}

bool optional_attribute(Attachment &ctx, Handle object, std::u32string name, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, std::move(name), key);
    get_attribute(ctx, object, roots.root(key.value()), out);
    if (failed(out) &&
        is_subtype(ctx, ctx.type_of(out.exception()), ctx.builtin(CoreObject::AttributeErrorType)))
        out.reset();
    return !failed(out);
}

void append_rooted(RootScope &roots, std::vector<Value> &values, Value value)
{
    roots.root(value);
    values.push_back(value);
}

void append_tuple(Attachment &ctx, RootScope &roots, std::vector<Value> &values, Handle tuple)
{
    for (auto value : ctx.payload<TupleObject>(tuple).items)
        append_rooted(roots, values, value);
}

std::size_t identity_index(std::span<const Value> values, Value value)
{
    for (std::size_t i = 0; i < values.size(); ++i)
        if (values[i] == value)
            return i;
    return values.size();
}

void union_args(Attachment &ctx, Value self, ReturnSlot &out)
{
    if (receiver(ctx, self, out))
        out.ok(ctx.payload<UnionObject>(self).args);
}

void union_parameters(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (!receiver(ctx, value, out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(value);
    auto &object = ctx.payload<UnionObject>(self);
    ctx.load(self.get(ctx), object.parameters, out);
    if (out.tag() == ResultTag::Ok)
        return;
    auto args = roots.root(object.args);
    std::vector<Value> parameters;
    auto append = [&](Value item) {
        if (identity_index(parameters, item) == parameters.size())
            append_rooted(roots, parameters, item);
    };
    ReturnSlot attribute(ctx);
    for (auto item : ctx.payload<TupleObject>(args).items) {
        if (ctx.has_layout<TypeObject>(item))
            continue;
        auto candidate = roots.root(item);
        if (!optional_attribute(ctx, candidate, U"__typing_subst__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok) {
            append(item);
            continue;
        }
        if (!optional_attribute(ctx, candidate, U"__parameters__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok && is_tuple(ctx, attribute.value())) {
            auto nested = roots.root(attribute.value());
            for (auto parameter : ctx.payload<TupleObject>(nested).items)
                append(parameter);
        }
    }
    cxxpython::core::make_tuple(ctx, parameters, out);
    if (!ctx.compare_exchange(self.get(ctx), object.parameters, {}, out.value()))
        ctx.load(self.get(ctx), object.parameters, out);
}

bool repr_item(Attachment &ctx, Handle item, std::u32string &text, ReturnSlot &out)
{
    if (item.get(ctx) == ctx.builtin(CoreObject::NoneType)) {
        text += U"None";
        return true;
    }
    RootScope roots(ctx);
    ReturnSlot origin(ctx), args(ctx), name(ctx), module(ctx), rendered(ctx);
    if (!optional_attribute(ctx, item, U"__origin__", origin)) {
        out.raised(origin.exception());
        return false;
    }
    bool generic = false;
    if (origin.tag() == ResultTag::Ok) {
        if (!optional_attribute(ctx, item, U"__args__", args)) {
            out.raised(args.exception());
            return false;
        }
        generic = args.tag() == ResultTag::Ok;
    }
    if (!generic) {
        if (!optional_attribute(ctx, item, U"__qualname__", name)) {
            out.raised(name.exception());
            return false;
        }
        if (name.tag() == ResultTag::Ok) {
            if (!optional_attribute(ctx, item, U"__module__", module)) {
                out.raised(module.exception());
                return false;
            }
            if (module.tag() == ResultTag::Ok && module.value() != ctx.builtin(CoreObject::None)) {
                bool builtin = ctx.has_layout<StrObject>(module.value()) &&
                               ctx.payload<StrObject>(module.value()).value == U"builtins";
                if (!builtin) {
                    str(ctx, roots.root(module.value()), rendered);
                    if (failed(rendered)) {
                        out.raised(rendered.exception());
                        return false;
                    }
                    text += ctx.payload<StrObject>(rendered.value()).value;
                    text += U".";
                }
                str(ctx, roots.root(name.value()), rendered);
                if (failed(rendered)) {
                    out.raised(rendered.exception());
                    return false;
                }
                text += ctx.payload<StrObject>(rendered.value()).value;
                return true;
            }
        }
    }
    repr(ctx, item, rendered);
    if (failed(rendered)) {
        out.raised(rendered.exception());
        return false;
    }
    text += ctx.payload<StrObject>(rendered.value()).value;
    return true;
}

void union_repr(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto arguments = roots.root(ctx.payload<UnionObject>(self).args);
    std::u32string text;
    bool first = true;
    for (auto item : ctx.payload<TupleObject>(arguments).items) {
        if (!first)
            text += U" | ";
        first = false;
        if (!repr_item(ctx, roots.root(item), text, out))
            return;
    }
    make_str(ctx, std::move(text), out);
}

template <bool Negate>
void union_compare(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    if (!is_union(ctx, args[1])) {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(ctx);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    auto a = roots.root(ctx.payload<UnionObject>(left).args);
    auto b = roots.root(ctx.payload<UnionObject>(right).args);
    ReturnSlot first(ctx), second(ctx);
    make_set(ctx, ctx.payload<TupleObject>(a).items, first);
    if (failed(first)) {
        out.raised(first.exception());
        return;
    }
    make_set(ctx, ctx.payload<TupleObject>(b).items, second);
    if (failed(second)) {
        out.raised(second.exception());
        return;
    }
    rich_compare(ctx, roots.root(first.value()), roots.root(second.value()),
                 Negate ? CompareOp::Ne : CompareOp::Eq, out);
}

void union_hash(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto arguments = roots.root(ctx.payload<UnionObject>(self).args);
    ReturnSlot set(ctx), frozen(ctx);
    make_set(ctx, ctx.payload<TupleObject>(arguments).items, set);
    if (failed(set)) {
        out.raised(set.exception());
        return;
    }
    auto table = roots.root(ctx.payload<SetObject>(set.value()).table);
    ctx.allocate_instance<SetObject>(roots.root(ctx.builtin(CoreObject::FrozenSetType)), frozen,
                                     table.get(ctx));
    hash(ctx, roots.root(frozen.value()), out);
}

template <bool Reverse>
void union_or(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    make_union(ctx, roots.root(args[Reverse ? 1 : 0]), roots.root(args[Reverse ? 0 : 1]), out);
}

void union_new(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(ctx, CoreObject::TypeErrorType, U"cannot create 'types.UnionType' instances", out);
}

bool unpack_arguments(Attachment &ctx, Handle item, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::vector<Value> source, values;
    if (is_tuple(ctx, item.get(ctx)))
        append_tuple(ctx, roots, source, item);
    else
        append_rooted(roots, source, item.get(ctx));
    ReturnSlot attribute(ctx);
    for (auto value : source) {
        if (!ctx.has_layout<TypeObject>(value)) {
            if (!optional_attribute(ctx, roots.root(value), U"__typing_unpacked_tuple_args__",
                                    attribute)) {
                out.raised(attribute.exception());
                return false;
            }
            if (attribute.tag() == ResultTag::Ok && is_tuple(ctx, attribute.value())) {
                auto tuple = roots.root(attribute.value());
                const auto &items = ctx.payload<TupleObject>(tuple).items;
                if (items.empty() || items.back() != ctx.builtin(CoreObject::Ellipsis)) {
                    append_tuple(ctx, roots, values, tuple);
                    continue;
                }
            }
        }
        append_rooted(roots, values, value);
    }
    cxxpython::core::make_tuple(ctx, values, out);
    return true;
}

bool substitute_nested(Attachment &ctx, Handle object, Handle parameters,
                       std::span<const Value> replacements, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot attribute(ctx), arguments(ctx);
    if (!optional_attribute(ctx, object, U"__parameters__", attribute)) {
        out.raised(attribute.exception());
        return false;
    }
    if (attribute.tag() != ResultTag::Ok || !is_tuple(ctx, attribute.value()) ||
        ctx.payload<TupleObject>(attribute.value()).items.empty()) {
        out.ok(object.get(ctx));
        return true;
    }
    auto nested = roots.root(attribute.value());
    const auto &params = ctx.payload<TupleObject>(parameters).items;
    std::vector<Value> substituted;
    for (auto parameter : ctx.payload<TupleObject>(nested).items) {
        auto index = identity_index(params, parameter);
        Value value = index == params.size() ? parameter : replacements[index];
        if (index != params.size() && is_tuple(ctx, value) &&
            detail::type_attribute(ctx, ctx.type_of(parameter), U"__iter__")) {
            append_tuple(ctx, roots, substituted, roots.root(value));
        } else {
            append_rooted(roots, substituted, value);
        }
    }
    cxxpython::core::make_tuple(ctx, substituted, arguments);
    get_item(ctx, object, roots.root(arguments.value()), out);
    return !failed(out);
}

void substitution_error(Attachment &ctx, Handle self, const std::u32string &prefix,
                        const std::u32string &suffix, ReturnSlot &out)
{
    ReturnSlot rendered(ctx);
    repr(ctx, self, rendered);
    if (failed(rendered)) {
        out.raised(rendered.exception());
        return;
    }
    raise_error(ctx, CoreObject::TypeErrorType,
                prefix + ctx.payload<StrObject>(rendered.value()).value + suffix, out);
}

std::u32string decimal(std::size_t value)
{
    const auto text = std::to_string(value);
    return {text.begin(), text.end()};
}

void union_getitem(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]), item = roots.root(args[1]);
    auto original = roots.root(ctx.payload<UnionObject>(self).args);
    ReturnSlot result(ctx), prepared(ctx), attribute(ctx), converted(ctx);
    union_parameters(ctx, self.get(ctx), result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto parameters = roots.root(result.value());
    const auto &params = ctx.payload<TupleObject>(parameters).items;
    if (params.empty()) {
        substitution_error(ctx, self, U"", U" is not a generic class", out);
        return;
    }
    if (!unpack_arguments(ctx, item, prepared)) {
        out.raised(prepared.exception());
        return;
    }
    for (auto parameter : params) {
        if (!optional_attribute(ctx, roots.root(parameter), U"__typing_prepare_subst__",
                                attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() != ResultTag::Ok || attribute.value() == ctx.builtin(CoreObject::None))
            continue;
        auto prepare = roots.root(attribute.value());
        auto arguments = roots.root(prepared.value());
        if (!is_tuple(ctx, arguments.get(ctx))) {
            const std::array single{arguments.get(ctx)};
            cxxpython::core::make_tuple(ctx, single, result);
            arguments = roots.root(result.value());
        }
        const std::array values{self.get(ctx), arguments.get(ctx)};
        call(ctx, prepare, {values}, prepared);
        if (failed(prepared)) {
            out.raised(prepared.exception());
            return;
        }
    }
    auto replacements_owner = roots.root(prepared.value());
    std::vector<Value> replacements;
    if (is_tuple(ctx, replacements_owner.get(ctx)))
        append_tuple(ctx, roots, replacements, replacements_owner);
    else
        append_rooted(roots, replacements, replacements_owner.get(ctx));
    if (replacements.size() != params.size()) {
        substitution_error(ctx, self,
                           replacements.size() > params.size() ? U"Too many arguments for "
                                                               : U"Too few arguments for ",
                           U"; actual " + decimal(replacements.size()) + U", expected " +
                               decimal(params.size()),
                           out);
        return;
    }
    std::vector<Value> substituted;
    for (auto value : ctx.payload<TupleObject>(original).items) {
        if (ctx.has_layout<TypeObject>(value)) {
            append_rooted(roots, substituted, value);
            continue;
        }
        auto argument = roots.root(value);
        if (!optional_attribute(ctx, argument, U"__typing_is_unpacked_typevartuple__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        bool unpack = false;
        if (attribute.tag() == ResultTag::Ok) {
            truth(ctx, roots.root(attribute.value()), converted);
            if (failed(converted)) {
                out.raised(converted.exception());
                return;
            }
            unpack = converted.value() == ctx.builtin(CoreObject::True);
        }
        if (!optional_attribute(ctx, argument, U"__typing_subst__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok) {
            auto index = identity_index(params, value);
            if (index == params.size()) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"type parameter is not present in __parameters__", out);
                return;
            }
            const std::array values{replacements[index]};
            call(ctx, roots.root(attribute.value()), {values}, result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
        } else if (!substitute_nested(ctx, argument, parameters, replacements, result)) {
            out.raised(result.exception());
            return;
        }
        auto replacement = roots.root(result.value());
        if (unpack) {
            if (!is_tuple(ctx, replacement.get(ctx))) {
                raise_error(ctx, CoreObject::TypeErrorType,
                            U"unpacked type variable substitution must return a tuple", out);
                return;
            }
            append_tuple(ctx, roots, substituted, replacement);
        } else {
            append_rooted(roots, substituted, replacement.get(ctx));
        }
    }
    if (substituted.empty()) {
        cxxpython::core::make_tuple(ctx, substituted, result);
        ctx.allocate_instance<UnionObject>(roots.root(ctx.builtin(CoreObject::UnionType)), out,
                                           result.value());
        return;
    }
    result.ok(substituted[0]);
    for (std::size_t i = 1; i < substituted.size(); ++i) {
        auto left = roots.root(result.value());
        binary(ctx, left, roots.root(substituted[i]), BinaryOp::BitOr, result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
    }
    out.ok(result.value());
}

bool subclass_match(Attachment &ctx, Handle subject, Handle candidate, ReturnSlot &out,
                    unsigned depth = 0)
{
    if (subject.get(ctx) == candidate.get(ctx)) {
        out.ok(ctx.builtin(CoreObject::True));
        return true;
    }
    if (ctx.has_layout<TypeObject>(subject.get(ctx))) {
        out.ok(ctx.builtin(is_subtype(ctx, subject, candidate) ? CoreObject::True
                                                               : CoreObject::False));
        return true;
    }
    if (depth > 1000) {
        raise_error(ctx, CoreObject::RecursionErrorType, U"recursive class check", out);
        return false;
    }
    RootScope roots(ctx);
    ReturnSlot bases(ctx);
    if (!optional_attribute(ctx, subject, U"__bases__", bases)) {
        out.raised(bases.exception());
        return false;
    }
    if (bases.tag() != ResultTag::Ok || !is_tuple(ctx, bases.value())) {
        if (!depth) {
            raise_error(ctx, CoreObject::TypeErrorType, U"issubclass() arg 1 must be a class", out);
            return false;
        }
        out.ok(ctx.builtin(CoreObject::False));
        return true;
    }
    auto owner = roots.root(bases.value());
    for (auto base : ctx.payload<TupleObject>(owner).items) {
        if (!subclass_match(ctx, roots.root(base), candidate, out, depth + 1))
            return false;
        if (out.value() == ctx.builtin(CoreObject::True))
            return true;
    }
    out.ok(ctx.builtin(CoreObject::False));
    return true;
}

template <bool Instance>
void union_check(Attachment &ctx, Handle subject, Handle self, ReturnSlot &out)
{
    if (!receiver(ctx, self.get(ctx), out))
        return;
    RootScope roots(ctx);
    self = roots.root(self.get(ctx));
    subject = roots.root(subject.get(ctx));
    auto arguments = roots.root(ctx.payload<UnionObject>(self).args);
    ReturnSlot result(ctx), actual(ctx);
    for (auto value : ctx.payload<TupleObject>(arguments).items) {
        auto candidate = roots.root(value);
        if constexpr (Instance) {
            if (ctx.type_of(subject) == value) {
                out.ok(ctx.builtin(CoreObject::True));
                return;
            }
        }
        if (ctx.type_of(candidate) != ctx.type_type()) {
            auto hook =
                detail::type_attribute(ctx, ctx.type_of(candidate),
                                       Instance ? U"__instancecheck__" : U"__subclasscheck__");
            if (hook) {
                const std::array values{subject.get(ctx)};
                detail::invoke_special(ctx, candidate, roots.root(*hook), {values, {}}, result);
                if (failed(result)) {
                    out.raised(result.exception());
                    return;
                }
                truth(ctx, roots.root(result.value()), out);
                if (failed(out) || out.value() == ctx.builtin(CoreObject::True))
                    return;
                continue;
            }
        }
        if constexpr (!Instance) {
            if (!subclass_match(ctx, subject, candidate, out) ||
                out.value() == ctx.builtin(CoreObject::True))
                return;
        } else {
            if (is_subtype(ctx, ctx.type_of(subject), value)) {
                out.ok(ctx.builtin(CoreObject::True));
                return;
            }
            if (!optional_attribute(ctx, subject, U"__class__", actual)) {
                out.raised(actual.exception());
                return;
            }
            if (actual.tag() == ResultTag::Ok && ctx.has_layout<TypeObject>(actual.value()) &&
                is_subtype(ctx, actual.value(), value)) {
                out.ok(ctx.builtin(CoreObject::True));
                return;
            }
        }
    }
    out.ok(ctx.builtin(CoreObject::False));
}
} // namespace

void union_instancecheck(Attachment &ctx, Handle subject, Handle union_type, ReturnSlot &out)
{
    union_check<true>(ctx, subject, union_type, out);
}

void union_subclasscheck(Attachment &ctx, Handle subject, Handle union_type, ReturnSlot &out)
{
    union_check<false>(ctx, subject, union_type, out);
}

void make_union(Attachment &ctx, Handle left, Handle right, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto unionable = [&](Handle item) {
        auto value = item.get(ctx);
        return value == ctx.builtin(CoreObject::None) || ctx.has_layout<TypeObject>(value) ||
               is_alias(ctx, value) || is_union(ctx, value) ||
               ctx.type_of(value) == ctx.builtin(CoreObject::TypeAliasType);
    };
    if (!unionable(left) || !unionable(right)) {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return;
    }
    left = roots.root(left.get(ctx) == ctx.builtin(CoreObject::None)
                          ? ctx.builtin(CoreObject::NoneType)
                          : left.get(ctx));
    right = roots.root(right.get(ctx) == ctx.builtin(CoreObject::None)
                           ? ctx.builtin(CoreObject::NoneType)
                           : right.get(ctx));
    std::vector<Value> first, second;
    auto flatten = [&](Handle item, std::vector<Value> &values) {
        if (is_union(ctx, item.get(ctx)))
            append_tuple(ctx, roots, values, roots.root(ctx.payload<UnionObject>(item).args));
        else
            append_rooted(roots, values, item.get(ctx));
    };
    flatten(left, first);
    flatten(right, second);
    auto merged = first;
    ReturnSlot compared(ctx);
    for (auto item : second) {
        bool duplicate = false;
        // CPython compares against the original left tuple, not other new members.
        for (auto existing : first) {
            if (existing == item) {
                duplicate = true;
                break;
            }
            if (is_alias(ctx, existing) && is_alias(ctx, item)) {
                container_equal(ctx, roots.root(existing), roots.root(item), compared);
                if (failed(compared)) {
                    out.raised(compared.exception());
                    return;
                }
                if (compared.value() == ctx.builtin(CoreObject::True)) {
                    duplicate = true;
                    break;
                }
            }
        }
        if (!duplicate)
            merged.push_back(item);
    }
    if (merged.size() == first.size()) {
        out.ok(left.get(ctx));
        return;
    }
    ReturnSlot tuple(ctx);
    cxxpython::core::make_tuple(ctx, merged, tuple);
    ctx.allocate_instance<UnionObject>(roots.root(ctx.builtin(CoreObject::UnionType)), out,
                                       tuple.value());
}

namespace detail {
void install_union(Attachment &ctx, std::span<const Handle> core)
{
    RootScope roots(ctx);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::UnionType);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(ctx, core, owner, U"__repr__", unary, union_repr);
    install_native_attribute(ctx, core, owner, U"__hash__", unary, union_hash);
    install_native_attribute(ctx, core, owner, U"__eq__", pair, union_compare<false>);
    install_native_attribute(ctx, core, owner, U"__ne__", pair, union_compare<true>);
    install_native_attribute(ctx, core, owner, U"__or__", pair, union_or<false>);
    install_native_attribute(ctx, core, owner, U"__ror__", pair, union_or<true>);
    install_native_attribute(ctx, core, owner, U"__getitem__", pair, union_getitem);
    install_native_field(ctx, core, owner, U"__args__", union_args);
    install_native_field(ctx, core, owner, U"__parameters__", union_parameters);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(ctx, core, owner, U"__new__", constructor, union_new);
    auto dictionary = *TypeView(ctx, owner).dictionary();
    auto function = roots.root(*name_lookup(ctx, dictionary, U"__new__"));
    ReturnSlot wrapper(ctx), key(ctx);
    ctx.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                              function.get(ctx));
    ctx.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(ctx, dictionary, key.value(), wrapper.value());
}
} // namespace detail
} // namespace cxxpython::core
