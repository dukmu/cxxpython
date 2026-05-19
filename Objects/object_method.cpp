#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string method_descriptor_repr(const Object &object)
        {
            auto *method = static_cast<const MethodDescriptorObject *>(&object);
            return "<method '" + method->name() + "' of '" +
                   type_qualified_name(method->owner()) + "' objects>";
        }

        std::int64_t identity_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool identity_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool always_true(const Object &object)
        {
            (void)object;
            return true;
        }

        void method_descriptor_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const MethodDescriptorObject *>(&object);
            if (method->owner())
                visitor(method->owner());
        }

        Object *method_descriptor_call(Object &object, const CallArguments &args)
        {
            auto *method = static_cast<MethodDescriptorObject *>(&object);
            return method->call_unbound(args);
        }

        Object *method_descriptor_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)owner;
            auto *method = static_cast<MethodDescriptorObject *>(&object);
            if (!instance)
                return method;
            return method->runtime()->new_bound_method(method, instance);
        }

        Object *list_append_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "list.append() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }

            auto *list = dynamic_cast<ListObject *>(self);
            if (!list)
                raise_type_error(&runtime, "list.append() receiver does not have list layout");

            list->append(args[0]);
            return runtime.none();
        }

        Object *object_getattribute_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "object.__getattribute__() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            auto *name = dynamic_cast<StrObject *>(args[0]);
            if (!name)
                raise_type_error(&runtime, "attribute name must be str");
            return object_get_attr_default(self, name);
        }

        Object *object_setattr_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 2)
            {
                std::ostringstream message;
                message << "object.__setattr__() takes exactly two arguments ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            auto *name = dynamic_cast<StrObject *>(args[0]);
            if (!name)
                raise_type_error(&runtime, "attribute name must be str");
            object_set_attr_default(self, name, args[1]);
            return runtime.none();
        }

        Object *object_delattr_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "object.__delattr__() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            auto *name = dynamic_cast<StrObject *>(args[0]);
            if (!name)
                raise_type_error(&runtime, "attribute name must be str");
            object_delete_attr_default(self, name);
            return runtime.none();
        }

        Object *object_init_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__init__() takes exactly zero arguments");
            return runtime.none();
        }

        Object *object_new_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (args.empty())
                raise_type_error(&runtime, "object.__new__() takes exactly one argument");
            auto *type = dynamic_cast<TypeObject *>(args[0]);
            if (!type)
                raise_type_error(&runtime, "object.__new__() argument 1 must be type");
            CallArguments empty{{}, nullptr};
            if (!type->slots.new_object)
                raise_type_error(&runtime, "object.__new__() cannot create '" + type_qualified_name(type) + "' instances");
            return type->slots.new_object(*type, empty);
        }

        Object *object_str_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__str__() takes exactly zero arguments");
            return runtime.new_str(object_str(self));
        }

        Object *object_format_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "object.__format__() takes exactly one argument");
            auto *format_spec = dynamic_cast<StrObject *>(args[0]);
            if (!format_spec)
                raise_type_error(&runtime, "format specifier must be a string");
            if (!format_spec->value.empty())
                raise_type_error(&runtime, "unsupported format string passed to object.__format__");
            return runtime.new_str(object_str(self));
        }

        Object *object_bool_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__bool__() takes exactly zero arguments");
            return is_true(self) ? runtime.true_value() : runtime.false_value();
        }

        Object *object_dir_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__dir__() takes exactly zero arguments");
            Object *dir_builtin = runtime.builtins_dict()->get(runtime.new_str("dir"));
            if (!dir_builtin)
                raise_type_error(&runtime, "dir builtin is not initialized");
            return object_call(dir_builtin, {self});
        }

        Object *object_sizeof_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__sizeof__() takes exactly zero arguments");
            return runtime.new_int(0);
        }

        Object *object_getstate_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__getstate__() takes exactly zero arguments");
            if (self && self->instance_dict())
                return self->instance_dict();
            return runtime.none();
        }

        Object *object_init_subclass_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "object.__init_subclass__() takes no arguments");
            return runtime.none();
        }

        Object *object_subclasshook_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "object.__subclasshook__() takes exactly one argument");
            return runtime.not_implemented();
        }

        Object *int_bool_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int.__bool__() takes exactly zero arguments");
            return is_true(self) ? runtime.true_value() : runtime.false_value();
        }

        Object *int_identity_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int conversion method takes exactly zero arguments");
            auto *integer = dynamic_cast<IntObject *>(self);
            if (!integer)
                raise_type_error(&runtime, "int conversion receiver does not have int layout");
            return integer;
        }

        Object *active_module_name(Runtime &runtime)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            DictObject *globals = frame ? frame->globals() : nullptr;
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name;
            }
            return runtime.new_str("builtins");
        }

        TypeObject *type_from_three_args(Runtime &runtime, TypeObject *metaclass, ObjectSpan args)
        {
            if (args.size() != 3)
                raise_type_error(&runtime, "type.__new__() takes exactly three arguments");
            auto *name = dynamic_cast<StrObject *>(args[0]);
            if (!name)
                raise_type_error(&runtime, "type.__new__() name must be a str");
            auto *bases_tuple = dynamic_cast<TupleObject *>(args[1]);
            if (!bases_tuple)
                raise_type_error(&runtime, "type.__new__() bases must be a tuple");
            auto *namespace_dict = dynamic_cast<DictObject *>(args[2]);
            if (!namespace_dict)
                raise_type_error(&runtime, "type.__new__() namespace must be a dict");

            std::vector<TypeObject *> bases;
            bases.reserve(bases_tuple->size());
            for (Object *base : bases_tuple->items())
            {
                auto *base_type = dynamic_cast<TypeObject *>(base);
                if (!base_type)
                    raise_type_error(&runtime, "type.__new__() bases must contain only types");
                bases.push_back(base_type);
            }

            ClassConstructionRequest request;
            request.name = name->value;
            request.bases = std::move(bases);
            request.namespace_dict = namespace_dict;
            if (auto *module = dynamic_cast<StrObject *>(active_module_name(runtime)))
                request.module_fallback = module->value;
            request.qualname = name->value;
            request.explicit_metaclass = metaclass == runtime.type_type() ? nullptr : metaclass;
            return runtime.new_class(std::move(request));
        }

        Object *type_call_method(Runtime &, Object *self, ObjectSpan args)
        {
            std::vector<Object *> positional(args.begin(), args.end());
            return object_call(self, CallArguments{std::move(positional), nullptr});
        }

        Object *type_new_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            auto *metaclass = dynamic_cast<TypeObject *>(self);
            if (!metaclass)
                raise_type_error(&runtime, "type.__new__() receiver must be a type");
            return type_from_three_args(runtime, metaclass, args);
        }

        Object *type_init_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (!args.empty() && args.size() != 3)
                raise_type_error(&runtime, "type.__init__() takes zero or three arguments");
            return runtime.none();
        }

        Object *type_prepare_method(Runtime &runtime, Object *, ObjectSpan args)
        {
            if (args.size() < 2)
                raise_type_error(&runtime, "type.__prepare__() missing name or bases");
            if (!dynamic_cast<StrObject *>(args[0]))
                raise_type_error(&runtime, "type.__prepare__() name must be a str");
            if (!dynamic_cast<TupleObject *>(args[1]))
                raise_type_error(&runtime, "type.__prepare__() bases must be a tuple");
            return runtime.new_dict();
        }

        Object *type_mro_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "type.mro() takes exactly zero arguments");
            auto *type = dynamic_cast<TypeObject *>(self);
            if (!type)
                raise_type_error(&runtime, "type.mro() receiver must be a type");
            std::vector<Object *> mro;
            mro.reserve(type->mro.size());
            for (TypeObject *item : type->mro)
                mro.push_back(item);
            return runtime.new_list(std::move(mro));
        }

        Object *type_instancecheck_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "type.__instancecheck__() takes exactly one argument");
            auto *type = dynamic_cast<TypeObject *>(self);
            if (!type)
                raise_type_error(&runtime, "type.__instancecheck__() receiver must be a type");
            return is_instance(args[0], type) ? runtime.true_value() : runtime.false_value();
        }

        Object *type_subclasscheck_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "type.__subclasscheck__() takes exactly one argument");
            auto *type = dynamic_cast<TypeObject *>(self);
            auto *derived = dynamic_cast<TypeObject *>(args[0]);
            if (!type || !derived)
                raise_type_error(&runtime, "type.__subclasscheck__() arguments must be types");
            return is_subclass(derived, type) ? runtime.true_value() : runtime.false_value();
        }

        PropertyObject *as_property(Runtime &runtime, Object *self)
        {
            auto *property = dynamic_cast<PropertyObject *>(self);
            if (!property)
                raise_type_error(&runtime, "property descriptor receiver does not have property layout");
            return property;
        }

        Object *property_getter_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "property.getter() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            PropertyObject *property = as_property(runtime, self);
            return runtime.allocate<PropertyObject>(
                property->type(),
                args[0],
                property->setter(),
                property->deleter(),
                property->doc());
        }

        Object *property_setter_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "property.setter() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            PropertyObject *property = as_property(runtime, self);
            return runtime.allocate<PropertyObject>(
                property->type(),
                property->getter(),
                args[0],
                property->deleter(),
                property->doc());
        }

        Object *property_deleter_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "property.deleter() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            PropertyObject *property = as_property(runtime, self);
            return runtime.allocate<PropertyObject>(
                property->type(),
                property->getter(),
                property->setter(),
                args[0],
                property->doc());
        }
    }

    MethodDescriptorObject::MethodDescriptorObject(TypeObject *type,
                                                   std::string name,
                                                   TypeObject *owner,
                                                   MethodDescriptorCallback callback)
        : Object(type), name_(std::move(name)), owner_(owner), callback_(std::move(callback))
    {
    }

    const std::string &MethodDescriptorObject::name() const noexcept
    {
        return name_;
    }

    TypeObject *MethodDescriptorObject::owner() const noexcept
    {
        return owner_;
    }

    Object *MethodDescriptorObject::call_unbound(const CallArguments &args)
    {
        if (args.keywords && args.keywords->size() != 0)
            raise_type_error(runtime(), "method descriptor does not accept keyword arguments");
        ObjectSpan all_args(args.positional.data(), args.positional.size());
        if (all_args.empty())
        {
            raise_type_error(runtime(),
                             "descriptor '" + name_ + "' of '" +
                                 type_qualified_name(owner_) +
                                 "' object needs an argument");
        }
        return call_bound(all_args.front(), all_args.subspan(1));
    }

    Object *MethodDescriptorObject::call_bound(Object *self, const CallArguments &args)
    {
        if (args.keywords && args.keywords->size() != 0)
            raise_type_error(runtime(), "method descriptor does not accept keyword arguments");
        return call_bound(self, ObjectSpan(args.positional.data(), args.positional.size()));
    }

    Object *MethodDescriptorObject::call_bound(Object *self, ObjectSpan args)
    {
        if (!callback_)
            raise_type_error(runtime(), "method descriptor has no implementation");
        if (!is_instance(self, owner_))
        {
            std::ostringstream message;
            message << "descriptor '" << name_ << "' for '"
                    << type_qualified_name(owner_)
                    << "' objects doesn't apply to a '"
                    << type_qualified_name(self ? self->type() : nullptr)
                    << "' object";
            raise_type_error(runtime(), message.str());
        }
        Object *result = callback_(*runtime(), self, args);
        if (!result)
            raise_type_error(runtime(), "method descriptor returned null");
        return result;
    }

    BoundMethodObject::BoundMethodObject(TypeObject *type, MethodDescriptorObject *method, Object *self)
        : Object(type), method_(method), self_(self)
    {
    }

    MethodDescriptorObject *BoundMethodObject::method() const noexcept
    {
        return method_;
    }

    Object *BoundMethodObject::self() const noexcept
    {
        return self_;
    }

    TypeSlots make_method_descriptor_slots()
    {
        return TypeSlots{
            .repr = method_descriptor_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = method_descriptor_traverse,
            .call = method_descriptor_call,
            .descr_get = method_descriptor_get,
        };
    }

    void Runtime::initialize_type_methods()
    {
        object_type_->dict->set(new_str("__getattribute__"),
                                new_method_descriptor("__getattribute__", object_type_, object_getattribute_method));
        object_type_->dict->set(new_str("__new__"),
                                new_method_descriptor("__new__", object_type_, object_new_method));
        object_type_->dict->set(new_str("__init__"),
                                new_method_descriptor("__init__", object_type_, object_init_method));
        object_type_->dict->set(new_str("__str__"),
                                new_method_descriptor("__str__", object_type_, object_str_method));
        object_type_->dict->set(new_str("__format__"),
                                new_method_descriptor("__format__", object_type_, object_format_method));
        object_type_->dict->set(new_str("__bool__"),
                                new_method_descriptor("__bool__", object_type_, object_bool_method));
        object_type_->dict->set(new_str("__dir__"),
                                new_method_descriptor("__dir__", object_type_, object_dir_method));
        object_type_->dict->set(new_str("__sizeof__"),
                                new_method_descriptor("__sizeof__", object_type_, object_sizeof_method));
        object_type_->dict->set(new_str("__getstate__"),
                                new_method_descriptor("__getstate__", object_type_, object_getstate_method));
        object_type_->dict->set(new_str("__init_subclass__"),
                                new_method_descriptor("__init_subclass__", object_type_, object_init_subclass_method));
        object_type_->dict->set(new_str("__subclasshook__"),
                                new_method_descriptor("__subclasshook__", object_type_, object_subclasshook_method));
        object_type_->dict->set(new_str("__setattr__"),
                                new_method_descriptor("__setattr__", object_type_, object_setattr_method));
        object_type_->dict->set(new_str("__delattr__"),
                                new_method_descriptor("__delattr__", object_type_, object_delattr_method));
        type_type_->dict->set(new_str("__call__"),
                              new_method_descriptor("__call__", type_type_, type_call_method));
        type_type_->dict->set(new_str("__new__"),
                              new_method_descriptor("__new__", type_type_, type_new_method));
        type_type_->dict->set(new_str("__init__"),
                              new_method_descriptor("__init__", type_type_, type_init_method));
        type_type_->dict->set(new_str("__prepare__"),
                              new_method_descriptor("__prepare__", type_type_, type_prepare_method));
        type_type_->dict->set(new_str("mro"),
                              new_method_descriptor("mro", type_type_, type_mro_method));
        type_type_->dict->set(new_str("__instancecheck__"),
                              new_method_descriptor("__instancecheck__", type_type_, type_instancecheck_method));
        type_type_->dict->set(new_str("__subclasscheck__"),
                              new_method_descriptor("__subclasscheck__", type_type_, type_subclasscheck_method));
        int_type_->dict->set(new_str("__bool__"),
                             new_method_descriptor("__bool__", int_type_, int_bool_method));
        int_type_->dict->set(new_str("__int__"),
                             new_method_descriptor("__int__", int_type_, int_identity_method));
        int_type_->dict->set(new_str("__index__"),
                             new_method_descriptor("__index__", int_type_, int_identity_method));
        list_type_->dict->set(new_str("append"),
                              new_method_descriptor("append", list_type_, list_append_method));
        property_type_->dict->set(new_str("getter"),
                                  new_method_descriptor("getter", property_type_, property_getter_method));
        property_type_->dict->set(new_str("setter"),
                                  new_method_descriptor("setter", property_type_, property_setter_method));
        property_type_->dict->set(new_str("deleter"),
                                  new_method_descriptor("deleter", property_type_, property_deleter_method));
    }
}
