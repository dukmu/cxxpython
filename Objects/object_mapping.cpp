#include "cxxpython/object.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string dict_repr(const Object &object)
        {
            auto *dict = static_cast<const DictObject *>(&object);
            std::ostringstream out;
            out << "{";
            const auto &entries = dict->entries();
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(entries[i].key) << ": " << object_repr(entries[i].value);
            }
            out << "}";
            return out.str();
        }

        std::int64_t dict_hash(const Object &object)
        {
            raise_type_error(object.runtime(), "unhashable type: 'dict'");
        }

        bool dict_equal(const Object &left, const Object &right)
        {
            auto *left_dict = static_cast<const DictObject *>(&left);
            auto *right_dict = dynamic_cast<const DictObject *>(&right);
            if (!right_dict || left_dict->entries().size() != right_dict->entries().size())
                return false;
            for (const DictObject::Entry &entry : left_dict->entries())
            {
                Object *right_value = right_dict->get(entry.key);
                if (!right_value || !object_equal(entry.value, right_value))
                    return false;
            }
            return true;
        }

        bool dict_truth(const Object &object)
        {
            return static_cast<const DictObject *>(&object)->size() != 0;
        }

        void dict_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *dict = static_cast<const DictObject *>(&object);
            for (const DictObject::Entry &entry : dict->entries())
            {
                if (entry.key)
                    visitor(entry.key);
                if (entry.value)
                    visitor(entry.value);
            }
        }
    }

    DictObject::DictObject(TypeObject *type)
        : Object(type)
    {
        rebuild_indices(8);
    }

    bool DictObject::contains(Object *key) const
    {
        return get(key) != nullptr;
    }

    Object *DictObject::get(Object *key) const
    {
        if (!key)
            raise_type_error(runtime(), "dict key cannot be null");

        const std::int64_t key_hash = object_hash(key);
        const SlotSearch search = find_slot(key, key_hash);
        if (!search.found)
            return nullptr;
        return entries_[indices_[search.slot]].value;
    }

    void DictObject::set(Object *key, Object *value)
    {
        if (!key)
            raise_type_error(runtime(), "dict key cannot be null");
        if (!value)
            raise_type_error(runtime(), "dict value cannot be null");

        const std::int64_t key_hash = object_hash(key);
        ensure_index_capacity();
        SlotSearch search = find_slot(key, key_hash);
        if (search.found)
        {
            entries_[indices_[search.slot]].value = value;
            return;
        }

        entries_.push_back(Entry{key, value, key_hash});
        indices_[search.slot] = entries_.size() - 1;
        ensure_index_capacity();
    }

    std::size_t DictObject::size() const noexcept
    {
        return entries_.size();
    }

    const std::vector<DictObject::Entry> &DictObject::entries() const noexcept
    {
        return entries_;
    }

    DictObject::SlotSearch DictObject::find_slot(Object *key, std::int64_t hash) const
    {
        const std::size_t mask = indices_.size() - 1;
        std::size_t slot = static_cast<std::uint64_t>(hash) & mask;
        std::size_t perturb = static_cast<std::uint64_t>(hash);
        while (true)
        {
            const std::size_t entry_index = indices_[slot];
            if (entry_index == empty_index)
                return SlotSearch{slot, false};

            const Entry &entry = entries_[entry_index];
            if (entry.hash == hash && object_equal(entry.key, key))
                return SlotSearch{slot, true};

            slot = (slot * 5 + perturb + 1) & mask;
            perturb >>= 5;
        }
    }

    void DictObject::ensure_index_capacity()
    {
        if (indices_.empty())
            rebuild_indices(8);
        if ((entries_.size() + 1) * 3 < indices_.size() * 2)
            return;
        rebuild_indices(indices_.size() * 2);
    }

    void DictObject::rebuild_indices(std::size_t capacity)
    {
        std::size_t normalized = 8;
        while (normalized < capacity)
            normalized *= 2;

        indices_.assign(normalized, empty_index);
        for (std::size_t entry_index = 0; entry_index < entries_.size(); ++entry_index)
        {
            const Entry &entry = entries_[entry_index];
            const SlotSearch search = find_slot(entry.key, entry.hash);
            indices_[search.slot] = entry_index;
        }
    }

    TypeSlots make_dict_slots()
    {
        return TypeSlots{
            .repr = dict_repr,
            .hash = dict_hash,
            .equal = dict_equal,
            .truth = dict_truth,
            .traverse = dict_traverse,
        };
    }
}
