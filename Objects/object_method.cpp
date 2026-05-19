#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

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
        object_type_->dict->set(new_str("__setattr__"),
                                new_method_descriptor("__setattr__", object_type_, object_setattr_method));
        object_type_->dict->set(new_str("__delattr__"),
                                new_method_descriptor("__delattr__", object_type_, object_delattr_method));
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
