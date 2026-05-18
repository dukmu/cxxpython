#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <sstream>
#include <stdexcept>

namespace cxxpython
{
    namespace
    {
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
        return TypeSlots{default_repr, default_hash, default_equal, default_truth, default_traverse};
    }

    TypeSlots make_type_slots()
    {
        return TypeSlots{type_repr, default_hash, default_equal, default_truth, type_traverse};
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
}
