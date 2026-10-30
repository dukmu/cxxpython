// Based on CPython 3.12.14 Objects/genericaliasobject.c (PSF license).
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"

#include <array>
#include <bit>

namespace cxxpython::core {
namespace {
bool alias_receiver(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.has_layout<GenericAliasObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::GenericAliasType)))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"descriptor requires a GenericAlias", out);
    return false;
}

void allocate_alias(Attachment &context, Handle type, Handle origin, Handle arguments,
                    ReturnSlot &out, bool starred = false)
{
    RootScope roots(context);
    type = roots.root(type.get(context));
    origin = roots.root(origin.get(context));
    arguments = roots.root(arguments.get(context));
    ReturnSlot tuple(context);
    if (context.has_layout<TupleObject>(arguments.get(context)) &&
        is_subtype(context, context.type_of(arguments.get(context)),
                   context.builtin(CoreObject::TupleType))) {
        tuple.ok(arguments.get(context));
    } else {
        const std::array values{arguments.get(context)};
        cxxpython::core::make_tuple(context, values, tuple);
    }
    auto items = roots.root(tuple.value());
    context.allocate_instance<GenericAliasObject>(type, out, origin.get(context),
                                                  items.get(context), starred);
}

void alias_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::GenericAliasType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"GenericAlias.__new__ requires a GenericAlias subtype", out);
        return;
    }
    RootScope roots(context);
    allocate_alias(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}

template <const Value GenericAliasObject::*Field>
void alias_field(Attachment &context, Value self, ReturnSlot &out)
{
    if (alias_receiver(context, self, out))
        out.ok(context.payload<GenericAliasObject>(self).*Field);
}

// AttributeError means absent; other exceptions are propagated unchanged.
bool optional_attribute(Attachment &context, Handle object, std::u32string name, ReturnSlot &out)
{
    RootScope roots(context);
    object = roots.root(object.get(context));
    ReturnSlot key(context);
    make_str(context, std::move(name), key);
    get_attribute(context, object, roots.root(key.value()), out);
    if (out.tag() == ResultTag::Raised &&
        is_subtype(context, context.type_of(out.exception()),
                   context.builtin(CoreObject::AttributeErrorType))) {
        out.reset();
    }
    return out.tag() != ResultTag::Raised;
}

void alias_parameters(Attachment &context, Value value, ReturnSlot &out)
{
    if (!alias_receiver(context, value, out))
        return;
    RootScope roots(context);
    auto self = roots.root(value);
    auto &alias = context.payload<GenericAliasObject>(self);
    context.load(self.get(context), alias.parameters, out);
    if (out.tag() == ResultTag::Ok)
        return;
    auto arguments = roots.root(alias.args);
    std::vector<Value> parameters;
    auto append = [&](Value item) {
        for (auto existing : parameters)
            if (existing == item)
                return;
        roots.root(item);
        parameters.push_back(item);
    };
    ReturnSlot attribute(context);
    for (auto item : context.payload<TupleObject>(arguments).items) {
        if (context.has_layout<TypeObject>(item))
            continue;
        auto candidate = roots.root(item);
        if (!optional_attribute(context, candidate, U"__typing_subst__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok) {
            append(item);
            continue;
        }
        if (!optional_attribute(context, candidate, U"__parameters__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok &&
            context.has_layout<TupleObject>(attribute.value()) &&
            is_subtype(context, context.type_of(attribute.value()),
                       context.builtin(CoreObject::TupleType))) {
            auto nested = roots.root(attribute.value());
            for (auto parameter : context.payload<TupleObject>(nested).items)
                append(parameter);
        }
    }
    cxxpython::core::make_tuple(context, parameters, out);
    if (!context.compare_exchange(self.get(context), alias.parameters, {}, out.value()))
        context.load(self.get(context), alias.parameters, out);
}

bool repr_item(Attachment &context, Handle item, std::u32string &text, ReturnSlot &out)
{
    RootScope roots(context);
    item = roots.root(item.get(context));
    if (item.get(context) == context.builtin(CoreObject::Ellipsis)) {
        text += U"...";
        return true;
    }
    ReturnSlot origin(context), arguments(context), name(context), module(context),
        rendered(context);
    if (!optional_attribute(context, item, U"__origin__", origin)) {
        out.raised(origin.exception());
        return false;
    }
    bool generic = false;
    if (origin.tag() == ResultTag::Ok) {
        if (!optional_attribute(context, item, U"__args__", arguments)) {
            out.raised(arguments.exception());
            return false;
        }
        generic = arguments.tag() == ResultTag::Ok;
    }
    if (!generic) {
        if (!optional_attribute(context, item, U"__qualname__", name)) {
            out.raised(name.exception());
            return false;
        }
        if (name.tag() == ResultTag::Ok) {
            if (!optional_attribute(context, item, U"__module__", module)) {
                out.raised(module.exception());
                return false;
            }
            if (module.tag() == ResultTag::Ok &&
                module.value() != context.builtin(CoreObject::None)) {
                auto name_handle = roots.root(name.value());
                auto module_handle = roots.root(module.value());
                const bool builtin = context.has_layout<StrObject>(module_handle.get(context)) &&
                                     context.payload<StrObject>(module_handle).value == U"builtins";
                if (!builtin) {
                    str(context, module_handle, rendered);
                    if (rendered.tag() == ResultTag::Raised) {
                        out.raised(rendered.exception());
                        return false;
                    }
                    text += context.payload<StrObject>(rendered.value()).value;
                    text += U".";
                }
                str(context, name_handle, rendered);
                if (rendered.tag() == ResultTag::Raised) {
                    out.raised(rendered.exception());
                    return false;
                }
                text += context.payload<StrObject>(rendered.value()).value;
                return true;
            }
        }
    }
    repr(context, item, rendered);
    if (rendered.tag() == ResultTag::Raised) {
        out.raised(rendered.exception());
        return false;
    }
    text += context.payload<StrObject>(rendered.value()).value;
    return true;
}

void alias_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const auto &alias = context.payload<GenericAliasObject>(self);
    auto arguments = roots.root(alias.args);
    std::u32string text = alias.starred ? U"*" : U"";
    if (!repr_item(context, roots.root(alias.origin), text, out))
        return;
    text += U"[";
    const auto &items = context.payload<TupleObject>(arguments).items;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i)
            text += U", ";
        auto item = roots.root(items[i]);
        if (context.type_of(item.get(context)) == context.builtin(CoreObject::ListType)) {
            std::vector<Value> list;
            detail::list_snapshot(context, item, roots, list);
            text += U"[";
            for (std::size_t j = 0; j < list.size(); ++j) {
                if (j)
                    text += U", ";
                if (!repr_item(context, roots.root(list[j]), text, out))
                    return;
            }
            text += U"]";
        } else if (!repr_item(context, item, text, out)) {
            return;
        }
    }
    if (items.empty())
        text += U"()";
    make_str(context, text + U"]", out);
}

void alias_mro_entries(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const std::array values{context.payload<GenericAliasObject>(self).origin};
    cxxpython::core::make_tuple(context, values, out);
}

void alias_unpacked(Attachment &context, Value self, ReturnSlot &out)
{
    if (alias_receiver(context, self, out))
        out.ok(context.builtin(context.payload<GenericAliasObject>(self).starred
                                   ? CoreObject::True
                                   : CoreObject::False));
}

void alias_unpacked_tuple_args(Attachment &context, Value self, ReturnSlot &out)
{
    if (!alias_receiver(context, self, out))
        return;
    const auto &alias = context.payload<GenericAliasObject>(self);
    out.ok(alias.starred && alias.origin == context.builtin(CoreObject::TupleType)
               ? alias.args
               : context.builtin(CoreObject::None));
}

void alias_iterator_iter(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
void alias_iterator_next(Attachment &, Handle, std::span<const Value>, ReturnSlot &);
void alias_iterator_reduce(Attachment &, Handle, std::span<const Value>, ReturnSlot &);

void alias_iter(Attachment &context, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto iterator_type = roots.root(context.payload<NativeFunctionObject>(function).captures[0]);
    context.allocate_instance<GenericAliasIteratorObject>(iterator_type, out, self.get(context));
}

void alias_iterator_iter(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<GenericAliasIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"expected generic alias iterator", out);
        return;
    }
    out.ok(args[0]);
}

void alias_iterator_next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<GenericAliasIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"expected generic alias iterator", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto &iterator = context.payload<GenericAliasIteratorObject>(self);
    ReturnSlot loaded(context), result(context);
    context.load(self.get(context), iterator.alias, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        detail::stop_iteration(context, out);
        return;
    }
    auto value = roots.root(loaded.value());
    const auto &alias = context.payload<GenericAliasObject>(value);
    allocate_alias(context, roots.root(context.builtin(CoreObject::GenericAliasType)),
                   roots.root(alias.origin), roots.root(alias.args), result, true);
    if (!context.compare_exchange(self.get(context), iterator.alias, value.get(context), {})) {
        detail::stop_iteration(context, out);
        return;
    }
    out.ok(result.value());
}

void alias_iterator_reduce(Attachment &context, Handle, std::span<const Value> args,
                           ReturnSlot &out)
{
    if (!context.has_layout<GenericAliasIteratorObject>(args[0])) {
        raise_error(context, CoreObject::TypeErrorType, U"expected generic alias iterator", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    ReturnSlot contents(context), tuple(context), argument(context);
    context.load(self.get(context), context.payload<GenericAliasIteratorObject>(self).alias,
                 contents);
    if (contents.tag() == ResultTag::Ok) {
        const std::array values{contents.value()};
        cxxpython::core::make_tuple(context, values, argument);
    } else {
        cxxpython::core::make_tuple(context, std::span<const Value>{}, tuple);
        const std::array values{tuple.value()};
        cxxpython::core::make_tuple(context, values, argument);
    }
    const auto iter = detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"iter");
    if (!iter) {
        raise_error(context, CoreObject::NameErrorType, U"name 'iter' is not defined", out);
        return;
    }
    auto arguments = roots.root(argument.value());
    const std::array values{*iter, arguments.get(context)};
    cxxpython::core::make_tuple(context, values, out);
}

void alias_reduce(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const auto &alias = context.payload<GenericAliasObject>(self);
    ReturnSlot argument(context), ordinary(context), iterator(context);
    Value callable = context.type_of(self.get(context));
    if (alias.starred) {
        const auto next =
            detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"next");
        if (!next) {
            raise_error(context, CoreObject::NameErrorType, U"name 'next' is not defined", out);
            return;
        }
        callable = *next;
        roots.root(callable);
        allocate_alias(context, roots.root(context.builtin(CoreObject::GenericAliasType)),
                       roots.root(alias.origin), roots.root(alias.args), ordinary);
        get_iter(context, roots.root(ordinary.value()), iterator);
        if (iterator.tag() == ResultTag::Raised) {
            out.raised(iterator.exception());
            return;
        }
        const std::array values{iterator.value()};
        cxxpython::core::make_tuple(context, values, argument);
    } else {
        const std::array values{alias.origin, alias.args};
        cxxpython::core::make_tuple(context, values, argument);
    }
    auto argument_handle = roots.root(argument.value());
    const std::array values{callable, argument_handle.get(context)};
    cxxpython::core::make_tuple(context, values, out);
}

void alias_getattribute(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]), name = roots.root(args[1]);
    if (context.has_layout<StrObject>(name.get(context))) {
        const auto &text = context.payload<StrObject>(name).value;
        constexpr std::array<std::u32string_view, 11> exceptions{
            U"__class__",       U"__origin__",     U"__args__",
            U"__unpacked__",    U"__parameters__", U"__typing_unpacked_tuple_args__",
            U"__mro_entries__", U"__reduce_ex__",  U"__reduce__",
            U"__copy__",        U"__deepcopy__"};
        bool local = false;
        for (auto exception : exceptions)
            local = local || text == exception;
        if (!local) {
            auto origin = roots.root(context.payload<GenericAliasObject>(self).origin);
            get_attribute(context, origin, name, out);
            return;
        }
    }
    auto descriptor =
        roots.root(*detail::type_attribute(context, context.object_type(), U"__getattribute__"));
    const std::array values{name.get(context)};
    detail::invoke_special(context, self, descriptor, {values, {}}, out);
}

bool tuple_like(Attachment &context, Value value)
{
    return context.has_layout<TupleObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::TupleType));
}

std::optional<std::size_t> parameter_index(std::span<const Value> parameters, Value item)
{
    for (std::size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i] == item)
            return i;
    return {};
}

bool unpack_substitution(Attachment &context, Handle item, RootScope &roots,
                         std::vector<Value> &items, ReturnSlot &out)
{
    auto append = [&](Value value) {
        roots.root(value);
        items.push_back(value);
    };
    auto unpack_one = [&](Value value) {
        auto current = roots.root(value);
        if (!context.has_layout<TypeObject>(value)) {
            ReturnSlot unpacked(context);
            if (!optional_attribute(context, current, U"__typing_unpacked_tuple_args__",
                                    unpacked)) {
                out.raised(unpacked.exception());
                return false;
            }
            if (unpacked.tag() == ResultTag::Ok && tuple_like(context, unpacked.value())) {
                auto tuple = roots.root(unpacked.value());
                const auto &values = context.payload<TupleObject>(tuple).items;
                if (values.empty() || values.back() != context.builtin(CoreObject::Ellipsis)) {
                    for (auto nested : values)
                        append(nested);
                    return true;
                }
            }
        }
        append(value);
        return true;
    };
    if (tuple_like(context, item.get(context))) {
        for (auto value : context.payload<TupleObject>(item).items)
            if (!unpack_one(value))
                return false;
    } else if (!unpack_one(item.get(context))) {
        return false;
    }
    return true;
}

void alias_getitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]), item = roots.root(args[1]);
    const auto &alias = context.payload<GenericAliasObject>(self);
    auto original = roots.root(alias.args), origin = roots.root(alias.origin);
    ReturnSlot parameters_slot(context);
    alias_parameters(context, self.get(context), parameters_slot);
    if (parameters_slot.tag() == ResultTag::Raised) {
        out.raised(parameters_slot.exception());
        return;
    }
    auto parameters = roots.root(parameters_slot.value());
    const auto &params = context.payload<TupleObject>(parameters).items;
    if (params.empty()) {
        ReturnSlot description(context);
        alias_repr(context, {}, std::span<const Value>(&args[0], 1), description);
        if (description.tag() == ResultTag::Raised) {
            out.raised(description.exception());
            return;
        }
        raise_error(context, CoreObject::TypeErrorType,
                    context.payload<StrObject>(description.value()).value +
                        U" is not a generic class",
                    out);
        return;
    }
    std::vector<Value> unpacked;
    if (!unpack_substitution(context, item, roots, unpacked, out))
        return;
    ReturnSlot prepared(context), attribute(context);
    cxxpython::core::make_tuple(context, unpacked, prepared);
    for (auto param : params) {
        auto parameter = roots.root(param);
        if (!optional_attribute(context, parameter, U"__typing_prepare_subst__", attribute)) {
            out.raised(attribute.exception());
            return;
        }
        if (attribute.tag() == ResultTag::Ok &&
            attribute.value() != context.builtin(CoreObject::None)) {
            auto method = roots.root(attribute.value());
            auto current = roots.root(prepared.value());
            if (!tuple_like(context, current.get(context))) {
                const std::array single{current.get(context)};
                cxxpython::core::make_tuple(context, single, prepared);
                current = roots.root(prepared.value());
            }
            const std::array values{self.get(context), current.get(context)};
            call(context, method, {values, {}}, prepared);
            if (prepared.tag() == ResultTag::Raised) {
                out.raised(prepared.exception());
                return;
            }
        }
    }
    auto prepared_value = roots.root(prepared.value());
    std::vector<Value> replacements;
    if (tuple_like(context, prepared_value.get(context)))
        replacements = context.payload<TupleObject>(prepared_value).items;
    else
        replacements.push_back(prepared_value.get(context));
    for (auto value : replacements)
        roots.root(value);
    if (replacements.size() != params.size()) {
        ReturnSlot description(context);
        alias_repr(context, {}, std::span<const Value>(&args[0], 1), description);
        if (description.tag() == ResultTag::Raised) {
            out.raised(description.exception());
            return;
        }
        auto number = [](std::size_t n) {
            auto text = std::to_string(n);
            return std::u32string(text.begin(), text.end());
        };
        raise_error(
            context, CoreObject::TypeErrorType,
            U"Too " + std::u32string(replacements.size() > params.size() ? U"many" : U"few") +
                U" arguments for " + context.payload<StrObject>(description.value()).value +
                U"; actual " + number(replacements.size()) + U", expected " + number(params.size()),
            out);
        return;
    }
    std::vector<Value> result;
    auto append = [&](Value value) {
        roots.root(value);
        result.push_back(value);
    };
    for (auto value : context.payload<TupleObject>(original).items) {
        auto argument = roots.root(value);
        if (context.has_layout<TypeObject>(value)) {
            append(value);
            continue;
        }
        ReturnSlot unpack_flag(context), subst(context), transformed(context);
        if (!optional_attribute(context, argument, U"__typing_is_unpacked_typevartuple__",
                                unpack_flag)) {
            out.raised(unpack_flag.exception());
            return;
        }
        bool is_unpack = false;
        if (unpack_flag.tag() == ResultTag::Ok) {
            truth(context, roots.root(unpack_flag.value()), transformed);
            if (transformed.tag() == ResultTag::Raised) {
                out.raised(transformed.exception());
                return;
            }
            is_unpack = transformed.value() == context.builtin(CoreObject::True);
        }
        if (!optional_attribute(context, argument, U"__typing_subst__", subst)) {
            out.raised(subst.exception());
            return;
        }
        if (subst.tag() == ResultTag::Ok) {
            auto position = parameter_index(params, value);
            if (!position) {
                raise_error(context, CoreObject::TypeErrorType,
                            U"type parameter missing from parameters", out);
                return;
            }
            auto method = roots.root(subst.value());
            const std::array values{replacements[*position]};
            call(context, method, {values, {}}, transformed);
        } else {
            ReturnSlot nested_slot(context);
            if (!optional_attribute(context, argument, U"__parameters__", nested_slot)) {
                out.raised(nested_slot.exception());
                return;
            }
            if (nested_slot.tag() == ResultTag::Ok && tuple_like(context, nested_slot.value()) &&
                !context.payload<TupleObject>(nested_slot.value()).items.empty()) {
                auto nested = roots.root(nested_slot.value());
                std::vector<Value> subargs;
                for (auto parameter : context.payload<TupleObject>(nested).items) {
                    auto position = parameter_index(params, parameter);
                    auto replacement = position ? replacements[*position] : parameter;
                    auto parameter_type = roots.root(context.type_of(parameter));
                    auto iter = resolve_protocol(context, parameter_type, Protocol::Iter, roots);
                    if (position && tuple_like(context, replacement) &&
                        iter.state != SlotState::Missing && iter.state != SlotState::Disabled) {
                        auto tuple = roots.root(replacement);
                        for (auto part : context.payload<TupleObject>(tuple).items)
                            subargs.push_back(part);
                    } else {
                        subargs.push_back(replacement);
                    }
                }
                ReturnSlot tuple(context);
                cxxpython::core::make_tuple(context, subargs, tuple);
                get_item(context, argument, roots.root(tuple.value()), transformed);
            } else {
                transformed.ok(value);
            }
        }
        if (transformed.tag() == ResultTag::Raised) {
            out.raised(transformed.exception());
            return;
        }
        auto newarg = roots.root(transformed.value());
        if (is_unpack && tuple_like(context, newarg.get(context))) {
            for (auto part : context.payload<TupleObject>(newarg).items)
                append(part);
        } else if (is_unpack) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"unpacked type variable must return a tuple", out);
            return;
        } else {
            append(newarg.get(context));
        }
    }
    ReturnSlot tuple(context);
    cxxpython::core::make_tuple(context, result, tuple);
    allocate_alias(context, roots.root(context.builtin(CoreObject::GenericAliasType)), origin,
                   roots.root(tuple.value()), out, alias.starred);
}

template <bool Instance>
void alias_check(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    raise_error(context, CoreObject::TypeErrorType,
                Instance ? U"isinstance() argument 2 cannot be a parameterized generic"
                         : U"issubclass() argument 2 cannot be a parameterized generic",
                out);
}

template <bool Negate>
void alias_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    if (!context.has_layout<GenericAliasObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]),
                    context.builtin(CoreObject::GenericAliasType))) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(context);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    const auto &a = context.payload<GenericAliasObject>(left);
    const auto &b = context.payload<GenericAliasObject>(right);
    if (a.starred != b.starred) {
        out.ok(context.builtin(Negate ? CoreObject::True : CoreObject::False));
        return;
    }
    ReturnSlot comparison(context);
    container_equal(context, roots.root(a.origin), roots.root(b.origin), comparison);
    if (comparison.tag() == ResultTag::Raised) {
        out.raised(comparison.exception());
        return;
    }
    if (comparison.value() == context.builtin(CoreObject::False)) {
        out.ok(context.builtin(Negate ? CoreObject::True : CoreObject::False));
        return;
    }
    rich_compare(context, roots.root(a.args), roots.root(b.args),
                 Negate ? CompareOp::Ne : CompareOp::Eq, out);
}

void alias_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    const auto &alias = context.payload<GenericAliasObject>(self);
    ReturnSlot first(context), second(context);
    hash(context, roots.root(alias.origin), first);
    if (first.tag() == ResultTag::Raised) {
        out.raised(first.exception());
        return;
    }
    hash(context, roots.root(alias.args), second);
    if (second.tag() == ResultTag::Raised) {
        out.raised(second.exception());
        return;
    }
    std::int64_t a = 0, b = 0;
    (void)int_fits_signed(context, first.value(), a);
    (void)int_fits_signed(context, second.value(), b);
    auto value = std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a) ^
                                             std::bit_cast<std::uint64_t>(b));
    make_int(context, value == -1 ? -2 : value, out);
}

void alias_call(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!alias_receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto positional = roots.root(args[1]);
    auto keywords = roots.root(args[2]);
    auto origin = roots.root(context.payload<GenericAliasObject>(self).origin);
    DictView view(context, keywords.get(context));
    std::vector<KeywordArg> named;
    for (const auto &entry : view.entries()) {
        roots.root(entry.key);
        roots.root(entry.value);
        named.push_back({entry.key, entry.value});
    }
    ReturnSlot result(context);
    call(context, origin, {context.payload<TupleObject>(positional).items, named}, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    auto instance = roots.root(result.value());
    ReturnSlot name(context), assigned(context);
    make_str(context, U"__orig_class__", name);
    set_attribute(context, instance, roots.root(name.value()), self, assigned);
    if (assigned.tag() == ResultTag::Raised &&
        !is_subtype(context, context.type_of(assigned.exception()),
                    context.builtin(CoreObject::AttributeErrorType)) &&
        !is_subtype(context, context.type_of(assigned.exception()),
                    context.builtin(CoreObject::TypeErrorType))) {
        out.raised(assigned.exception());
        return;
    }
    out.ok(instance.get(context));
}
} // namespace

void make_generic_alias(Attachment &context, Handle origin, Handle arguments, ReturnSlot &out)
{
    RootScope roots(context);
    allocate_alias(context, roots.root(context.builtin(CoreObject::GenericAliasType)), origin,
                   arguments, out);
}

namespace detail {
void install_genericalias(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::GenericAliasType);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"origin", ParameterKind::PositionalOnly},
                              {U"args", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, owner, U"__new__", constructor, alias_new);
    RootScope roots(context);
    ReturnSlot temporary(context);
    auto dictionary = *TypeView(context, owner).dictionary();
    auto function = roots.root(*name_lookup(context, dictionary, U"__new__"));
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                  function.get(context));
    auto wrapper = roots.root(temporary.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    name_store(context, dictionary, temporary.value(), wrapper.get(context));

    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, owner, U"__hash__", unary, alias_hash);
    install_native_attribute(context, core, owner, U"__repr__", unary, alias_repr);
    auto iterator_type = at(CoreObject::GenericAliasIteratorType);
    install_native_attribute(context, core, iterator_type, U"__iter__", unary, alias_iterator_iter);
    install_native_attribute(context, core, iterator_type, U"__next__", unary, alias_iterator_next);
    install_native_attribute(context, core, iterator_type, U"__reduce__", unary,
                             alias_iterator_reduce);
    install_native_attribute(context, core, owner, U"__iter__", unary, alias_iter,
                             {iterator_type.get(context)});
    SignatureLayout callable = unary;
    callable.varargs = U"args";
    callable.varkwargs = U"kwargs";
    install_native_attribute(context, core, owner, U"__call__", callable, alias_call);
    install_native_attribute(context, core, owner, U"__reduce__", unary, alias_reduce);
    install_native_attribute(context, core, owner, U"__eq__", pair, alias_compare<false>);
    install_native_attribute(context, core, owner, U"__ne__", pair, alias_compare<true>);
    install_native_attribute(context, core, owner, U"__mro_entries__", pair, alias_mro_entries);
    install_native_attribute(context, core, owner, U"__getitem__", pair, alias_getitem);
    install_native_attribute(context, core, owner, U"__getattribute__", pair, alias_getattribute);
    install_native_attribute(context, core, owner, U"__instancecheck__", pair, alias_check<true>);
    install_native_attribute(context, core, owner, U"__subclasscheck__", pair, alias_check<false>);
    install_native_field(context, core, owner, U"__origin__",
                         alias_field<&GenericAliasObject::origin>);
    install_native_field(context, core, owner, U"__args__", alias_field<&GenericAliasObject::args>);
    install_native_field(context, core, owner, U"__parameters__", alias_parameters);
    install_native_field(context, core, owner, U"__unpacked__", alias_unpacked);
    install_native_field(context, core, owner, U"__typing_unpacked_tuple_args__",
                         alias_unpacked_tuple_args);
}
} // namespace detail
} // namespace cxxpython::core
