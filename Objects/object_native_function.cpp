#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        std::string native_function_repr(const Object &object)
        {
            if (auto *function = dynamic_cast<const NativeFunctionObject *>(&object))
                return "<built-in function " + function->name() + ">";
            if (auto *bound = dynamic_cast<const BoundMethodObject *>(&object))
            {
                return "<built-in method " + bound->method()->name() +
                       " of " + type_qualified_name(bound->self()->type()) +
                       " object at " + pointer_hex(bound->self()) + ">";
            }
            return "<builtin_function_or_method object at " + pointer_hex(&object) + ">";
        }

        std::int64_t native_function_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool native_function_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool native_function_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void native_function_traverse(const Object &object, const VisitChildren &visitor)
        {
            if (auto *bound = dynamic_cast<const BoundMethodObject *>(&object))
            {
                if (bound->method())
                    visitor(bound->method());
                if (bound->self())
                    visitor(bound->self());
            }
        }

        Object *native_function_call(Object &object, const CallArguments &args)
        {
            if (auto *function = dynamic_cast<NativeFunctionObject *>(&object))
                return function->call(*function->runtime(), args);
            if (auto *bound = dynamic_cast<BoundMethodObject *>(&object))
                return bound->method()->call_bound(bound->self(), args);
            raise_type_error(object.runtime(), "builtin function object has invalid layout");
        }
    }

    NativeFunctionObject::NativeFunctionObject(TypeObject *type,
                                               std::string name,
                                               NativeFunctionCallback callback)
        : Object(type), name_(std::move(name)), callback_(std::move(callback))
    {
    }

    const std::string &NativeFunctionObject::name() const noexcept
    {
        return name_;
    }

    Object *NativeFunctionObject::call(Runtime &runtime, const CallArguments &args)
    {
        if (!callback_)
            raise_type_error(&runtime, "native function has no implementation");
        Object *result = callback_(runtime, args);
        if (!result)
            raise_type_error(&runtime, "native function returned null");
        return result;
    }

    TypeSlots make_native_function_slots()
    {
        return TypeSlots{
            .repr = native_function_repr,
            .hash = native_function_hash,
            .equal = native_function_equal,
            .truth = native_function_truth,
            .traverse = native_function_traverse,
            .call = native_function_call,
        };
    }
}
