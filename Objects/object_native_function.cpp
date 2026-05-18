#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        std::string native_function_repr(const Object &object)
        {
            auto *function = static_cast<const NativeFunctionObject *>(&object);
            return "<built-in function " + function->name() + ">";
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

        Object *native_function_call(Object &object, const std::vector<Object *> &args)
        {
            auto *function = static_cast<NativeFunctionObject *>(&object);
            return function->call(*function->runtime(), args);
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

    Object *NativeFunctionObject::call(Runtime &runtime, const std::vector<Object *> &args)
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
            native_function_repr,
            native_function_hash,
            native_function_equal,
            native_function_truth,
            default_traverse,
            nullptr,
            native_function_call,
        };
    }
}
