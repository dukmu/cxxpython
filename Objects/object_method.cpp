#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        MethodDescriptorObject *require_method_descriptor(Runtime &runtime, Object *instance, const char *name)
        {
            auto *method = dynamic_cast<MethodDescriptorObject *>(instance);
            if (!method)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have method descriptor layout");
            return method;
        }

        Object *method_descriptor_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_method_descriptor(runtime, instance, "__name__")->name());
        }

        Object *method_descriptor_doc_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            const std::string &doc = require_method_descriptor(runtime, instance, "__doc__")->doc();
            if (doc.empty())
                return runtime.none();
            return runtime.new_str(doc);
        }

        Object *method_descriptor_text_signature_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            const std::string &signature =
                require_method_descriptor(runtime, instance, "__text_signature__")->text_signature();
            if (signature.empty())
                return runtime.none();
            return runtime.new_str(signature);
        }

        std::string method_descriptor_repr(const Object &object)
        {
            auto *method = static_cast<const MethodDescriptorObject *>(&object);
            return "<method '" + method->name() + "' of '" +
                   type_qualified_name(method->owner()) + "' objects>";
        }

        std::int64_t identity_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool identity_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool always_true(const Object &object)
        {
            (void)object;
            return true;
        }

        void method_descriptor_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const MethodDescriptorObject *>(&object);
            if (method->owner())
                visitor(method->owner());
        }

        Object *method_descriptor_call(Object &object, const CallArguments &args)
        {
            auto *method = static_cast<MethodDescriptorObject *>(&object);
            return method->call_unbound(args);
        }

        Object *method_descriptor_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)owner;
            auto *method = static_cast<MethodDescriptorObject *>(&object);
            if (!instance)
                return method;
            return method->runtime()->new_bound_method(method, instance);
        }

    }

    MethodDescriptorObject::MethodDescriptorObject(TypeObject *type,
                                                   std::string name,
                                                   TypeObject *owner,
                                                   MethodDescriptorCallback callback,
                                                   std::string doc,
                                                   std::string text_signature)
        : Object(type),
          name_(std::move(name)),
          doc_(std::move(doc)),
          text_signature_(std::move(text_signature)),
          owner_(owner),
          callback_(std::move(callback))
    {
    }

    const std::string &MethodDescriptorObject::name() const noexcept
    {
        return name_;
    }

    const std::string &MethodDescriptorObject::doc() const noexcept
    {
        return doc_;
    }

    const std::string &MethodDescriptorObject::text_signature() const noexcept
    {
        return text_signature_;
    }

    TypeObject *MethodDescriptorObject::owner() const noexcept
    {
        return owner_;
    }

    Object *MethodDescriptorObject::call_unbound(const CallArguments &args)
    {
        if (args.keywords && args.keywords->size() != 0)
            raise_type_error(runtime(), "method descriptor does not accept keyword arguments");
        ObjectSpan all_args(args.positional.data(), args.positional.size());
        if (all_args.empty())
        {
            raise_type_error(runtime(),
                             "descriptor '" + name_ + "' of '" +
                                 type_qualified_name(owner_) +
                                 "' object needs an argument");
        }
        return call_bound(all_args.front(), all_args.subspan(1));
    }

    Object *MethodDescriptorObject::call_bound(Object *self, const CallArguments &args)
    {
        if (args.keywords && args.keywords->size() != 0)
            raise_type_error(runtime(), "method descriptor does not accept keyword arguments");
        return call_bound(self, ObjectSpan(args.positional.data(), args.positional.size()));
    }

    Object *MethodDescriptorObject::call_bound(Object *self, ObjectSpan args)
    {
        if (!callback_)
            raise_type_error(runtime(), "method descriptor has no implementation");
        if (!is_instance(self, owner_))
        {
            std::ostringstream message;
            message << "descriptor '" << name_ << "' for '"
                    << type_qualified_name(owner_)
                    << "' objects doesn't apply to a '"
                    << type_qualified_name(self ? self->type() : nullptr)
                    << "' object";
            raise_type_error(runtime(), message.str());
        }
        Object *result = callback_(*runtime(), self, args);
        if (!result)
            raise_type_error(runtime(), "method descriptor returned null");
        return result;
    }

    BoundMethodObject::BoundMethodObject(TypeObject *type, MethodDescriptorObject *method, Object *self)
        : Object(type), method_(method), self_(self)
    {
    }

    MethodDescriptorObject *BoundMethodObject::method() const noexcept
    {
        return method_;
    }

    Object *BoundMethodObject::self() const noexcept
    {
        return self_;
    }

    TypeSlots make_method_descriptor_slots()
    {
        return TypeSlots{
            .repr = method_descriptor_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = method_descriptor_traverse,
            .call = method_descriptor_call,
            .descr_get = method_descriptor_get,
        };
    }

    void initialize_method_descriptor_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__name__"),
                        runtime.new_getset_descriptor("__name__",
                                                      type,
                                                      method_descriptor_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__doc__"),
                        runtime.new_getset_descriptor("__doc__",
                                                      type,
                                                      method_descriptor_doc_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__text_signature__"),
                        runtime.new_getset_descriptor("__text_signature__",
                                                      type,
                                                      method_descriptor_text_signature_getter,
                                                      nullptr));
    }

    void Runtime::initialize_type_methods()
    {
        initialize_object_type_methods(*this, object_type_);
        initialize_type_type_methods(*this, type_type_);
        initialize_int_type_methods(*this, int_type_);
        initialize_str_type_methods(*this, str_type_);
        initialize_list_type_methods(*this, list_type_);
        initialize_slice_type_methods(*this, slice_type_);
        initialize_code_type_methods(*this, code_type_);
        initialize_text_io_methods(*this, text_io_type_);
        initialize_property_type_methods(*this, property_type_);
    }
}
