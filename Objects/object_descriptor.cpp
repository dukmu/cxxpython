#include "cxxpython/object.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        std::string getset_descriptor_repr(const Object &object)
        {
            auto *descriptor = static_cast<const GetSetDescriptorObject *>(&object);
            return "<attribute '" + descriptor->name() + "' of '" +
                   type_qualified_name(descriptor->owner()) + "' objects>";
        }

        std::int64_t getset_descriptor_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool getset_descriptor_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool getset_descriptor_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void getset_descriptor_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *descriptor = static_cast<const GetSetDescriptorObject *>(&object);
            if (descriptor->owner())
                visitor(descriptor->owner());
        }

        Object *getset_descriptor_get(Object &object, Object *instance, TypeObject *owner)
        {
            auto *descriptor = static_cast<GetSetDescriptorObject *>(&object);
            if (!instance)
                return descriptor;
            return descriptor->get(instance, owner);
        }

        void getset_descriptor_set(Object &object, Object *instance, Object *value)
        {
            auto *descriptor = static_cast<GetSetDescriptorObject *>(&object);
            descriptor->set(instance, value);
        }
    }

    GetSetDescriptorObject::GetSetDescriptorObject(TypeObject *type,
                                                   std::string name,
                                                   TypeObject *owner,
                                                   GetSetGetter getter,
                                                   GetSetSetter setter)
        : Object(type),
          name_(std::move(name)),
          owner_(owner),
          getter_(std::move(getter)),
          setter_(std::move(setter))
    {
    }

    const std::string &GetSetDescriptorObject::name() const noexcept
    {
        return name_;
    }

    TypeObject *GetSetDescriptorObject::owner() const noexcept
    {
        return owner_;
    }

    Object *GetSetDescriptorObject::get(Object *instance, TypeObject *owner)
    {
        if (!getter_)
            raise_attribute_error(runtime(), "attribute '" + name_ + "' is not readable");
        Object *result = getter_(*runtime(), instance, owner);
        if (!result)
            raise_type_error(runtime(), "getset descriptor returned null");
        return result;
    }

    void GetSetDescriptorObject::set(Object *instance, Object *value)
    {
        if (!setter_)
            raise_attribute_error(runtime(), "attribute '" + name_ + "' is read-only");
        setter_(*runtime(), instance, value);
    }

    TypeSlots make_getset_descriptor_slots()
    {
        return TypeSlots{
            .repr = getset_descriptor_repr,
            .hash = getset_descriptor_hash,
            .equal = getset_descriptor_equal,
            .truth = getset_descriptor_truth,
            .traverse = getset_descriptor_traverse,
            .descr_get = getset_descriptor_get,
            .descr_set = getset_descriptor_set,
        };
    }
}
