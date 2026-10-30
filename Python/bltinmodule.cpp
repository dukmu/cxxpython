#include "cxxpython/execution.hpp"

namespace cxxpython::core {
namespace {
bool type_object(Attachment &context, Value value)
{
    return context.has_layout<TypeObject>(value) &&
           is_subtype(context, context.type_of(value), context.type_type());
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void attribute_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto &defaults = context.payload<TupleObject>(args[2]).items;
    if (defaults.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"getattr expected at most 3 arguments",
                    out);
        return;
    }
    get_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
    if (failed(out) && !defaults.empty() &&
        is_subtype(context, context.type_of(out.exception()),
                   context.builtin(CoreObject::AttributeErrorType)))
        out.ok(defaults[0]);
}
void attribute_has(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    get_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
    if (failed(out)) {
        if (!is_subtype(context, context.type_of(out.exception()),
                        context.builtin(CoreObject::AttributeErrorType)))
            return;
        out.ok(context.builtin(CoreObject::False));
    } else
        out.ok(context.builtin(CoreObject::True));
}
void attribute_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    set_attribute(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void attribute_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    del_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
}
void builtin_format(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    format(context, roots.root(args[0]), roots.root(args[1]), out);
}
void builtin_ascii(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ascii(context, roots.root(args[0]), out);
}
void mapping_store(Attachment &context, Handle mapping, std::u32string name, Value value,
                   ReturnSlot &out)
{
    RootScope roots(context);
    const auto item = roots.root(value);
    ReturnSlot key(context);
    make_str(context, std::move(name), key);
    if (context.type_of(mapping) == context.builtin(CoreObject::DictType)) {
        detail::name_store(context, mapping.get(context), key.value(), item.get(context));
        out.ok(context.builtin(CoreObject::None));
    } else
        set_item(context, mapping, roots.root(key.value()), item, out);
}
void build_class(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.builtin(CoreObject::FunctionType) ||
        !context.has_layout<FunctionObject>(args[0]) || !context.has_layout<StrObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::StrType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"__build_class__ requires a function and string name", out);
        return;
    }
    RootScope roots(context);
    const auto original_bases = roots.root(args[2]);
    ReturnSlot temporary(context), callback(context);
    std::vector<Value> bases;
    bool changed = false;
    for (auto base : context.payload<TupleObject>(original_bases).items) {
        const auto value = roots.root(base);
        if (!type_object(context, base)) {
            make_str(context, U"__mro_entries__", temporary);
            get_attribute(context, value, roots.root(temporary.value()), callback);
            if (failed(callback) && !is_subtype(context, context.type_of(callback.exception()),
                                                context.builtin(CoreObject::AttributeErrorType))) {
                out.raised(callback.exception());
                return;
            }
            if (!failed(callback)) {
                const std::array values{original_bases.get(context)};
                call(context, roots.root(callback.value()), {values, {}}, temporary);
                if (failed(temporary)) {
                    out.raised(temporary.exception());
                    return;
                }
                if (!context.has_layout<TupleObject>(temporary.value()) ||
                    !is_subtype(context, context.type_of(temporary.value()),
                                context.builtin(CoreObject::TupleType))) {
                    raise_error(context, CoreObject::TypeErrorType,
                                U"__mro_entries__ must return a tuple", out);
                    return;
                }
                for (auto item : context.payload<TupleObject>(temporary.value()).items)
                    bases.push_back(roots.root(item).get(context));
                changed = true;
                continue;
            }
        }
        bases.push_back(base);
    }
    Handle bases_tuple = original_bases;
    if (changed) {
        cxxpython::core::make_tuple(context, bases, temporary);
        bases_tuple = roots.root(temporary.value());
    }
    std::vector<KeywordArg> named;
    std::optional<Handle> explicit_meta;
    DictView keyword_args(context, args[3]);
    for (auto entry : keyword_args.entries()) {
        roots.root(entry.key);
        const auto value = roots.root(entry.value);
        if (context.payload<StrObject>(entry.key).value == U"metaclass")
            explicit_meta = value;
        else
            named.push_back({entry.key, value.get(context)});
    }
    auto meta = explicit_meta.value_or(
        roots.root(bases.empty() ? context.type_type() : context.type_of(bases.front())));
    if (type_object(context, meta.get(context))) {
        for (auto base : bases) {
            const auto candidate = roots.root(context.type_of(base));
            if (is_subtype(context, meta, candidate))
                continue;
            if (!is_subtype(context, candidate, meta)) {
                raise_error(context, CoreObject::TypeErrorType, U"metaclass conflict", out);
                return;
            }
            meta = candidate;
        }
    }
    make_str(context, U"__prepare__", temporary);
    get_attribute(context, meta, roots.root(temporary.value()), callback);
    if (failed(callback)) {
        if (!is_subtype(context, context.type_of(callback.exception()),
                        context.builtin(CoreObject::AttributeErrorType))) {
            out.raised(callback.exception());
            return;
        }
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              temporary);
    } else {
        const std::array values{args[1], bases_tuple.get(context)};
        call(context, roots.root(callback.value()), {values, named}, temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return;
        }
    }
    const auto mapping = roots.root(temporary.value());
    if (context.type_of(mapping) != context.builtin(CoreObject::DictType)) {
        const auto item = resolve_protocol(context, roots.root(context.type_of(mapping)),
                                           Protocol::GetItem, roots);
        if (item.state == SlotState::Missing) {
            raise_error(context, CoreObject::TypeErrorType, U"__prepare__ must return a mapping",
                        out);
            return;
        }
    }
    ReturnSlot body(context);
    detail::execute_class_body(context, roots.root(args[0]), mapping, body);
    if (failed(body)) {
        out.raised(body.exception());
        return;
    }
    const auto returned = roots.root(body.value());
    if (changed) {
        mapping_store(context, mapping, U"__orig_bases__", original_bases.get(context), temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return;
        }
    }
    const std::array values{args[1], bases_tuple.get(context), mapping.get(context)};
    call(context, meta, {values, named}, out);
    if (failed(out))
        return;
    if (type_object(context, out.value()) &&
        context.has_layout<CellObject>(returned.get(context))) {
        ReturnSlot cell(context);
        if (!read_cell(context, returned.get(context), cell))
            raise_error(context, CoreObject::RuntimeErrorType,
                        U"__class__ not set; was __classcell__ propagated to type.__new__?", out);
        else if (cell.value() != out.value())
            raise_error(context, CoreObject::TypeErrorType,
                        U"__class__ cell does not contain the created class", out);
    }
}
} // namespace
namespace detail {
void install_class_builtins(Attachment &context, std::span<const Handle> core, Value mapping)
{
    RootScope roots(context);
    ReturnSlot function(context), name(context);
    SignatureLayout signature;
    signature.parameters = {{U"func", ParameterKind::PositionalOnly},
                            {U"name", ParameterKind::PositionalOnly}};
    signature.varargs = U"bases";
    signature.varkwargs = U"keywords";
    context.allocate_instance<NativeFunctionObject>(
        core[static_cast<std::size_t>(CoreObject::NativeFunctionType)], function, "__build_class__",
        signature, build_class, std::vector<Value>{});
    context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], name,
                                         U"__build_class__");
    name_store(context, mapping, name.value(), function.value());
    auto install = [&](const char *label, std::u32string key, SignatureLayout layout,
                       NativeEntry entry) {
        context.allocate_instance<NativeFunctionObject>(
            core[static_cast<std::size_t>(CoreObject::NativeFunctionType)], function, label,
            std::move(layout), entry, std::vector<Value>{});
        context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)],
                                             name, std::move(key));
        name_store(context, mapping, name.value(), function.value());
    };
    SignatureLayout attributes;
    attributes.parameters = {{U"object", ParameterKind::PositionalOnly},
                             {U"name", ParameterKind::PositionalOnly}};
    install("hasattr", U"hasattr", attributes, attribute_has);
    install("delattr", U"delattr", attributes, attribute_delete);
    auto get = attributes;
    get.varargs = U"default";
    install("getattr", U"getattr", get, attribute_get);
    attributes.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    install("setattr", U"setattr", attributes, attribute_set);
    SignatureLayout ascii_signature;
    ascii_signature.parameters = {{U"object", ParameterKind::PositionalOnly}};
    install("ascii", U"ascii", ascii_signature, builtin_ascii);
    context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], name,
                                         U"");
    const auto empty_spec = roots.root(name.value());
    SignatureLayout format_signature;
    format_signature.parameters = {
        {U"value", ParameterKind::PositionalOnly},
        {U"format_spec", ParameterKind::PositionalOnly, empty_spec.get(context)}};
    install("format", U"format", format_signature, builtin_format);
}
} // namespace detail
} // namespace cxxpython::core
