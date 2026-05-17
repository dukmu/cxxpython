#pragma once

#include <cstdint>
#include <functional>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cxxpython
{
    class Runtime;
    class Object;
    class TypeObject;
    class ListObject;
    class ExceptionObject;

    using VisitChildren = std::function<void(Object *)>;
    using ReprSlot = std::string (*)(const Object &);
    using HashSlot = std::int64_t (*)(const Object &);
    using EqualSlot = bool (*)(const Object &, const Object &);
    using TruthSlot = bool (*)(const Object &);
    using TraverseSlot = void (*)(const Object &, const VisitChildren &);

    struct TypeSlots
    {
        ReprSlot repr = nullptr;
        HashSlot hash = nullptr;
        EqualSlot equal = nullptr;
        TruthSlot truth = nullptr;
        TraverseSlot traverse = nullptr;
    };

    class PythonError final : public std::exception
    {
    public:
        explicit PythonError(ExceptionObject *value);

        ExceptionObject *value() const noexcept;
        TypeObject *type() const noexcept;
        std::string type_name() const;
        const char *what() const noexcept override;

    private:
        ExceptionObject *value_;
        std::string what_;
    };

    class Object
    {
    public:
        explicit Object(TypeObject *type);
        virtual ~Object() = default;

        Runtime *runtime() const noexcept;
        void set_runtime(Runtime *runtime) noexcept;
        TypeObject *type() const noexcept;
        void set_type(TypeObject *type) noexcept;
        std::string repr() const;
        std::int64_t hash() const;
        bool equals(const Object &other) const;
        bool truth() const;
        void visit_children(const VisitChildren &visitor) const;

    private:
        Runtime *runtime_ = nullptr;
        TypeObject *type_;
    };

    class TypeObject : public Object
    {
    public:
        explicit TypeObject(std::string name);

        std::string name;
        std::string module = "builtins";
        TypeObject *base = nullptr;
        std::vector<TypeObject *> bases;
        std::vector<TypeObject *> mro;
        TypeSlots slots;
    };

    class NoneObject final : public Object
    {
    public:
        explicit NoneObject(TypeObject *type);
    };

    class IntObject : public Object
    {
    public:
        IntObject(TypeObject *type, std::int64_t value);

        std::int64_t value;
    };

    class BoolObject final : public IntObject
    {
    public:
        BoolObject(TypeObject *type, bool value);
    };

    class StrObject final : public Object
    {
    public:
        StrObject(TypeObject *type, std::string value);

        std::string value;
    };

    class ListObject final : public Object
    {
    public:
        ListObject(TypeObject *type, std::vector<Object *> items);

        void append(Object *item);
        Object *get(std::int64_t index) const;
        void set(std::int64_t index, Object *item);
        const std::vector<Object *> &items() const noexcept;
        std::size_t size() const noexcept;

    private:
        std::size_t normalize_index(std::int64_t index) const;

        std::vector<Object *> items_;
    };

    class TupleObject final : public Object
    {
    public:
        TupleObject(TypeObject *type, std::vector<Object *> items);

        const std::vector<Object *> &items() const noexcept;
        std::size_t size() const noexcept;

    private:
        std::vector<Object *> items_;
    };

    class DictObject final : public Object
    {
    public:
        struct Entry
        {
            Object *key;
            Object *value;
            std::int64_t hash;
        };

        explicit DictObject(TypeObject *type);

        bool contains(Object *key) const;
        Object *get(Object *key) const;
        void set(Object *key, Object *value);
        std::size_t size() const noexcept;
        const std::vector<Entry> &entries() const noexcept;

    private:
        struct SlotSearch
        {
            std::size_t slot;
            bool found;
        };

        static constexpr std::size_t empty_index = std::numeric_limits<std::size_t>::max();

        SlotSearch find_slot(Object *key, std::int64_t hash) const;
        void ensure_index_capacity();
        void rebuild_indices(std::size_t capacity);

        std::vector<Entry> entries_;
        std::vector<std::size_t> indices_;
    };

    class ExceptionObject final : public Object
    {
    public:
        ExceptionObject(TypeObject *type, std::string message);

        const std::string &message() const noexcept;

    private:
        std::string message_;
    };

    bool is_subclass(const TypeObject *derived, const TypeObject *base) noexcept;
    bool is_instance(const Object *object, const TypeObject *type) noexcept;
    bool is_true(const Object *object);

    std::int64_t object_hash(const Object *object);
    bool object_equal(const Object *left, const Object *right);
    std::string object_repr(const Object *object);
    std::string type_qualified_name(const TypeObject *type);

    TypeSlots make_object_slots();
    TypeSlots make_type_slots();
    TypeSlots make_none_slots();
    TypeSlots make_int_slots();
    TypeSlots make_bool_slots();
    TypeSlots make_str_slots();
    TypeSlots make_list_slots();
    TypeSlots make_tuple_slots();
    TypeSlots make_dict_slots();
    TypeSlots make_exception_slots();

    [[noreturn]] void raise_type_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_index_error(Runtime *runtime, std::string message);
}
