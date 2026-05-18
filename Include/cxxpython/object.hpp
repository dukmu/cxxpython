#pragma once

#include <cstdint>
#include <functional>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cxxpython
{
    class Runtime;
    class Object;
    class TypeObject;
    class ListObject;
    class DictObject;
    class MappingProxyObject;
    class ExceptionObject;
    class MethodDescriptorObject;
    class GetSetDescriptorObject;

    using VisitChildren = std::function<void(Object *)>;
    using ObjectSpan = std::span<Object *const>;
    using NativeFunctionCallback = std::function<Object *(Runtime &, const std::vector<Object *> &)>;
    using MethodDescriptorCallback =
        std::function<Object *(Runtime &, Object *, ObjectSpan)>;
    using GetSetGetter = std::function<Object *(Runtime &, Object *, TypeObject *)>;
    using GetSetSetter = std::function<void(Runtime &, Object *, Object *)>;
    using ReprSlot = std::string (*)(const Object &);
    using HashSlot = std::int64_t (*)(const Object &);
    using EqualSlot = bool (*)(const Object &, const Object &);
    using TruthSlot = bool (*)(const Object &);
    using TraverseSlot = void (*)(const Object &, const VisitChildren &);
    using BinarySlot = Object *(*)(const Object &, const Object &);
    using CallSlot = Object *(*)(Object &, const std::vector<Object *> &);
    using DescrGetSlot = Object *(*)(Object &, Object *, TypeObject *);
    using DescrSetSlot = void (*)(Object &, Object *, Object *);

    struct TypeSlots
    {
        ReprSlot repr = nullptr;
        HashSlot hash = nullptr;
        EqualSlot equal = nullptr;
        TruthSlot truth = nullptr;
        TraverseSlot traverse = nullptr;
        BinarySlot binary_add = nullptr;
        CallSlot call = nullptr;
        DescrGetSlot descr_get = nullptr;
        DescrSetSlot descr_set = nullptr;
    };

    using TypeFlags = std::uint32_t;
    constexpr TypeFlags TypeFlagHeapType = 1U << 0;
    constexpr TypeFlags TypeFlagHasInstanceDict = 1U << 1;

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
        DictObject *instance_dict() const noexcept;
        void set_instance_dict(DictObject *dict) noexcept;

    private:
        Runtime *runtime_ = nullptr;
        TypeObject *type_;
        DictObject *instance_dict_ = nullptr;
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
        DictObject *dict = nullptr;
        TypeFlags flags = 0;
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

    class MappingProxyObject final : public Object
    {
    public:
        MappingProxyObject(TypeObject *type, DictObject *mapping);

        const DictObject *mapping() const noexcept;
        DictObject *referenced_mapping() const noexcept;

    private:
        DictObject *mapping_;
    };

    class ModuleObject final : public Object
    {
    public:
        ModuleObject(TypeObject *type, std::string name, std::string filename, DictObject *dict);

        const std::string &name() const noexcept;
        const std::string &filename() const noexcept;
        DictObject *dict() const noexcept;

    private:
        std::string name_;
        std::string filename_;
        DictObject *dict_;
    };

    class NativeFunctionObject final : public Object
    {
    public:
        NativeFunctionObject(TypeObject *type, std::string name, NativeFunctionCallback callback);

        const std::string &name() const noexcept;
        Object *call(Runtime &runtime, const std::vector<Object *> &args);

    private:
        std::string name_;
        NativeFunctionCallback callback_;
    };

    class MethodDescriptorObject final : public Object
    {
    public:
        MethodDescriptorObject(TypeObject *type,
                               std::string name,
                               TypeObject *owner,
                               MethodDescriptorCallback callback);

        const std::string &name() const noexcept;
        TypeObject *owner() const noexcept;
        Object *call_unbound(const std::vector<Object *> &args);
        Object *call_bound(Object *self, ObjectSpan args);

    private:
        std::string name_;
        TypeObject *owner_;
        MethodDescriptorCallback callback_;
    };

    class BoundMethodObject final : public Object
    {
    public:
        BoundMethodObject(TypeObject *type, MethodDescriptorObject *method, Object *self);

        MethodDescriptorObject *method() const noexcept;
        Object *self() const noexcept;

    private:
        MethodDescriptorObject *method_;
        Object *self_;
    };

    class GetSetDescriptorObject final : public Object
    {
    public:
        GetSetDescriptorObject(TypeObject *type,
                               std::string name,
                               TypeObject *owner,
                               GetSetGetter getter,
                               GetSetSetter setter = {});

        const std::string &name() const noexcept;
        TypeObject *owner() const noexcept;
        Object *get(Object *instance, TypeObject *owner);
        void set(Object *instance, Object *value);

    private:
        std::string name_;
        TypeObject *owner_;
        GetSetGetter getter_;
        GetSetSetter setter_;
    };

    class ExceptionObject final : public Object
    {
    public:
        ExceptionObject(TypeObject *type, TupleObject *args, std::string message);

        TupleObject *args() const noexcept;
        const std::string &message() const noexcept;

    private:
        TupleObject *args_;
        std::string message_;
    };

    bool is_subclass(const TypeObject *derived, const TypeObject *base) noexcept;
    bool is_instance(const Object *object, const TypeObject *type) noexcept;
    bool is_true(const Object *object);

    std::int64_t object_hash(const Object *object);
    bool object_equal(const Object *left, const Object *right);
    Object *object_binary_add(Object *left, Object *right);
    Object *object_call(Object *callable, const std::vector<Object *> &args);
    Object *object_get_attr(Object *object, StrObject *name);
    Object *object_get_attr(Object *object, const std::string &name);
    void object_set_attr(Object *object, StrObject *name, Object *value);
    void object_set_attr(Object *object, const std::string &name, Object *value);
    Object *type_lookup_attribute(TypeObject *type, StrObject *name);
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
    TypeSlots make_mappingproxy_slots();
    TypeSlots make_module_slots();
    TypeSlots make_native_function_slots();
    TypeSlots make_method_descriptor_slots();
    TypeSlots make_getset_descriptor_slots();
    TypeSlots make_exception_slots();
    TypeSlots make_code_slots();
    TypeSlots make_frame_slots();

    [[noreturn]] void raise_type_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_index_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_syntax_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_name_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_attribute_error(Runtime *runtime, std::string message);
}
