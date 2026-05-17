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
        return inherited;
    }
}
