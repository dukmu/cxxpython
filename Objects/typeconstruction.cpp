#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/unionobject.hpp"
#include "internal/exceptions.hpp"

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
Value unwrapped(Attachment &context, Value value, RootScope &roots)
{
    if (context.has_layout<StaticMethodObject>(value)) {
        ReturnSlot function(context);
        context.load(value, context.payload<StaticMethodObject>(value).callable, function);
        if (function.tag() == ResultTag::Ok)
            return roots.root(function.value()).get(context);
    }
    return value;
}
bool object_default(Attachment &context, Value type, const std::u32string &name)
{
    auto method = detail::type_attribute(context, type, name);
    RootScope roots(context);
    if (method)
        roots.root(*method);
    return method &&
           unwrapped(context, *method, roots) ==
               unwrapped(context, *detail::type_attribute(context, context.object_type(), name),
                         roots);
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
    RootScope roots(context);
    TypeView metadata(context, args[0]);
    if (metadata.snapshot() && context.payload<TypeSnapshot>(*metadata.snapshot()).abstract) {
        auto methods = DictView(context, metadata.storage()).find(U"__abstractmethods__");
        ReturnSlot result(context);
        get_iter(context, roots.root(*methods), result);
        if (raised(result)) {
            out.raised(result.exception());
            return;
        }
        auto iterator = roots.root(result.value());
        std::vector<std::u32string> names;
        for (;;) {
            auto status = iter_next(context, iterator, result);
            if (status == NextStatus::Done)
                break;
            if (status == NextStatus::Raised) {
                out.raised(result.exception());
                return;
            }
            if (!context.has_layout<StrObject>(result.value())) {
                type_error(context, U"abstract method name must be a string", out);
                return;
            }
            names.push_back(context.payload<StrObject>(result.value()).value);
        }
        std::sort(names.begin(), names.end());
        auto name = metadata.name();
        auto text = std::holds_alternative<Value>(name)
                        ? context.payload<StrObject>(std::get<Value>(name)).value
                        : std::get<std::u32string>(name);
        std::u32string message = U"Can't instantiate abstract class " + text +
                                 U" without an implementation for abstract method";
        if (names.size() != 1)
            message += U"s";
        message += U" '";
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i)
                message += U"', '";
            message += names[i];
        }
        type_error(context, message + U"'", out);
        return;
    }
    auto prefix = context.payload<TypeObject>(args[0]).instance_layout;
    prefix.fields = {};
    if (prefix != LayoutDescriptor::of<InstanceObject>()) {
        type_error(context, U"object.__new__ cannot allocate this native layout", out);
        return;
    }
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
// CPython 3.12.14 Objects/exceptions.c ImportError_init, using traced tail slots.
void import_error_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::ImportErrorType)) {
        type_error(context, U"ImportError.__init__ requires ImportError", out);
        return;
    }
    context.store(args[0], context.payload<ExceptionObject>(args[0]).args, args[4]);
    context.store(args[0], context.instance_slot(args[0], 0), args[1]);
    context.store(args[0], context.instance_slot(args[0], 1), args[2]);
    context.store(args[0], context.instance_slot(args[0], 3), args[3]);
    const auto &items = context.payload<TupleObject>(args[4]).items;
    context.store(args[0], context.instance_slot(args[0], 2),
                  items.size() == 1 ? items[0] : context.builtin(CoreObject::None));
    out.ok(context.builtin(CoreObject::None));
}
void name_error_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_instance(context, args[0], CoreObject::NameErrorType)) {
        type_error(context, U"NameError.__init__ requires NameError", out);
        return;
    }
    context.store(args[0], context.payload<ExceptionObject>(args[0]).args, args[2]);
    context.store(args[0], context.instance_slot(args[0], 0), args[1]);
    out.ok(context.builtin(CoreObject::None));
}
void name_error_get(Attachment &context, Value self, ReturnSlot &out)
{
    context.load(self, context.instance_slot(self, 0), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
void name_error_set(Attachment &context, Value self, std::optional<Value> value, ReturnSlot &out)
{
    context.store(self, context.instance_slot(self, 0), value);
    out.ok(context.builtin(CoreObject::None));
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
void type_subclasses(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0])) {
        type_error(context, U"type.__subclasses__ requires a type", out);
        return;
    }
    RootScope roots(context);
    detail::direct_subclasses(context, roots.root(args[0]), out);
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
    context.load(args[0], context.payload<StaticMethodObject>(args[0]).callable, out);
    if (out.tag() == ResultTag::Empty)
        raise_error(context, CoreObject::RuntimeErrorType, U"uninitialized staticmethod object",
                    out);
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
    RootScope roots(context);
    ReturnSlot loaded(context);
    context.load(args[0], context.payload<ClassMethodObject>(args[0]).callable, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        raise_error(context, CoreObject::RuntimeErrorType, U"uninitialized classmethod object",
                    out);
        return;
    }
    auto callable = roots.root(loaded.value()).get(context);
    if (detail::type_attribute(context, context.type_of(callable), U"__get__")) {
        detail::bind_descriptor(context, callable, owner, owner, out);
        return;
    }
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
    ReturnSlot loaded(context);
    context.load(args[0], context.payload<StaticMethodObject>(args[0]).callable, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        raise_error(context, CoreObject::RuntimeErrorType, U"uninitialized staticmethod object",
                    out);
        return;
    }
    call(context, roots.root(loaded.value()), {context.payload<TupleObject>(args[1]).items, named},
         out);
}
void static_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::StaticMethodType))) {
        type_error(context, U"staticmethod.__new__ requires a staticmethod subtype", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<StaticMethodObject>(roots.root(args[0]), out);
}
void class_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::ClassMethodType))) {
        type_error(context, U"classmethod.__new__ requires a classmethod subtype", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<ClassMethodObject>(roots.root(args[0]), out);
}
template <class Wrapper>
void wrapper_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<Wrapper>(args[0])) {
        type_error(context, U"wrapper descriptor requires a matching instance", out);
        return;
    }
    if (context.payload<Wrapper>(args[0]).native) {
        type_error(context, U"cannot initialize a native method descriptor", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]), function = roots.root(args[1]);
    context.store(args[0], context.payload<Wrapper>(self).callable, args[1]);
    ReturnSlot key(context), value(context), changed(context);
    for (auto name :
         {U"__module__", U"__name__", U"__qualname__", U"__doc__", U"__annotations__"}) {
        make_str(context, name, key);
        auto attribute = roots.root(key.value());
        get_attribute(context, function, attribute, value);
        if (raised(value)) {
            if (is_subtype(context, context.type_of(value.exception()),
                           context.builtin(CoreObject::AttributeErrorType))) {
                roots.release(attribute);
                continue;
            }
            out.raised(value.exception());
            return;
        }
        set_attribute(context, self, attribute, roots.root(value.value()), changed);
        if (raised(changed)) {
            out.raised(changed.exception());
            return;
        }
        roots.release(attribute);
    }
    out.ok(context.builtin(CoreObject::None));
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
    if (!function ||
        (context.type_of(*function) != context.builtin(CoreObject::NativeFunctionType) &&
         context.type_of(*function) != context.builtin(CoreObject::FunctionType)))
        return;
    RootScope roots(context);
    auto callable = roots.root(*function);
    ReturnSlot out(context);
    if (class_method)
        context.allocate_instance<ClassMethodObject>(
            roots.root(context.builtin(CoreObject::ClassMethodType)), out, callable.get(context),
            false);
    else
        context.allocate_instance<StaticMethodObject>(
            roots.root(context.builtin(CoreObject::StaticMethodType)), out, callable.get(context),
            false);
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
            auto original = roots.root(callback.exception());
            auto name = [&](Value type) {
                const auto value = TypeView(context, type).name();
                return (std::holds_alternative<Value>(value)
                            ? context.payload<StrObject>(std::get<Value>(value)).value
                            : std::get<std::u32string>(value))
                    .substr(0, 100);
            };
            const auto descriptor = name(context.type_of(entry.value));
            const auto owner = name(cls.get(context));
            ReturnSlot key(context), note(context);
            repr(context, roots.root(entry.key), key);
            if (raised(key)) {
                out.raised(key.exception());
                return;
            }
            make_str(context,
                     U"Error calling __set_name__ on '" + descriptor + U"' instance " +
                         context.payload<StrObject>(key.value()).value + U" in '" + owner + U"'",
                     note);
            detail::add_exception_note(context, original, roots.root(note.value()), out);
            if (!raised(out))
                out.raised(original.get(context));
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
        const auto source = roots.root(*slots);
        spec.slots.emplace();
        std::vector<Value> items;
        if (is_instance(context, source.get(context), CoreObject::StrType))
            items.push_back(source.get(context));
        else {
            ReturnSlot iterator(context), item(context);
            get_iter(context, source, iterator);
            if (raised(iterator)) {
                out.raised(iterator.exception());
                return;
            }
            const auto input = roots.root(iterator.value());
            const auto hint = detail::length_hint(context, source, item);
            if (!hint) {
                out.raised(item.exception());
                return;
            }
            if (*hint > items.max_size()) {
                out.raised(context.builtin(CoreObject::MemoryError));
                return;
            }
            items.reserve(*hint);
            // CPython 3.12.14 type_new_get_slots materializes the entire
            // iterable before validating names. Keep yielded objects rooted
            // across later callbacks, even if they remove __slots__ and collect.
            for (;;) {
                const auto status = iter_next(context, input, item);
                if (status == NextStatus::Done)
                    break;
                if (status == NextStatus::Raised) {
                    out.raised(item.exception());
                    return;
                }
                items.push_back(roots.root(item.value()).get(context));
            }
        }
        for (auto item : items) {
            if (!is_instance(context, item, CoreObject::StrType)) {
                TypeView type(context, context.type_of(item));
                const auto name = type.name();
                const auto text = std::holds_alternative<Value>(name)
                                      ? context.payload<StrObject>(std::get<Value>(name)).value
                                      : std::get<std::u32string>(name);
                type_error(context,
                           U"__slots__ items must be strings, not '" + text.substr(0, 200) + U"'",
                           out);
                return;
            }
            const auto &text = context.payload<StrObject>(item).value;
            if (!is_identifier(text)) {
                type_error(context, U"__slots__ must be identifiers", out);
                return;
            }
            spec.slots->push_back(text);
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
void type_prepare(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
}
} // namespace
std::optional<std::size_t> detail::length_hint(Attachment &context, Handle source, ReturnSlot &out)
{
    RootScope roots(context);
    const auto type = roots.root(context.type_of(source));
    const auto length_slot = resolve_protocol(context, type, Protocol::Len, roots);
    if (length_slot.state != SlotState::Missing) {
        length(context, source, out);
        if (out.tag() == ResultTag::Ok) {
            std::int64_t value;
            if (int_fits_signed(context, out.value(), value))
                return static_cast<std::size_t>(value);
            raise_error(context, CoreObject::OverflowErrorType, U"length does not fit Py_ssize_t",
                        out);
        }
        if (out.tag() != ResultTag::Raised ||
            !is_subtype(context, context.type_of(out.exception()),
                        context.builtin(CoreObject::TypeErrorType)))
            return {};
    }
    out.reset();
    const auto hint_type = roots.root(context.type_of(source));
    const auto hint = detail::type_attribute(context, hint_type.get(context), U"__length_hint__");
    if (!hint)
        return 0;
    ReturnSlot method(context);
    detail::bind_descriptor(context, *hint, source.get(context), hint_type.get(context), method);
    if (raised(method)) {
        out.raised(method.exception());
        return {};
    }
    // A TypeError declines the call's hint, not a failing descriptor lookup.
    call(context, roots.root(method.value()), {}, out);
    if (raised(out)) {
        if (is_subtype(context, context.type_of(out.exception()),
                       context.builtin(CoreObject::TypeErrorType))) {
            out.reset();
            return 0;
        }
        return {};
    }
    if (out.value() == context.builtin(CoreObject::NotImplemented))
        return 0;
    if (!is_int(context, out.value())) {
        type_error(context, U"__length_hint__ must return an integer", out);
        return {};
    }
    std::int64_t value;
    if (!int_fits_signed(context, out.value(), value) ||
        value > std::numeric_limits<std::intptr_t>::max()) {
        raise_error(context, CoreObject::OverflowErrorType, U"length hint does not fit Py_ssize_t",
                    out);
        return {};
    }
    if (value < 0) {
        raise_error(context, CoreObject::ValueErrorType, U"__length_hint__ must be nonnegative",
                    out);
        return {};
    }
    return static_cast<std::size_t>(value);
}
namespace {
void tuple_getnewargs(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TupleObject>(args[0]) ||
        !is_subtype(context, context.type_of(args[0]), context.builtin(CoreObject::TupleType))) {
        type_error(context, U"tuple descriptor requires a tuple", out);
        return;
    }
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    ReturnSlot value(context);
    if (context.type_of(self) == context.builtin(CoreObject::TupleType))
        value.ok(self.get(context));
    else
        cxxpython::core::make_tuple(context, context.payload<TupleObject>(self).items, value);
    const std::array result{value.value()};
    cxxpython::core::make_tuple(context, result, out);
}
void tuple_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::TupleType))) {
        type_error(context, U"tuple.__new__ requires a tuple type", out);
        return;
    }
    const auto &values = context.payload<TupleObject>(args[1]).items;
    if (values.size() > 1) {
        type_error(context, U"tuple expects at most one iterable", out);
        return;
    }
    RootScope roots(context);
    const auto cls = roots.root(args[0]);
    const bool exact = args[0] == context.builtin(CoreObject::TupleType);
    if (!exact && context.payload<TypeObject>(args[0]).immutable) {
        type_error(context, U"tuple.__new__ cannot bypass a native tuple subtype constructor", out);
        return;
    }
    if (exact && values.size() == 1 &&
        context.type_of(values[0]) == context.builtin(CoreObject::TupleType)) {
        out.ok(values[0]);
        return;
    }
    std::vector<Value> items;
    if (!values.empty()) {
        const auto source = roots.root(values[0]);
        if (context.type_of(source) == context.builtin(CoreObject::ListType))
            detail::list_snapshot(context, source, roots, items);
        else {
            ReturnSlot iterator(context), item(context);
            get_iter(context, source, iterator);
            if (raised(iterator)) {
                out.raised(iterator.exception());
                return;
            }
            const auto input = roots.root(iterator.value());
            const auto hint = detail::length_hint(context, source, item);
            if (!hint) {
                out.raised(item.exception());
                return;
            }
            if (*hint > items.max_size()) {
                out.raised(context.builtin(CoreObject::MemoryError));
                return;
            }
            items.reserve(*hint);
            for (;;) {
                const auto status = iter_next(context, input, item);
                if (status == NextStatus::Done)
                    break;
                if (status == NextStatus::Raised) {
                    out.raised(item.exception());
                    return;
                }
                items.push_back(roots.root(item.value()).get(context));
            }
        }
    }
    if (exact && items.empty())
        out.ok(context.builtin(CoreObject::EmptyTuple));
    else
        context.allocate_instance<TupleObject>(cls, out, std::move(items));
}
void string_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::StrType))) {
        type_error(context, U"str.__new__ requires a str type", out);
        return;
    }
    const auto &positional = context.payload<TupleObject>(args[1]).items;
    if (positional.size() > 3) {
        type_error(context, U"str expects at most three arguments", out);
        return;
    }
    constexpr std::array<std::u32string_view, 3> names{U"object", U"encoding", U"errors"};
    std::array<std::optional<Value>, 3> values;
    std::copy(positional.begin(), positional.end(), values.begin());
    DictView keywords(context, args[2]);
    for (auto entry : keywords.entries()) {
        const auto &name = context.payload<StrObject>(entry.key).value;
        const auto found = std::find(names.begin(), names.end(), name);
        if (found == names.end() || values[found - names.begin()]) {
            type_error(context, U"invalid or repeated str keyword argument", out);
            return;
        }
        values[found - names.begin()] = entry.value;
    }
    for (std::size_t i = 1; i < values.size(); ++i)
        if (values[i] && !is_instance(context, *values[i], CoreObject::StrType)) {
            type_error(context, U"str encoding and errors must be strings", out);
            return;
        }
    RootScope roots(context);
    ReturnSlot converted(context);
    if (!values[0])
        make_str(context, U"", converted);
    else if (values[1] || values[2]) {
        type_error(context, U"decoding requires a bytes-like object", out);
        return;
    } else
        str(context, roots.root(*values[0]), converted);
    if (raised(converted)) {
        out.raised(converted.exception());
        return;
    }
    if (args[0] == context.builtin(CoreObject::StrType))
        out.ok(converted.value());
    else
        context.allocate_instance<StrObject>(roots.root(args[0]), out,
                                             context.payload<StrObject>(converted.value()).value);
}
void object_subclass(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_type(context, args[0])) {
        type_error(context, U"__init_subclass__ requires a type", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
void object_subclasshook(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(context.builtin(CoreObject::NotImplemented));
}
void class_getitem(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    make_generic_alias(context, roots.root(args[0]), roots.root(args[1]), out);
}
template <bool reverse>
void type_union(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    make_union(context, roots.root(args[reverse ? 1 : 0]), roots.root(args[reverse ? 0 : 1]), out);
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
    install(at(CoreObject::BaseExceptionType), U"__new__", variadic, exception_new,
            Binding::Static);
    install(at(CoreObject::BaseExceptionType), U"__init__", exception, exception_init);
    install(at(CoreObject::StopIterationType), U"__init__", exception, stop_init);
    auto import_error = exception;
    import_error.parameters.push_back(
        {U"name", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)});
    import_error.parameters.push_back(
        {U"path", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)});
    import_error.parameters.push_back(
        {U"name_from", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)});
    install(at(CoreObject::ImportErrorType), U"__init__", import_error, import_error_init);
    auto name_error = exception;
    name_error.parameters.push_back(
        {U"name", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)});
    install(at(CoreObject::NameErrorType), U"__init__", name_error, name_error_init);
    install_native_field(context, core, at(CoreObject::NameErrorType), U"name", name_error_get,
                         name_error_set);
    install(type, U"__call__", variadic, type_call);
    SignatureLayout union_signature;
    union_signature.parameters = {{U"self", ParameterKind::PositionalOnly},
                                  {U"other", ParameterKind::PositionalOnly}};
    install(type, U"__or__", union_signature, type_union<false>);
    install(type, U"__ror__", union_signature, type_union<true>);
    install(at(CoreObject::GenericAliasType), U"__or__", union_signature, type_union<false>);
    install(at(CoreObject::GenericAliasType), U"__ror__", union_signature, type_union<true>);
    install(type, U"__init__", variadic, type_init);
    install(type, U"__prepare__", variadic, type_prepare, Binding::Class);
    install(at(CoreObject::StrType), U"__new__", variadic, string_new, Binding::Static);
    SignatureLayout sequence;
    sequence.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    sequence.varargs = U"args";
    install(at(CoreObject::TupleType), U"__new__", sequence, tuple_new, Binding::Static);
    SignatureLayout tuple_state;
    tuple_state.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install(at(CoreObject::TupleType), U"__getnewargs__", tuple_state, tuple_getnewargs);
    SignatureLayout generic;
    generic.parameters = {{U"cls", ParameterKind::PositionalOnly},
                          {U"item", ParameterKind::PositionalOnly}};
    for (auto id : {CoreObject::ListType, CoreObject::TupleType, CoreObject::DictType,
                    CoreObject::SetType, CoreObject::FrozenSetType})
        install(at(id), U"__class_getitem__", generic, class_getitem, Binding::Class);
    SignatureLayout definition;
    definition.parameters = {{U"cls", ParameterKind::PositionalOnly},
                             {U"name", ParameterKind::PositionalOnly},
                             {U"bases", ParameterKind::PositionalOnly},
                             {U"dict", ParameterKind::PositionalOnly}};
    definition.varkwargs = U"kwargs";
    install(type, U"__new__", definition, type_new, Binding::Static);
    SignatureLayout subclass;
    subclass.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    install(type, U"__subclasses__", subclass, type_subclasses);
    install(object, U"__init_subclass__", subclass, object_subclass, Binding::Class);
    subclass.varargs = U"args";
    install(object, U"__subclasshook__", subclass, object_subclasshook, Binding::Class);
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
    install(at(CoreObject::StaticMethodType), U"__new__", variadic, static_new, Binding::Static);
    install(at(CoreObject::ClassMethodType), U"__new__", variadic, class_new, Binding::Static);
    install(at(CoreObject::StaticMethodType), U"__init__", wrapped,
            wrapper_init<StaticMethodObject>);
    install(at(CoreObject::ClassMethodType), U"__init__", wrapped, wrapper_init<ClassMethodObject>);
    SignatureLayout cell;
    cell.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    cell.varargs = U"contents";
    install(at(CoreObject::CellType), U"__new__", cell, cell_new, Binding::Static);
}
} // namespace detail
} // namespace cxxpython::core
