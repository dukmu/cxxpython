#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
namespace {
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}

bool property_check(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.has_layout<PropertyObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::PropertyType)))
        return true;
    type_error(context, U"property descriptor requires a property", out);
    return false;
}
void native_self(Attachment &context, Value value, ReturnSlot &out)
{
    const auto &function = context.payload<NativeFunctionObject>(value);
    if (function.bound_self)
        out.ok(*function.bound_self);
    else if (function.binding == NativeFunctionBinding::Function)
        out.ok(context.builtin(CoreObject::None));
    else
        raise_error(context, CoreObject::AttributeErrorType,
                    U"unbound method descriptor has no __self__", out);
}
void property_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::PropertyType))) {
        type_error(context, U"property.__new__ requires a property subtype", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<PropertyObject>(roots.root(args[0]), out);
}
void property_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!property_check(context, args[0], out))
        return;
    auto &property = context.payload<PropertyObject>(args[0]);
    context.store(args[0], property.getter, args[1]);
    context.store(args[0], property.setter, args[2]);
    context.store(args[0], property.deleter, args[3]);
    RootScope roots(context);
    auto doc = roots.root(args[4]);
    ReturnSlot name(context), result(context);
    if (args[4] == context.builtin(CoreObject::None) &&
        args[1] != context.builtin(CoreObject::None)) {
        make_str(context, U"__doc__", name);
        get_attribute(context, roots.root(args[1]), roots.root(name.value()), result);
        if (result.tag() == ResultTag::Raised) {
            if (!is_subtype(context, context.type_of(result.exception()),
                            context.builtin(CoreObject::AttributeErrorType))) {
                out.raised(result.exception());
                return;
            }
        } else
            doc = roots.root(result.value());
    }
    context.store(args[0], property.doc, doc.get(context));
    property.doc_from_getter.store(args[4] == context.builtin(CoreObject::None),
                                   std::memory_order_relaxed);
    out.ok(context.builtin(CoreObject::None));
}
template <AtomicGcEdge PropertyObject::*Field>
void property_field(Attachment &context, Value receiver, ReturnSlot &out)
{
    if (!property_check(context, receiver, out))
        return;
    context.load(receiver, context.payload<PropertyObject>(receiver).*Field, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
void property_doc_set(Attachment &context, Value receiver, std::optional<Value> value,
                      ReturnSlot &out)
{
    if (!property_check(context, receiver, out))
        return;
    context.store(receiver, context.payload<PropertyObject>(receiver).doc,
                  value.value_or(context.builtin(CoreObject::None)));
    context.payload<PropertyObject>(receiver).doc_from_getter.store(false,
                                                                    std::memory_order_relaxed);
    out.ok(context.builtin(CoreObject::None));
}
template <AtomicGcEdge PropertyObject::*Field>
void property_invoke(Attachment &context, std::span<const Value> args, ReturnSlot &out)
{
    if (!property_check(context, args[0], out))
        return;
    RootScope roots(context);
    ReturnSlot callback(context);
    context.load(args[0], context.payload<PropertyObject>(args[0]).*Field, callback);
    if (callback.tag() == ResultTag::Empty ||
        callback.value() == context.builtin(CoreObject::None)) {
        raise_error(context, CoreObject::AttributeErrorType, U"property has no accessor", out);
        return;
    }
    call(context, roots.root(callback.value()), {args.subspan(1), {}}, out);
}
void property_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[1] == context.builtin(CoreObject::None)) {
        if (property_check(context, args[0], out))
            out.ok(args[0]);
        return;
    }
    property_invoke<&PropertyObject::getter>(context, args.first(2), out);
}
void property_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    property_invoke<&PropertyObject::setter>(context, args, out);
}
void property_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    property_invoke<&PropertyObject::deleter>(context, args, out);
}
template <AtomicGcEdge PropertyObject::*Field>
void property_clone(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!property_check(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    ReturnSlot getter(context), setter(context), deleter(context), doc(context);
    auto &property = context.payload<PropertyObject>(self);
    context.load(args[0], property.getter, getter);
    context.load(args[0], property.setter, setter);
    context.load(args[0], property.deleter, deleter);
    context.load(args[0], property.doc, doc);
    const auto none = context.builtin(CoreObject::None);
    auto value = [&](ReturnSlot &slot) {
        return slot.tag() == ResultTag::Empty ? none : slot.value();
    };
    std::array values{value(getter), value(setter), value(deleter), value(doc)};
    if constexpr (Field == &PropertyObject::getter) {
        values[0] = args[1];
        if (property.doc_from_getter.load(std::memory_order_relaxed))
            values[3] = none;
    } else if constexpr (Field == &PropertyObject::setter)
        values[1] = args[1];
    else
        values[2] = args[1];
    call(context, roots.root(context.type_of(self)), {values, {}}, out);
}
void property_abstract(Attachment &context, Value receiver, ReturnSlot &out)
{
    if (!property_check(context, receiver, out))
        return;
    RootScope roots(context);
    ReturnSlot callback(context), name(context), result(context), converted(context);
    make_str(context, U"__isabstractmethod__", name);
    auto key = roots.root(name.value());
    for (auto field :
         {&PropertyObject::getter, &PropertyObject::setter, &PropertyObject::deleter}) {
        context.load(receiver, context.payload<PropertyObject>(receiver).*field, callback);
        if (callback.tag() == ResultTag::Empty ||
            callback.value() == context.builtin(CoreObject::None))
            continue;
        get_attribute(context, roots.root(callback.value()), key, result);
        if (result.tag() == ResultTag::Raised) {
            if (is_subtype(context, context.type_of(result.exception()),
                           context.builtin(CoreObject::AttributeErrorType)))
                continue;
            out.raised(result.exception());
            return;
        }
        truth(context, roots.root(result.value()), converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return;
        }
        if (converted.value() == context.builtin(CoreObject::True)) {
            out.ok(converted.value());
            return;
        }
    }
    out.ok(context.builtin(CoreObject::False));
}

template <typename Wrapper>
void wrapper_abstract(Attachment &context, Value receiver, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot name(context), result(context), loaded(context);
    context.load(receiver, context.payload<Wrapper>(receiver).callable, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        out.ok(context.builtin(CoreObject::False));
        return;
    }
    auto callable = roots.root(loaded.value());
    make_str(context, U"__isabstractmethod__", name);
    get_attribute(context, callable, roots.root(name.value()), result);
    if (result.tag() == ResultTag::Raised) {
        if (is_subtype(context, context.type_of(result.exception()),
                       context.builtin(CoreObject::AttributeErrorType)))
            out.ok(context.builtin(CoreObject::False));
        else
            out.raised(result.exception());
        return;
    }
    truth(context, roots.root(result.value()), out);
}

GetSetDescriptorObject *descriptor(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.type_of(value) != context.builtin(CoreObject::GetSetDescriptorType)) {
        type_error(context, U"getset wrapper requires a descriptor", out);
        return nullptr;
    }
    return &context.payload<GetSetDescriptorObject>(value);
}
template <class T> void descriptor_name(Attachment &context, Value value, ReturnSlot &out)
{
    out.ok(context.payload<T>(value).name);
}
template <class T> void descriptor_owner(Attachment &context, Value value, ReturnSlot &out)
{
    out.ok(context.payload<T>(value).owner);
}
template <class T> void descriptor_qualname(Attachment &context, Value value, ReturnSlot &out)
{
    RootScope roots(context);
    auto self = roots.root(value);
    auto &field = context.payload<T>(self);
    context.load(self.get(context), field.qualname, out);
    if (out.tag() == ResultTag::Ok)
        return;
    make_str(context, U"__qualname__", out);
    get_attribute(context, roots.root(field.owner), roots.root(out.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    if (!context.has_layout<StrObject>(out.value()) ||
        !is_subtype(context, context.type_of(out.value()), context.builtin(CoreObject::StrType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"<descriptor>.__objclass__.__qualname__ is not a unicode object", out);
        return;
    }
    auto result = context.payload<StrObject>(out.value()).value;
    result += U".";
    result += context.payload<StrObject>(field.name).value;
    make_str(context, std::move(result), out);
    if (!context.compare_exchange(self.get(context), field.qualname, {}, out.value()))
        context.load(self.get(context), field.qualname, out);
}
bool compatible(Attachment &context, const GetSetDescriptorObject &field, Value receiver,
                ReturnSlot &out)
{
    if (is_subtype(context, context.type_of(receiver), field.owner))
        return true;
    type_error(context, U"descriptor does not apply to this receiver", out);
    return false;
}
void descriptor_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[1] == context.builtin(CoreObject::None) &&
        args[2] == context.builtin(CoreObject::None)) {
        type_error(context, U"__get__(None, None) is invalid", out);
        return;
    }
    detail::get_native_field(
        context, args[0],
        args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
}
void descriptor_change(Attachment &context, std::span<const Value> args, bool erase,
                       ReturnSlot &out)
{
    auto *field = descriptor(context, args[0], out);
    if (!field || !compatible(context, *field, args[1], out))
        return;
    if (!field->setter) {
        raise_error(context, CoreObject::AttributeErrorType, U"attribute is read-only", out);
        return;
    }
    field->setter(context, args[1], erase ? std::optional<Value>{} : args[2], out);
}
void descriptor_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    descriptor_change(context, args, false, out);
}
void descriptor_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    descriptor_change(context, args, true, out);
}

MemberDescriptorObject *member(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.type_of(value) != context.builtin(CoreObject::MemberDescriptorType)) {
        type_error(context, U"member wrapper requires a member descriptor", out);
        return nullptr;
    }
    return &context.payload<MemberDescriptorObject>(value);
}
bool member_receiver(Attachment &context, const MemberDescriptorObject &field, Value receiver,
                     ReturnSlot &out)
{
    if (is_subtype(context, context.type_of(receiver), field.owner))
        return true;
    type_error(context, U"member descriptor does not apply to this receiver", out);
    return false;
}
void member_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[1] == context.builtin(CoreObject::None) &&
        args[2] == context.builtin(CoreObject::None)) {
        type_error(context, U"__get__(None, None) is invalid", out);
        return;
    }
    detail::get_native_member(
        context, args[0],
        args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
}
void member_change(Attachment &context, std::span<const Value> args, bool erase, ReturnSlot &out)
{
    auto *field = member(context, args[0], out);
    if (!field || !member_receiver(context, *field, args[1], out))
        return;
    const auto *index = std::get_if<std::size_t>(&field->access);
    if (!index) {
        raise_error(context, CoreObject::AttributeErrorType, U"readonly attribute", out);
        return;
    }
    auto &slot = context.instance_slot(args[1], *index);
    if (erase) {
        context.exchange(args[1], slot, {}, out);
        if (out.tag() == ResultTag::Empty) {
            raise_error(context, CoreObject::AttributeErrorType, U"slot is unbound", out);
            return;
        }
    } else
        context.store(args[1], slot, args[2]);
    out.ok(context.builtin(CoreObject::None));
}
void member_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    member_change(context, args, false, out);
}
void member_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    member_change(context, args, true, out);
}

void class_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    out.ok(context.type_of(receiver));
}
void class_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
{
    if (!value || !context.has_layout<TypeObject>(*value) ||
        !is_subtype(context, context.type_of(*value), context.type_type())) {
        type_error(context, U"__class__ must be set to a class", out);
        return;
    }
    auto &old_type = context.payload<TypeObject>(context.type_of(receiver));
    auto &new_type = context.payload<TypeObject>(*value);
    if (new_type.state != TypeState::Ready || old_type.immutable || new_type.immutable ||
        !is_layout_compatible(context, context.type_of(receiver), *value)) {
        type_error(context, U"__class__ assignment requires compatible mutable types", out);
        return;
    }
    RootScope roots(context);
    context.store(roots.root(receiver), receiver.object()->type, roots.root(*value));
    out.ok(context.builtin(CoreObject::None));
}
void instance_dict_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    auto *field = context.instance_dictionary(receiver);
    if (!field)
        throw std::logic_error("dictionary descriptor requires dictionary storage");
    context.load(receiver, *field, out);
    if (out.tag() == ResultTag::Ok)
        return;
    RootScope roots(context);
    roots.root(receiver);
    ReturnSlot created(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          created);
    for (;;) {
        if (context.compare_exchange(receiver, *field, {}, created.value())) {
            out.ok(created.value());
            return;
        }
        context.load(receiver, *field, out);
        if (out.tag() == ResultTag::Ok)
            return;
        context.poll();
    }
}
void instance_dict_set(Attachment &context, Value receiver, std::optional<Value> value,
                       ReturnSlot &out)
{
    if (value &&
        (!context.has_layout<DictObject>(*value) ||
         !is_subtype(context, context.type_of(*value), context.builtin(CoreObject::DictType)))) {
        type_error(context, U"__dict__ must be set to a dictionary", out);
        return;
    }
    auto *dictionary = context.instance_dictionary(receiver);
    if (!dictionary)
        throw std::logic_error("dictionary descriptor requires dictionary storage");
    context.store(receiver, *dictionary, value);
    out.ok(context.builtin(CoreObject::None));
}
void type_bases(Attachment &context, Value receiver, ReturnSlot &out)
{
    TypeView view(context, receiver);
    out.ok(context.payload<TypeSnapshot>(*view.snapshot()).bases);
}
void type_mro(Attachment &context, Value receiver, ReturnSlot &out)
{
    TypeView view(context, receiver);
    out.ok(context.payload<TypeSnapshot>(*view.snapshot()).mro);
}
void type_base(Attachment &context, Value receiver, ReturnSlot &out)
{
    out.ok(context.payload<TypeObject>(receiver).base.value_or(context.builtin(CoreObject::None)));
}
void type_dict_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    auto dictionary = TypeView(context, receiver).dictionary();
    if (!dictionary)
        throw std::logic_error("ready type has no namespace");
    RootScope roots(context);
    context.allocate_instance<MappingProxyObject>(
        roots.root(context.builtin(CoreObject::MappingProxyType)), out, *dictionary);
}
void type_module_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    TypeView view(context, receiver);
    auto module = detail::name_lookup(context, *view.dictionary(), U"__module__");
    const auto &type = context.payload<TypeObject>(receiver);
    if (module && (!type.immutable || context.has_layout<StrObject>(*module))) {
        out.ok(*module);
        return;
    }
    if (type.immutable && type.declared_module)
        make_str(context, *type.declared_module, out);
    else
        raise_error(context, CoreObject::AttributeErrorType, U"__module__", out);
}
void type_module_set(Attachment &context, Value receiver, std::optional<Value> value,
                     ReturnSlot &out)
{
    if (context.payload<TypeObject>(receiver).immutable || !value) {
        type_error(context, U"cannot set or delete __module__ on this type", out);
        return;
    }
    RootScope roots(context);
    auto owner = roots.root(receiver), module = roots.root(*value);
    ReturnSlot key(context);
    make_str(context, U"__module__", key);
    detail::type_namespace_change(context, owner.get(context), U"__module__", key.value(),
                                  module.get(context));
    out.ok(context.builtin(CoreObject::None));
}
void type_abstract_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    auto dictionary = TypeView(context, receiver).dictionary();
    auto value = dictionary ? detail::name_lookup(context, *dictionary, U"__abstractmethods__")
                            : std::optional<Value>{};
    if (value)
        out.ok(*value);
    else
        raise_error(context, CoreObject::AttributeErrorType, U"__abstractmethods__", out);
}
void type_annotations_get(Attachment &context, Value receiver, ReturnSlot &out)
{
    if (context.payload<TypeObject>(receiver).immutable) {
        raise_error(context, CoreObject::AttributeErrorType, U"__annotations__", out);
        return;
    }
    RootScope roots(context);
    auto owner = roots.root(receiver);
    auto dictionary = roots.root(*TypeView(context, owner).dictionary());
    ReturnSlot key(context), value(context), empty(context);
    make_str(context, U"__annotations__", key);
    auto name = roots.root(key.value());
    dict_lookup(context, dictionary, name, value);
    if (value.tag() == ResultTag::Empty) {
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              empty);
        dict_setdefault(context, dictionary, name, roots.root(empty.value()), value);
    }
    if (value.tag() == ResultTag::Raised) {
        out.raised(value.exception());
        return;
    }
    detail::bind_descriptor(context, value.value(), {}, owner.get(context), out);
}
void type_annotations_set(Attachment &context, Value receiver, std::optional<Value> value,
                          ReturnSlot &out)
{
    if (context.payload<TypeObject>(receiver).immutable) {
        type_error(context, U"cannot set attributes of immutable type", out);
        return;
    }
    RootScope roots(context);
    auto owner = roots.root(receiver);
    if (value)
        roots.root(*value);
    ReturnSlot key(context);
    make_str(context, U"__annotations__", key);
    if (!detail::type_namespace_change(context, owner.get(context), U"__annotations__",
                                       value ? std::optional<Value>(key.value()) : std::nullopt,
                                       value)) {
        raise_error(context, CoreObject::AttributeErrorType, U"__annotations__", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
void type_abstract_set(Attachment &context, Value receiver, std::optional<Value> value,
                       ReturnSlot &out)
{
    if (context.payload<TypeObject>(receiver).immutable) {
        type_error(context, U"cannot set attributes of immutable type", out);
        return;
    }
    RootScope roots(context);
    auto owner = roots.root(receiver);
    ReturnSlot result(context), key(context);
    bool abstract = false;
    if (value) {
        auto rooted = roots.root(*value);
        truth(context, rooted, result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
        abstract = result.value() == context.builtin(CoreObject::True);
    }
    make_str(context, U"__abstractmethods__", key);
    if (!detail::type_namespace_change(context, owner.get(context), U"__abstractmethods__",
                                       value ? std::optional<Value>(key.value()) : std::nullopt,
                                       value, abstract)) {
        raise_error(context, CoreObject::AttributeErrorType, U"__abstractmethods__", out);
        return;
    }
    out.ok(context.builtin(CoreObject::None));
}
void method_function(Attachment &context, Value receiver, ReturnSlot &out)
{
    out.ok(context.payload<BoundMethodObject>(receiver).function);
}
void method_receiver(Attachment &context, Value receiver, ReturnSlot &out)
{
    out.ok(context.payload<BoundMethodObject>(receiver).self);
}
void static_function(Attachment &context, Value receiver, ReturnSlot &out)
{
    context.load(receiver, context.payload<StaticMethodObject>(receiver).callable, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
void class_function(Attachment &context, Value receiver, ReturnSlot &out)
{
    context.load(receiver, context.payload<ClassMethodObject>(receiver).callable, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
void cell_contents(Attachment &context, Value receiver, ReturnSlot &out)
{
    if (!read_cell(context, receiver, out))
        raise_error(context, CoreObject::ValueErrorType, U"cell is empty", out);
}
void cell_contents_set(Attachment &context, Value receiver, std::optional<Value> value,
                       ReturnSlot &out)
{
    write_cell(context, receiver, value);
    out.ok(context.builtin(CoreObject::None));
}
void type_name(Attachment &context, Value receiver, ReturnSlot &out)
{
    TypeView metadata(context, receiver);
    out.ok(std::get<Value>(metadata.name()));
}
void type_qualname(Attachment &context, Value receiver, ReturnSlot &out)
{
    TypeView metadata(context, receiver);
    out.ok(std::get<Value>(metadata.qualname()));
}
void change_type_name(Attachment &context, Value receiver, std::optional<Value> value,
                      bool qualified, ReturnSlot &out)
{
    auto &type = context.payload<TypeObject>(receiver);
    if (type.immutable || !value || !context.has_layout<StrObject>(*value) ||
        !is_subtype(context, context.type_of(*value), context.builtin(CoreObject::StrType))) {
        type_error(context, U"type name requires a mutable type and a str", out);
        return;
    }
    const auto &text = context.payload<StrObject>(*value).value;
    if (!qualified && text.find(U'\0') != std::u32string::npos) {
        raise_error(context, CoreObject::ValueErrorType,
                    U"type name must not contain null characters", out);
        return;
    }
    detail::type_name_store(context, receiver, *value, qualified);
    out.ok(context.builtin(CoreObject::None));
}
void type_name_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
{
    change_type_name(context, receiver, value, false, out);
}
void type_qualname_set(Attachment &context, Value receiver, std::optional<Value> value,
                       ReturnSlot &out)
{
    change_type_name(context, receiver, value, true, out);
}

void install_field(Attachment &context, Handle descriptor_type, Handle string_type, Handle owner,
                   std::u32string name, NativeGetter getter, NativeSetter setter)
{
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<StrObject>(string_type, out, std::move(name));
    auto label = roots.root(out.value());
    context.allocate_instance<GetSetDescriptorObject>(descriptor_type, out, owner.get(context),
                                                      label.get(context), getter, setter);
    auto field = roots.root(out.value());
    detail::name_store(context, *TypeView(context, owner).dictionary(), label.get(context),
                       field.get(context));
}
} // namespace
namespace detail {
void install_native_field(Attachment &context, std::span<const Handle> core, Handle owner,
                          std::u32string name, NativeGetter getter, NativeSetter setter)
{
    install_field(context, core[static_cast<std::size_t>(CoreObject::GetSetDescriptorType)],
                  core[static_cast<std::size_t>(CoreObject::StrType)], owner, std::move(name),
                  getter, setter);
}
void install_native_member(Attachment &context, std::span<const Handle> core, Handle owner,
                           std::u32string name, NativeGetter getter)
{
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], out,
                                         std::move(name));
    auto label = roots.root(out.value());
    context.allocate_instance<MemberDescriptorObject>(
        core[static_cast<std::size_t>(CoreObject::MemberDescriptorType)], out, owner.get(context),
        label.get(context), getter);
    name_store(context, *TypeView(context, owner).dictionary(), label.get(context), out.value());
}
void get_native_member(Attachment &context, Value value, std::optional<Value> receiver,
                       ReturnSlot &out)
{
    auto *field = member(context, value, out);
    if (!field)
        return;
    if (!receiver) {
        out.ok(value);
        return;
    }
    RootScope roots(context);
    roots.root(value);
    roots.root(*receiver);
    if (!member_receiver(context, *field, *receiver, out))
        return;
    if (const auto *index = std::get_if<std::size_t>(&field->access)) {
        context.load(*receiver, context.instance_slot(*receiver, *index), out);
        if (out.tag() == ResultTag::Empty)
            raise_error(context, CoreObject::AttributeErrorType, U"slot is unbound", out);
    } else
        std::get<NativeGetter>(field->access)(context, *receiver, out);
}
void install_slot_descriptors(Attachment &context, Handle type, std::size_t first)
{
    RootScope roots(context);
    auto descriptor_type = roots.root(context.builtin(CoreObject::MemberDescriptorType));
    const auto &payload = context.payload<TypeObject>(type);
    ReturnSlot out(context);
    for (std::size_t i = first; i < payload.slot_names.size(); ++i) {
        make_str(context, payload.slot_names[i], out);
        auto label = roots.root(out.value());
        context.allocate_instance<MemberDescriptorObject>(descriptor_type, out, type.get(context),
                                                          label.get(context), i);
        auto field = roots.root(out.value());
        name_store(context, *payload.dictionary, label.get(context), field.get(context));
    }
}
void get_native_field(Attachment &context, Value value, std::optional<Value> receiver,
                      ReturnSlot &out)
{
    auto *field = descriptor(context, value, out);
    if (!field)
        return;
    if (!receiver) {
        out.ok(value);
        return;
    }
    RootScope roots(context);
    roots.root(value);
    roots.root(*receiver);
    if (!compatible(context, *field, *receiver, out))
        return;
    if (!field->getter)
        throw std::logic_error("getset descriptor has no getter");
    field->getter(context, *receiver, out);
}
void install_instance_dictionary(Attachment &context, Handle type)
{
    RootScope roots(context);
    install_field(context, roots.root(context.builtin(CoreObject::GetSetDescriptorType)),
                  roots.root(context.builtin(CoreObject::StrType)), type, U"__dict__",
                  instance_dict_get, instance_dict_set);
}
void install_instance_weakrefs(Attachment &context, Handle type)
{
    RootScope roots(context);
    install_field(
        context, roots.root(context.builtin(CoreObject::GetSetDescriptorType)),
        roots.root(context.builtin(CoreObject::StrType)), type, U"__weakref__",
        +[](Attachment &context, Value self, ReturnSlot &out) {
            RootScope roots(context);
            std::vector<Value> refs;
            context.weakrefs(roots.root(self), roots, refs);
            out.ok(refs.empty() ? context.builtin(CoreObject::None) : refs.front());
        },
        nullptr);
}
void install_attribute_descriptors(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(context);
    auto property_type = at(CoreObject::PropertyType);
    SignatureLayout property_new_signature;
    property_new_signature.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    property_new_signature.varargs = U"args";
    property_new_signature.varkwargs = U"kwargs";
    install_native_attribute(context, core, property_type, U"__new__", property_new_signature,
                             property_new);
    ReturnSlot wrapper(context);
    auto function =
        *name_lookup(context, *TypeView(context, property_type).dictionary(), U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  function);
    auto wrapped = roots.root(wrapper.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), wrapper, U"__new__");
    name_store(context, *TypeView(context, property_type).dictionary(), wrapper.value(),
               wrapped.get(context));
    SignatureLayout property_init_signature;
    const auto none = at(CoreObject::None).get(context);
    property_init_signature.parameters = {{U"self", ParameterKind::PositionalOnly},
                                          {U"fget", ParameterKind::PositionalOrKeyword, none},
                                          {U"fset", ParameterKind::PositionalOrKeyword, none},
                                          {U"fdel", ParameterKind::PositionalOrKeyword, none},
                                          {U"doc", ParameterKind::PositionalOrKeyword, none}};
    install_native_attribute(context, core, property_type, U"__init__", property_init_signature,
                             property_init);
    SignatureLayout get;
    get.parameters = {{U"self", ParameterKind::PositionalOnly},
                      {U"instance", ParameterKind::PositionalOnly},
                      {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)}};
    auto descriptor_type = at(CoreObject::GetSetDescriptorType);
    install_native_attribute(context, core, descriptor_type, U"__get__", get, descriptor_get);
    get.parameters.pop_back();
    install_native_attribute(context, core, descriptor_type, U"__delete__", get, descriptor_delete);
    get.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, descriptor_type, U"__set__", get, descriptor_set);
    auto member_type = at(CoreObject::MemberDescriptorType);
    install_native_attribute(context, core, member_type, U"__set__", get, member_set);
    get.parameters.pop_back();
    install_native_attribute(context, core, member_type, U"__delete__", get, member_delete);
    get.parameters.push_back(
        {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)});
    install_native_attribute(context, core, member_type, U"__get__", get, member_get);
    install_native_attribute(context, core, property_type, U"__get__", get, property_get);
    get.parameters.pop_back();
    install_native_attribute(context, core, property_type, U"__delete__", get, property_delete);
    install_native_attribute(context, core, property_type, U"getter", get,
                             property_clone<&PropertyObject::getter>);
    install_native_attribute(context, core, property_type, U"setter", get,
                             property_clone<&PropertyObject::setter>);
    install_native_attribute(context, core, property_type, U"deleter", get,
                             property_clone<&PropertyObject::deleter>);
    get.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, property_type, U"__set__", get, property_set);
    auto string_type = at(CoreObject::StrType);
    install_field(context, descriptor_type, string_type, descriptor_type, U"__name__",
                  descriptor_name<GetSetDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, descriptor_type, U"__objclass__",
                  descriptor_owner<GetSetDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, descriptor_type, U"__qualname__",
                  descriptor_qualname<GetSetDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, member_type, U"__name__",
                  descriptor_name<MemberDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, member_type, U"__objclass__",
                  descriptor_owner<MemberDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, member_type, U"__qualname__",
                  descriptor_qualname<MemberDescriptorObject>, nullptr);
    install_field(context, descriptor_type, string_type, property_type, U"fget",
                  property_field<&PropertyObject::getter>, nullptr);
    install_field(context, descriptor_type, string_type, property_type, U"fset",
                  property_field<&PropertyObject::setter>, nullptr);
    install_field(context, descriptor_type, string_type, property_type, U"fdel",
                  property_field<&PropertyObject::deleter>, nullptr);
    install_field(context, descriptor_type, string_type, property_type, U"__doc__",
                  property_field<&PropertyObject::doc>, property_doc_set);
    install_field(context, descriptor_type, string_type, property_type, U"__isabstractmethod__",
                  property_abstract, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::ClassMethodType),
                  U"__isabstractmethod__", wrapper_abstract<ClassMethodObject>, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::StaticMethodType),
                  U"__isabstractmethod__", wrapper_abstract<StaticMethodObject>, nullptr);
    install_field(context, descriptor_type, string_type, roots.root(context.object_type()),
                  U"__class__", class_get, class_set);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__dict__", type_dict_get, nullptr);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__module__", type_module_get, type_module_set);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__abstractmethods__", type_abstract_get, type_abstract_set);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__annotations__", type_annotations_get, type_annotations_set);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__bases__", type_bases, nullptr);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__mro__", type_mro, nullptr);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__base__", type_base, nullptr);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__name__", type_name, type_name_set);
    install_field(context, descriptor_type, string_type, roots.root(context.type_type()),
                  U"__qualname__", type_qualname, type_qualname_set);
    install_field(context, descriptor_type, string_type, at(CoreObject::BoundMethodType),
                  U"__func__", method_function, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::BoundMethodType),
                  U"__self__", method_receiver, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::NativeFunctionType),
                  U"__self__", native_self, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::BaseExceptionType),
                  U"__dict__", instance_dict_get, instance_dict_set);
    install_field(context, descriptor_type, string_type, at(CoreObject::StaticMethodType),
                  U"__func__", static_function, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::StaticMethodType),
                  U"__wrapped__", static_function, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::ClassMethodType),
                  U"__func__", class_function, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::ClassMethodType),
                  U"__wrapped__", class_function, nullptr);
    install_field(context, descriptor_type, string_type, at(CoreObject::CellType), U"cell_contents",
                  cell_contents, cell_contents_set);
    install_field(context, descriptor_type, string_type, at(CoreObject::FunctionType), U"__dict__",
                  instance_dict_get, instance_dict_set);
}
} // namespace detail
} // namespace cxxpython::core
