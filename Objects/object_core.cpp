#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <sstream>
#include <stdexcept>

namespace cxxpython
{
    namespace
    {
        bool is_data_descriptor(Object *value)
        {
            return value && value->type() && value->type()->slots.descr_set;
        }

        bool allows_instance_dict(const TypeObject *type)
        {
            return type && (type->flags & TypeFlagHasInstanceDict) != 0;
        }

        Object *apply_descriptor(Object *value, Object *instance, TypeObject *owner)
        {
            if (!value || !value->type() || !value->type()->slots.descr_get)
                return value;
            Object *result = value->type()->slots.descr_get(*value, instance, owner);
            if (!result)
                raise_type_error(value->runtime(), "descriptor __get__ returned null");
            return result;
        }

        std::string type_repr(const Object &object)
        {
            return "<class '" + type_qualified_name(static_cast<const TypeObject *>(&object)) + "'>";
        }

        void type_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *type = static_cast<const TypeObject *>(&object);
            if (type->base)
                visitor(type->base);
            for (TypeObject *item : type->bases)
            {
                if (item)
                    visitor(item);
            }
            for (TypeObject *item : type->mro)
            {
                if (item)
                    visitor(item);
            }
            if (type->dict)
                visitor(type->dict);
        }
    }

    PythonError::PythonError(ExceptionObject *value)
        : value_(value), what_(value ? value->message() : "")
    {
    }

    ExceptionObject *PythonError::value() const noexcept
    {
        return value_;
    }

    TypeObject *PythonError::type() const noexcept
    {
        return value_ ? value_->type() : nullptr;
    }

    std::string PythonError::type_name() const
    {
        return type_qualified_name(type());
    }

    const char *PythonError::what() const noexcept
    {
        return what_.c_str();
    }

    Object::Object(TypeObject *type)
        : type_(type)
    {
    }

    TypeObject *Object::type() const noexcept
    {
        return type_;
    }

    Runtime *Object::runtime() const noexcept
    {
        return runtime_;
    }

    void Object::set_runtime(Runtime *runtime) noexcept
    {
        runtime_ = runtime;
    }

    void Object::set_type(TypeObject *type) noexcept
    {
        type_ = type;
    }

    std::string Object::repr() const
    {
        if (type_ && type_->slots.repr)
            return type_->slots.repr(*this);
        return default_repr(*this);
    }

    std::int64_t Object::hash() const
    {
        if (type_ && type_->slots.hash)
            return type_->slots.hash(*this);
        return default_hash(*this);
    }

    bool Object::equals(const Object &other) const
    {
        if (type_ && type_->slots.equal)
            return type_->slots.equal(*this, other);
        return default_equal(*this, other);
    }

    bool Object::truth() const
    {
        if (type_ && type_->slots.truth)
            return type_->slots.truth(*this);
        return default_truth(*this);
    }

    void Object::visit_children(const VisitChildren &visitor) const
    {
        if (type_ && type_->slots.traverse)
            type_->slots.traverse(*this, visitor);
        else
            default_traverse(*this, visitor);
        if (instance_dict_)
            visitor(instance_dict_);
    }

    DictObject *Object::instance_dict() const noexcept
    {
        return instance_dict_;
    }

    void Object::set_instance_dict(DictObject *dict) noexcept
    {
        instance_dict_ = dict;
    }

    TypeObject::TypeObject(std::string name)
        : Object(nullptr), name(std::move(name))
    {
    }

    bool is_subclass(const TypeObject *derived, const TypeObject *base) noexcept
    {
        if (!derived || !base)
            return false;
        if (derived == base)
            return true;
        for (const TypeObject *type : derived->mro)
        {
            if (type == base)
                return true;
        }
        return false;
    }

    bool is_instance(const Object *object, const TypeObject *type) noexcept
    {
        return object && type && is_subclass(object->type(), type);
    }

    bool is_true(const Object *object)
    {
        return object && object->truth();
    }

    std::int64_t object_hash(const Object *object)
    {
        if (!object)
            raise_type_error(nullptr, "hash() argument cannot be null");
        return object->hash();
    }

    bool object_equal(const Object *left, const Object *right)
    {
        if (left == right)
            return true;
        if (!left || !right)
            return false;
        return left->equals(*right);
    }

    Object *object_binary_add(Object *left, Object *right)
    {
        if (!left || !right)
            raise_type_error(left ? left->runtime() : (right ? right->runtime() : nullptr),
                             "unsupported operand type(s) for +: '<null>' and '<null>'");

        if (left->type() && left->type()->slots.binary_add)
            return left->type()->slots.binary_add(*left, *right);

        std::ostringstream message;
        message << "unsupported operand type(s) for +: '"
                << type_qualified_name(left->type())
                << "' and '"
                << type_qualified_name(right->type())
                << "'";
        raise_type_error(left->runtime(), message.str());
    }

    Object *object_call(Object *callable, const std::vector<Object *> &args)
    {
        if (!callable)
            raise_type_error(nullptr, "'<null>' object is not callable");

        if (callable->type() && callable->type()->slots.call)
            return callable->type()->slots.call(*callable, args);

        std::ostringstream message;
        message << "'" << type_qualified_name(callable->type()) << "' object is not callable";
        raise_type_error(callable->runtime(), message.str());
    }

    Object *type_lookup_attribute(TypeObject *type, StrObject *name)
    {
        if (!type || !name)
            return nullptr;
        for (TypeObject *candidate : type->mro)
        {
            if (!candidate || !candidate->dict)
                continue;
            if (Object *value = candidate->dict->get(name))
                return value;
        }
        return nullptr;
    }

    Object *object_get_attr(Object *object, StrObject *name)
    {
        if (!object || !name)
            raise_attribute_error(object ? object->runtime() : (name ? name->runtime() : nullptr),
                                  "'<null>' object has no attribute '<null>'");

        if (auto *module = dynamic_cast<ModuleObject *>(object))
        {
            if (Object *value = module->dict()->get(name))
                return value;
        }

        if (auto *type = dynamic_cast<TypeObject *>(object))
        {
            Object *metatype_value = type->type() ? type_lookup_attribute(type->type(), name) : nullptr;
            if (is_data_descriptor(metatype_value))
                return apply_descriptor(metatype_value, object, type->type());
            if (Object *value = type_lookup_attribute(type, name))
                return apply_descriptor(value, nullptr, type);
            if (metatype_value)
                return apply_descriptor(metatype_value, object, type->type());
        }
        else
        {
            Object *type_value = type_lookup_attribute(object->type(), name);
            if (is_data_descriptor(type_value))
                return apply_descriptor(type_value, object, object->type());
            if (object->instance_dict())
            {
                if (Object *value = object->instance_dict()->get(name))
                    return value;
            }
            if (type_value)
                return apply_descriptor(type_value, object, object->type());
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object has no attribute '"
                << name->value << "'";
        raise_attribute_error(object->runtime(), message.str());
    }

    Object *object_get_attr(Object *object, const std::string &name)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        return object_get_attr(object, runtime->new_str(name));
    }

    void object_set_attr(Object *object, StrObject *name, Object *value)
    {
        if (!object || !name || !value)
            raise_attribute_error(object ? object->runtime() : nullptr,
                                  "'<null>' object has no attribute '<null>'");

        if (auto *module = dynamic_cast<ModuleObject *>(object))
        {
            module->dict()->set(name, value);
            return;
        }

        if (auto *type = dynamic_cast<TypeObject *>(object))
        {
            Object *metatype_value = type->type() ? type_lookup_attribute(type->type(), name) : nullptr;
            if (metatype_value && metatype_value->type() && metatype_value->type()->slots.descr_set)
            {
                metatype_value->type()->slots.descr_set(*metatype_value, object, value);
                return;
            }
            type->dict->set(name, value);
            return;
        }

        Object *type_value = type_lookup_attribute(object->type(), name);
        if (type_value && type_value->type() && type_value->type()->slots.descr_set)
        {
            type_value->type()->slots.descr_set(*type_value, object, value);
            return;
        }

        if (!allows_instance_dict(object->type()))
        {
            std::ostringstream message;
            message << "'" << type_qualified_name(object->type())
                    << "' object has no attribute '"
                    << name->value << "'";
            raise_attribute_error(object->runtime(), message.str());
        }

        if (!object->instance_dict())
            object->set_instance_dict(object->runtime()->new_dict());
        object->instance_dict()->set(name, value);
    }

    void object_set_attr(Object *object, const std::string &name, Object *value)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        object_set_attr(object, runtime->new_str(name), value);
    }

    std::string type_qualified_name(const TypeObject *type)
    {
        if (!type)
            return "<null>";
        if (type->module.empty() || type->module == "builtins")
            return type->name;
        return type->module + "." + type->name;
    }

    std::string object_repr(const Object *object)
    {
        if (!object)
            return "<null>";
        return object->repr();
    }

    TypeSlots make_object_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = default_traverse,
        };
    }

    TypeSlots make_type_slots()
    {
        return TypeSlots{
            .repr = type_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = type_traverse,
        };
    }

    void raise_type_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python TypeError without a runtime");
        throw PythonError(runtime->new_type_error(std::move(message)));
    }

    void raise_index_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python IndexError without a runtime");
        throw PythonError(runtime->new_index_error(std::move(message)));
    }

    void raise_syntax_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python SyntaxError without a runtime");
        throw PythonError(runtime->new_syntax_error(std::move(message)));
    }

    void raise_name_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python NameError without a runtime");
        throw PythonError(runtime->new_name_error(std::move(message)));
    }

    void raise_attribute_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python AttributeError without a runtime");
        throw PythonError(runtime->new_attribute_error(std::move(message)));
    }
}
