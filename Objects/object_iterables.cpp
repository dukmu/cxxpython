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
        Object *iterator_iter(Object &object)
        {
            return &object;
        }

        std::int64_t require_int_arg(Runtime *runtime, const char *callable, Object *object)
        {
            auto *integer = dynamic_cast<IntObject *>(object);
            if (!integer)
                raise_type_error(runtime, std::string(callable) + " integer argument expected");
            return integer->value;
        }

        struct RangeArguments
        {
            std::int64_t start = 0;
            std::int64_t stop = 0;
            std::int64_t step = 1;
        };

        RangeArguments parse_range_args(Runtime *runtime, const CallArguments &args)
        {
            require_no_keywords(runtime, "range", args);
            require_positional_range(runtime, "range", args, 1, 3);

            RangeArguments parsed;
            if (args.positional.size() == 1)
            {
                parsed.stop = require_int_arg(runtime, "range", args.positional[0]);
            }
            else
            {
                parsed.start = require_int_arg(runtime, "range", args.positional[0]);
                parsed.stop = require_int_arg(runtime, "range", args.positional[1]);
                if (args.positional.size() == 3)
                    parsed.step = require_int_arg(runtime, "range", args.positional[2]);
            }
            if (parsed.step == 0)
                raise_value_error(runtime, "range() arg 3 must not be zero");
            return parsed;
        }

        std::int64_t normalize_range_index(Runtime *runtime, const RangeObject &range, Object *key)
        {
            std::int64_t index = require_int_arg(runtime, "range", key);
            const std::int64_t length = static_cast<std::int64_t>(range.length());
            if (index < 0)
                index += length;
            if (index < 0 || index >= length)
                raise_index_error(runtime, "range object index out of range");
            return index;
        }

        std::string range_repr(const Object &object)
        {
            auto *range = static_cast<const RangeObject *>(&object);
            std::ostringstream out;
            out << "range(" << range->start() << ", " << range->stop();
            if (range->step() != 1)
                out << ", " << range->step();
            out << ")";
            return out.str();
        }

        std::size_t range_length(const Object &object)
        {
            return static_cast<const RangeObject *>(&object)->length();
        }

        Object *range_getitem(Object &object, Object *key)
        {
            auto &range = static_cast<RangeObject &>(object);
            const std::int64_t index = normalize_range_index(object.runtime(), range, key);
            return object.runtime()->new_int(range.value_at(static_cast<std::size_t>(index)));
        }

        bool range_contains(Object &object, Object *key)
        {
            auto *integer = dynamic_cast<IntObject *>(key);
            if (!integer)
                return false;

            auto &range = static_cast<RangeObject &>(object);
            if (range.length() == 0)
                return false;

            const std::int64_t value = integer->value;
            const std::int64_t step = range.step();
            if (step > 0)
            {
                if (value < range.start() || value >= range.stop())
                    return false;
            }
            else
            {
                if (value > range.start() || value <= range.stop())
                    return false;
            }
            return (value - range.start()) % step == 0;
        }

        Object *range_iter(Object &object)
        {
            auto &range = static_cast<RangeObject &>(object);
            return object.runtime()->new_range_iterator(range.start(), range.step(), range.length());
        }

        Object *range_new(TypeObject &type, const CallArguments &args)
        {
            RangeArguments parsed = parse_range_args(type.runtime(), args);
            return type.runtime()->allocate<RangeObject>(&type, parsed.start, parsed.stop, parsed.step);
        }

        void range_init(Object &object, const CallArguments &args)
        {
            (void)parse_range_args(object.runtime(), args);
        }

        Object *range_iterator_next(Object &object)
        {
            auto &iterator = static_cast<RangeIteratorObject &>(object);
            if (iterator.remaining() == 0)
                raise_stop_iteration(object.runtime());
            return object.runtime()->new_int(iterator.consume_value());
        }

        void enumerate_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *enumerate = static_cast<const EnumerateObject *>(&object);
            if (enumerate->iterator())
                visitor(enumerate->iterator());
        }

        Object *enumerate_next(Object &object)
        {
            auto &enumerate = static_cast<EnumerateObject &>(object);
            Object *item = object_next(enumerate.iterator());
            auto item_root = object.runtime()->root(item);
            Object *index = object.runtime()->new_int(enumerate.consume_index());
            auto index_root = object.runtime()->root(index);
            return object.runtime()->new_tuple({index_root.get(), item_root.get()});
        }

        Object *enumerate_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "enumerate", args);
            require_positional_range(type.runtime(), "enumerate", args, 1, 2);
            std::int64_t start = 0;
            if (args.positional.size() == 2)
                start = require_int_arg(type.runtime(), "enumerate", args.positional[1]);
            Object *iterator = object_iter(args.positional[0]);
            auto iterator_root = type.runtime()->root(iterator);
            return type.runtime()->allocate<EnumerateObject>(&type, iterator_root.get(), start);
        }

        void enumerate_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "enumerate", args);
            require_positional_range(object.runtime(), "enumerate", args, 1, 2);
            if (args.positional.size() == 2)
                (void)require_int_arg(object.runtime(), "enumerate", args.positional[1]);
        }

        void vector_iterator_traverse(const std::vector<Object *> &iterators, const VisitChildren &visitor)
        {
            for (Object *iterator : iterators)
            {
                if (iterator)
                    visitor(iterator);
            }
        }

        void zip_traverse(const Object &object, const VisitChildren &visitor)
        {
            vector_iterator_traverse(static_cast<const ZipObject *>(&object)->iterators(), visitor);
        }

        Object *zip_next(Object &object)
        {
            auto &zip = static_cast<ZipObject &>(object);
            if (zip.iterators().empty())
                raise_stop_iteration(object.runtime());

            std::vector<Object *> items;
            std::vector<Rooted<Object>> roots;
            items.reserve(zip.iterators().size());
            roots.reserve(zip.iterators().size());
            for (Object *iterator : zip.iterators())
            {
                Object *item = object_next(iterator);
                roots.push_back(object.runtime()->root(item));
                items.push_back(item);
            }
            return object.runtime()->new_tuple(std::move(items));
        }

        Object *zip_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "zip", args);
            std::vector<Object *> iterators;
            std::vector<Rooted<Object>> roots;
            iterators.reserve(args.positional.size());
            roots.reserve(args.positional.size());
            for (Object *iterable : args.positional)
            {
                Object *iterator = object_iter(iterable);
                roots.push_back(type.runtime()->root(iterator));
                iterators.push_back(iterator);
            }
            return type.runtime()->allocate<ZipObject>(&type, std::move(iterators));
        }

        void zip_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "zip", args);
        }

        void map_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *map = static_cast<const MapObject *>(&object);
            if (map->callable())
                visitor(map->callable());
            vector_iterator_traverse(map->iterators(), visitor);
        }

        Object *map_next(Object &object)
        {
            auto &map = static_cast<MapObject &>(object);
            std::vector<Object *> args;
            std::vector<Rooted<Object>> roots;
            args.reserve(map.iterators().size());
            roots.reserve(map.iterators().size());
            for (Object *iterator : map.iterators())
            {
                Object *item = object_next(iterator);
                roots.push_back(object.runtime()->root(item));
                args.push_back(item);
            }
            auto callable_root = object.runtime()->root(map.callable());
            return object_call(
                callable_root.get(),
                CallArguments{ObjectSpan(args.data(), args.size()), nullptr});
        }

        Object *map_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "map", args);
            if (args.positional.size() < 2)
                raise_type_error(type.runtime(), "map() must have at least two arguments");

            std::vector<Object *> iterators;
            std::vector<Rooted<Object>> roots;
            iterators.reserve(args.positional.size() - 1);
            roots.reserve(args.positional.size() - 1);
            for (std::size_t index = 1; index < args.positional.size(); ++index)
            {
                Object *iterator = object_iter(args.positional[index]);
                roots.push_back(type.runtime()->root(iterator));
                iterators.push_back(iterator);
            }
            return type.runtime()->allocate<MapObject>(&type, args.positional[0], std::move(iterators));
        }

        void map_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "map", args);
            if (args.positional.size() < 2)
                raise_type_error(object.runtime(), "map() must have at least two arguments");
        }

        void filter_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *filter = static_cast<const FilterObject *>(&object);
            if (filter->callable())
                visitor(filter->callable());
            if (filter->iterator())
                visitor(filter->iterator());
        }

        Object *filter_next(Object &object)
        {
            auto &filter = static_cast<FilterObject &>(object);
            while (true)
            {
                Object *item = object_next(filter.iterator());
                auto item_root = object.runtime()->root(item);
                bool keep = false;
                if (filter.callable() == object.runtime()->none())
                {
                    keep = is_true(item_root.get());
                }
                else
                {
                    auto callable_root = object.runtime()->root(filter.callable());
                    Object *result = object_call(callable_root.get(), {item_root.get()});
                    auto result_root = object.runtime()->root(result);
                    keep = is_true(result_root.get());
                }
                if (keep)
                    return item_root.get();
            }
        }

        Object *filter_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "filter", args);
            require_positional_count(type.runtime(), "filter", args, 2);
            Object *iterator = object_iter(args.positional[1]);
            auto iterator_root = type.runtime()->root(iterator);
            return type.runtime()->allocate<FilterObject>(&type, args.positional[0], iterator_root.get());
        }

        void filter_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "filter", args);
            require_positional_count(object.runtime(), "filter", args, 2);
        }
    }

    RangeObject::RangeObject(TypeObject *type, std::int64_t start, std::int64_t stop, std::int64_t step)
        : Object(type),
          start_(start),
          stop_(stop),
          step_(step),
          length_(compute_length(start, stop, step))
    {
    }

    std::int64_t RangeObject::start() const noexcept
    {
        return start_;
    }

    std::int64_t RangeObject::stop() const noexcept
    {
        return stop_;
    }

    std::int64_t RangeObject::step() const noexcept
    {
        return step_;
    }

    std::size_t RangeObject::length() const noexcept
    {
        return length_;
    }

    std::int64_t RangeObject::value_at(std::size_t index) const
    {
        if (index >= length_)
            raise_index_error(runtime(), "range object index out of range");
        return start_ + static_cast<std::int64_t>(index) * step_;
    }

    std::size_t RangeObject::compute_length(std::int64_t start, std::int64_t stop, std::int64_t step)
    {
        if (step > 0)
        {
            if (start >= stop)
                return 0;
            return static_cast<std::size_t>(1 + (stop - start - 1) / step);
        }
        if (start <= stop)
            return 0;
        return static_cast<std::size_t>(1 + (start - stop - 1) / (-step));
    }

    RangeIteratorObject::RangeIteratorObject(TypeObject *type,
                                             std::int64_t current,
                                             std::int64_t step,
                                             std::size_t remaining)
        : Object(type), current_(current), step_(step), remaining_(remaining)
    {
    }

    std::int64_t RangeIteratorObject::consume_value()
    {
        if (remaining_ == 0)
            raise_stop_iteration(runtime());
        const std::int64_t value = current_;
        current_ += step_;
        --remaining_;
        return value;
    }

    std::size_t RangeIteratorObject::remaining() const noexcept
    {
        return remaining_;
    }

    EnumerateObject::EnumerateObject(TypeObject *type, Object *iterator, std::int64_t next_index)
        : Object(type), iterator_(iterator), next_index_(next_index)
    {
        if (iterator_)
            iterator_->incref();
    }

    EnumerateObject::~EnumerateObject()
    {
        clear_references();
    }

    void EnumerateObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(iterator_);
        Object::clear_references();
    }

    Object *EnumerateObject::iterator() const noexcept
    {
        return iterator_;
    }

    std::int64_t EnumerateObject::consume_index() noexcept
    {
        return next_index_++;
    }

    ZipObject::ZipObject(TypeObject *type, std::vector<Object *> iterators)
        : Object(type), iterators_(std::move(iterators))
    {
        for (Object *iterator : iterators_)
        {
            if (iterator)
                iterator->incref();
        }
    }

    ZipObject::~ZipObject()
    {
        clear_references();
    }

    void ZipObject::clear_references()
    {
        CXXPYTHON_CLEAR_VECTOR_REF(iterators_);
        Object::clear_references();
    }

    const std::vector<Object *> &ZipObject::iterators() const noexcept
    {
        return iterators_;
    }

    MapObject::MapObject(TypeObject *type, Object *callable, std::vector<Object *> iterators)
        : Object(type), callable_(callable), iterators_(std::move(iterators))
    {
        if (callable_)
            callable_->incref();
        for (Object *iterator : iterators_)
        {
            if (iterator)
                iterator->incref();
        }
    }

    MapObject::~MapObject()
    {
        clear_references();
    }

    void MapObject::clear_references()
    {
        CXXPYTHON_CLEAR_VECTOR_REF(iterators_);
        CXXPYTHON_CLEAR_REF(callable_);
        Object::clear_references();
    }

    Object *MapObject::callable() const noexcept
    {
        return callable_;
    }

    const std::vector<Object *> &MapObject::iterators() const noexcept
    {
        return iterators_;
    }

    FilterObject::FilterObject(TypeObject *type, Object *callable, Object *iterator)
        : Object(type), callable_(callable), iterator_(iterator)
    {
        if (callable_)
            callable_->incref();
        if (iterator_)
            iterator_->incref();
    }

    FilterObject::~FilterObject()
    {
        clear_references();
    }

    void FilterObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(iterator_);
        CXXPYTHON_CLEAR_REF(callable_);
        Object::clear_references();
    }

    Object *FilterObject::callable() const noexcept
    {
        return callable_;
    }

    Object *FilterObject::iterator() const noexcept
    {
        return iterator_;
    }

    TypeSlots make_range_slots()
    {
        return TypeSlots{
            .repr = range_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .length = range_length,
            .getitem = range_getitem,
            .contains = range_contains,
            .iter = range_iter,
            .new_object = range_new,
            .init = range_init,
        };
    }

    TypeSlots make_range_iterator_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .iter = iterator_iter,
            .iternext = range_iterator_next,
        };
    }

    TypeSlots make_enumerate_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = enumerate_traverse,
            .iter = iterator_iter,
            .iternext = enumerate_next,
            .new_object = enumerate_new,
            .init = enumerate_init,
        };
    }

    TypeSlots make_zip_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = zip_traverse,
            .iter = iterator_iter,
            .iternext = zip_next,
            .new_object = zip_new,
            .init = zip_init,
        };
    }

    TypeSlots make_map_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = map_traverse,
            .iter = iterator_iter,
            .iternext = map_next,
            .new_object = map_new,
            .init = map_init,
        };
    }

    TypeSlots make_filter_slots()
    {
        return TypeSlots{
            .repr = default_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = filter_traverse,
            .iter = iterator_iter,
            .iternext = filter_next,
            .new_object = filter_new,
            .init = filter_init,
        };
    }

    void initialize_range_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_sequence_slot_wrappers(type, false);
    }

    void initialize_range_iterator_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }

    void initialize_enumerate_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }

    void initialize_zip_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }

    void initialize_map_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }

    void initialize_filter_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
        add_slot_wrapper(type, "__next__", SlotWrapperKind::Next);
    }
}
