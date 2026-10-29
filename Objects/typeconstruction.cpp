#include "cxxpython/objects.hpp"

#include <algorithm>

namespace cxxpython::core {
namespace {
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}
void definition_error(Attachment &context, const TypeDefinitionError &error, ReturnSlot &out)
{
    const std::string message(error.what());
    raise_error(context, error.error_type, {message.begin(), message.end()}, out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool is_type(Attachment &context, Value value)
{
    return context.has_layout<TypeObject>(value) &&
           is_subtype(context, context.type_of(value), context.type_type()) &&
           context.payload<TypeObject>(value).state == TypeState::Ready;
}
bool is_instance(Attachment &context, Value value, CoreObject type)
{
    return is_subtype(context, context.type_of(value), context.builtin(type));
}
std::vector<KeywordArg> keywords(Attachment &context, Value dictionary, RootScope &roots)
{
    std::vector<KeywordArg> result;
    DictView snapshot(context, dictionary);
    for (const auto &entry : snapshot.entries()) {
        roots.root(entry.key);
        roots.root(entry.value);
        result.push_back({entry.key, entry.value});
    }
    return result;
}
bool excess(Attachment &context, std::span<const Value> args)
{
    return !context.payload<TupleObject>(args[1]).items.empty() ||
           !DictView(context, args[2]).entries().empty();
}
Value unwrapped(Attachment &context, Value value)
{
    return context.type_of(value) == context.builtin(CoreObject::StaticMethodType)
               ? context.payload<StaticMethodObject>(value).callable
               : value;
}
bool object_default(Attachment &context, Value type, const std::u32string &name)
{
    auto method = detail::type_attribute(context, type, name);
    RootScope roots(context);
    if (method)
        roots.root(*method);
    return method &&
           unwrapped(context, *method) ==
               unwrapped(context, *detail::type_attribute(context, context.object_type(), name));
}
void object_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0])) {
        type_error(context, U"object.__new__ requires a type", out);
        return;
    }
    if (excess(context, args) && (!object_default(context, args[0], U"__new__") ||
                                  object_default(context, args[0], U"__init__"))) {
        type_error(context, U"object.__new__ received excess arguments", out);
        return;
    }
    auto prefix = context.payload<TypeObject>(args[0]).instance_layout;
    prefix.fields = {};
    if (prefix != LayoutDescriptor::of<InstanceObject>()) {
        type_error(context, U"object.__new__ cannot allocate this native layout", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<InstanceObject>(roots.root(args[0]), out);
}
void object_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    auto type = context.type_of(args[0]);
    if (excess(context, args) && (!object_default(context, type, U"__init__") ||
                                  object_default(context, type, U"__new__"))) {
        type_error(context, U"object.__init__ received excess arguments", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
void exception_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::BaseExceptionType))) {
        type_error(context, U"BaseException.__new__ requires an exception type", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<ExceptionObject>(roots.root(args[0]), out, args[1]);
}
void exception_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::BaseExceptionType) ||
        !context.has_layout<ExceptionObject>(args[0])) {
        type_error(context, U"BaseException.__init__ requires an exception", out);
        return;
    }
    context.store(args[0], context.payload<ExceptionObject>(args[0]).args, args[1]);
    out.ok(context.builtin(CoreObject::None));
}
void stop_init(Attachment &context, Handle fn, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::StopIterationType)) {
        type_error(context, U"StopIteration.__init__ requires StopIteration", out);
        return;
    }
    exception_init(context, fn, args, out);
    if (raised(out))
        return;
    const auto &items = context.payload<TupleObject>(args[1]).items;
    context.store(args[0], context.instance_slot(args[0], 0),
                  items.empty() ? context.builtin(CoreObject::None) : items[0]);
}
void type_call(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0])) {
        type_error(context, U"type.__call__ requires a type", out);
        return;
    }
    RootScope roots(context);
    auto &positional = context.payload<TupleObject>(args[1]).items;
    auto named = keywords(context, args[2], roots);
    if (args[0] == context.type_type()) {
        if (positional.size() == 1 && named.empty()) {
            out.ok(context.type_of(positional[0]));
            return;
        }
        if (positional.size() != 3) {
            type_error(context, U"type() takes 1 or 3 arguments", out);
            return;
        }
    }
    auto cls = roots.root(args[0]);
    auto constructor = detail::type_attribute(context, args[0], U"__new__");
    if (!constructor) {
        type_error(context, U"type has no __new__", out);
        return;
    }
    auto original = roots.root(*constructor);
    ReturnSlot target(context);
    // Builtin allocation entries do not run metaclass attribute hooks.
    // User __new__ uses ordinary class lookup, including those hooks.
    auto builtin_new = detail::type_attribute(context, context.type_type(), U"__new__");
    if (object_default(context, args[0], U"__new__") || *constructor == *builtin_new)
        detail::bind_descriptor(context, original.get(context), {}, args[0], target);
    else {
        ReturnSlot name(context);
        make_str(context, U"__new__", name);
        get_attribute(context, cls, roots.root(name.value()), target);
    }
    if (raised(target)) {
        out.raised(target.exception());
        return;
    }
    std::vector<Value> new_args{args[0]};
    new_args.insert(new_args.end(), positional.begin(), positional.end());
    ReturnSlot created(context);
    call(context, roots.root(target.value()), {new_args, named}, created);
    if (raised(created)) {
        out.raised(created.exception());
        return;
    }
    auto self = roots.root(created.value());
    if (!is_subtype(context, context.type_of(self), args[0])) {
        out.ok(self.get(context));
        return;
    }
    ReturnSlot initialized(context);
    detail::special(context, self.get(context), U"__init__", {positional, named}, initialized);
    if (raised(initialized)) {
        out.raised(initialized.exception());
        return;
    }
    if (initialized.value() != context.builtin(CoreObject::None)) {
        type_error(context, U"__init__ must return None", out);
        return;
    }
    out.ok(self.get(context));
}
void static_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::StaticMethodType)) {
        type_error(context, U"staticmethod descriptor requires a staticmethod", out);
        return;
    }
    if (args[1] == context.builtin(CoreObject::None) &&
        args[2] == context.builtin(CoreObject::None)) {
        type_error(context, U"__get__(None, None) is invalid", out);
        return;
    }
    out.ok(context.payload<StaticMethodObject>(args[0]).callable);
}
void class_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::ClassMethodType)) {
        type_error(context, U"classmethod descriptor requires a classmethod", out);
        return;
    }
    if (args[1] == context.builtin(CoreObject::None) &&
        args[2] == context.builtin(CoreObject::None)) {
        type_error(context, U"__get__(None, None) is invalid", out);
        return;
    }
    auto owner = args[2];
    if (owner == context.builtin(CoreObject::None))
        owner = context.type_of(args[1]);
    auto callable = context.payload<ClassMethodObject>(args[0]).callable;
    if (detail::type_attribute(context, context.type_of(callable), U"__get__")) {
        detail::bind_descriptor(context, callable, owner, owner, out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<BoundMethodObject>(
        roots.root(context.builtin(CoreObject::BoundMethodType)), out, callable, owner);
}
void static_call(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::StaticMethodType)) {
        type_error(context, U"staticmethod call requires a staticmethod", out);
        return;
    }
    RootScope roots(context);
    auto named = keywords(context, args[2], roots);
    call(context, roots.root(context.payload<StaticMethodObject>(args[0]).callable),
         {context.payload<TupleObject>(args[1]).items, named}, out);
}
void static_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::StaticMethodType))) {
        type_error(context, U"staticmethod.__new__ requires a staticmethod subtype", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<StaticMethodObject>(roots.root(args[0]), out, args[1]);
}
void class_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::ClassMethodType))) {
        type_error(context, U"classmethod.__new__ requires a classmethod subtype", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<ClassMethodObject>(roots.root(args[0]), out, args[1]);
}
void cell_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != context.builtin(CoreObject::CellType)) {
        type_error(context, U"cell.__new__ requires cell", out);
        return;
    }
    const auto &items = context.payload<TupleObject>(args[1]).items;
    if (items.size() > 1) {
        type_error(context, U"cell accepts at most one argument", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<CellObject>(roots.root(args[0]), out,
                                          items.empty() ? std::optional<Value>{} : items[0]);
}
void wrap_definition(Attachment &context, Handle cls, const std::u32string &name, bool class_method)
{
    auto dictionary = *TypeView(context, cls.get(context)).dictionary();
    auto function = detail::name_lookup(context, dictionary, name);
    if (!function || context.type_of(*function) != context.builtin(CoreObject::NativeFunctionType))
        return;
    RootScope roots(context);
    auto callable = roots.root(*function);
    ReturnSlot out(context);
    if (class_method)
        context.allocate_instance<ClassMethodObject>(
            roots.root(context.builtin(CoreObject::ClassMethodType)), out, callable.get(context));
    else
        context.allocate_instance<StaticMethodObject>(
            roots.root(context.builtin(CoreObject::StaticMethodType)), out, callable.get(context));
    auto wrapper = roots.root(out.value());
    make_str(context, name, out);
    detail::name_store(context, dictionary, out.value(), wrapper.get(context));
}
void finish_class(Attachment &context, Handle cls, std::span<const KeywordArg> named,
                  ReturnSlot &out)
{
    // Callbacks may resize/replace the namespace and collect removed
    // entries. Snapshot and root the original key/value pairs first.
    TypeView metadata(context, cls.get(context));
    DictView namespace_view(context, metadata.storage());
    auto entries = namespace_view.entries();
    RootScope roots(context);
    for (const auto &entry : entries) {
        roots.root(entry.key);
        roots.root(entry.value);
    }
    ReturnSlot callback(context);
    for (const auto &entry : entries) {
        if (!detail::type_attribute(context, context.type_of(entry.value), U"__set_name__"))
            continue;
        std::array args{cls.get(context), entry.key};
        detail::special(context, entry.value, U"__set_name__", {args, {}}, callback);
        if (raised(callback)) {
            const auto cause = callback.exception();
            raise_error(context, CoreObject::RuntimeErrorType, U"error calling __set_name__", out);
            if (context.type_of(out.exception()) == context.builtin(CoreObject::RuntimeErrorType)) {
                auto &error = context.payload<ExceptionObject>(out.exception());
                const auto wrapped = out.exception();
                context.store(wrapped, error.cause, cause);
                context.store(wrapped, error.context, cause);
                error.suppress_context.store(true, std::memory_order_release);
            }
            return;
        }
    }
    const auto &mro = context.payload<TypeObject>(cls).mro;
    for (auto base : std::span(mro).subspan(1)) {
        TypeView base_metadata(context, base);
        DictView dictionary(context, base_metadata.storage());
        auto hook = dictionary.find(U"__init_subclass__");
        if (!hook)
            continue;
        detail::bind_descriptor(context, *hook, cls.get(context), cls.get(context), callback);
        if (raised(callback)) {
            out.raised(callback.exception());
            return;
        }
        call(context, roots.root(callback.value()), {{}, named}, out);
        if (!raised(out))
            out.ok(cls.get(context));
        return;
    }
    throw std::logic_error("object has no __init_subclass__");
}
void type_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) || !is_subtype(context, args[0], context.type_type()) ||
        !is_instance(context, args[1], CoreObject::StrType) ||
        !is_instance(context, args[2], CoreObject::TupleType) ||
        !is_instance(context, args[3], CoreObject::DictType)) {
        type_error(context, U"type.__new__ requires a metaclass, str, tuple and dict", out);
        return;
    }
    RootScope roots(context);
    auto named = keywords(context, args[4], roots);
    std::vector<Handle> bases;
    for (auto base : context.payload<TupleObject>(args[2]).items)
        bases.push_back(roots.root(base));
    Handle winner;
    try {
        winner = detail::select_metaclass(context, roots.root(args[0]), bases, roots);
    } catch (const TypeDefinitionError &error) {
        definition_error(context, error, out);
        return;
    }
    auto constructor = detail::type_attribute(context, winner.get(context), U"__new__");
    if (constructor)
        roots.root(*constructor);
    if (winner.get(context) != args[0] &&
        constructor != detail::type_attribute(context, context.type_type(), U"__new__")) {
        ReturnSlot bound(context);
        detail::bind_descriptor(context, *constructor, {}, winner.get(context), bound);
        if (raised(bound)) {
            out.raised(bound.exception());
            return;
        }
        std::array values{winner.get(context), args[1], args[2], args[3]};
        call(context, roots.root(bound.value()), {values, named}, out);
        return;
    }
    for (auto base : bases)
        if (!is_type(context, base.get(context))) {
            type_error(context, U"bases must be types", out);
            return;
        }
    const auto &name = context.payload<StrObject>(args[1]).value;
    if (std::find(name.begin(), name.end(), U'\0') != name.end()) {
        raise_error(context, CoreObject::ValueErrorType,
                    U"type name must not contain null characters", out);
        return;
    }
    auto space = roots.root(args[3]);
    TypeSpec spec{name, bases, winner};
    spec.module.reset();
    spec.dictionary = space;
    if (auto qualname = detail::name_lookup(context, args[3], U"__qualname__")) {
        if (!is_instance(context, *qualname, CoreObject::StrType)) {
            type_error(context, U"__qualname__ must be a str", out);
            return;
        }
        spec.qualname = context.payload<StrObject>(*qualname).value;
    }
    if (auto slots = detail::name_lookup(context, args[3], U"__slots__")) {
        spec.slots.emplace();
        auto append = [&](Value value) {
            if (!is_instance(context, value, CoreObject::StrType))
                return false;
            spec.slots->push_back(context.payload<StrObject>(value).value);
            return true;
        };
        bool valid = true;
        if (is_instance(context, *slots, CoreObject::StrType))
            valid = append(*slots);
        else if (is_instance(context, *slots, CoreObject::TupleType)) {
            for (auto item : context.payload<TupleObject>(*slots).items)
                valid &= append(item);
        } else if (is_instance(context, *slots, CoreObject::DictType)) {
            DictView names(context, *slots);
            for (auto entry : names.entries())
                valid &= append(entry.key);
        } else
            valid = false;
        if (!valid) {
            type_error(context, U"__slots__ requires string names from str, tuple or dict", out);
            return;
        }
    }
    auto classcell = detail::name_lookup(context, args[3], U"__classcell__");
    if (classcell)
        roots.root(*classcell);
    if (classcell && !is_instance(context, *classcell, CoreObject::CellType)) {
        type_error(context, U"__classcell__ must be a cell", out);
        return;
    }
    ReturnSlot created(context);
    try {
        create_type(context, spec, created);
    } catch (const TypeDefinitionError &error) {
        definition_error(context, error, out);
        return;
    }
    auto cls = roots.root(created.value());
    auto qualified =
        roots.root(detail::name_lookup(context, args[3], U"__qualname__").value_or(args[1]));
    detail::type_name_store(context, cls.get(context), args[1], false);
    detail::type_name_store(context, cls.get(context), qualified.get(context), true);
    const auto dictionary = *TypeView(context, cls.get(context)).dictionary();
    detail::name_erase(context, dictionary, U"__qualname__");
    wrap_definition(context, cls, U"__new__", false);
    wrap_definition(context, cls, U"__init_subclass__", true);
    wrap_definition(context, cls, U"__class_getitem__", true);
    if (classcell) {
        detail::name_erase(context, dictionary, U"__classcell__");
        write_cell(context, *classcell, cls.get(context));
    }
    if (auto cell = detail::name_lookup(context, dictionary, U"__classdictcell__")) {
        roots.root(*cell);
        if (!is_instance(context, *cell, CoreObject::CellType)) {
            type_error(context, U"__classdictcell__ must be a cell", out);
            return;
        }
        detail::name_erase(context, dictionary, U"__classdictcell__");
        write_cell(context, *cell, dictionary);
    }
    finish_class(context, cls, named, out);
}
void type_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    auto size = context.payload<TupleObject>(args[1]).items.size();
    if (!is_type(context, args[0]) || (size != 1 && size != 3) ||
        (size == 1 && !DictView(context, args[2]).entries().empty())) {
        type_error(context, U"type.__init__ takes 1 or 3 arguments", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
void object_subclass(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0])) {
        type_error(context, U"__init_subclass__ requires a type", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
} // namespace
namespace detail {
void install_construction(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(context);
    auto object = roots.root(context.object_type());
    auto type = roots.root(context.type_type());
    enum class Binding { Plain, Static, Class };
    auto install = [&](Handle owner, std::u32string name, SignatureLayout signature,
                       NativeEntry entry, Binding binding = Binding::Plain) {
        install_native_attribute(context, core, owner, name, std::move(signature), entry);
        if (binding == Binding::Plain)
            return;
        ReturnSlot out(context);
        auto function = *name_lookup(context, *TypeView(context, owner).dictionary(), name);
        if (binding == Binding::Static)
            context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out,
                                                          function);
        else
            context.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), out,
                                                         function);
        auto wrapper = roots.root(out.value());
        context.allocate_instance<StrObject>(at(CoreObject::StrType), out, std::move(name));
        name_store(context, *TypeView(context, owner).dictionary(), out.value(),
                   wrapper.get(context));
    };
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install(object, U"__new__", variadic, object_new, Binding::Static);
    install(object, U"__init__", variadic, object_init);
    SignatureLayout exception;
    exception.parameters = {{U"self", ParameterKind::PositionalOnly}};
    exception.varargs = U"args";
    install(at(CoreObject::BaseExceptionType), U"__new__", exception, exception_new,
            Binding::Static);
    install(at(CoreObject::BaseExceptionType), U"__init__", exception, exception_init);
    install(at(CoreObject::StopIterationType), U"__init__", exception, stop_init);
    install(type, U"__call__", variadic, type_call);
    install(type, U"__init__", variadic, type_init);
    SignatureLayout definition;
    definition.parameters = {{U"cls", ParameterKind::PositionalOnly},
                             {U"name", ParameterKind::PositionalOnly},
                             {U"bases", ParameterKind::PositionalOnly},
                             {U"dict", ParameterKind::PositionalOnly}};
    definition.varkwargs = U"kwargs";
    install(type, U"__new__", definition, type_new, Binding::Static);
    SignatureLayout subclass;
    subclass.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    install(object, U"__init_subclass__", subclass, object_subclass, Binding::Class);
    SignatureLayout get;
    get.parameters = {{U"self", ParameterKind::PositionalOnly},
                      {U"instance", ParameterKind::PositionalOnly},
                      {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)}};
    install(at(CoreObject::StaticMethodType), U"__get__", get, static_get);
    install(at(CoreObject::ClassMethodType), U"__get__", get, class_get);
    install(at(CoreObject::StaticMethodType), U"__call__", variadic, static_call);
    SignatureLayout wrapped;
    wrapped.parameters = {{U"cls", ParameterKind::PositionalOnly},
                          {U"function", ParameterKind::PositionalOnly}};
    install(at(CoreObject::StaticMethodType), U"__new__", wrapped, static_new, Binding::Static);
    install(at(CoreObject::ClassMethodType), U"__new__", wrapped, class_new, Binding::Static);
    SignatureLayout cell;
    cell.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    cell.varargs = U"contents";
    install(at(CoreObject::CellType), U"__new__", cell, cell_new, Binding::Static);
}
} // namespace detail
} // namespace cxxpython::core
