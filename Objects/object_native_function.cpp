#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

namespace cxxpython
{
    namespace
    {
        NativeFunctionObject *require_native_function(Runtime &runtime, Object *instance, const char *name)
        {
            auto *function = dynamic_cast<NativeFunctionObject *>(instance);
            if (!function)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have native function layout");
            return function;
        }

        Object *native_function_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_native_function(runtime, instance, "__name__")->name());
        }

        Object *native_function_doc_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            const std::string &doc = require_native_function(runtime, instance, "__doc__")->doc();
            if (doc.empty())
                return runtime.none();
            return runtime.new_str(doc);
        }

        Object *native_function_text_signature_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            const std::string &signature = require_native_function(runtime, instance, "__text_signature__")->text_signature();
            if (signature.empty())
                return runtime.none();
            return runtime.new_str(signature);
        }

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
                                               NativeFunctionCallback callback,
                                               std::string doc,
                                               std::string text_signature)
        : Object(type),
          name_(std::move(name)),
          doc_(std::move(doc)),
          text_signature_(std::move(text_signature)),
          callback_(std::move(callback))
    {
    }

    const std::string &NativeFunctionObject::name() const noexcept
    {
        return name_;
    }

    const std::string &NativeFunctionObject::doc() const noexcept
    {
        return doc_;
    }

    const std::string &NativeFunctionObject::text_signature() const noexcept
    {
        return text_signature_;
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

    void initialize_native_function_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__name__"),
                        runtime.new_getset_descriptor("__name__",
                                                      type,
                                                      native_function_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__doc__"),
                        runtime.new_getset_descriptor("__doc__",
                                                      type,
                                                      native_function_doc_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__text_signature__"),
                        runtime.new_getset_descriptor("__text_signature__",
                                                      type,
                                                      native_function_text_signature_getter,
                                                      nullptr));
    }
}
