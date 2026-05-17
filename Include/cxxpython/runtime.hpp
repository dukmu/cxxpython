#pragma once

#include <memory>
#include <string>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "cxxpython/object.hpp"

namespace cxxpython
{
    class Runtime final
    {
    public:
        Runtime();
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        TypeObject *object_type() const noexcept;
        TypeObject *type_type() const noexcept;
        TypeObject *none_type() const noexcept;
        TypeObject *int_type() const noexcept;
        TypeObject *bool_type() const noexcept;
        TypeObject *str_type() const noexcept;
        TypeObject *tuple_type() const noexcept;
        TypeObject *dict_type() const noexcept;

        NoneObject *none() const noexcept;
        BoolObject *true_value() const noexcept;
        BoolObject *false_value() const noexcept;

        IntObject *new_int(std::int64_t value);
        StrObject *new_str(std::string value);
        TupleObject *new_tuple(std::vector<Object *> items);
        DictObject *new_dict();
        TypeObject *new_heap_type(std::string name, TypeObject *base, TypeSlots slots = {});

        void add_root(Object *object);
        void remove_root(Object *object);
        void visit_roots(const std::function<void(Object *)> &visitor) const;
        void visit_heap(const std::function<void(Object *)> &visitor) const;
        std::size_t heap_size() const noexcept;
        std::size_t root_count() const noexcept;

        template <class T, class... Args>
        T *allocate(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            auto object = std::make_unique<T>(std::forward<Args>(args)...);
            T *raw = object.get();
            heap_.push_back(std::move(object));
            return raw;
        }

    private:
        TypeObject *allocate_type(std::string name);
        void initialize_type(TypeObject *type, TypeObject *base);
        TypeSlots inherit_slots(TypeObject *base, TypeSlots slots) const;

        std::vector<std::unique_ptr<Object>> heap_;
        std::vector<Object *> roots_;

        TypeObject *object_type_ = nullptr;
        TypeObject *type_type_ = nullptr;
        TypeObject *none_type_ = nullptr;
        TypeObject *int_type_ = nullptr;
        TypeObject *bool_type_ = nullptr;
        TypeObject *str_type_ = nullptr;
        TypeObject *tuple_type_ = nullptr;
        TypeObject *dict_type_ = nullptr;

        NoneObject *none_ = nullptr;
        BoolObject *true_ = nullptr;
        BoolObject *false_ = nullptr;
    };
}
