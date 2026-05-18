#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string list_repr(const Object &object)
        {
            auto *list = static_cast<const ListObject *>(&object);
            std::ostringstream out;
            out << "[";
            for (std::size_t i = 0; i < list->items().size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(list->items()[i]);
            }
            out << "]";
            return out.str();
        }

        std::int64_t list_hash(const Object &object)
        {
            raise_type_error(object.runtime(), "unhashable type: 'list'");
        }

        bool list_equal(const Object &left, const Object &right)
        {
            auto *left_list = static_cast<const ListObject *>(&left);
            auto *right_list = dynamic_cast<const ListObject *>(&right);
            if (!right_list || left_list->items().size() != right_list->items().size())
                return false;
            for (std::size_t i = 0; i < left_list->items().size(); ++i)
            {
                if (!object_equal(left_list->items()[i], right_list->items()[i]))
                    return false;
            }
            return true;
        }

        bool list_truth(const Object &object)
        {
            return static_cast<const ListObject *>(&object)->size() != 0;
        }

        void list_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *list = static_cast<const ListObject *>(&object);
            for (Object *item : list->items())
            {
                if (item)
                    visitor(item);
            }
        }

        std::int64_t sequence_index(Object &object, Object *key, const char *type_name)
        {
            auto *index = dynamic_cast<IntObject *>(key);
            if (!index)
                raise_type_error(object.runtime(), std::string(type_name) + " indices must be integers");
            return index->value;
        }

        std::size_t list_length(const Object &object)
        {
            return static_cast<const ListObject *>(&object)->size();
        }

        Object *list_getitem(Object &object, Object *key)
        {
            auto &list = static_cast<ListObject &>(object);
            return list.get(sequence_index(object, key, "list"));
        }

        void list_setitem(Object &object, Object *key, Object *value)
        {
            auto &list = static_cast<ListObject &>(object);
            list.set(sequence_index(object, key, "list"), value);
        }

        bool list_contains(Object &object, Object *key)
        {
            auto &list = static_cast<ListObject &>(object);
            for (Object *item : list.items())
            {
                if (object_equal(item, key))
                    return true;
            }
            return false;
        }

        Object *list_new(TypeObject &type, const std::vector<Object *> &args)
        {
            if (!args.empty())
                raise_type_error(type.runtime(), "list() takes no arguments");
            return type.runtime()->allocate<ListObject>(&type, std::vector<Object *>{});
        }

        std::string tuple_repr(const Object &object)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            std::ostringstream out;
            out << "(";
            for (std::size_t i = 0; i < tuple->items().size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(tuple->items()[i]);
            }
            if (tuple->items().size() == 1)
                out << ",";
            out << ")";
            return out.str();
        }

        std::int64_t tuple_hash(const Object &object)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            std::uint64_t result = 0x345678UL;
            for (const Object *item : tuple->items())
                result = (result ^ static_cast<std::uint64_t>(object_hash(item))) * 1000003UL;
            result ^= tuple->items().size();
            return static_cast<std::int64_t>(result);
        }

        bool tuple_equal(const Object &left, const Object &right)
        {
            auto *left_tuple = static_cast<const TupleObject *>(&left);
            auto *right_tuple = dynamic_cast<const TupleObject *>(&right);
            if (!right_tuple || left_tuple->items().size() != right_tuple->items().size())
                return false;
            for (std::size_t i = 0; i < left_tuple->items().size(); ++i)
            {
                if (!object_equal(left_tuple->items()[i], right_tuple->items()[i]))
                    return false;
            }
            return true;
        }

        bool tuple_truth(const Object &object)
        {
            return static_cast<const TupleObject *>(&object)->size() != 0;
        }

        void tuple_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            for (Object *item : tuple->items())
            {
                if (item)
                    visitor(item);
            }
        }

        std::size_t tuple_length(const Object &object)
        {
            return static_cast<const TupleObject *>(&object)->size();
        }

        Object *tuple_getitem(Object &object, Object *key)
        {
            auto &tuple = static_cast<TupleObject &>(object);
            const std::int64_t size = static_cast<std::int64_t>(tuple.size());
            const std::int64_t index = sequence_index(object, key, "tuple");
            const std::int64_t resolved = index < 0 ? size + index : index;
            if (resolved < 0 || resolved >= size)
                raise_index_error(object.runtime(), "tuple index out of range");
            return tuple.items()[static_cast<std::size_t>(resolved)];
        }

        void tuple_setitem(Object &object, Object *, Object *)
        {
            raise_type_error(object.runtime(), "'tuple' object does not support item assignment");
        }

        bool tuple_contains(Object &object, Object *key)
        {
            auto &tuple = static_cast<TupleObject &>(object);
            for (Object *item : tuple.items())
            {
                if (object_equal(item, key))
                    return true;
            }
            return false;
        }
    }

    ListObject::ListObject(TypeObject *type, std::vector<Object *> items)
        : Object(type), items_(std::move(items))
    {
    }

    void ListObject::append(Object *item)
    {
        if (!item)
            raise_type_error(runtime(), "list item cannot be null");
        items_.push_back(item);
    }

    Object *ListObject::get(std::int64_t index) const
    {
        return items_[normalize_index(index)];
    }

    void ListObject::set(std::int64_t index, Object *item)
    {
        if (!item)
            raise_type_error(runtime(), "list item cannot be null");
        items_[normalize_index(index)] = item;
    }

    const std::vector<Object *> &ListObject::items() const noexcept
    {
        return items_;
    }

    std::size_t ListObject::size() const noexcept
    {
        return items_.size();
    }

    std::size_t ListObject::normalize_index(std::int64_t index) const
    {
        const std::int64_t size = static_cast<std::int64_t>(items_.size());
        const std::int64_t resolved = index < 0 ? size + index : index;
        if (resolved < 0 || resolved >= size)
            raise_index_error(runtime(), "list index out of range");
        return static_cast<std::size_t>(resolved);
    }

    TupleObject::TupleObject(TypeObject *type, std::vector<Object *> items)
        : Object(type), items_(std::move(items))
    {
    }

    const std::vector<Object *> &TupleObject::items() const noexcept
    {
        return items_;
    }

    std::size_t TupleObject::size() const noexcept
    {
        return items_.size();
    }

    TypeSlots make_list_slots()
    {
        return TypeSlots{
            .repr = list_repr,
            .hash = list_hash,
            .equal = list_equal,
            .truth = list_truth,
            .traverse = list_traverse,
            .length = list_length,
            .getitem = list_getitem,
            .setitem = list_setitem,
            .contains = list_contains,
            .new_object = list_new,
        };
    }

    TypeSlots make_tuple_slots()
    {
        return TypeSlots{
            .repr = tuple_repr,
            .hash = tuple_hash,
            .equal = tuple_equal,
            .truth = tuple_truth,
            .traverse = tuple_traverse,
            .length = tuple_length,
            .getitem = tuple_getitem,
            .setitem = tuple_setitem,
            .contains = tuple_contains,
        };
    }
}
