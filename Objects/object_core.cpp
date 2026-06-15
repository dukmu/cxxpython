#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace cxxpython
{
    namespace
    {
        bool owns_type_reference(TypeObject *type) noexcept
        {
            return type && (type->flags & TypeFlagHeapType) != 0;
        }

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

        Object *call_attribute_hook(Object *hook,
                                    Object *instance,
                                    TypeObject *owner,
                                    std::initializer_list<Object *> args)
        {
            if (!hook)
                return nullptr;
            Object *callable = apply_descriptor(hook, instance, owner);
            return object_call(callable, CallArguments{args, nullptr});
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

        Object *heap_sequence_iter_slot(Object &object)
        {
            return object.runtime()->new_sequence_iterator(&object);
        }

        bool type_dict_contains(TypeObject *type, const char *name)
        {
            return type && type->dict && type->dict->contains(type->runtime()->new_str(name));
        }

        template <class Slot>
        void inherit_heap_slot(TypeObject *type, Slot TypeSlots::*member, Slot fallback = nullptr)
        {
            type->slots.*member = type->base ? type->base->slots.*member : fallback;
        }

        template <class Slot>
        void override_heap_slot_if_present(TypeObject *type,
                                           const char *method_name,
                                           Slot TypeSlots::*member,
                                           Slot slot)
        {
            if (type_dict_contains(type, method_name))
                type->slots.*member = slot;
        }

        Object *call_heap_special(Object &object, const char *name, const CallArguments &args)
        {
            Object *descriptor = type_lookup_attribute(object.type(), object.runtime()->new_str(name));
            if (!descriptor)
                return object.runtime()->not_implemented();
            Object *method = apply_descriptor(descriptor, &object, object.type());
            auto method_root = object.runtime()->root(method);
            return object_call(method_root.get(), args);
        }

        Object *call_heap_special_no_args(Object &object, const char *name)
        {
            return call_heap_special(object, name, CallArguments{});
        }

        Object *heap_iter_slot(Object &object)
        {
            return call_heap_special_no_args(object, "__iter__");
        }

        Object *heap_iternext_slot(Object &object)
        {
            return call_heap_special_no_args(object, "__next__");
        }

        std::string heap_repr_slot(const Object &object)
        {
            auto &mutable_object = const_cast<Object &>(object);
            Object *result = call_heap_special_no_args(mutable_object, "__repr__");
            auto result_root = object.runtime()->root(result);
            auto *text = dynamic_cast<StrObject *>(result_root.get());
            if (!text)
                raise_type_error(object.runtime(), "__repr__() should return str");
            return text->value;
        }

        std::size_t heap_length_slot(const Object &object)
        {
            auto &mutable_object = const_cast<Object &>(object);
            Object *result = call_heap_special_no_args(mutable_object, "__len__");
            auto result_root = object.runtime()->root(result);
            auto *length = dynamic_cast<IntObject *>(result_root.get());
            if (!length)
                raise_type_error(object.runtime(), "__len__() should return int");
            if (length->value < 0)
                raise_value_error(object.runtime(), "__len__() should return >= 0");
            return static_cast<std::size_t>(length->value);
        }

        bool heap_truth_slot(const Object &object)
        {
            auto &mutable_object = const_cast<Object &>(object);
            if (type_dict_contains(mutable_object.type(), "__bool__"))
            {
                Object *result = call_heap_special_no_args(mutable_object, "__bool__");
                auto result_root = object.runtime()->root(result);
                if (result_root.get() == object.runtime()->true_value())
                    return true;
                if (result_root.get() == object.runtime()->false_value())
                    return false;
                raise_type_error(object.runtime(), "__bool__() should return bool");
            }
            if (mutable_object.type() && mutable_object.type()->slots.length)
                return mutable_object.type()->slots.length(object) != 0;
            return default_truth(object);
        }

        Object *call_heap_special_one_arg(Object &object, const char *name, Object *arg)
        {
            auto arg_root = object.runtime()->root(arg);
            Object *args[] = {arg_root.get()};
            return call_heap_special(object, name, CallArguments{ObjectSpan(args, 1), nullptr});
        }

        Object *heap_getitem_slot(Object &object, Object *key)
        {
            return call_heap_special_one_arg(object, "__getitem__", key);
        }

        void heap_setitem_slot(Object &object, Object *key, Object *value)
        {
            auto key_root = object.runtime()->root(key);
            auto value_root = object.runtime()->root(value);
            Object *args[] = {key_root.get(), value_root.get()};
            (void)call_heap_special(object,
                                    "__setitem__",
                                    CallArguments{ObjectSpan(args, 2), nullptr});
        }

        void heap_delitem_slot(Object &object, Object *key)
        {
            (void)call_heap_special_one_arg(object, "__delitem__", key);
        }

        bool heap_contains_slot(Object &object, Object *key)
        {
            Object *result = call_heap_special_one_arg(object, "__contains__", key);
            auto result_root = object.runtime()->root(result);
            return is_true(result_root.get());
        }

        const char *binary_method_name(BinaryOp op)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return "__add__";
            case BinaryOp::Subtract:
                return "__sub__";
            case BinaryOp::Multiply:
                return "__mul__";
            case BinaryOp::TrueDivide:
                return "__truediv__";
            case BinaryOp::FloorDivide:
                return "__floordiv__";
            case BinaryOp::Modulo:
                return "__mod__";
            case BinaryOp::Power:
                return "__pow__";
            case BinaryOp::ShiftLeft:
                return "__lshift__";
            case BinaryOp::ShiftRight:
                return "__rshift__";
            case BinaryOp::BitAnd:
                return "__and__";
            case BinaryOp::BitXor:
                return "__xor__";
            case BinaryOp::BitOr:
                return "__or__";
            }
            return nullptr;
        }

        const char *reflected_binary_method_name(BinaryOp op)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return "__radd__";
            case BinaryOp::Subtract:
                return "__rsub__";
            case BinaryOp::Multiply:
                return "__rmul__";
            case BinaryOp::TrueDivide:
                return "__rtruediv__";
            case BinaryOp::FloorDivide:
                return "__rfloordiv__";
            case BinaryOp::Modulo:
                return "__rmod__";
            case BinaryOp::Power:
                return "__rpow__";
            case BinaryOp::ShiftLeft:
                return "__rlshift__";
            case BinaryOp::ShiftRight:
                return "__rrshift__";
            case BinaryOp::BitAnd:
                return "__rand__";
            case BinaryOp::BitXor:
                return "__rxor__";
            case BinaryOp::BitOr:
                return "__ror__";
            }
            return nullptr;
        }

        const char *inplace_binary_method_name(BinaryOp op)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return "__iadd__";
            case BinaryOp::Subtract:
                return "__isub__";
            case BinaryOp::Multiply:
                return "__imul__";
            case BinaryOp::TrueDivide:
                return "__itruediv__";
            case BinaryOp::FloorDivide:
                return "__ifloordiv__";
            case BinaryOp::Modulo:
                return "__imod__";
            case BinaryOp::Power:
                return "__ipow__";
            case BinaryOp::ShiftLeft:
                return "__ilshift__";
            case BinaryOp::ShiftRight:
                return "__irshift__";
            case BinaryOp::BitAnd:
                return "__iand__";
            case BinaryOp::BitXor:
                return "__ixor__";
            case BinaryOp::BitOr:
                return "__ior__";
            }
            return nullptr;
        }

        const char *unary_method_name(UnaryOp op)
        {
            switch (op)
            {
            case UnaryOp::Positive:
                return "__pos__";
            case UnaryOp::Negative:
                return "__neg__";
            case UnaryOp::Invert:
                return "__invert__";
            case UnaryOp::Not:
                return nullptr;
            }
            return nullptr;
        }

        const char *rich_compare_method_name(RichCompareOp op)
        {
            switch (op)
            {
            case RichCompareOp::Equal:
                return "__eq__";
            case RichCompareOp::NotEqual:
                return "__ne__";
            case RichCompareOp::Less:
                return "__lt__";
            case RichCompareOp::LessEqual:
                return "__le__";
            case RichCompareOp::Greater:
                return "__gt__";
            case RichCompareOp::GreaterEqual:
                return "__ge__";
            }
            return nullptr;
        }

        Object *heap_binary_op_slot(const Object &left, const Object &right, BinaryOp op)
        {
            auto &mutable_left = const_cast<Object &>(left);
            auto right_root = left.runtime()->root(const_cast<Object *>(&right));
            return call_heap_special_one_arg(mutable_left, binary_method_name(op), right_root.get());
        }

        Object *heap_reflected_binary_op_slot(const Object &right, const Object &left, BinaryOp op)
        {
            auto &mutable_right = const_cast<Object &>(right);
            auto left_root = right.runtime()->root(const_cast<Object *>(&left));
            return call_heap_special_one_arg(mutable_right, reflected_binary_method_name(op), left_root.get());
        }

        Object *heap_inplace_binary_op_slot(const Object &left, const Object &right, BinaryOp op)
        {
            auto &mutable_left = const_cast<Object &>(left);
            auto right_root = left.runtime()->root(const_cast<Object *>(&right));
            return call_heap_special_one_arg(mutable_left, inplace_binary_method_name(op), right_root.get());
        }

        Object *heap_unary_op_slot(const Object &operand, UnaryOp op)
        {
            auto &mutable_operand = const_cast<Object &>(operand);
            return call_heap_special_no_args(mutable_operand, unary_method_name(op));
        }

        Object *heap_rich_compare_slot(const Object &left, const Object &right, RichCompareOp op)
        {
            auto &mutable_left = const_cast<Object &>(left);
            auto right_root = left.runtime()->root(const_cast<Object *>(&right));
            return call_heap_special_one_arg(mutable_left, rich_compare_method_name(op), right_root.get());
        }

        Object *heap_call_slot(Object &object, const CallArguments &args)
        {
            return call_heap_special(object, "__call__", args);
        }

        bool type_dict_contains_any(TypeObject *type, const char *const *names, std::size_t count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (type_dict_contains(type, names[index]))
                    return true;
            }
            return false;
        }

        template <std::size_t Count>
        bool type_dict_contains_any(TypeObject *type, const char *const (&names)[Count])
        {
            return type_dict_contains_any(type, names, Count);
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

        Object *class_getter(Runtime &, Object *instance, TypeObject *)
        {
            return instance->type();
        }

        void class_setter(Runtime &runtime, Object *, Object *)
        {
            raise_type_error(&runtime, "__class__ assignment only supported for mutable types or ModuleType subclasses");
        }

        Object *dict_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            if (auto *type = dynamic_cast<TypeObject *>(instance))
                return runtime.new_mappingproxy(type->dict);
            if (auto *module = dynamic_cast<ModuleObject *>(instance))
                return module->dict();
            if ((instance->type()->flags & TypeFlagHasInstanceDict) == 0)
            {
                raise_attribute_error(&runtime,
                                      "'" + type_qualified_name(instance->type()) +
                                          "' object has no attribute '__dict__'");
            }
            if (!instance->instance_dict())
                instance->set_instance_dict(runtime.new_dict());
            return instance->instance_dict();
        }

        void dict_setter(Runtime &runtime, Object *, Object *)
        {
            raise_attribute_error(&runtime, "attribute '__dict__' is read-only");
        }

        Object *doc_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            if (auto *type = dynamic_cast<TypeObject *>(instance))
            {
                if (type->dict)
                {
                    if (Object *doc = type->dict->get(runtime.new_str("__doc__")))
                        return doc;
                }
            }
            return runtime.none();
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
            request.module_fallback = runtime->active_module_name("builtins");
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
            request.module_fallback = runtime.active_module_name("builtins");
            request.qualname = name->value;
            request.explicit_metaclass = metaclass == runtime.type_type() ? nullptr : metaclass;
            return runtime.new_class(std::move(request));
        }

        Object *type_call_method(Runtime &, Object *self, ObjectSpan args)
        {
            std::vector<Object *> positional(args.begin(), args.end());
            return object_call(self, CallArguments{ObjectSpan(positional.data(), positional.size()), nullptr});
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

        TypeObject *require_type_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *type = dynamic_cast<TypeObject *>(instance);
            if (!type)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have type layout");
            return type;
        }

        Object *type_base_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__base__");
            return type->base ? static_cast<Object *>(type->base) : static_cast<Object *>(runtime.none());
        }

        Object *type_bases_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__bases__");
            std::vector<Object *> bases;
            bases.reserve(type->bases.size());
            for (TypeObject *base : type->bases)
                bases.push_back(base);
            return runtime.new_tuple(std::move(bases));
        }

        Object *type_mro_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__mro__");
            std::vector<Object *> mro;
            mro.reserve(type->mro.size());
            for (TypeObject *item : type->mro)
                mro.push_back(item);
            return runtime.new_tuple(std::move(mro));
        }

        Object *type_qualname_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__qualname__");
            if (type->dict)
            {
                if (auto *qualname = dynamic_cast<StrObject *>(type->dict->get(runtime.new_str("__qualname__"))))
                    return qualname;
            }
            return runtime.new_str(type->name);
        }

        Object *type_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__name__");
            return runtime.new_str(type->name);
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
        : value_(value), runtime_(value ? value->runtime() : nullptr), what_(value ? value->message() : "")
    {
        if (value_)
        {
            value_->incref();
            runtime_->add_root(value_);
        }
    }

    PythonError::PythonError(const PythonError &other)
        : value_(other.value_), runtime_(other.runtime_), what_(other.what_)
    {
        if (value_)
        {
            value_->incref();
            runtime_->add_root(value_);
        }
    }

    PythonError::PythonError(PythonError &&other) noexcept
        : value_(other.value_), runtime_(other.runtime_), what_(std::move(other.what_))
    {
        other.value_ = nullptr;
        other.runtime_ = nullptr;
    }

    PythonError::~PythonError()
    {
        if (value_)
        {
            runtime_->remove_root(value_);
            value_->decref();
        }
    }

    PythonError &PythonError::operator=(const PythonError &other)
    {
        if (this == &other)
            return *this;
        if (other.value_)
        {
            other.value_->incref();
            other.runtime_->add_root(other.value_);
        }
        if (value_)
        {
            runtime_->remove_root(value_);
            value_->decref();
        }
        value_ = other.value_;
        runtime_ = other.runtime_;
        what_ = other.what_;
        return *this;
    }

    PythonError &PythonError::operator=(PythonError &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (value_)
        {
            runtime_->remove_root(value_);
            value_->decref();
        }
        value_ = other.value_;
        runtime_ = other.runtime_;
        what_ = std::move(other.what_);
        other.value_ = nullptr;
        other.runtime_ = nullptr;
        return *this;
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
        if (owns_type_reference(type_))
            type_->incref();
    }

    Object::~Object()
    {
        clear_references();
    }

    void Object::clear_references()
    {
        CXXPYTHON_CLEAR_REF(instance_dict_);
        if (owns_type_reference(type_))
            type_->decref();
        type_ = nullptr;
    }

    void Object::set_runtime(Runtime *runtime) noexcept
    {
        runtime_ = runtime;
    }

    void Object::incref() noexcept
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void Object::decref() noexcept
    {
        ref_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    std::size_t Object::ref_count() const noexcept
    {
        return ref_count_.load(std::memory_order_relaxed);
    }

    bool Object::gc_tracked() const noexcept
    {
        return (gc_state_ & kGcTrackedBit) != 0;
    }

    void Object::set_gc_tracked(bool tracked) noexcept
    {
        if (tracked)
            gc_state_ |= kGcTrackedBit;
        else
            gc_state_ &= ~kGcTrackedBit;
    }

    void Object::set_heap_index(std::size_t index) noexcept
    {
        heap_index_ = index;
    }

    std::size_t Object::heap_index() const noexcept
    {
        return heap_index_;
    }

    GCGeneration Object::gc_generation() const noexcept
    {
        return (gc_state_ & kGcOldBit) ? GCGeneration::Old : GCGeneration::Young;
    }

    void Object::promote_gc_generation() noexcept
    {
        gc_state_ |= kGcOldBit;
    }

    bool Object::gc_marked() const noexcept
    {
        return (gc_state_ & kGcMarkBit) != 0;
    }

    void Object::gc_mark() noexcept
    {
        gc_state_ |= kGcMarkBit;
    }

    void Object::gc_unmark() noexcept
    {
        gc_state_ &= ~kGcMarkBit;
    }

    void Object::set_type(TypeObject *type) noexcept
    {
        if (type == type_)
            return;
        if (owns_type_reference(type))
            type->incref();
        if (owns_type_reference(type_))
            type_->decref();
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
        if (type_)
            visitor(type_);
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
        if (dict == instance_dict_)
            return;
        if (dict)
            dict->incref();
        if (instance_dict_)
            instance_dict_->decref();
        instance_dict_ = dict;
    }

    TypeObject::TypeObject(std::string name)
        : Object(nullptr), name(std::move(name))
    {
    }

    TypeObject::~TypeObject()
    {
        clear_references();
    }

    void TypeObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(dict);
        for (TypeObject *item : mro)
        {
            if (item)
                item->decref();
        }
        mro.clear();
        for (TypeObject *item : bases)
        {
            if (item)
                item->decref();
        }
        bases.clear();
        CXXPYTHON_CLEAR_REF(solid_base);
        CXXPYTHON_CLEAR_REF(base);
        Object::clear_references();
    }

    void TypeObject::set_base(TypeObject *value) noexcept
    {
        if (value == base)
            return;
        if (value)
            value->incref();
        if (base)
            base->decref();
        base = value;
    }

    void TypeObject::set_solid_base(TypeObject *value) noexcept
    {
        if (value == solid_base)
            return;
        if (value)
            value->incref();
        if (solid_base)
            solid_base->decref();
        solid_base = value;
    }

    void TypeObject::set_dict(DictObject *value) noexcept
    {
        if (value == dict)
            return;
        if (value)
            value->incref();
        if (dict)
            dict->decref();
        dict = value;
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

        if (op == UnaryOp::Not)
            return is_true(operand) ? operand->runtime()->false_value() : operand->runtime()->true_value();

        if (operand->type() && operand->type()->slots.unary_op)
        {
            Object *result = operand->type()->slots.unary_op(*operand, op);
            if (!result)
                raise_type_error(operand->runtime(), "unary operator slot returned null");
            return result;
        }

        raise_type_error(operand->runtime(), unsupported_unary_op_message(op, operand));
    }

    Object *object_call(Object *callable, std::initializer_list<Object *> args)
    {
        return object_call(callable, CallArguments{ObjectSpan(args.begin(), args.size()), nullptr});
    }

    Object *object_call_rooted(Object *callable, ObjectSpan positional, DictObject *keywords)
    {
        if (!callable)
            raise_type_error(nullptr, "'<null>' object is not callable");

        Runtime *runtime = callable->runtime();
        if (auto *function = dynamic_cast<PythonFunctionObject *>(callable))
            return function->call_rooted(positional, keywords);

        CallArguments rooted_args{positional, keywords};
        if (callable->type() && callable->type()->slots.call)
            return callable->type()->slots.call(*callable, rooted_args);

        std::ostringstream message;
        message << "'" << type_qualified_name(callable->type())
                << "' object is not callable";
        raise_type_error(runtime, message.str());
    }

    Object *object_call(Object *callable, const CallArguments &args)
    {
        if (!callable)
            raise_type_error(nullptr, "'<null>' object is not callable");

        Runtime *runtime = callable->runtime();
        std::optional<Rooted<Object>> callable_root;
        std::vector<Rooted<Object>> positional_roots;
        positional_roots.reserve(args.positional.size());
        std::vector<Object *> positional(args.positional.begin(), args.positional.end());
        if (runtime)
            callable_root.emplace(runtime->root(callable));
        for (Object *arg : positional)
        {
            if (runtime)
                positional_roots.emplace_back(runtime->root(arg));
        }
        DictObject *keywords = args.keywords;
        std::optional<Rooted<DictObject>> keyword_roots;
        if (runtime && keywords)
            keyword_roots.emplace(runtime->root(keywords));
        CallArguments rooted_args{ObjectSpan(positional.data(), positional.size()), keyword_roots ? keyword_roots->get() : keywords};

        Object *call_target = callable_root ? callable_root->get() : callable;
        if (call_target && call_target->type() && call_target->type()->slots.call)
            return call_target->type()->slots.call(*call_target, rooted_args);

        std::ostringstream message;
        message << "'" << type_qualified_name(call_target ? call_target->type() : callable->type())
                << "' object is not callable";
        raise_type_error(runtime, message.str());
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
        if (object->type() && object->type()->slots.iter)
        {
            Object *iterator = object_iter(object);
            auto iterator_root = object->runtime()->root(iterator);
            while (true)
            {
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = object->runtime()->root(item);
                    if (object_equal(item_root.get(), key))
                        return true;
                }
                catch (const PythonError &error)
                {
                    if (error.type() == object->runtime()->stop_iteration_type())
                        return false;
                    throw;
                }
            }
        }

        std::ostringstream message;
        message << "argument of type '" << type_qualified_name(object->type())
                << "' is not iterable";
        raise_type_error(object->runtime(), message.str());
    }

    Object *object_format(Object *object, StrObject *format_spec)
    {
        if (!object || !format_spec)
            raise_type_error(object ? object->runtime() : (format_spec ? format_spec->runtime() : nullptr),
                             "format value and spec cannot be null");
        Runtime *runtime = object->runtime();
        auto object_root = runtime->root(object);
        auto spec_root = runtime->root(format_spec);
        StrObject *name = runtime->new_str("__format__");
        auto name_root = runtime->root(name);
        Object *method = type_lookup_attribute(object_root->type(), name_root.get());
        if (!method)
        {
            std::ostringstream message;
            message << "'" << type_qualified_name(object_root->type())
                    << "' object has no __format__ method";
            raise_type_error(runtime, message.str());
        }
        auto method_root = runtime->root(method);
        Object *formatted = object_call(method_root.get(), {object_root.get(), spec_root.get()});
        auto formatted_root = runtime->root(formatted);
        if (!dynamic_cast<StrObject *>(formatted_root.get()))
            raise_type_error(runtime, "__format__ must return str");
        return formatted_root.get();
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

    void refresh_heap_type_slots_from_dict(TypeObject *type)
    {
        if (!type || (type->flags & TypeFlagHeapType) == 0)
            return;
        inherit_heap_slot(type, &TypeSlots::repr, default_repr);
        inherit_heap_slot(type, &TypeSlots::iter);
        inherit_heap_slot(type, &TypeSlots::iternext);
        inherit_heap_slot(type, &TypeSlots::length);
        inherit_heap_slot(type, &TypeSlots::truth, default_truth);
        inherit_heap_slot(type, &TypeSlots::getitem);
        inherit_heap_slot(type, &TypeSlots::setitem);
        inherit_heap_slot(type, &TypeSlots::delitem);
        inherit_heap_slot(type, &TypeSlots::contains);
        inherit_heap_slot(type, &TypeSlots::binary_op);
        inherit_heap_slot(type, &TypeSlots::reflected_binary_op);
        inherit_heap_slot(type, &TypeSlots::inplace_binary_op);
        inherit_heap_slot(type, &TypeSlots::unary_op);
        inherit_heap_slot(type, &TypeSlots::rich_compare);
        inherit_heap_slot(type, &TypeSlots::call);
        if (type_dict_contains(type, "__len__"))
        {
            type->slots.length = heap_length_slot;
            type->slots.truth = heap_truth_slot;
        }
        override_heap_slot_if_present(type, "__repr__", &TypeSlots::repr, heap_repr_slot);
        override_heap_slot_if_present(type, "__bool__", &TypeSlots::truth, heap_truth_slot);
        override_heap_slot_if_present(type, "__iter__", &TypeSlots::iter, heap_iter_slot);
        override_heap_slot_if_present(type, "__next__", &TypeSlots::iternext, heap_iternext_slot);
        override_heap_slot_if_present(type, "__getitem__", &TypeSlots::getitem, heap_getitem_slot);
        override_heap_slot_if_present(type, "__setitem__", &TypeSlots::setitem, heap_setitem_slot);
        override_heap_slot_if_present(type, "__delitem__", &TypeSlots::delitem, heap_delitem_slot);
        override_heap_slot_if_present(type, "__contains__", &TypeSlots::contains, heap_contains_slot);
        if (!type->slots.iter && type->slots.length && type->slots.getitem)
            type->slots.iter = heap_sequence_iter_slot;

        static constexpr const char *binary_methods[] = {
            "__add__",
            "__sub__",
            "__mul__",
            "__truediv__",
            "__floordiv__",
            "__mod__",
            "__pow__",
            "__lshift__",
            "__rshift__",
            "__and__",
            "__xor__",
            "__or__",
        };
        static constexpr const char *reflected_binary_methods[] = {
            "__radd__",
            "__rsub__",
            "__rmul__",
            "__rtruediv__",
            "__rfloordiv__",
            "__rmod__",
            "__rpow__",
            "__rlshift__",
            "__rrshift__",
            "__rand__",
            "__rxor__",
            "__ror__",
        };
        static constexpr const char *inplace_binary_methods[] = {
            "__iadd__",
            "__isub__",
            "__imul__",
            "__itruediv__",
            "__ifloordiv__",
            "__imod__",
            "__ipow__",
            "__ilshift__",
            "__irshift__",
            "__iand__",
            "__ixor__",
            "__ior__",
        };
        static constexpr const char *unary_methods[] = {
            "__pos__",
            "__neg__",
            "__invert__",
        };
        static constexpr const char *rich_compare_methods[] = {
            "__eq__",
            "__ne__",
            "__lt__",
            "__le__",
            "__gt__",
            "__ge__",
        };

        if (type_dict_contains_any(type, binary_methods))
            type->slots.binary_op = heap_binary_op_slot;
        if (type_dict_contains_any(type, reflected_binary_methods))
            type->slots.reflected_binary_op = heap_reflected_binary_op_slot;
        if (type_dict_contains_any(type, inplace_binary_methods))
            type->slots.inplace_binary_op = heap_inplace_binary_op_slot;
        if (type_dict_contains_any(type, unary_methods))
            type->slots.unary_op = heap_unary_op_slot;
        if (type_dict_contains_any(type, rich_compare_methods))
            type->slots.rich_compare = heap_rich_compare_slot;
        override_heap_slot_if_present(type, "__call__", &TypeSlots::call, heap_call_slot);
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

        Runtime *runtime = object->runtime();
        std::optional<Rooted<Object>> object_root;
        std::optional<Rooted<StrObject>> name_root;
        Object *target = object;
        StrObject *attr = name;
        if (runtime)
        {
            object_root.emplace(runtime->root(object));
            name_root.emplace(runtime->root(name));
            target = object_root->get();
            attr = name_root->get();
        }

        try
        {
            if (Object *override_result = call_getattribute_override(target, attr))
                return override_result;
        }
        catch (const PythonError &error)
        {
            if (!is_attribute_error(error))
                throw;
            if (Object *fallback_result = call_getattr_fallback(target, attr))
                return fallback_result;
            throw;
        }

        try
        {
            return object_get_attr_default(target, attr);
        }
        catch (const PythonError &error)
        {
            if (!is_attribute_error(error))
                throw;
            if (Object *fallback_result = call_getattr_fallback(target, attr))
                return fallback_result;
            throw;
        }
    }

    Object *object_get_attr(Object *object, const std::string &name)
    {
        Runtime *runtime = object ? object->runtime() : nullptr;
        if (!runtime)
            raise_attribute_error(nullptr, "'<null>' object has no attribute '" + name + "'");
        auto object_root = runtime->root(object);
        auto name_root = runtime->root(runtime->new_str(name));
        return object_get_attr(object_root.get(), name_root.get());
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
            refresh_heap_type_slots_from_dict(type);
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
        auto object_root = runtime->root(object);
        auto name_root = runtime->root(runtime->new_str(name));
        object_set_attr(object_root.get(), name_root.get(), value);
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
            {
                refresh_heap_type_slots_from_dict(type);
                return;
            }
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

    void initialize_object_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__getattribute__"),
                        runtime.new_method_descriptor("__getattribute__", type, object_getattribute_method));
        type->dict->set(runtime.new_str("__new__"),
                        runtime.new_method_descriptor("__new__", type, object_new_method));
        type->dict->set(runtime.new_str("__init__"),
                        runtime.new_method_descriptor("__init__", type, object_init_method));
        type->dict->set(runtime.new_str("__str__"),
                        runtime.new_method_descriptor("__str__", type, object_str_method));
        type->dict->set(runtime.new_str("__format__"),
                        runtime.new_method_descriptor("__format__", type, object_format_method));
        type->dict->set(runtime.new_str("__bool__"),
                        runtime.new_method_descriptor("__bool__", type, object_bool_method));
        type->dict->set(runtime.new_str("__dir__"),
                        runtime.new_method_descriptor("__dir__", type, object_dir_method));
        type->dict->set(runtime.new_str("__sizeof__"),
                        runtime.new_method_descriptor("__sizeof__", type, object_sizeof_method));
        type->dict->set(runtime.new_str("__getstate__"),
                        runtime.new_method_descriptor("__getstate__", type, object_getstate_method));
        type->dict->set(runtime.new_str("__init_subclass__"),
                        runtime.new_method_descriptor("__init_subclass__", type, object_init_subclass_method));
        type->dict->set(runtime.new_str("__subclasshook__"),
                        runtime.new_method_descriptor("__subclasshook__", type, object_subclasshook_method));
        type->dict->set(runtime.new_str("__setattr__"),
                        runtime.new_method_descriptor("__setattr__", type, object_setattr_method));
        type->dict->set(runtime.new_str("__delattr__"),
                        runtime.new_method_descriptor("__delattr__", type, object_delattr_method));
    }

    void initialize_type_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__call__"),
                        runtime.new_method_descriptor("__call__", type, type_call_method));
        type->dict->set(runtime.new_str("__new__"),
                        runtime.new_method_descriptor("__new__", type, type_new_method));
        type->dict->set(runtime.new_str("__init__"),
                        runtime.new_method_descriptor("__init__", type, type_init_method));
        type->dict->set(runtime.new_str("__prepare__"),
                        runtime.new_method_descriptor("__prepare__", type, type_prepare_method));
        type->dict->set(runtime.new_str("mro"),
                        runtime.new_method_descriptor("mro", type, type_mro_method));
        type->dict->set(runtime.new_str("__instancecheck__"),
                        runtime.new_method_descriptor("__instancecheck__", type, type_instancecheck_method));
        type->dict->set(runtime.new_str("__subclasscheck__"),
                        runtime.new_method_descriptor("__subclasscheck__", type, type_subclasscheck_method));
    }

    void initialize_object_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__class__"),
                        runtime.new_getset_descriptor("__class__",
                                                      type,
                                                      class_getter,
                                                      class_setter));
        type->dict->set(runtime.new_str("__dict__"),
                        runtime.new_getset_descriptor("__dict__",
                                                      type,
                                                      dict_getter,
                                                      dict_setter));
        type->dict->set(runtime.new_str("__doc__"),
                        runtime.new_getset_descriptor("__doc__",
                                                      type,
                                                      doc_getter,
                                                      nullptr));
    }

    void initialize_type_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__base__"),
                        runtime.new_getset_descriptor("__base__",
                                                      type,
                                                      type_base_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__bases__"),
                        runtime.new_getset_descriptor("__bases__",
                                                      type,
                                                      type_bases_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__mro__"),
                        runtime.new_getset_descriptor("__mro__",
                                                      type,
                                                      type_mro_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__name__"),
                        runtime.new_getset_descriptor("__name__",
                                                      type,
                                                      type_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__qualname__"),
                        runtime.new_getset_descriptor("__qualname__",
                                                      type,
                                                      type_qualname_getter,
                                                      nullptr));
    }

    void initialize_object_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
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

    void raise_recursion_error(Runtime *runtime, std::string message)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python RecursionError without a runtime");
        throw PythonError(runtime->new_recursion_error(std::move(message)));
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

    void raise_keyboard_interrupt(Runtime *runtime)
    {
        if (!runtime)
            throw std::logic_error("cannot raise Python KeyboardInterrupt without a runtime");
        throw PythonError(runtime->new_keyboard_interrupt());
    }
}
