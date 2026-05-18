#include "cxxpython/runtime.hpp"

#include <algorithm>

namespace cxxpython
{
    void Runtime::add_root(Object *object)
    {
        if (!object)
            return;
        if (std::find(roots_.begin(), roots_.end(), object) == roots_.end())
            roots_.push_back(object);
    }

    void Runtime::remove_root(Object *object)
    {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), object), roots_.end());
    }

    void Runtime::visit_roots(const std::function<void(Object *)> &visitor) const
    {
        for (Object *root : roots_)
        {
            if (root)
                visitor(root);
        }
    }

    void Runtime::visit_heap(const std::function<void(Object *)> &visitor) const
    {
        for (const auto &object : heap_)
        {
            if (object)
                visitor(object.get());
        }
    }

    std::size_t Runtime::heap_size() const noexcept
    {
        return heap_.size();
    }

    std::size_t Runtime::root_count() const noexcept
    {
        return roots_.size();
    }

    TypeObject *Runtime::allocate_type(std::string name)
    {
        return allocate<TypeObject>(std::move(name));
    }

    void Runtime::initialize_type(TypeObject *type, TypeObject *base)
    {
        type->base = base;
        type->bases = base ? std::vector<TypeObject *>{base} : std::vector<TypeObject *>{};
        type->mro.clear();
        type->mro.push_back(type);
        if (base)
        {
            for (TypeObject *mro_item : base->mro)
                type->mro.push_back(mro_item);
        }
    }

    void Runtime::initialize_type_dict(TypeObject *type)
    {
        if (!type || type->dict)
            return;
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str(type->name));
        dict->set(new_str("__module__"), new_str(type->module));
        type->dict = dict;
    }

    void Runtime::initialize_type_dicts()
    {
        initialize_type_dict(object_type_);
        initialize_type_dict(type_type_);
        initialize_type_dict(none_type_);
        initialize_type_dict(int_type_);
        initialize_type_dict(bool_type_);
        initialize_type_dict(str_type_);
        initialize_type_dict(list_type_);
        initialize_type_dict(tuple_type_);
        initialize_type_dict(dict_type_);
        initialize_type_dict(module_type_);
        initialize_type_dict(native_function_type_);
        initialize_type_dict(method_descriptor_type_);
        initialize_type_dict(code_type_);
        initialize_type_dict(frame_type_);
        initialize_type_dict(base_exception_type_);
        initialize_type_dict(exception_type_);
        initialize_type_dict(type_error_type_);
        initialize_type_dict(index_error_type_);
        initialize_type_dict(syntax_error_type_);
        initialize_type_dict(name_error_type_);
        initialize_type_dict(attribute_error_type_);
    }

    TypeSlots Runtime::inherit_slots(TypeObject *base, TypeSlots slots) const
    {
        if (!base)
            return slots;
        TypeSlots inherited = base->slots;
        if (slots.repr)
            inherited.repr = slots.repr;
        if (slots.hash)
            inherited.hash = slots.hash;
        if (slots.equal)
            inherited.equal = slots.equal;
        if (slots.truth)
            inherited.truth = slots.truth;
        if (slots.traverse)
            inherited.traverse = slots.traverse;
        if (slots.binary_add)
            inherited.binary_add = slots.binary_add;
        if (slots.call)
            inherited.call = slots.call;
        if (slots.descr_get)
            inherited.descr_get = slots.descr_get;
        if (slots.descr_set)
            inherited.descr_set = slots.descr_set;
        return inherited;
    }
}
