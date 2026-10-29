#include "cxxpython/core/objects.hpp"

namespace cxxpython::core
{
    namespace
    {
        void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, std::move(message), out); }

        GetSetDescriptorObject *descriptor(Attachment &context, Value value, ReturnSlot &out)
        {
            if (context.type_of(value) != context.builtin(CoreObject::GetSetDescriptorType))
            { type_error(context, U"getset wrapper requires a descriptor", out); return nullptr; }
            return &context.payload<GetSetDescriptorObject>(value);
        }
        bool compatible(Attachment &context, const GetSetDescriptorObject &field, Value receiver, ReturnSlot &out)
        {
            if (is_subtype(context, context.type_of(receiver), field.owner)) return true;
            type_error(context, U"descriptor does not apply to this receiver", out);
            return false;
        }
        void descriptor_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[1] == context.builtin(CoreObject::None) && args[2] == context.builtin(CoreObject::None))
            { type_error(context, U"__get__(None, None) is invalid", out); return; }
            detail::get_native_field(context, args[0],
                                     args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
        }
        void descriptor_change(Attachment &context, std::span<const Value> args, bool erase, ReturnSlot &out)
        {
            auto *field = descriptor(context, args[0], out);
            if (!field || !compatible(context, *field, args[1], out)) return;
            if (!field->setter)
            { raise_error(context, CoreObject::AttributeErrorType, U"attribute is read-only", out); return; }
            field->setter(context, args[1], erase ? std::optional<Value>{} : args[2], out);
        }
        void descriptor_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { descriptor_change(context, args, false, out); }
        void descriptor_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { descriptor_change(context, args, true, out); }

        MemberDescriptorObject *member(Attachment &context, Value value, ReturnSlot &out)
        {
            if (context.type_of(value) != context.builtin(CoreObject::MemberDescriptorType))
            { type_error(context, U"member wrapper requires a member descriptor", out); return nullptr; }
            return &context.payload<MemberDescriptorObject>(value);
        }
        bool member_receiver(Attachment &context, const MemberDescriptorObject &field, Value receiver, ReturnSlot &out)
        {
            if (is_subtype(context, context.type_of(receiver), field.owner)) return true;
            type_error(context, U"member descriptor does not apply to this receiver", out);
            return false;
        }
        void member_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[1] == context.builtin(CoreObject::None) && args[2] == context.builtin(CoreObject::None))
            { type_error(context, U"__get__(None, None) is invalid", out); return; }
            detail::get_native_member(context, args[0],
                                      args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
        }
        void member_change(Attachment &context, std::span<const Value> args, bool erase, ReturnSlot &out)
        {
            auto *field = member(context, args[0], out);
            if (!field || !member_receiver(context, *field, args[1], out)) return;
            auto &slot = context.instance_slot(args[1], field->index);
            if (erase)
            {
                context.exchange(args[1], slot, {}, out);
                if (out.tag() == ResultTag::Empty)
                { raise_error(context, CoreObject::AttributeErrorType, U"slot is unbound", out); return; }
            }
            else context.store(args[1], slot, args[2]);
            out.ok(context.builtin(CoreObject::None));
        }
        void member_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { member_change(context, args, false, out); }
        void member_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { member_change(context, args, true, out); }

        void class_get(Attachment &context, Value receiver, ReturnSlot &out)
        { out.ok(context.type_of(receiver)); }
        void class_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
        {
            if (!value || !context.has_layout<TypeObject>(*value) ||
                !is_subtype(context, context.type_of(*value), context.type_type()))
            { type_error(context, U"__class__ must be set to a class", out); return; }
            auto &old_type = context.payload<TypeObject>(context.type_of(receiver));
            auto &new_type = context.payload<TypeObject>(*value);
            if (new_type.state != TypeState::Ready || old_type.immutable || new_type.immutable ||
                !is_layout_compatible(context, context.type_of(receiver), *value))
            { type_error(context, U"__class__ assignment requires compatible mutable types", out); return; }
            RootScope roots(context);
            context.store(roots.root(receiver), receiver.object()->type, roots.root(*value));
            out.ok(context.builtin(CoreObject::None));
        }
        void instance_dict_get(Attachment &context, Value receiver, ReturnSlot &out)
        {
            auto *field = context.instance_dictionary(receiver);
            if (!field) throw std::logic_error("dictionary descriptor requires dictionary storage");
            context.load(receiver, *field, out); if (out.tag() == ResultTag::Ok) return;
            RootScope roots(context);
            roots.root(receiver); ReturnSlot created(context);
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), created);
            for (;;)
            {
                if (context.compare_exchange(receiver, *field, {}, created.value())) { out.ok(created.value()); return; }
                context.load(receiver, *field, out); if (out.tag() == ResultTag::Ok) return;
                context.poll();
            }
        }
        void instance_dict_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
        {
            if (value && (!context.has_layout<DictObject>(*value) ||
                          !is_subtype(context, context.type_of(*value), context.builtin(CoreObject::DictType))))
            { type_error(context, U"__dict__ must be set to a dictionary", out); return; }
            auto *dictionary = context.instance_dictionary(receiver);
            if (!dictionary) throw std::logic_error("dictionary descriptor requires dictionary storage");
            context.store(receiver, *dictionary, value);
            out.ok(context.builtin(CoreObject::None));
        }
        void type_dict_get(Attachment &context, Value receiver, ReturnSlot &out)
        {
            auto dictionary = TypeView(context, receiver).dictionary();
            if (!dictionary) throw std::logic_error("ready type has no namespace");
            RootScope roots(context);
            context.allocate_instance<MappingProxyObject>(roots.root(context.builtin(CoreObject::MappingProxyType)),
                                                          out, *dictionary);
        }
        void method_function(Attachment &context, Value receiver, ReturnSlot &out)
        { out.ok(context.payload<BoundMethodObject>(receiver).function); }
        void method_receiver(Attachment &context, Value receiver, ReturnSlot &out)
        { out.ok(context.payload<BoundMethodObject>(receiver).self); }
        void static_function(Attachment &context, Value receiver, ReturnSlot &out)
        { out.ok(context.payload<StaticMethodObject>(receiver).callable); }
        void class_function(Attachment &context, Value receiver, ReturnSlot &out)
        { out.ok(context.payload<ClassMethodObject>(receiver).callable); }
        void cell_contents(Attachment &context, Value receiver, ReturnSlot &out)
        {
            if (!read_cell(context, receiver, out)) raise_error(context, CoreObject::ValueErrorType, U"cell is empty", out);
        }
        void cell_contents_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
        {
            write_cell(context, receiver, value);
            out.ok(context.builtin(CoreObject::None));
        }
        void type_name(Attachment &context, Value receiver, ReturnSlot &out)
        { TypeView metadata(context, receiver); out.ok(std::get<Value>(metadata.name())); }
        void type_qualname(Attachment &context, Value receiver, ReturnSlot &out)
        { TypeView metadata(context, receiver); out.ok(std::get<Value>(metadata.qualname())); }
        void change_type_name(Attachment &context, Value receiver, std::optional<Value> value, bool qualified, ReturnSlot &out)
        {
            auto &type = context.payload<TypeObject>(receiver);
            if (type.immutable || !value || !context.has_layout<StrObject>(*value) ||
                !is_subtype(context, context.type_of(*value), context.builtin(CoreObject::StrType)))
            { type_error(context, U"type name requires a mutable type and a str", out); return; }
            const auto &text = context.payload<StrObject>(*value).value;
            if (!qualified && text.find(U'\0') != std::u32string::npos)
            { raise_error(context, CoreObject::ValueErrorType, U"type name must not contain null characters", out); return; }
            detail::type_name_store(context, receiver, *value, qualified);
            out.ok(context.builtin(CoreObject::None));
        }
        void type_name_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
        { change_type_name(context, receiver, value, false, out); }
        void type_qualname_set(Attachment &context, Value receiver, std::optional<Value> value, ReturnSlot &out)
        { change_type_name(context, receiver, value, true, out); }

        void install_field(Attachment &context, Handle descriptor_type, Handle string_type, Handle owner,
                           std::u32string name, NativeGetter getter, NativeSetter setter)
        {
            RootScope roots(context);
            ReturnSlot out(context);
            context.allocate_instance<GetSetDescriptorObject>(descriptor_type, out, owner.get(context), getter, setter);
            auto field = roots.root(out.value());
            context.allocate_instance<StrObject>(string_type, out, std::move(name));
            detail::name_store(context, *TypeView(context, owner).dictionary(), out.value(), field.get(context));
        }
    }
    namespace detail
    {
        void get_native_member(Attachment &context, Value value, std::optional<Value> receiver, ReturnSlot &out)
        {
            auto *field = member(context, value, out);
            if (!field) return;
            if (!receiver) { out.ok(value); return; }
            RootScope roots(context);
            roots.root(value); roots.root(*receiver);
            if (!member_receiver(context, *field, *receiver, out)) return;
            context.load(*receiver, context.instance_slot(*receiver, field->index), out);
            if (out.tag() == ResultTag::Empty) raise_error(context, CoreObject::AttributeErrorType, U"slot is unbound", out);
        }
        void install_slot_descriptors(Attachment &context, Handle type, std::size_t first)
        {
            RootScope roots(context);
            auto descriptor_type = roots.root(context.builtin(CoreObject::MemberDescriptorType));
            const auto &payload = context.payload<TypeObject>(type);
            ReturnSlot out(context);
            for (std::size_t i = first; i < payload.slot_names.size(); ++i)
            {
                context.allocate_instance<MemberDescriptorObject>(descriptor_type, out, type.get(context), i);
                auto field = roots.root(out.value());
                make_str(context, payload.slot_names[i], out);
                name_store(context, *payload.dictionary, out.value(), field.get(context));
            }
        }
        void get_native_field(Attachment &context, Value value, std::optional<Value> receiver, ReturnSlot &out)
        {
            auto *field = descriptor(context, value, out);
            if (!field) return;
            if (!receiver) { out.ok(value); return; }
            RootScope roots(context);
            roots.root(value);
            roots.root(*receiver);
            if (!compatible(context, *field, *receiver, out)) return;
            if (!field->getter) throw std::logic_error("getset descriptor has no getter");
            field->getter(context, *receiver, out);
        }
        void install_instance_dictionary(Attachment &context, Handle type)
        {
            RootScope roots(context);
            install_field(context, roots.root(context.builtin(CoreObject::GetSetDescriptorType)),
                          roots.root(context.builtin(CoreObject::StrType)), type, U"__dict__",
                          instance_dict_get, instance_dict_set);
        }
        void install_attribute_descriptors(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            SignatureLayout get;
            get.parameters = {{U"self", ParameterKind::PositionalOnly}, {U"instance", ParameterKind::PositionalOnly},
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
            get.parameters.push_back({U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)});
            install_native_attribute(context, core, member_type, U"__get__", get, member_get);
            RootScope roots(context);
            auto string_type = at(CoreObject::StrType);
            install_field(context, descriptor_type, string_type, roots.root(context.object_type()), U"__class__", class_get, class_set);
            install_field(context, descriptor_type, string_type, roots.root(context.type_type()), U"__dict__", type_dict_get, nullptr);
            install_field(context, descriptor_type, string_type, roots.root(context.type_type()), U"__name__", type_name, type_name_set);
            install_field(context, descriptor_type, string_type, roots.root(context.type_type()), U"__qualname__", type_qualname, type_qualname_set);
            install_field(context, descriptor_type, string_type, at(CoreObject::BoundMethodType), U"__func__", method_function, nullptr);
            install_field(context, descriptor_type, string_type, at(CoreObject::BoundMethodType), U"__self__", method_receiver, nullptr);
            install_field(context, descriptor_type, string_type, at(CoreObject::StaticMethodType), U"__func__", static_function, nullptr);
            install_field(context, descriptor_type, string_type, at(CoreObject::ClassMethodType), U"__func__", class_function, nullptr);
            install_field(context, descriptor_type, string_type, at(CoreObject::CellType), U"cell_contents", cell_contents, cell_contents_set);
            install_field(context, descriptor_type, string_type, at(CoreObject::FunctionType), U"__dict__", instance_dict_get, instance_dict_set);
        }
    }
}
