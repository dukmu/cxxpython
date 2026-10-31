#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
bool is_module(Attachment &context, Value value)
{
    return context.has_layout<ModuleObject>(value) &&
           is_subtype(context, context.type_of(value), context.builtin(CoreObject::ModuleType));
}

Value module_dict(Attachment &context, Value value)
{
    if (!is_module(context, value))
        throw std::invalid_argument("module namespace requires a module");
    ReturnSlot result(context);
    context.load(value, *context.instance_dictionary(value), result);
    return result.value();
}

namespace {
bool receiver(Attachment &context, Value value, ReturnSlot &out)
{
    if (is_module(context, value))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"module descriptor requires a module", out);
    return false;
}
void initialize(Attachment &context, Handle module, Handle name, Value doc, ReturnSlot &out)
{
    RootScope roots(context);
    auto dictionary = roots.root(module_dict(context, module.get(context)));
    auto document = roots.root(doc);
    ReturnSlot key(context);
    for (auto [label, value] : {std::pair{U"__name__", name.get(context)},
                                {U"__doc__", document.get(context)},
                                {U"__package__", context.builtin(CoreObject::None)},
                                {U"__loader__", context.builtin(CoreObject::None)},
                                {U"__spec__", context.builtin(CoreObject::None)}}) {
        make_str(context, label, key);
        dict_store(context, dictionary, roots.root(key.value()), roots.root(value), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
}
void module_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(args[0]);
    if (!context.has_layout<TypeObject>(type.get(context)) ||
        !is_subtype(context, type.get(context), context.builtin(CoreObject::ModuleType))) {
        raise_error(context, CoreObject::TypeErrorType, U"module.__new__ requires a module type",
                    out);
        return;
    }
    context.allocate_instance<ModuleObject>(type, out);
    auto module = roots.root(out.value());
    ReturnSlot dictionary(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          dictionary);
    context.store(module.get(context), *context.instance_dictionary(module.get(context)),
                  dictionary.value());
    out.ok(module.get(context));
}
void module_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    if (!context.has_layout<StrObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::StrType))) {
        raise_error(context, CoreObject::TypeErrorType, U"module name must be a string", out);
        return;
    }
    RootScope roots(context);
    initialize(context, roots.root(args[0]), roots.root(args[1]), args[2], out);
}
void module_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto mapping = roots.root(module_dict(context, args[0]));
    make_str(context, U"__name__", out);
    dict_lookup(context, mapping, roots.root(out.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    std::u32string text = U"<module '?'>";
    if (out.tag() == ResultTag::Ok && context.has_layout<StrObject>(out.value()))
        text = U"<module '" + context.payload<StrObject>(out.value()).value + U"'>";
    make_str(context, std::move(text), out);
}
void module_getattr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto name = roots.root(args[1]);
    const auto generic =
        roots.root(*detail::type_attribute(context, context.object_type(), U"__getattribute__"));
    std::array arguments{name.get(context)};
    detail::invoke_special(context, self, generic, {arguments, {}}, out);
    if (out.tag() != ResultTag::Raised ||
        !is_subtype(context, context.type_of(out.exception()),
                    context.builtin(CoreObject::AttributeErrorType)))
        return;
    make_str(context, U"__getattr__", out);
    dict_lookup(context, roots.root(module_dict(context, self.get(context))),
                roots.root(out.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    if (out.tag() == ResultTag::Empty) {
        // Follow moduleobject.c: module __getattr__ has already had first
        // refusal; spec inspection is best-effort diagnostic context only.
        const auto dictionary = roots.root(module_dict(context, self.get(context)));
        ReturnSlot key(context), module_name(context), spec(context);
        make_str(context, U"__name__", key);
        dict_lookup(context, dictionary, roots.root(key.value()), module_name);
        if (module_name.tag() == ResultTag::Raised) {
            out.raised(module_name.exception());
            return;
        }
        const auto attribute = context.payload<StrObject>(name).value;
        std::u32string message = U"module has no attribute '" + attribute + U"'";
        if (module_name.tag() == ResultTag::Ok &&
            context.has_layout<StrObject>(module_name.value()) &&
            is_subtype(context, context.type_of(module_name.value()),
                       context.builtin(CoreObject::StrType))) {
            const auto label = context.payload<StrObject>(module_name.value()).value;
            make_str(context, U"__spec__", key);
            dict_lookup(context, dictionary, roots.root(key.value()), spec);
            if (spec.tag() == ResultTag::Raised) {
                out.raised(spec.exception());
                return;
            }
            auto spec_flag = [&](const char32_t *field, bool membership) {
                if (spec.tag() != ResultTag::Ok ||
                    spec.value() == context.builtin(CoreObject::None))
                    return false;
                ReturnSlot value(context), result(context);
                make_str(context, field, key);
                get_attribute(context, roots.root(spec.value()), roots.root(key.value()), value);
                if (value.tag() == ResultTag::Raised)
                    return false;
                if (membership)
                    rich_compare(context, name, roots.root(value.value()), CompareOp::In, result);
                else
                    truth(context, roots.root(value.value()), result);
                return result.tag() == ResultTag::Ok &&
                       result.value() == context.builtin(CoreObject::True);
            };
            if (spec_flag(U"_initializing", false))
                message = U"partially initialized module '" + label + U"' has no attribute '" +
                          attribute + U"' (most likely due to a circular import)";
            else if (spec_flag(U"_uninitialized_submodules", true))
                message = U"cannot access submodule '" + attribute + U"' of module '" + label +
                          U"' (most likely due to a circular import)";
            else
                message = U"module '" + label + U"' has no attribute '" + attribute + U"'";
        }
        raise_error(context, CoreObject::AttributeErrorType, std::move(message), out);
        return;
    }
    auto function = roots.root(out.value());
    std::array values{name.get(context)};
    call(context, function, {values, {}}, out);
}
void module_dictionary(Attachment &context, Value module, ReturnSlot &out)
{
    out.ok(module_dict(context, module));
}
} // namespace

void make_module(Attachment &context, std::u32string name, ReturnSlot &out,
                 std::optional<Handle> dictionary)
{
    RootScope roots(context);
    if (dictionary)
        roots.root(dictionary->get(context));
    context.allocate_instance<ModuleObject>(roots.root(context.builtin(CoreObject::ModuleType)),
                                            out);
    auto module = roots.root(out.value());
    ReturnSlot temporary(context);
    if (!dictionary) {
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              temporary);
        dictionary = roots.root(temporary.value());
    }
    context.store(module.get(context), *context.instance_dictionary(module.get(context)),
                  dictionary->get(context));
    make_str(context, std::move(name), temporary);
    initialize(context, module, roots.root(temporary.value()), context.builtin(CoreObject::None),
               out);
    if (out.tag() != ResultTag::Raised)
        out.ok(module.get(context));
}

namespace detail {
void install_modules(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::ModuleType);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(context, core, owner, U"__new__", constructor, module_new);
    RootScope roots(context);
    ReturnSlot temporary(context);
    auto dict = *TypeView(context, owner).dictionary();
    auto function = roots.root(*name_lookup(context, dict, U"__new__"));
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                  function.get(context));
    auto wrapper = roots.root(temporary.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
    name_store(context, dict, temporary.value(), wrapper.get(context));
    SignatureLayout init;
    init.parameters = {
        {U"self", ParameterKind::PositionalOnly},
        {U"name", ParameterKind::PositionalOrKeyword},
        {U"doc", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(context)}};
    install_native_attribute(context, core, owner, U"__init__", init, module_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, owner, U"__repr__", unary, module_repr);
    unary.parameters.push_back({U"name", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, owner, U"__getattribute__", unary, module_getattr);
    install_native_field(context, core, owner, U"__dict__", module_dictionary);
}
} // namespace detail
} // namespace cxxpython::core
