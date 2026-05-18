#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
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

        Object *method_descriptor_call(Object &object, const std::vector<Object *> &args)
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

        Object *list_append_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "list.append() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }

            auto *list = dynamic_cast<ListObject *>(self);
            if (!list)
                raise_type_error(&runtime, "list.append() receiver does not have list layout");

            list->append(args[0]);
            return runtime.none();
        }
    }

    MethodDescriptorObject::MethodDescriptorObject(TypeObject *type,
                                                   std::string name,
                                                   TypeObject *owner,
                                                   MethodDescriptorCallback callback)
        : Object(type), name_(std::move(name)), owner_(owner), callback_(std::move(callback))
    {
    }

    const std::string &MethodDescriptorObject::name() const noexcept
    {
        return name_;
    }

    TypeObject *MethodDescriptorObject::owner() const noexcept
    {
        return owner_;
    }

    Object *MethodDescriptorObject::call_unbound(const std::vector<Object *> &args)
    {
        ObjectSpan all_args(args.data(), args.size());
        if (all_args.empty())
        {
            raise_type_error(runtime(),
                             "descriptor '" + name_ + "' of '" +
                                 type_qualified_name(owner_) +
                                 "' object needs an argument");
        }
        return call_bound(all_args.front(), all_args.subspan(1));
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

    void Runtime::initialize_type_methods()
    {
        list_type_->dict->set(new_str("append"),
                              new_method_descriptor("append", list_type_, list_append_method));
    }
}
