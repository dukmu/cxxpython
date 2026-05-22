#pragma once

#include <cstdint>
#include <functional>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cxxpython
{
    class Runtime;
    class Object;
    class TypeObject;
    class CodeObject;
    class NotImplementedObject;
    class ListObject;
    class SliceObject;
    class DictObject;
    class MappingProxyObject;
    class SequenceIteratorObject;
    class DictKeyIteratorObject;
    class RangeObject;
    class RangeIteratorObject;
    class EnumerateObject;
    class ZipObject;
    class MapObject;
    class FilterObject;
    class ExceptionObject;
    class MethodDescriptorObject;
    class SlotWrapperObject;
    class BoundSlotWrapperObject;
    class GetSetDescriptorObject;
    class PropertyObject;
    class StaticMethodObject;
    class ClassMethodObject;
    class PythonFunctionObject;
    class CellObject;
    class SuperObject;
    class TextIOObject;

    using VisitChildren = std::function<void(Object *)>;
    using ObjectSpan = std::span<Object *const>;

    struct CallArguments
    {
        CallArguments(std::vector<Object *> positional_in, DictObject *keywords_in)
            : positional(std::move(positional_in)), keywords(keywords_in)
        {
        }

        std::vector<Object *> positional;
        DictObject *keywords = nullptr;
    };

    using NativeFunctionCallback = std::function<Object *(Runtime &, const CallArguments &)>;
    using MethodDescriptorCallback =
        std::function<Object *(Runtime &, Object *, ObjectSpan)>;
    using GetSetGetter = std::function<Object *(Runtime &, Object *, TypeObject *)>;
    using GetSetSetter = std::function<void(Runtime &, Object *, Object *)>;

    enum class RichCompareOp : std::uint8_t
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
    };

    enum class ComparisonOp : std::uint8_t
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Is,
        IsNot,
        In,
        NotIn,
    };

    enum class BinaryOp : std::uint8_t
    {
        Add,
        Subtract,
        Multiply,
        TrueDivide,
        FloorDivide,
        Modulo,
        Power,
    };

    enum class UnaryOp : std::uint8_t
    {
        Positive,
        Negative,
        Invert,
    };

    enum class SlotWrapperKind : std::uint8_t
    {
        Repr,
        Hash,
        Length,
        GetItem,
        SetItem,
        DeleteItem,
        Contains,
        Iter,
        Next,
        BinaryOp,
        UnaryOp,
        RichCompare,
    };

    using ReprSlot = std::string (*)(const Object &);
    using HashSlot = std::int64_t (*)(const Object &);
    using EqualSlot = bool (*)(const Object &, const Object &);
    using RichCompareSlot = Object *(*)(const Object &, const Object &, RichCompareOp);
    using TruthSlot = bool (*)(const Object &);
    using TraverseSlot = void (*)(const Object &, const VisitChildren &);
    using BinaryOpSlot = Object *(*)(const Object &, const Object &, BinaryOp);
    using UnaryOpSlot = Object *(*)(const Object &, UnaryOp);
    using CallSlot = Object *(*)(Object &, const CallArguments &);
    using LengthSlot = std::size_t (*)(const Object &);
    using GetItemSlot = Object *(*)(Object &, Object *);
    using SetItemSlot = void (*)(Object &, Object *, Object *);
    using DelItemSlot = void (*)(Object &, Object *);
    using ContainsSlot = bool (*)(Object &, Object *);
    using IterSlot = Object *(*)(Object &);
    using IterNextSlot = Object *(*)(Object &);
    using DescrGetSlot = Object *(*)(Object &, Object *, TypeObject *);
    using DescrSetSlot = void (*)(Object &, Object *, Object *);
    using NewSlot = Object *(*)(TypeObject &, const CallArguments &);
    using InitSlot = void (*)(Object &, const CallArguments &);

    struct TypeSlots
    {
        ReprSlot repr = nullptr;
        HashSlot hash = nullptr;
        EqualSlot equal = nullptr;
        RichCompareSlot rich_compare = nullptr;
        TruthSlot truth = nullptr;
        TraverseSlot traverse = nullptr;
        BinaryOpSlot binary_op = nullptr;
        BinaryOpSlot reflected_binary_op = nullptr;
        BinaryOpSlot inplace_binary_op = nullptr;
        UnaryOpSlot unary_op = nullptr;
        CallSlot call = nullptr;
        LengthSlot length = nullptr;
        GetItemSlot getitem = nullptr;
        SetItemSlot setitem = nullptr;
        DelItemSlot delitem = nullptr;
        ContainsSlot contains = nullptr;
        IterSlot iter = nullptr;
        IterNextSlot iternext = nullptr;
        DescrGetSlot descr_get = nullptr;
        DescrSetSlot descr_set = nullptr;
        NewSlot new_object = nullptr;
        InitSlot init = nullptr;
    };

    struct SliceRange
    {
        std::int64_t start;
        std::int64_t stop;
        std::int64_t step;
        std::size_t length;
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
        TypeObject *solid_base = nullptr;
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

    class NotImplementedObject final : public Object
    {
    public:
        explicit NotImplementedObject(TypeObject *type);
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
        void erase(std::int64_t index);
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

    class SliceObject final : public Object
    {
    public:
        SliceObject(TypeObject *type, Object *start, Object *stop, Object *step);

        Object *start() const noexcept;
        Object *stop() const noexcept;
        Object *step() const noexcept;

    private:
        Object *start_;
        Object *stop_;
        Object *step_;
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
        bool erase(Object *key);
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

    class SequenceIteratorObject final : public Object
    {
    public:
        SequenceIteratorObject(TypeObject *type, Object *sequence);

        Object *sequence() const noexcept;
        std::size_t index() const noexcept;
        std::size_t consume_index() noexcept;

    private:
        Object *sequence_;
        std::size_t index_ = 0;
    };

    class DictKeyIteratorObject final : public Object
    {
    public:
        DictKeyIteratorObject(TypeObject *type, DictObject *dict);

        DictObject *dict() const noexcept;
        std::size_t index() const noexcept;
        std::size_t consume_index() noexcept;

    private:
        DictObject *dict_;
        std::size_t index_ = 0;
    };

    class RangeObject final : public Object
    {
    public:
        RangeObject(TypeObject *type, std::int64_t start, std::int64_t stop, std::int64_t step);

        std::int64_t start() const noexcept;
        std::int64_t stop() const noexcept;
        std::int64_t step() const noexcept;
        std::size_t length() const noexcept;
        std::int64_t value_at(std::size_t index) const;

    private:
        static std::size_t compute_length(std::int64_t start, std::int64_t stop, std::int64_t step);

        std::int64_t start_;
        std::int64_t stop_;
        std::int64_t step_;
        std::size_t length_;
    };

    class RangeIteratorObject final : public Object
    {
    public:
        RangeIteratorObject(TypeObject *type,
                            std::int64_t current,
                            std::int64_t step,
                            std::size_t remaining);

        std::int64_t consume_value();
        std::size_t remaining() const noexcept;

    private:
        std::int64_t current_;
        std::int64_t step_;
        std::size_t remaining_;
    };

    class EnumerateObject final : public Object
    {
    public:
        EnumerateObject(TypeObject *type, Object *iterator, std::int64_t next_index);

        Object *iterator() const noexcept;
        std::int64_t consume_index() noexcept;

    private:
        Object *iterator_;
        std::int64_t next_index_;
    };

    class ZipObject final : public Object
    {
    public:
        ZipObject(TypeObject *type, std::vector<Object *> iterators);

        const std::vector<Object *> &iterators() const noexcept;

    private:
        std::vector<Object *> iterators_;
    };

    class MapObject final : public Object
    {
    public:
        MapObject(TypeObject *type, Object *callable, std::vector<Object *> iterators);

        Object *callable() const noexcept;
        const std::vector<Object *> &iterators() const noexcept;

    private:
        Object *callable_;
        std::vector<Object *> iterators_;
    };

    class FilterObject final : public Object
    {
    public:
        FilterObject(TypeObject *type, Object *callable, Object *iterator);

        Object *callable() const noexcept;
        Object *iterator() const noexcept;

    private:
        Object *callable_;
        Object *iterator_;
    };

    class TextIOObject final : public Object
    {
    public:
        enum class Capability : std::uint8_t
        {
            Readable = 1U << 0,
            Writable = 1U << 1,
        };

        TextIOObject(TypeObject *type,
                     std::string name,
                     std::istream *input,
                     std::ostream *output,
                     bool owns_streams,
                     std::unique_ptr<std::iostream> owned_stream = nullptr);
        ~TextIOObject() override;

        const std::string &name() const noexcept;
        bool readable() const noexcept;
        bool writable() const noexcept;
        bool closed() const noexcept;
        std::string read_all();
        std::string readline();
        void write(std::string_view text);
        void flush();
        void close();

    private:
        std::string name_;
        std::istream *input_ = nullptr;
        std::ostream *output_ = nullptr;
        bool owns_streams_ = false;
        bool closed_ = false;
        std::unique_ptr<std::iostream> owned_stream_;
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
        NativeFunctionObject(TypeObject *type,
                             std::string name,
                             NativeFunctionCallback callback,
                             std::string doc = "",
                             std::string text_signature = "");

        const std::string &name() const noexcept;
        const std::string &doc() const noexcept;
        const std::string &text_signature() const noexcept;
        Object *call(Runtime &runtime, const CallArguments &args);

    private:
        std::string name_;
        std::string doc_;
        std::string text_signature_;
        NativeFunctionCallback callback_;
    };

    class PythonFunctionObject final : public Object
    {
    public:
        PythonFunctionObject(TypeObject *type,
                             std::string name,
                             CodeObject *code,
                             DictObject *globals,
                             std::vector<CellObject *> closure = {});

        const std::string &name() const noexcept;
        Object *doc() const noexcept;
        std::string text_signature() const;
        CodeObject *code() const noexcept;
        DictObject *globals() const noexcept;
        const std::vector<CellObject *> &closure() const noexcept;
        Object *call(const CallArguments &args);

    private:
        std::string name_;
        CodeObject *code_;
        DictObject *globals_;
        Object *doc_ = nullptr;
        std::vector<CellObject *> closure_;
    };

    class CellObject final : public Object
    {
    public:
        CellObject(TypeObject *type, Object *value);

        Object *value() const noexcept;
        void set_value(Object *value) noexcept;
        bool is_empty() const noexcept;

    private:
        Object *value_ = nullptr;
    };

    class SuperObject final : public Object
    {
    public:
        SuperObject(TypeObject *type, TypeObject *start_type, Object *object, TypeObject *object_type);

        TypeObject *start_type() const noexcept;
        Object *object() const noexcept;
        TypeObject *object_type() const noexcept;

    private:
        TypeObject *start_type_;
        Object *object_;
        TypeObject *object_type_;
    };

    class MethodDescriptorObject final : public Object
    {
    public:
        MethodDescriptorObject(TypeObject *type,
                               std::string name,
                               TypeObject *owner,
                               MethodDescriptorCallback callback,
                               std::string doc = "",
                               std::string text_signature = "");

        const std::string &name() const noexcept;
        const std::string &doc() const noexcept;
        const std::string &text_signature() const noexcept;
        TypeObject *owner() const noexcept;
        Object *call_unbound(const CallArguments &args);
        Object *call_bound(Object *self, const CallArguments &args);
        Object *call_bound(Object *self, ObjectSpan args);

    private:
        std::string name_;
        std::string doc_;
        std::string text_signature_;
        TypeObject *owner_;
        MethodDescriptorCallback callback_;
    };

    class PythonMethodObject final : public Object
    {
    public:
        PythonMethodObject(TypeObject *type, Object *callable, Object *self);

        PythonFunctionObject *function() const noexcept;
        Object *callable() const noexcept;
        Object *self() const noexcept;

    private:
        Object *callable_;
        Object *self_;
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

    class SlotWrapperObject final : public Object
    {
    public:
        SlotWrapperObject(TypeObject *type,
                          std::string name,
                          TypeObject *owner,
                          SlotWrapperKind kind,
                          BinaryOp binary_op = BinaryOp::Add,
                          UnaryOp unary_op = UnaryOp::Positive,
                          RichCompareOp rich_compare_op = RichCompareOp::Equal);

        const std::string &name() const noexcept;
        TypeObject *owner() const noexcept;
        SlotWrapperKind kind() const noexcept;
        BinaryOp binary_op() const noexcept;
        UnaryOp unary_op() const noexcept;
        RichCompareOp rich_compare_op() const noexcept;
        Object *call_unbound(const CallArguments &args);
        Object *call_bound(Object *self, const CallArguments &args);
        Object *call_bound(Object *self, ObjectSpan args);

    private:
        std::string name_;
        TypeObject *owner_;
        SlotWrapperKind kind_;
        BinaryOp binary_op_;
        UnaryOp unary_op_;
        RichCompareOp rich_compare_op_;
    };

    class BoundSlotWrapperObject final : public Object
    {
    public:
        BoundSlotWrapperObject(TypeObject *type, SlotWrapperObject *wrapper, Object *self);

        SlotWrapperObject *wrapper() const noexcept;
        Object *self() const noexcept;

    private:
        SlotWrapperObject *wrapper_;
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

    class PropertyObject final : public Object
    {
    public:
        PropertyObject(TypeObject *type, Object *getter, Object *setter, Object *deleter, Object *doc);

        Object *getter() const noexcept;
        Object *setter() const noexcept;
        Object *deleter() const noexcept;
        Object *doc() const noexcept;

    private:
        Object *getter_;
        Object *setter_;
        Object *deleter_;
        Object *doc_;
    };

    class StaticMethodObject final : public Object
    {
    public:
        StaticMethodObject(TypeObject *type, Object *callable);

        Object *callable() const noexcept;

    private:
        Object *callable_;
    };

    class ClassMethodObject final : public Object
    {
    public:
        ClassMethodObject(TypeObject *type, Object *callable);

        Object *callable() const noexcept;

    private:
        Object *callable_;
    };

    class ExceptionObject final : public Object
    {
    public:
        struct TracebackEntry
        {
            std::string filename;
            std::uint32_t line = 0;
            std::string name;
        };

        ExceptionObject(TypeObject *type, TupleObject *args, std::string message);

        TupleObject *args() const noexcept;
        const std::string &message() const noexcept;
        bool has_source_location() const noexcept;
        const std::string &source_filename() const noexcept;
        const std::string &source_name() const noexcept;
        std::uint32_t source_line() const noexcept;
        void set_source_location(std::string filename, std::uint32_t line, std::string name);
        void add_traceback_entry(std::string filename, std::uint32_t line, std::string name);
        const std::vector<TracebackEntry> &traceback_entries() const noexcept;

    private:
        TupleObject *args_;
        std::string message_;
        std::string source_filename_;
        std::string source_name_;
        std::uint32_t source_line_ = 0;
        std::vector<TracebackEntry> traceback_;
    };

    bool is_subclass(const TypeObject *derived, const TypeObject *base) noexcept;
    bool is_instance(const Object *object, const TypeObject *type) noexcept;
    bool is_true(const Object *object);

    std::int64_t object_hash(const Object *object);
    bool object_equal(const Object *left, const Object *right);
    Object *object_rich_compare(Object *left, Object *right, RichCompareOp op);
    Object *object_binary_op(Object *left, Object *right, BinaryOp op);
    Object *object_inplace_binary_op(Object *left, Object *right, BinaryOp op);
    Object *object_unary_op(Object *operand, UnaryOp op);
    Object *object_call(Object *callable, const CallArguments &args);
    Object *object_call(Object *callable, const std::vector<Object *> &args);
    std::size_t object_length(Object *object);
    Object *object_get_item(Object *object, Object *key);
    void object_set_item(Object *object, Object *key, Object *value);
    void object_delete_item(Object *object, Object *key);
    bool object_contains(Object *object, Object *key);
    Object *object_iter(Object *object);
    Object *object_next(Object *iterator);
    SliceRange normalize_slice(Runtime &runtime, const SliceObject &slice, std::int64_t sequence_length);
    Object *super_get_attr_default(SuperObject *super, StrObject *name);
    Object *object_get_attr_default(Object *object, StrObject *name);
    Object *object_get_attr(Object *object, StrObject *name);
    Object *object_get_attr(Object *object, const std::string &name);
    void object_set_attr_default(Object *object, StrObject *name, Object *value);
    void object_set_attr(Object *object, StrObject *name, Object *value);
    void object_set_attr(Object *object, const std::string &name, Object *value);
    void object_delete_attr_default(Object *object, StrObject *name);
    void object_delete_attr(Object *object, StrObject *name);
    void object_delete_attr(Object *object, const std::string &name);
    Object *type_lookup_attribute(TypeObject *type, StrObject *name);
    void refresh_heap_type_slots_from_dict(TypeObject *type);
    std::string object_str(const Object *object);
    std::string object_repr(const Object *object);
    std::string type_qualified_name(const TypeObject *type);

    TypeSlots make_object_slots();
    TypeSlots make_type_slots();
    TypeSlots make_none_slots();
    TypeSlots make_not_implemented_slots();
    TypeSlots make_int_slots();
    TypeSlots make_bool_slots();
    TypeSlots make_str_slots();
    TypeSlots make_list_slots();
    TypeSlots make_tuple_slots();
    TypeSlots make_slice_slots();
    TypeSlots make_dict_slots();
    TypeSlots make_mappingproxy_slots();
    TypeSlots make_sequence_iterator_slots();
    TypeSlots make_dict_key_iterator_slots();
    TypeSlots make_range_slots();
    TypeSlots make_range_iterator_slots();
    TypeSlots make_enumerate_slots();
    TypeSlots make_zip_slots();
    TypeSlots make_map_slots();
    TypeSlots make_filter_slots();
    TypeSlots make_text_io_slots();
    TypeSlots make_module_slots();
    TypeSlots make_native_function_slots();
    TypeSlots make_function_slots();
    TypeSlots make_method_slots();
    TypeSlots make_method_descriptor_slots();
    TypeSlots make_slot_wrapper_slots();
    TypeSlots make_bound_slot_wrapper_slots();
    TypeSlots make_getset_descriptor_slots();
    TypeSlots make_property_slots();
    TypeSlots make_staticmethod_slots();
    TypeSlots make_classmethod_slots();
    TypeSlots make_exception_slots();
    TypeSlots make_code_slots();
    TypeSlots make_frame_slots();
    TypeSlots make_cell_slots();
    TypeSlots make_super_slots();
    void initialize_text_io_methods(Runtime &runtime, TypeObject *type);

    [[noreturn]] void raise_type_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_value_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_index_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_key_error(Runtime *runtime, Object *key);
    [[noreturn]] void raise_syntax_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_name_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_unbound_local_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_recursion_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_attribute_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_import_error(Runtime *runtime, std::string message);
    [[noreturn]] void raise_stop_iteration(Runtime *runtime);
}
