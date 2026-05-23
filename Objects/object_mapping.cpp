#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <cstddef>
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
            const std::vector<DictObject::Entry> entries = dict->entries();
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
            if (!right_dict)
                return false;
            const std::vector<DictObject::Entry> left_entries = left_dict->entries();
            if (left_entries.size() != right_dict->size())
                return false;
            for (const DictObject::Entry &entry : left_entries)
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

        std::size_t dict_length(const Object &object)
        {
            return static_cast<const DictObject *>(&object)->size();
        }

        Object *dict_getitem(Object &object, Object *key)
        {
            auto &dict = static_cast<DictObject &>(object);
            if (Object *value = dict.get(key))
                return value;
            raise_key_error(object.runtime(), key);
        }

        void dict_setitem(Object &object, Object *key, Object *value)
        {
            auto &dict = static_cast<DictObject &>(object);
            dict.set(key, value);
        }

        void dict_delitem(Object &object, Object *key)
        {
            auto &dict = static_cast<DictObject &>(object);
            if (!dict.erase(key))
                raise_key_error(object.runtime(), key);
        }

        bool dict_contains(Object &object, Object *key)
        {
            auto &dict = static_cast<DictObject &>(object);
            return dict.contains(key);
        }

        Object *dict_iter(Object &object)
        {
            return object.runtime()->new_dict_key_iterator(static_cast<DictObject *>(&object));
        }

        Object *dict_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "dict", args);
            require_no_positional(type.runtime(), "dict", args);
            return type.runtime()->allocate<DictObject>(&type);
        }

        void dict_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "dict", args);
            require_no_positional(object.runtime(), "dict", args);
        }

        std::string mappingproxy_repr(const Object &object)
        {
            auto *proxy = static_cast<const MappingProxyObject *>(&object);
            return "mappingproxy(" + object_repr(proxy->mapping()) + ")";
        }

        std::int64_t mappingproxy_hash(const Object &object)
        {
            raise_type_error(object.runtime(), "unhashable type: 'mappingproxy'");
        }

        bool mappingproxy_equal(const Object &left, const Object &right)
        {
            auto *left_proxy = static_cast<const MappingProxyObject *>(&left);
            if (auto *right_proxy = dynamic_cast<const MappingProxyObject *>(&right))
                return object_equal(left_proxy->mapping(), right_proxy->mapping());
            return object_equal(left_proxy->mapping(), &right);
        }

        bool mappingproxy_truth(const Object &object)
        {
            auto *proxy = static_cast<const MappingProxyObject *>(&object);
            return proxy->mapping() && proxy->mapping()->size() != 0;
        }

        void mappingproxy_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *proxy = static_cast<const MappingProxyObject *>(&object);
            if (proxy->referenced_mapping())
                visitor(proxy->referenced_mapping());
        }

        std::size_t mappingproxy_length(const Object &object)
        {
            auto *proxy = static_cast<const MappingProxyObject *>(&object);
            return proxy->mapping()->size();
        }

        Object *mappingproxy_getitem(Object &object, Object *key)
        {
            auto &proxy = static_cast<MappingProxyObject &>(object);
            if (Object *value = proxy.referenced_mapping()->get(key))
                return value;
            raise_key_error(object.runtime(), key);
        }

        void mappingproxy_setitem(Object &object, Object *, Object *)
        {
            raise_type_error(object.runtime(), "'mappingproxy' object does not support item assignment");
        }

        void mappingproxy_delitem(Object &object, Object *)
        {
            raise_type_error(object.runtime(), "'mappingproxy' object does not support item deletion");
        }

        bool mappingproxy_contains(Object &object, Object *key)
        {
            auto &proxy = static_cast<MappingProxyObject &>(object);
            return proxy.referenced_mapping()->contains(key);
        }

        Object *mappingproxy_iter(Object &object)
        {
            auto &proxy = static_cast<MappingProxyObject &>(object);
            return object.runtime()->new_dict_key_iterator(proxy.referenced_mapping());
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

        std::scoped_lock lock(mutex_);
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

        std::scoped_lock lock(mutex_);
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

    bool DictObject::erase(Object *key)
    {
        if (!key)
            raise_type_error(runtime(), "dict key cannot be null");

        std::scoped_lock lock(mutex_);
        const std::int64_t key_hash = object_hash(key);
        const SlotSearch search = find_slot(key, key_hash);
        if (!search.found)
            return false;

        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(indices_[search.slot]));
        rebuild_indices(indices_.size());
        return true;
    }

    std::size_t DictObject::size() const
    {
        std::scoped_lock lock(mutex_);
        return entries_.size();
    }

    std::vector<DictObject::Entry> DictObject::entries() const
    {
        std::scoped_lock lock(mutex_);
        return entries_;
    }

    MappingProxyObject::MappingProxyObject(TypeObject *type, DictObject *mapping)
        : Object(type), mapping_(mapping)
    {
    }

    const DictObject *MappingProxyObject::mapping() const noexcept
    {
        return mapping_;
    }

    DictObject *MappingProxyObject::referenced_mapping() const noexcept
    {
        return mapping_;
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
            .length = dict_length,
            .getitem = dict_getitem,
            .setitem = dict_setitem,
            .delitem = dict_delitem,
            .contains = dict_contains,
            .iter = dict_iter,
            .new_object = dict_new,
            .init = dict_init,
        };
    }

    TypeSlots make_mappingproxy_slots()
    {
        return TypeSlots{
            .repr = mappingproxy_repr,
            .hash = mappingproxy_hash,
            .equal = mappingproxy_equal,
            .truth = mappingproxy_truth,
            .traverse = mappingproxy_traverse,
            .length = mappingproxy_length,
            .getitem = mappingproxy_getitem,
            .setitem = mappingproxy_setitem,
            .delitem = mappingproxy_delitem,
            .contains = mappingproxy_contains,
            .iter = mappingproxy_iter,
        };
    }

    void initialize_dict_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, false);
        add_sequence_slot_wrappers(type, true);
    }

    void initialize_mappingproxy_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, false);
        add_sequence_slot_wrappers(type, false);
    }
}
