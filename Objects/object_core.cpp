#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

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

        Object *call_getattr_hook(Object *hook, Object *instance, TypeObject *owner, StrObject *name)
        {
            if (!hook)
                return nullptr;
            Object *callable = apply_descriptor(hook, instance, owner);
            return object_call(callable, {name});
        }

        Object *call_attribute_hook(Object *hook, Object *instance, TypeObject *owner, const std::vector<Object *> &args)
        {
            if (!hook)
                return nullptr;
            Object *callable = apply_descriptor(hook, instance, owner);
            return object_call(callable, args);
        }

        std::string type_repr(const Object &object)
        {
            return "<class '" + type_qualified_name(static_cast<const TypeObject *>(&object)) + "'>";
        }

        void type_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *type = static_cast<const TypeObject *>(&object);
            if (type->type() && type->type() != type)
                visitor(type->type());
            if (type->base)
                visitor(type->base);
            if (type->solid_base && type->solid_base != type->base)
                visitor(type->solid_base);
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

        Object *object_new(TypeObject &type, const CallArguments &args)
        {
            (void)args;
            return type.runtime()->allocate<Object>(&type);
        }

        void call_init_slot(TypeObject &type, Object *instance, const CallArguments &args)
        {
            if (!is_instance(instance, &type))
                return;
            if ((type.flags & TypeFlagHeapType) != 0)
            {
                Object *init_descriptor = type_lookup_attribute(&type, type.runtime()->new_str("__init__"));
                Object *default_init = type.runtime()->object_type()->dict->get(type.runtime()->new_str("__init__"));
                if (type.slots.init && (!init_descriptor || init_descriptor == default_init))
                {
                    type.slots.init(*instance, args);
                    return;
                }
                if (init_descriptor)
                {
                    Object *init = apply_descriptor(init_descriptor, instance, &type);
                    auto init_root = type.runtime()->root(init);
                    Object *result = object_call(init_root.get(), args);
                    if (result != type.runtime()->none())
                        raise_type_error(type.runtime(), "__init__() should return None");
                    return;
                }
            }
            if (type.slots.init)
            {
                type.slots.init(*instance, args);
                return;
            }
            require_no_keywords(type.runtime(), type_qualified_name(&type), args);
            require_no_positional(type.runtime(), type_qualified_name(&type), args);
        }

        std::string active_module_name(Runtime &runtime)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            DictObject *globals = frame ? frame->globals() : nullptr;
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            return "builtins";
        }

        TypeObject *type_new_from_three_args(TypeObject &metaclass, const CallArguments &args)
        {
            Runtime *runtime = metaclass.runtime();
            auto *name = dynamic_cast<StrObject *>(args.positional[0]);
            if (!name)
                raise_type_error(runtime, "type() name must be a str");
            auto *bases_tuple = dynamic_cast<TupleObject *>(args.positional[1]);
            if (!bases_tuple)
                raise_type_error(runtime, "type() bases must be a tuple");
            auto *namespace_dict = dynamic_cast<DictObject *>(args.positional[2]);
            if (!namespace_dict)
                raise_type_error(runtime, "type() namespace must be a dict");

            std::vector<TypeObject *> bases;
            bases.reserve(bases_tuple->size());
            for (Object *base : bases_tuple->items())
            {
                auto *base_type = dynamic_cast<TypeObject *>(base);
                if (!base_type)
                    raise_type_error(runtime, "type() bases must contain only types");
                bases.push_back(base_type);
            }
            ClassConstructionRequest request;
            request.name = name->value;
            request.bases = std::move(bases);
            request.namespace_dict = namespace_dict;
            request.module_fallback = active_module_name(*runtime);
            request.qualname = name->value;
            request.explicit_metaclass = &metaclass == runtime->type_type() ? nullptr : &metaclass;
            return runtime->new_class(std::move(request));
        }

        Object *type_call(Object &object, const CallArguments &args)
        {
            auto *type = dynamic_cast<TypeObject *>(&object);
            if (!type)
                raise_type_error(object.runtime(), "type call target has invalid layout");
            if (is_subclass(type, type->runtime()->type_type()))
            {
                require_no_keywords(type->runtime(), "type", args);
                if (type == type->runtime()->type_type() && args.positional.size() == 1)
                {
                    if (!args.positional[0])
                        raise_type_error(type->runtime(), "type() argument cannot be null");
                    return args.positional[0]->type();
                }
                if (args.positional.size() != 3)
                    raise_type_error(type->runtime(), "type() takes 1 or 3 arguments");
                return type_new_from_three_args(*type, args);
            }
            if (!type->slots.new_object)
            {
                std::ostringstream message;
                message << "cannot create '" << type_qualified_name(type) << "' instances";
                raise_type_error(type->runtime(), message.str());
            }
            Object *instance = type->slots.new_object(*type, args);
            if (!instance)
                raise_type_error(type->runtime(), "__new__ returned null");
            call_init_slot(*type, instance, args);
            return instance;
        }

        bool is_not_implemented(Object *object)
        {
            return object && object->runtime() && object == object->runtime()->not_implemented();
        }

        Object *call_binary_slot(BinaryOpSlot slot,
                                 const Object &left,
                                 const Object &right,
                                 BinaryOp op,
                                 const char *null_message)
        {
            Object *result = slot(left, right, op);
            if (!result)
                raise_type_error(left.runtime(), null_message);
            return result;
        }

        Object *try_reflected_binary_op(Object *left, Object *right, BinaryOp op)
        {
            if (!right || !right->type() || !right->type()->slots.reflected_binary_op)
                return nullptr;
            Object *result = call_binary_slot(
                right->type()->slots.reflected_binary_op,
                *right,
                *left,
                op,
                "reflected binary operator slot returned null");
            return is_not_implemented(result) ? nullptr : result;
        }

        RichCompareOp reflected_rich_compare_op(RichCompareOp op)
        {
            switch (op)
            {
            case RichCompareOp::Equal:
                return RichCompareOp::Equal;
            case RichCompareOp::NotEqual:
                return RichCompareOp::NotEqual;
            case RichCompareOp::Less:
                return RichCompareOp::Greater;
            case RichCompareOp::LessEqual:
                return RichCompareOp::GreaterEqual;
            case RichCompareOp::Greater:
                return RichCompareOp::Less;
            case RichCompareOp::GreaterEqual:
                return RichCompareOp::LessEqual;
            }
            return op;
        }

        Object *call_rich_compare_slot(RichCompareSlot slot,
                                       const Object &left,
                                       const Object &right,
                                       RichCompareOp op)
        {
            Object *result = slot(left, right, op);
            if (!result)
                raise_type_error(left.runtime(), "rich comparison returned null");
            return result;
        }

        Object *try_rich_compare(Object *left, Object *right, RichCompareOp op)
        {
            if (!left || !left->type() || !left->type()->slots.rich_compare)
                return nullptr;
            Object *result = call_rich_compare_slot(left->type()->slots.rich_compare, *left, *right, op);
            return is_not_implemented(result) ? nullptr : result;
        }

        Object *try_reflected_rich_compare(Object *left, Object *right, RichCompareOp op)
        {
            return try_rich_compare(right, left, reflected_rich_compare_op(op));
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

    Object *object_rich_compare(Object *left, Object *right, RichCompareOp op)
    {
        Runtime *runtime = left ? left->runtime() : (right ? right->runtime() : nullptr);
        if (!left || !right)
            raise_type_error(runtime, "rich comparison operands cannot be null");

        const bool right_is_strict_subclass =
            left->type() && right->type() &&
            left->type() != right->type() &&
            is_subclass(right->type(), left->type());

        if (right_is_strict_subclass)
        {
            if (Object *result = try_reflected_rich_compare(left, right, op))
                return result;
        }

        if (Object *result = try_rich_compare(left, right, op))
            return result;

        if (!right_is_strict_subclass && right->type() && right->type() != left->type())
        {
            if (Object *result = try_reflected_rich_compare(left, right, op))
                return result;
        }

        if (op == RichCompareOp::Equal || op == RichCompareOp::NotEqual)
        {
            const bool equal = object_equal(left, right);
            return equal == (op == RichCompareOp::Equal)
                       ? left->runtime()->true_value()
                       : left->runtime()->false_value();
        }

        std::ostringstream message;
        message << "comparison not supported between instances of '"
                << type_qualified_name(left->type())
                << "' and '"
                << type_qualified_name(right->type())
                << "'";
        raise_type_error(left->runtime(), message.str());
    }

    Object *object_binary_op(Object *left, Object *right, BinaryOp op)
    {
        if (!left || !right)
            raise_type_error(left ? left->runtime() : (right ? right->runtime() : nullptr),
                             unsupported_binary_op_message(op, left, right));

        const bool right_is_strict_subclass =
            left->type() && right->type() &&
            left->type() != right->type() &&
            is_subclass(right->type(), left->type());

        if (right_is_strict_subclass)
        {
            if (Object *result = try_reflected_binary_op(left, right, op))
                return result;
        }

        if (left->type() && left->type()->slots.binary_op)
        {
            Object *result = call_binary_slot(
                left->type()->slots.binary_op,
                *left,
                *right,
                op,
                "binary operator slot returned null");
            if (!is_not_implemented(result))
                return result;
        }

        if (!right_is_strict_subclass && right->type() && right->type() != left->type())
        {
            if (Object *result = try_reflected_binary_op(left, right, op))
                return result;
        }

        raise_type_error(left->runtime(), unsupported_binary_op_message(op, left, right));
    }

    Object *object_inplace_binary_op(Object *left, Object *right, BinaryOp op)
    {
        if (!left || !right)
            raise_type_error(left ? left->runtime() : (right ? right->runtime() : nullptr),
                             unsupported_binary_op_message(op, left, right));

        if (left->type() && left->type()->slots.inplace_binary_op)
        {
            Object *result = call_binary_slot(
                left->type()->slots.inplace_binary_op,
                *left,
                *right,
                op,
                "in-place binary operator slot returned null");
            if (!is_not_implemented(result))
                return result;
        }

        return object_binary_op(left, right, op);
    }

    Object *object_unary_op(Object *operand, UnaryOp op)
    {
        if (!operand)
            raise_type_error(nullptr, unsupported_unary_op_message(op, nullptr));

        if (operand->type() && operand->type()->slots.unary_op)
        {
            Object *result = operand->type()->slots.unary_op(*operand, op);
            if (!result)
                raise_type_error(operand->runtime(), "unary operator slot returned null");
            return result;
        }

        raise_type_error(operand->runtime(), unsupported_unary_op_message(op, operand));
    }

    Object *object_call(Object *callable, const std::vector<Object *> &args)
    {
        return object_call(callable, CallArguments{args, nullptr});
    }

    Object *object_call(Object *callable, const CallArguments &args)
    {
        if (!callable)
            raise_type_error(nullptr, "'<null>' object is not callable");

        if (callable->type() && callable->type()->slots.call)
            return callable->type()->slots.call(*callable, args);

        std::ostringstream message;
        message << "'" << type_qualified_name(callable->type()) << "' object is not callable";
        raise_type_error(callable->runtime(), message.str());
    }

    std::size_t object_length(Object *object)
    {
        if (!object)
            raise_type_error(nullptr, "object of type '<null>' has no len()");
        if (object->type() && object->type()->slots.length)
            return object->type()->slots.length(*object);

        std::ostringstream message;
        message << "object of type '" << type_qualified_name(object->type())
                << "' has no len()";
        raise_type_error(object->runtime(), message.str());
    }

    Object *object_get_item(Object *object, Object *key)
    {
        if (!object || !key)
            raise_type_error(object ? object->runtime() : nullptr, "subscript target and key cannot be null");
        if (object->type() && object->type()->slots.getitem)
            return object->type()->slots.getitem(*object, key);

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object is not subscriptable";
        raise_type_error(object->runtime(), message.str());
    }

    void object_set_item(Object *object, Object *key, Object *value)
    {
        if (!object || !key || !value)
            raise_type_error(object ? object->runtime() : nullptr, "subscript target, key, and value cannot be null");
        if (object->type() && object->type()->slots.setitem)
        {
            object->type()->slots.setitem(*object, key, value);
            return;
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object does not support item assignment";
        raise_type_error(object->runtime(), message.str());
    }

    void object_delete_item(Object *object, Object *key)
    {
        if (!object || !key)
            raise_type_error(object ? object->runtime() : nullptr, "subscript target and key cannot be null");
        if (object->type() && object->type()->slots.delitem)
        {
            object->type()->slots.delitem(*object, key);
            return;
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object does not support item deletion";
        raise_type_error(object->runtime(), message.str());
    }

    bool object_contains(Object *object, Object *key)
    {
        if (!object || !key)
            raise_type_error(object ? object->runtime() : nullptr, "contains target and key cannot be null");
        if (object->type() && object->type()->slots.contains)
            return object->type()->slots.contains(*object, key);

        std::ostringstream message;
        message << "argument of type '" << type_qualified_name(object->type())
                << "' is not iterable";
        raise_type_error(object->runtime(), message.str());
    }

    Object *object_iter(Object *object)
    {
        if (!object)
            raise_type_error(nullptr, "'<null>' object is not iterable");
        if (object->type() && object->type()->slots.iter)
        {
            Object *iterator = object->type()->slots.iter(*object);
            if (!iterator)
                raise_type_error(object->runtime(), "__iter__ returned null");
            if (!iterator->type() || !iterator->type()->slots.iternext)
                raise_type_error(object->runtime(), "iter() returned non-iterator");
            return iterator;
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object is not iterable";
        raise_type_error(object->runtime(), message.str());
    }

    Object *object_next(Object *iterator)
    {
        if (!iterator)
            raise_type_error(nullptr, "'<null>' object is not an iterator");
        if (iterator->type() && iterator->type()->slots.iternext)
        {
            Object *item = iterator->type()->slots.iternext(*iterator);
            if (!item)
                raise_type_error(iterator->runtime(), "__next__ returned null");
            return item;
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(iterator->type())
                << "' object is not an iterator";
        raise_type_error(iterator->runtime(), message.str());
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

    Object *object_get_attr_default(Object *object, StrObject *name)
    {
        if (!object || !name)
            raise_attribute_error(object ? object->runtime() : (name ? name->runtime() : nullptr),
                                  "'<null>' object has no attribute '<null>'");

        if (auto *super = dynamic_cast<SuperObject *>(object))
            return super_get_attr_default(super, name);

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

    namespace
    {
        bool is_attribute_error(const PythonError &error)
        {
            return error.type() && error.value() &&
                   error.type() == error.value()->runtime()->attribute_error_type();
        }

        Object *call_getattribute_override(Object *object, StrObject *name)
        {
            if (!object || !name || !object->type())
                return nullptr;

            Runtime *runtime = object->runtime();
            Object *default_getattribute =
                runtime->object_type()->dict->get(runtime->new_str("__getattribute__"));
            Object *hook =
                type_lookup_attribute(object->type(), runtime->new_str("__getattribute__"));
            if (!hook || hook == default_getattribute)
                return nullptr;

            Object *callable = apply_descriptor(hook, object, object->type());
            return object_call(callable, {name});
        }

        Object *lookup_type_special(Object *object, const char *name)
        {
            if (!object || !object->type())
                return nullptr;
            Runtime *runtime = object->runtime();
            return type_lookup_attribute(object->type(), runtime->new_str(name));
        }

        Object *lookup_default_object_special(Runtime *runtime, const char *name)
        {
            return runtime->object_type()->dict->get(runtime->new_str(name));
        }

        Object *call_setattr_override(Object *object, StrObject *name, Object *value)
        {
            if (!object || !name || !value)
                return nullptr;

            Runtime *runtime = object->runtime();
            Object *default_setattr = lookup_default_object_special(runtime, "__setattr__");
            Object *hook = lookup_type_special(object, "__setattr__");
            if (!hook || hook == default_setattr)
                return nullptr;
            return call_attribute_hook(hook, object, object->type(), {name, value});
        }

        Object *call_delattr_override(Object *object, StrObject *name)
        {
            if (!object || !name)
                return nullptr;

            Runtime *runtime = object->runtime();
            Object *default_delattr = lookup_default_object_special(runtime, "__delattr__");
            Object *hook = lookup_type_special(object, "__delattr__");
            if (!hook || hook == default_delattr)
                return nullptr;
            return call_attribute_hook(hook, object, object->type(), {name});
        }

        Object *call_getattr_fallback(Object *object, StrObject *name)
        {
            if (!object || !name)
                return nullptr;

            if (auto *module = dynamic_cast<ModuleObject *>(object))
            {
                if (Object *hook = module->dict()->get(object->runtime()->new_str("__getattr__")))
                    return object_call(hook, {name});
            }

            if (auto *type = dynamic_cast<TypeObject *>(object))
            {
                return call_getattr_hook(
                    type->type() ? type_lookup_attribute(type->type(), object->runtime()->new_str("__getattr__")) : nullptr,
                    object,
                    type->type(),
                    name);
            }

            return call_getattr_hook(
                type_lookup_attribute(object->type(), object->runtime()->new_str("__getattr__")),
                object,
                object->type(),
                name);
        }
    }

    Object *object_get_attr(Object *object, StrObject *name)
    {
        if (!object || !name)
            raise_attribute_error(object ? object->runtime() : (name ? name->runtime() : nullptr),
                                  "'<null>' object has no attribute '<null>'");

        try
        {
            if (Object *override_result = call_getattribute_override(object, name))
                return override_result;
        }
        catch (const PythonError &error)
        {
            if (!is_attribute_error(error))
                throw;
            if (Object *fallback_result = call_getattr_fallback(object, name))
                return fallback_result;
            throw;
        }

        try
        {
            return object_get_attr_default(object, name);
        }
        catch (const PythonError &error)
        {
            if (!is_attribute_error(error))
                throw;
            if (Object *fallback_result = call_getattr_fallback(object, name))
                return fallback_result;
            throw;
        }
    }

    Object *object_get_attr(Object *object, const std::string &name)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        return object_get_attr(object, runtime->new_str(name));
    }

    void object_set_attr_default(Object *object, StrObject *name, Object *value)
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

    void object_set_attr(Object *object, StrObject *name, Object *value)
    {
        if (!object || !name || !value)
            raise_attribute_error(object ? object->runtime() : nullptr,
                                  "'<null>' object has no attribute '<null>'");
        Runtime *runtime = object->runtime();
        auto object_root = runtime->root(object);
        auto name_root = runtime->root(name);
        auto value_root = runtime->root(value);
        if (call_setattr_override(object_root.get(), name_root.get(), value_root.get()))
            return;
        object_set_attr_default(object_root.get(), name_root.get(), value_root.get());
    }

    void object_set_attr(Object *object, const std::string &name, Object *value)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        object_set_attr(object, runtime->new_str(name), value);
    }

    void object_delete_attr_default(Object *object, StrObject *name)
    {
        if (!object || !name)
            raise_attribute_error(object ? object->runtime() : nullptr,
                                  "'<null>' object has no attribute '<null>'");

        if (auto *module = dynamic_cast<ModuleObject *>(object))
        {
            if (module->dict()->erase(name))
                return;
        }
        else if (auto *type = dynamic_cast<TypeObject *>(object))
        {
            Object *metatype_value = type->type() ? type_lookup_attribute(type->type(), name) : nullptr;
            if (metatype_value && metatype_value->type() && metatype_value->type()->slots.descr_set)
            {
                metatype_value->type()->slots.descr_set(*metatype_value, object, nullptr);
                return;
            }
            if (type->dict->erase(name))
                return;
        }
        else
        {
            Object *type_value = type_lookup_attribute(object->type(), name);
            if (type_value && type_value->type() && type_value->type()->slots.descr_set)
            {
                type_value->type()->slots.descr_set(*type_value, object, nullptr);
                return;
            }
            if (object->instance_dict() && object->instance_dict()->erase(name))
                return;
        }

        std::ostringstream message;
        message << "'" << type_qualified_name(object->type())
                << "' object has no attribute '"
                << name->value << "'";
        raise_attribute_error(object->runtime(), message.str());
    }

    void object_delete_attr(Object *object, StrObject *name)
    {
        if (!object || !name)
            raise_attribute_error(object ? object->runtime() : nullptr,
                                  "'<null>' object has no attribute '<null>'");
        Runtime *runtime = object->runtime();
        auto object_root = runtime->root(object);
        auto name_root = runtime->root(name);
        if (call_delattr_override(object_root.get(), name_root.get()))
            return;
        object_delete_attr_default(object_root.get(), name_root.get());
    }

    void object_delete_attr(Object *object, const std::string &name)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        object_delete_attr(object, runtime->new_str(name));
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

    std::string object_str(const Object *object)
    {
        if (!object)
            return "<null>";
        if (auto *text = dynamic_cast<const StrObject *>(object))
            return text->value;
        return object_repr(object);
    }

    TypeSlots make_object_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = default_traverse,
            .new_object = object_new,
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
            .call = type_call,
        };
    }

    void raise_type_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python TypeError without a runtime");
        throw PythonError(runtime->new_type_error(std::move(message)));
    }

    void raise_value_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python ValueError without a runtime");
        throw PythonError(runtime->new_value_error(std::move(message)));
    }

    void raise_index_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python IndexError without a runtime");
        throw PythonError(runtime->new_index_error(std::move(message)));
    }

    void raise_key_error(Runtime *runtime, Object *key)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python KeyError without a runtime");
        throw PythonError(runtime->new_key_error(key));
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

    void raise_unbound_local_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python UnboundLocalError without a runtime");
        throw PythonError(runtime->new_unbound_local_error(std::move(message)));
    }

    void raise_attribute_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python AttributeError without a runtime");
        throw PythonError(runtime->new_attribute_error(std::move(message)));
    }

    void raise_import_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python ImportError without a runtime");
        throw PythonError(runtime->new_import_error(std::move(message)));
    }
}
