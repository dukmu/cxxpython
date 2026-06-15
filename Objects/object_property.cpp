#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <array>
#include <sstream>

namespace cxxpython
{
    namespace
    {
        Object *argument_or_null(const CallArguments &args, std::size_t index)
        {
            if (index >= args.positional.size())
                return nullptr;
            Object *value = args.positional[index];
            return value == value->runtime()->none() ? nullptr : value;
        }

        void validate_property_args(Runtime *runtime, const CallArguments &args)
        {
            require_no_keywords(runtime, "property", args);
            require_positional_at_most(runtime, "property", args, 4);
        }

        std::string property_state(Object *object)
        {
            return object ? "set" : "none";
        }

        std::string property_repr(const Object &object)
        {
            auto *property = static_cast<const PropertyObject *>(&object);
            return "<property getter=" + property_state(property->getter()) +
                   ", setter=" + property_state(property->setter()) +
                   ", deleter=" + property_state(property->deleter()) + ">";
        }

        std::int64_t property_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool property_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool property_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void property_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *property = static_cast<const PropertyObject *>(&object);
            std::array<Object *, 4> children{
                property->getter(),
                property->setter(),
                property->deleter(),
                property->doc(),
            };
            for (Object *child : children)
            {
                if (child)
                    visitor(child);
            }
        }

        Object *property_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)owner;
            auto *property = static_cast<PropertyObject *>(&object);
            if (!instance)
                return property;
            if (!property->getter())
                raise_attribute_error(property->runtime(), "unreadable attribute");
            return object_call(property->getter(), {instance});
        }

        void property_set(Object &object, Object *instance, Object *value)
        {
            auto *property = static_cast<PropertyObject *>(&object);
            if (!instance)
                raise_attribute_error(property->runtime(), value ? "can't set attribute" : "can't delete attribute");
            if (!value)
            {
                if (!property->deleter())
                    raise_attribute_error(property->runtime(), "can't delete attribute");
                (void)object_call(property->deleter(), {instance});
                return;
            }
            if (!property->setter())
                raise_attribute_error(property->runtime(), "can't set attribute");
            (void)object_call(property->setter(), {instance, value});
        }

        Object *property_new(TypeObject &type, const CallArguments &args)
        {
            validate_property_args(type.runtime(), args);
            return type.runtime()->allocate<PropertyObject>(
                &type,
                argument_or_null(args, 0),
                argument_or_null(args, 1),
                argument_or_null(args, 2),
                argument_or_null(args, 3));
        }

        void property_init(Object &object, const CallArguments &args)
        {
            validate_property_args(object.runtime(), args);
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

    PropertyObject::PropertyObject(TypeObject *type,
                                   Object *getter,
                                   Object *setter,
                                   Object *deleter,
                                   Object *doc)
        : Object(type),
          getter_(getter),
          setter_(setter),
          deleter_(deleter),
          doc_(doc)
    {
        if (getter_)
            getter_->incref();
        if (setter_)
            setter_->incref();
        if (deleter_)
            deleter_->incref();
        if (doc_)
            doc_->incref();
    }

    PropertyObject::~PropertyObject()
    {
        clear_references();
    }

    void PropertyObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(getter_);
        CXXPYTHON_CLEAR_REF(setter_);
        CXXPYTHON_CLEAR_REF(deleter_);
        CXXPYTHON_CLEAR_REF(doc_);
        Object::clear_references();
    }

    Object *PropertyObject::getter() const noexcept
    {
        return getter_;
    }

    Object *PropertyObject::setter() const noexcept
    {
        return setter_;
    }

    Object *PropertyObject::deleter() const noexcept
    {
        return deleter_;
    }

    Object *PropertyObject::doc() const noexcept
    {
        return doc_;
    }

    TypeSlots make_property_slots()
    {
        return TypeSlots{
            .repr = property_repr,
            .hash = property_hash,
            .equal = property_equal,
            .truth = property_truth,
            .traverse = property_traverse,
            .descr_get = property_get,
            .descr_set = property_set,
            .new_object = property_new,
            .init = property_init,
        };
    }

    void initialize_property_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("getter"),
                        runtime.new_method_descriptor("getter", type, property_getter_method));
        type->dict->set(runtime.new_str("setter"),
                        runtime.new_method_descriptor("setter", type, property_setter_method));
        type->dict->set(runtime.new_str("deleter"),
                        runtime.new_method_descriptor("deleter", type, property_deleter_method));
    }
}
