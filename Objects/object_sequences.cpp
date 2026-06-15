#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <cstddef>
#include <sstream>
#include <utility>

namespace cxxpython
{
    namespace
    {
        std::string list_repr(const Object &object)
        {
            auto *list = static_cast<const ListObject *>(&object);
            const std::vector<Object *> items = list->items();
            std::ostringstream out;
            out << "[";
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(items[i]);
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
            if (!right_list)
                return false;
            const std::vector<Object *> left_items = left_list->items();
            const std::vector<Object *> right_items = right_list->items();
            if (left_items.size() != right_items.size())
                return false;
            for (std::size_t i = 0; i < left_items.size(); ++i)
            {
                if (!object_equal(left_items[i], right_items[i]))
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

        std::vector<Object *> slice_items(Runtime &runtime,
                                          const std::vector<Object *> &items,
                                          const SliceObject &slice)
        {
            SliceRange range = normalize_slice(runtime, slice, static_cast<std::int64_t>(items.size()));
            std::vector<Object *> result;
            result.reserve(range.length);
            std::int64_t index = range.start;
            for (std::size_t count = 0; count < range.length; ++count)
            {
                result.push_back(items[static_cast<std::size_t>(index)]);
                index += range.step;
            }
            return result;
        }

        std::size_t list_length(const Object &object)
        {
            return static_cast<const ListObject *>(&object)->size();
        }

        Object *list_getitem(Object &object, Object *key)
        {
            auto &list = static_cast<ListObject &>(object);
            if (auto *slice = dynamic_cast<SliceObject *>(key))
                return object.runtime()->new_list(slice_items(*object.runtime(), list.items(), *slice));
            return list.get(sequence_index(object, key, "list"));
        }

        void list_setitem(Object &object, Object *key, Object *value)
        {
            auto &list = static_cast<ListObject &>(object);
            list.set(sequence_index(object, key, "list"), value);
        }

        void list_delitem(Object &object, Object *key)
        {
            auto &list = static_cast<ListObject &>(object);
            list.erase(sequence_index(object, key, "list"));
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

        Object *sequence_iter(Object &object)
        {
            return object.runtime()->new_sequence_iterator(&object);
        }

        Object *list_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "list", args);
            require_positional_at_most(type.runtime(), "list", args, 1);
            if (args.positional.empty())
                return type.runtime()->allocate<ListObject>(&type, std::vector<Object *>{});

            Runtime *runtime = type.runtime();
            return runtime->allocate<ListObject>(&type, collect_iterable_items(runtime, args.positional[0]));
        }

        void list_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "list", args);
            require_positional_at_most(object.runtime(), "list", args, 1);
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

        Object *list_extend_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "list.extend() takes exactly one argument");
            ListObject *list = static_cast<ListObject *>(self);
            std::vector<Object *> items = collect_iterable_items(&runtime, args[0]);
            for (Object *item : items)
                list->append(item);
            return runtime.none();
        }

        Object *list_pop_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() > 1)
                raise_type_error(&runtime, "list.pop() takes at most one argument");
            ListObject *list = static_cast<ListObject *>(self);
            if (list->size() == 0)
                raise_index_error(&runtime, "pop from empty list");
            std::int64_t index = -1;
            if (args.size() == 1)
            {
                auto *index_object = dynamic_cast<IntObject *>(args[0]);
                if (!index_object)
                    raise_type_error(&runtime, "list.pop() index must be an integer");
                index = index_object->value;
            }
            Object *item = list->get(index);
            auto item_root = runtime.root(item);
            list->erase(index);
            return item_root.get();
        }

        Object *list_clear_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "list.clear() takes exactly zero arguments");
            ListObject *list = static_cast<ListObject *>(self);
            while (list->size() != 0)
                list->erase(-1);
            return runtime.none();
        }

        Object *list_copy_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "list.copy() takes exactly zero arguments");
            return runtime.new_list(static_cast<ListObject *>(self)->items());
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
            if (auto *slice = dynamic_cast<SliceObject *>(key))
                return object.runtime()->new_tuple(slice_items(*object.runtime(), tuple.items(), *slice));
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

        void tuple_delitem(Object &object, Object *)
        {
            raise_type_error(object.runtime(), "'tuple' object does not support item deletion");
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

        Object *tuple_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "tuple", args);
            require_positional_at_most(type.runtime(), "tuple", args, 1);
            if (args.positional.empty())
                return type.runtime()->allocate<TupleObject>(&type, std::vector<Object *>{});

            Runtime *runtime = type.runtime();
            return runtime->allocate<TupleObject>(&type, collect_iterable_items(runtime, args.positional[0]));
        }

        void tuple_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "tuple", args);
            require_positional_at_most(object.runtime(), "tuple", args, 1);
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
        std::scoped_lock lock(mutex_);
        items_.push_back(item);
    }

    Object *ListObject::get(std::int64_t index) const
    {
        std::scoped_lock lock(mutex_);
        return items_[normalize_index(index)];
    }

    void ListObject::set(std::int64_t index, Object *item)
    {
        if (!item)
            raise_type_error(runtime(), "list item cannot be null");
        std::scoped_lock lock(mutex_);
        items_[normalize_index(index)] = item;
    }

    void ListObject::erase(std::int64_t index)
    {
        std::scoped_lock lock(mutex_);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(normalize_index(index)));
    }

    std::vector<Object *> ListObject::items() const
    {
        std::scoped_lock lock(mutex_);
        return items_;
    }

    std::size_t ListObject::size() const
    {
        std::scoped_lock lock(mutex_);
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
            .delitem = list_delitem,
            .contains = list_contains,
            .iter = sequence_iter,
            .new_object = list_new,
            .init = list_init,
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
            .delitem = tuple_delitem,
            .contains = tuple_contains,
            .iter = sequence_iter,
            .new_object = tuple_new,
            .init = tuple_init,
        };
    }

    void initialize_list_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("append"),
                        runtime.new_method_descriptor("append",
                                                      type,
                                                      list_append_method,
                                                      "Append object to the end of the list.",
                                                      "($self, object, /)"));
        type->dict->set(runtime.new_str("extend"),
                        runtime.new_method_descriptor("extend", type, list_extend_method));
        type->dict->set(runtime.new_str("pop"),
                        runtime.new_method_descriptor("pop", type, list_pop_method));
        type->dict->set(runtime.new_str("clear"),
                        runtime.new_method_descriptor("clear", type, list_clear_method));
        type->dict->set(runtime.new_str("copy"),
                        runtime.new_method_descriptor("copy", type, list_copy_method));
    }

    void initialize_list_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, false);
        add_sequence_slot_wrappers(type, true);
    }

    void initialize_tuple_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_sequence_slot_wrappers(type, false);
    }
}
