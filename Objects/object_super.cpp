#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string super_repr(const Object &object)
        {
            auto *super = static_cast<const SuperObject *>(&object);
            return "<super: <class '" + type_qualified_name(super->start_type()) +
                   "'>, <" + type_qualified_name(super->object_type()) + " object>>";
        }

        void super_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *super = static_cast<const SuperObject *>(&object);
            if (super->start_type())
                visitor(super->start_type());
            if (super->object())
                visitor(super->object());
            if (super->object_type() && super->object_type() != super->start_type())
                visitor(super->object_type());
        }

        TypeObject *resolve_super_object_type(Runtime &runtime, TypeObject *start_type, Object *object)
        {
            if (auto *type_object = dynamic_cast<TypeObject *>(object))
            {
                if (!is_subclass(type_object, start_type))
                    raise_type_error(&runtime, "super(type, obj): obj must be an instance or subtype of type");
                return type_object;
            }

            TypeObject *object_type = object->type();
            if (!is_subclass(object_type, start_type))
                raise_type_error(&runtime, "super(type, obj): obj must be an instance or subtype of type");
            return object_type;
        }

        Object *super_new(TypeObject &type, const CallArguments &args)
        {
            Runtime *runtime = type.runtime();
            if (args.keywords && args.keywords->size() != 0)
                raise_type_error(runtime, "super() does not accept keyword arguments");

            if (args.positional.empty())
            {
                return runtime->new_zero_argument_super();
            }

            if (args.positional.size() != 2)
                raise_type_error(runtime, "super() expected 0 or 2 arguments");
            auto *start_type = dynamic_cast<TypeObject *>(args.positional[0]);
            if (!start_type)
                raise_type_error(runtime, "super() argument 1 must be a type");
            Object *object = args.positional[1];
            TypeObject *object_type = resolve_super_object_type(*runtime, start_type, object);
            return runtime->new_super(start_type, object, object_type);
        }

        void super_init(Object &object, const CallArguments &args)
        {
            if (args.keywords && args.keywords->size() != 0)
                raise_type_error(object.runtime(), "super() does not accept keyword arguments");
            if (args.positional.size() != 0 && args.positional.size() != 2)
                raise_type_error(object.runtime(), "super() expected 0 or 2 arguments");
        }
    }

    SuperObject::SuperObject(TypeObject *type, TypeObject *start_type, Object *object, TypeObject *object_type)
        : Object(type),
          start_type_(start_type),
          object_(object),
          object_type_(object_type)
    {
        if (start_type_)
            start_type_->incref();
        if (object_)
            object_->incref();
        if (object_type_)
            object_type_->incref();
    }

    SuperObject::~SuperObject()
    {
        clear_references();
    }

    void SuperObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(start_type_);
        CXXPYTHON_CLEAR_REF(object_);
        CXXPYTHON_CLEAR_REF(object_type_);
        Object::clear_references();
    }

    TypeObject *SuperObject::start_type() const noexcept
    {
        return start_type_;
    }

    Object *SuperObject::object() const noexcept
    {
        return object_;
    }

    TypeObject *SuperObject::object_type() const noexcept
    {
        return object_type_;
    }

    Object *super_get_attr_default(SuperObject *super, StrObject *name)
    {
        if (!super || !name)
            raise_attribute_error(super ? super->runtime() : (name ? name->runtime() : nullptr),
                                  "'super' object has no attribute '<null>'");

        const std::vector<TypeObject *> &mro = super->object_type()->mro;
        bool after_start = false;
        for (TypeObject *candidate : mro)
        {
            if (!candidate)
                continue;
            if (!after_start)
            {
                if (candidate == super->start_type())
                    after_start = true;
                continue;
            }
            if (!candidate->dict)
                continue;
            Object *value = candidate->dict->get(name);
            if (!value)
                continue;
            if (value->type() && value->type()->slots.descr_get)
            {
                Object *result = value->type()->slots.descr_get(*value, super->object(), super->start_type());
                if (!result)
                    raise_type_error(super->runtime(), "descriptor __get__ returned null");
                return result;
            }
            return value;
        }

        std::ostringstream message;
        message << "'super' object has no attribute '" << name->value << "'";
        raise_attribute_error(super->runtime(), message.str());
    }

    TypeSlots make_super_slots()
    {
        return TypeSlots{
            .repr = super_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = super_traverse,
            .new_object = super_new,
            .init = super_init,
        };
    }
}
