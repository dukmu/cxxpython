#include "cxxpython/runtime.hpp"

#include <algorithm>
#include <sstream>

namespace cxxpython
{
    void Runtime::add_root(Object *object)
    {
        if (!object)
            return;
        std::scoped_lock lock(roots_mutex_);
        roots_.push_back(object);
    }

    void Runtime::remove_root(Object *object)
    {
        std::scoped_lock lock(roots_mutex_);
        const auto found = std::find(roots_.begin(), roots_.end(), object);
        if (found != roots_.end())
            roots_.erase(found);
    }

    void Runtime::visit_roots(const std::function<void(Object *)> &visitor) const
    {
        std::scoped_lock lock(roots_mutex_);
        for (Object *root : roots_)
        {
            if (root)
                visitor(root);
        }
    }

    void Runtime::register_thread_state(ThreadState *thread)
    {
        if (!thread)
            return;
        std::scoped_lock lock(thread_states_mutex_);
        thread_states_.push_back(thread);
    }

    void Runtime::unregister_thread_state(ThreadState *thread)
    {
        std::scoped_lock lock(thread_states_mutex_);
        const auto found = std::find(thread_states_.begin(), thread_states_.end(), thread);
        if (found != thread_states_.end())
            thread_states_.erase(found);
    }

    void Runtime::visit_thread_states(const std::function<void(ThreadState &)> &visitor) const
    {
        std::scoped_lock lock(thread_states_mutex_);
        for (ThreadState *thread : thread_states_)
        {
            if (thread)
                visitor(*thread);
        }
    }

    void Runtime::visit_heap(const std::function<void(Object *)> &visitor) const
    {
        std::scoped_lock lock(heap_mutex_);
        for (const auto &object : heap_)
        {
            if (object)
                visitor(object.get());
        }
    }

    std::size_t Runtime::heap_size() const noexcept
    {
        std::scoped_lock lock(heap_mutex_);
        return heap_.size();
    }

    std::size_t Runtime::root_count() const noexcept
    {
        std::scoped_lock lock(roots_mutex_);
        return roots_.size();
    }

    TypeObject *Runtime::allocate_type(std::string name)
    {
        return allocate<TypeObject>(std::move(name));
    }

    void Runtime::initialize_type(TypeObject *type, TypeObject *base)
    {
        initialize_type(type, base ? std::vector<TypeObject *>{base} : std::vector<TypeObject *>{});
    }

    void Runtime::initialize_type(TypeObject *type, const std::vector<TypeObject *> &bases)
    {
        type->base = bases.empty() ? nullptr : bases[0];
        type->bases = bases;
        type->mro.clear();
        type->mro.push_back(type);

        std::vector<std::vector<TypeObject *>> sequences;
        sequences.reserve(bases.size() + 1);
        std::vector<TypeObject *> seen_bases;
        for (TypeObject *base : bases)
        {
            if (!base)
                raise_type_error(this, "class bases cannot contain null");
            if (std::find(seen_bases.begin(), seen_bases.end(), base) != seen_bases.end())
            {
                std::ostringstream message;
                message << "duplicate base class " << type_qualified_name(base);
                raise_type_error(this, message.str());
            }
            seen_bases.push_back(base);
            sequences.push_back(base->mro);
        }
        sequences.push_back(bases);

        while (true)
        {
            sequences.erase(std::remove_if(sequences.begin(),
                                           sequences.end(),
                                           [](const std::vector<TypeObject *> &sequence)
                                           { return sequence.empty(); }),
                            sequences.end());
            if (sequences.empty())
                return;

            TypeObject *candidate = nullptr;
            for (const std::vector<TypeObject *> &sequence : sequences)
            {
                TypeObject *head = sequence.front();
                bool appears_in_tail = false;
                for (const std::vector<TypeObject *> &other : sequences)
                {
                    if (std::find(other.begin() + 1, other.end(), head) != other.end())
                    {
                        appears_in_tail = true;
                        break;
                    }
                }
                if (!appears_in_tail)
                {
                    candidate = head;
                    break;
                }
            }

            if (!candidate)
            {
                std::ostringstream message;
                message << "cannot create a consistent method resolution order (MRO) for bases";
                for (TypeObject *base : bases)
                    message << " " << type_qualified_name(base);
                raise_type_error(this, message.str());
            }

            type->mro.push_back(candidate);
            for (std::vector<TypeObject *> &sequence : sequences)
            {
                if (!sequence.empty() && sequence.front() == candidate)
                    sequence.erase(sequence.begin());
            }
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
        initialize_type_dict(not_implemented_type_);
        initialize_type_dict(int_type_);
        initialize_type_dict(bool_type_);
        initialize_type_dict(str_type_);
        initialize_type_dict(bytes_type_);
        initialize_type_dict(list_type_);
        initialize_type_dict(tuple_type_);
        initialize_type_dict(slice_type_);
        initialize_type_dict(dict_type_);
        initialize_type_dict(mappingproxy_type_);
        initialize_type_dict(sequence_iterator_type_);
        initialize_type_dict(dict_key_iterator_type_);
        initialize_type_dict(reverse_iterator_type_);
        initialize_type_dict(range_type_);
        initialize_type_dict(range_iterator_type_);
        initialize_type_dict(enumerate_type_);
        initialize_type_dict(zip_type_);
        initialize_type_dict(map_type_);
        initialize_type_dict(filter_type_);
        initialize_type_dict(text_io_type_);
        initialize_type_dict(thread_lock_type_);
        initialize_type_dict(module_type_);
        initialize_type_dict(native_function_type_);
        initialize_type_dict(function_type_);
        initialize_type_dict(cell_type_);
        initialize_type_dict(super_type_);
        initialize_type_dict(method_type_);
        initialize_type_dict(method_descriptor_type_);
        initialize_type_dict(slot_wrapper_type_);
        initialize_type_dict(bound_slot_wrapper_type_);
        initialize_type_dict(getset_descriptor_type_);
        initialize_type_dict(property_type_);
        initialize_type_dict(staticmethod_type_);
        initialize_type_dict(classmethod_type_);
        initialize_type_dict(code_type_);
        initialize_type_dict(frame_type_);
        initialize_type_dict(traceback_type_);
        initialize_type_dict(base_exception_type_);
        initialize_type_dict(exception_type_);
        initialize_type_dict(type_error_type_);
        initialize_type_dict(value_error_type_);
        initialize_type_dict(index_error_type_);
        initialize_type_dict(key_error_type_);
        initialize_type_dict(syntax_error_type_);
        initialize_type_dict(name_error_type_);
        initialize_type_dict(unbound_local_error_type_);
        initialize_type_dict(recursion_error_type_);
        initialize_type_dict(attribute_error_type_);
        initialize_type_dict(import_error_type_);
        initialize_type_dict(os_error_type_);
        initialize_type_dict(system_exit_type_);
        initialize_type_dict(keyboard_interrupt_type_);
        initialize_type_dict(stop_iteration_type_);
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
        if (slots.rich_compare)
            inherited.rich_compare = slots.rich_compare;
        if (slots.truth)
            inherited.truth = slots.truth;
        if (slots.traverse)
            inherited.traverse = slots.traverse;
        if (slots.binary_op)
            inherited.binary_op = slots.binary_op;
        if (slots.reflected_binary_op)
            inherited.reflected_binary_op = slots.reflected_binary_op;
        if (slots.inplace_binary_op)
            inherited.inplace_binary_op = slots.inplace_binary_op;
        if (slots.unary_op)
            inherited.unary_op = slots.unary_op;
        if (slots.call)
            inherited.call = slots.call;
        if (slots.length)
            inherited.length = slots.length;
        if (slots.getitem)
            inherited.getitem = slots.getitem;
        if (slots.setitem)
            inherited.setitem = slots.setitem;
        if (slots.delitem)
            inherited.delitem = slots.delitem;
        if (slots.contains)
            inherited.contains = slots.contains;
        if (slots.iter)
            inherited.iter = slots.iter;
        if (slots.iternext)
            inherited.iternext = slots.iternext;
        if (slots.descr_get)
            inherited.descr_get = slots.descr_get;
        if (slots.descr_set)
            inherited.descr_set = slots.descr_set;
        if (slots.new_object)
            inherited.new_object = slots.new_object;
        if (slots.init)
            inherited.init = slots.init;
        return inherited;
    }

    TypeObject *Runtime::best_base_for_layout(const std::vector<TypeObject *> &bases) const
    {
        if (bases.empty())
            return nullptr;

        for (TypeObject *base : bases)
        {
            TypeObject *solid = base && base->solid_base ? base->solid_base : base;
            if (solid && solid != object_type_)
                return base;
        }
        return bases[0];
    }

    void Runtime::validate_base_layouts(const std::vector<TypeObject *> &bases)
    {
        TypeObject *solid_base = nullptr;
        for (TypeObject *base : bases)
        {
            if (!base)
                raise_type_error(this, "class bases cannot contain null");
            TypeObject *candidate = base->solid_base ? base->solid_base : base;
            if (!candidate || candidate == object_type_)
                continue;
            if (!solid_base)
            {
                solid_base = candidate;
                continue;
            }
            if (solid_base != candidate)
                raise_type_error(this, "multiple bases have instance lay-out conflict");
        }
    }

    TypeObject *Runtime::select_metaclass(TypeObject *explicit_metaclass,
                                          const std::vector<TypeObject *> &bases)
    {
        TypeObject *winner = explicit_metaclass;
        if (winner && !is_subclass(winner, type_type_))
            raise_type_error(this, "metaclass must be a subclass of type");

        if (!winner)
            winner = bases.empty() ? type_type_ : bases[0]->type();

        for (TypeObject *base : bases)
        {
            TypeObject *base_metaclass = base ? base->type() : nullptr;
            if (!base_metaclass)
                raise_type_error(this, "class base has no metaclass");
            if (is_subclass(winner, base_metaclass))
                continue;
            if (is_subclass(base_metaclass, winner))
            {
                winner = base_metaclass;
                continue;
            }
            raise_type_error(
                this,
                "metaclass conflict: the metaclass of a derived class must be a (non-strict) "
                "subclass of the metaclasses of all its bases");
        }

        return winner;
    }
}
