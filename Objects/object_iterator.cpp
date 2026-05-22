#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

namespace cxxpython
{
    namespace
    {
        Object *iterator_iter(Object &object)
        {
            return &object;
        }

        void sequence_iterator_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *iterator = static_cast<const SequenceIteratorObject *>(&object);
            if (iterator->sequence())
                visitor(iterator->sequence());
        }

        Object *sequence_iterator_next(Object &object)
        {
            auto &iterator = static_cast<SequenceIteratorObject &>(object);
            Object *sequence = iterator.sequence();
            if (!sequence)
                raise_stop_iteration(object.runtime());

            const std::size_t index = iterator.index();
            if (index >= object_length(sequence))
                raise_stop_iteration(object.runtime());

            IntObject *key = object.runtime()->new_int(static_cast<std::int64_t>(iterator.consume_index()));
            auto key_root = object.runtime()->root(key);
            return object_get_item(sequence, key_root.get());
        }

        void dict_key_iterator_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *iterator = static_cast<const DictKeyIteratorObject *>(&object);
            if (iterator->dict())
                visitor(iterator->dict());
        }

        Object *dict_key_iterator_next(Object &object)
        {
            auto &iterator = static_cast<DictKeyIteratorObject &>(object);
            DictObject *dict = iterator.dict();
            if (!dict)
                raise_stop_iteration(object.runtime());

            const std::size_t index = iterator.index();
            if (index >= dict->entries().size())
                raise_stop_iteration(object.runtime());
            (void)iterator.consume_index();
            return dict->entries()[index].key;
        }
    }

    SequenceIteratorObject::SequenceIteratorObject(TypeObject *type, Object *sequence)
        : Object(type), sequence_(sequence)
    {
    }

    Object *SequenceIteratorObject::sequence() const noexcept
    {
        return sequence_;
    }

    std::size_t SequenceIteratorObject::index() const noexcept
    {
        return index_;
    }

    std::size_t SequenceIteratorObject::consume_index() noexcept
    {
        return index_++;
    }

    DictKeyIteratorObject::DictKeyIteratorObject(TypeObject *type, DictObject *dict)
        : Object(type), dict_(dict)
    {
    }

    DictObject *DictKeyIteratorObject::dict() const noexcept
    {
        return dict_;
    }

    std::size_t DictKeyIteratorObject::index() const noexcept
    {
        return index_;
    }

    std::size_t DictKeyIteratorObject::consume_index() noexcept
    {
        return index_++;
    }

    TypeSlots make_sequence_iterator_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = sequence_iterator_traverse,
            .iter = iterator_iter,
            .iternext = sequence_iterator_next,
        };
    }

    TypeSlots make_dict_key_iterator_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = dict_key_iterator_traverse,
            .iter = iterator_iter,
            .iternext = dict_key_iterator_next,
        };
    }

    void initialize_iterator_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }
}
