#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        void validate_single_descriptor_arg(Runtime *runtime,
                                            const std::string &name,
                                            const CallArguments &args)
        {
            require_no_keywords(runtime, name, args);
            require_positional_count(runtime, name, args, 1);
        }

        std::string staticmethod_repr(const Object &object)
        {
            auto *method = static_cast<const StaticMethodObject *>(&object);
            return "<staticmethod " + object_repr(method->callable()) + ">";
        }

        void staticmethod_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const StaticMethodObject *>(&object);
            if (method->callable())
                visitor(method->callable());
        }

        Object *staticmethod_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)instance;
            (void)owner;
            return static_cast<StaticMethodObject *>(&object)->callable();
        }

        Object *staticmethod_new(TypeObject &type, const CallArguments &args)
        {
            validate_single_descriptor_arg(type.runtime(), "staticmethod", args);
            return type.runtime()->allocate<StaticMethodObject>(&type, args.positional[0]);
        }

        void staticmethod_init(Object &object, const CallArguments &args)
        {
            validate_single_descriptor_arg(object.runtime(), "staticmethod", args);
        }

        std::string classmethod_repr(const Object &object)
        {
            auto *method = static_cast<const ClassMethodObject *>(&object);
            return "<classmethod " + object_repr(method->callable()) + ">";
        }

        void classmethod_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const ClassMethodObject *>(&object);
            if (method->callable())
                visitor(method->callable());
        }

        Object *classmethod_get(Object &object, Object *instance, TypeObject *owner)
        {
            auto *method = static_cast<ClassMethodObject *>(&object);
            TypeObject *type = owner ? owner : (instance ? instance->type() : nullptr);
            if (!type)
                return method;
            return method->runtime()->new_method(method->callable(), type);
        }

        Object *classmethod_new(TypeObject &type, const CallArguments &args)
        {
            validate_single_descriptor_arg(type.runtime(), "classmethod", args);
            return type.runtime()->allocate<ClassMethodObject>(&type, args.positional[0]);
        }

        void classmethod_init(Object &object, const CallArguments &args)
        {
            validate_single_descriptor_arg(object.runtime(), "classmethod", args);
        }
    }

    StaticMethodObject::StaticMethodObject(TypeObject *type, Object *callable)
        : Object(type), callable_(callable)
    {
        if (callable_)
            callable_->incref();
    }

    StaticMethodObject::~StaticMethodObject()
    {
        clear_references();
    }

    void StaticMethodObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(callable_);
        Object::clear_references();
    }

    Object *StaticMethodObject::callable() const noexcept
    {
        return callable_;
    }

    ClassMethodObject::ClassMethodObject(TypeObject *type, Object *callable)
        : Object(type), callable_(callable)
    {
        if (callable_)
            callable_->incref();
    }

    ClassMethodObject::~ClassMethodObject()
    {
        clear_references();
    }

    void ClassMethodObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(callable_);
        Object::clear_references();
    }

    Object *ClassMethodObject::callable() const noexcept
    {
        return callable_;
    }

    TypeSlots make_staticmethod_slots()
    {
        return TypeSlots{
            .repr = staticmethod_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = staticmethod_traverse,
            .descr_get = staticmethod_get,
            .new_object = staticmethod_new,
            .init = staticmethod_init,
        };
    }

    TypeSlots make_classmethod_slots()
    {
        return TypeSlots{
            .repr = classmethod_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = classmethod_traverse,
            .descr_get = classmethod_get,
            .new_object = classmethod_new,
            .init = classmethod_init,
        };
    }
}
