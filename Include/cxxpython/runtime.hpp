#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>
#include <filesystem>

#include "cxxpython/object.hpp"

namespace cxxpython
{
    struct Instruction;
    class CodeObject;
    class Frame;
    class ModuleObject;
    class NativeFunctionObject;
    class PythonFunctionObject;
    class PythonMethodObject;
    class VirtualMachine;
    template <class T>
    class Rooted;

    struct ClassConstructionRequest
    {
        std::string name;
        std::vector<TypeObject *> bases;
        DictObject *namespace_dict = nullptr;
        std::string module_fallback = "builtins";
        std::string qualname;
        TypeObject *explicit_metaclass = nullptr;
    };

    struct ClassBuildRequest
    {
        std::string name;
        CodeObject *body_code = nullptr;
        TupleObject *bases = nullptr;
        DictObject *keywords = nullptr;
        DictObject *globals = nullptr;
        DictObject *builtins = nullptr;
        std::string module_fallback = "builtins";
        std::string qualname;
    };

    enum class ModuleLoadKind : std::uint8_t
    {
        Builtin,
        Source,
        Extension,
    };

    struct ModuleLoadSpec
    {
        std::string name;
        std::string filename;
        std::string origin;
        std::string loader_name;
        std::string package;
        std::string package_dir;
        bool is_package = false;
        ModuleLoadKind kind = ModuleLoadKind::Source;
    };

    struct ImportRequest
    {
        std::string name;
        DictObject *globals = nullptr;
        DictObject *locals = nullptr;
        Object *fromlist = nullptr;
        std::int64_t level = 0;
    };

    class Runtime final
    {
    public:
        Runtime();
        ~Runtime();
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        TypeObject *object_type() const noexcept;
        TypeObject *type_type() const noexcept;
        TypeObject *none_type() const noexcept;
        TypeObject *not_implemented_type() const noexcept;
        TypeObject *int_type() const noexcept;
        TypeObject *bool_type() const noexcept;
        TypeObject *str_type() const noexcept;
        TypeObject *bytes_type() const noexcept;
        TypeObject *list_type() const noexcept;
        TypeObject *tuple_type() const noexcept;
        TypeObject *slice_type() const noexcept;
        TypeObject *dict_type() const noexcept;
        TypeObject *mappingproxy_type() const noexcept;
        TypeObject *sequence_iterator_type() const noexcept;
        TypeObject *dict_key_iterator_type() const noexcept;
        TypeObject *reverse_iterator_type() const noexcept;
        TypeObject *range_type() const noexcept;
        TypeObject *range_iterator_type() const noexcept;
        TypeObject *enumerate_type() const noexcept;
        TypeObject *zip_type() const noexcept;
        TypeObject *map_type() const noexcept;
        TypeObject *filter_type() const noexcept;
        TypeObject *text_io_type() const noexcept;
        TypeObject *module_type() const noexcept;
        TypeObject *native_function_type() const noexcept;
        TypeObject *function_type() const noexcept;
        TypeObject *cell_type() const noexcept;
        TypeObject *super_type() const noexcept;
        TypeObject *method_type() const noexcept;
        TypeObject *method_descriptor_type() const noexcept;
        TypeObject *slot_wrapper_type() const noexcept;
        TypeObject *bound_slot_wrapper_type() const noexcept;
        TypeObject *getset_descriptor_type() const noexcept;
        TypeObject *property_type() const noexcept;
        TypeObject *staticmethod_type() const noexcept;
        TypeObject *classmethod_type() const noexcept;
        TypeObject *code_type() const noexcept;
        TypeObject *frame_type() const noexcept;
        TypeObject *traceback_type() const noexcept;
        TypeObject *base_exception_type() const noexcept;
        TypeObject *exception_type() const noexcept;
        TypeObject *type_error_type() const noexcept;
        TypeObject *value_error_type() const noexcept;
        TypeObject *index_error_type() const noexcept;
        TypeObject *key_error_type() const noexcept;
        TypeObject *syntax_error_type() const noexcept;
        TypeObject *name_error_type() const noexcept;
        TypeObject *unbound_local_error_type() const noexcept;
        TypeObject *recursion_error_type() const noexcept;
        TypeObject *attribute_error_type() const noexcept;
        TypeObject *import_error_type() const noexcept;
        TypeObject *os_error_type() const noexcept;
        TypeObject *system_exit_type() const noexcept;
        TypeObject *keyboard_interrupt_type() const noexcept;
        TypeObject *stop_iteration_type() const noexcept;

        NoneObject *none() const noexcept;
        NotImplementedObject *not_implemented() const noexcept;
        BoolObject *true_value() const noexcept;
        BoolObject *false_value() const noexcept;
        ModuleObject *builtins_module() const noexcept;
        DictObject *builtins_dict() const noexcept;
        DictObject *modules_dict() const noexcept;
        const std::vector<std::string> &module_search_paths() const noexcept;
        void add_module_search_path(std::string path);
        const std::vector<std::string> &program_arguments() const noexcept;
        void set_program_arguments(std::vector<std::string> args);
        std::size_t collect_garbage();
        std::ostream &stdout_stream() const noexcept;
        void set_stdout(std::ostream &stream) noexcept;
        VirtualMachine *active_vm() const noexcept;
        void set_active_vm(VirtualMachine *vm) noexcept;
        std::string active_module_name(std::string fallback = "builtins");

        IntObject *new_int(std::int64_t value);
        StrObject *new_str(std::string value);
        BytesObject *new_bytes(std::string value);
        ListObject *new_list(std::vector<Object *> items);
        TupleObject *new_tuple(std::vector<Object *> items);
        SliceObject *new_slice(Object *start, Object *stop, Object *step);
        DictObject *new_dict();
        MappingProxyObject *new_mappingproxy(DictObject *mapping);
        SequenceIteratorObject *new_sequence_iterator(Object *sequence);
        DictKeyIteratorObject *new_dict_key_iterator(DictObject *dict);
        ReverseIteratorObject *new_reverse_iterator(Object *sequence);
        RangeObject *new_range(std::int64_t start, std::int64_t stop, std::int64_t step);
        RangeIteratorObject *new_range_iterator(std::int64_t current,
                                                std::int64_t step,
                                                std::size_t remaining);
        EnumerateObject *new_enumerate(Object *iterator, std::int64_t next_index);
        ZipObject *new_zip(std::vector<Object *> iterators);
        MapObject *new_map(Object *callable, std::vector<Object *> iterators);
        FilterObject *new_filter(Object *callable, Object *iterator);
        TextIOObject *new_text_io(std::string name,
                                  std::istream *input,
                                  std::ostream *output,
                                  bool owns_streams,
                                  std::unique_ptr<std::iostream> owned_stream = nullptr);
        TextIOObject *stdin_object() const noexcept;
        TextIOObject *stdout_object() const noexcept;
        TextIOObject *stderr_object() const noexcept;
        ModuleObject *new_module(std::string name, std::string filename);
        NativeFunctionObject *new_native_function(std::string name,
                                                  NativeFunctionCallback callback,
                                                  std::string doc = "",
                                                  std::string text_signature = "");
        PythonFunctionObject *new_function(std::string name,
                                           CodeObject *code,
                                           DictObject *globals,
                                           TupleObject *defaults = nullptr,
                                           DictObject *kwdefaults = nullptr,
                                           DictObject *annotations = nullptr,
                                           std::vector<CellObject *> closure = {});
        PythonFunctionObject *new_function(std::string name,
                                           CodeObject *code,
                                           DictObject *globals,
                                           std::vector<CellObject *> closure);
        Object *call_python_function(PythonFunctionObject *function, DictObject *locals);
        CellObject *new_cell(Object *value = nullptr);
        SuperObject *new_super(TypeObject *start_type, Object *object, TypeObject *object_type);
        SuperObject *new_zero_argument_super();
        PythonMethodObject *new_method(Object *callable, Object *self);
        MethodDescriptorObject *new_method_descriptor(std::string name,
                                                       TypeObject *owner,
                                                       MethodDescriptorCallback callback,
                                                       std::string doc = "",
                                                       std::string text_signature = "");
        BoundMethodObject *new_bound_method(MethodDescriptorObject *method, Object *self);
        SlotWrapperObject *new_slot_wrapper(std::string name,
                                            TypeObject *owner,
                                            SlotWrapperKind kind,
                                            BinaryOp binary_op = BinaryOp::Add,
                                            UnaryOp unary_op = UnaryOp::Positive,
                                            RichCompareOp rich_compare_op = RichCompareOp::Equal);
        BoundSlotWrapperObject *new_bound_slot_wrapper(SlotWrapperObject *wrapper, Object *self);
        GetSetDescriptorObject *new_getset_descriptor(std::string name,
                                                       TypeObject *owner,
                                                       GetSetGetter getter,
                                                       GetSetSetter setter = {});
        PropertyObject *new_property(Object *getter, Object *setter = nullptr, Object *deleter = nullptr, Object *doc = nullptr);
        StaticMethodObject *new_staticmethod(Object *callable);
        ClassMethodObject *new_classmethod(Object *callable);
        CodeObject *new_code(std::vector<Object *> constants,
                              std::vector<Instruction> instructions);
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::vector<Object *> constants,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::uint32_t arg_count,
                              std::vector<Object *> varnames,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::uint32_t arg_count,
                              std::uint32_t flags,
                              std::vector<Object *> varnames,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::uint32_t arg_count,
                              std::uint32_t flags,
                              std::vector<Object *> varnames,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Object *> cellvars,
                              std::vector<Object *> freevars,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::uint32_t arg_count,
                              std::uint32_t posonly_arg_count,
                              std::uint32_t kwonly_arg_count,
                              std::uint32_t flags,
                              std::vector<Object *> varnames,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Object *> cellvars,
                              std::vector<Object *> freevars,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::uint32_t arg_count,
                              std::uint32_t kwonly_arg_count,
                              std::uint32_t flags,
                              std::vector<Object *> varnames,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Object *> cellvars,
                              std::vector<Object *> freevars,
                              std::vector<Instruction> instructions,
                              std::vector<std::uint32_t> lines = {});
        Frame *new_frame(CodeObject *code,
                         DictObject *locals = nullptr,
                         DictObject *globals = nullptr,
                         DictObject *builtins = nullptr,
                         std::vector<CellObject *> closure = {});
        TracebackObject *new_traceback(TracebackObject *next,
                                       Frame *frame,
                                       std::size_t last_instruction_index,
                                       std::uint32_t line);
        TypeObject *new_heap_type(std::string name, TypeObject *base, TypeSlots slots = {});
        Object *new_plain_instance(TypeObject *type);
        DictObject *prepare_class_namespace(const ClassConstructionRequest &request);
        TypeObject *new_class(ClassConstructionRequest request);
        TypeObject *new_class(std::string name,
                              std::vector<TypeObject *> bases,
                              DictObject *namespace_dict,
                              std::string module_fallback = "builtins");
        TypeObject *new_class(std::string name,
                              TypeObject *base,
                              DictObject *namespace_dict,
                              std::string module_fallback = "builtins");
        TypeObject *build_class(const ClassBuildRequest &request);
        Object *import_name(const ImportRequest &request);
        ModuleObject *import_module(std::string name);
        Object *new_module_loader_object(const ModuleLoadSpec &spec);
        Object *new_module_spec_object(const ModuleLoadSpec &spec, Object *loader);
        void initialize_module_import_metadata(ModuleObject *module,
                                               const ModuleLoadSpec &spec,
                                               Object *loader,
                                               Object *module_spec);
        ExceptionObject *new_exception(TypeObject *type, std::string message);
        ExceptionObject *new_exception(TypeObject *type, std::vector<Object *> args);
        ExceptionObject *new_type_error(std::string message);
        ExceptionObject *new_value_error(std::string message);
        ExceptionObject *new_index_error(std::string message);
        ExceptionObject *new_key_error(Object *key);
        ExceptionObject *new_syntax_error(std::string message);
        ExceptionObject *new_name_error(std::string message);
        ExceptionObject *new_unbound_local_error(std::string message);
        ExceptionObject *new_recursion_error(std::string message);
        ExceptionObject *new_attribute_error(std::string message);
        ExceptionObject *new_import_error(std::string message);
        ExceptionObject *new_os_error(std::string message);
        ExceptionObject *new_keyboard_interrupt();
        ExceptionObject *new_stop_iteration();

        void add_root(Object *object);
        void remove_root(Object *object);
        template <class T>
        Rooted<T> root(T *object);
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
            raw->set_runtime(this);
            heap_.push_back(std::move(object));
            return raw;
        }

    private:
        TypeObject *allocate_type(std::string name);
        void initialize_type(TypeObject *type, TypeObject *base);
        void initialize_type(TypeObject *type, const std::vector<TypeObject *> &bases);
        void initialize_type_dict(TypeObject *type);
        void initialize_type_dicts();
        void initialize_type_descriptors();
        void initialize_type_methods();
        void initialize_slot_wrappers();
        TypeSlots inherit_slots(TypeObject *base, TypeSlots slots) const;
        TypeObject *best_base_for_layout(const std::vector<TypeObject *> &bases) const;
        void validate_base_layouts(const std::vector<TypeObject *> &bases);
        TypeObject *select_metaclass(TypeObject *explicit_metaclass,
                                      const std::vector<TypeObject *> &bases);
        void initialize_builtins();
        ModuleObject *import_stdlib_module(std::string_view name);
        ModuleLoadSpec *find_module_spec(std::string_view name,
                                         ModuleLoadSpec &storage,
                                         const std::vector<std::string> *package_paths = nullptr,
                                         std::string_view search_name = {});
        ModuleObject *load_module_from_spec(const ModuleLoadSpec &spec);
        ModuleObject *load_extension_module_from_spec(const ModuleLoadSpec &spec);
        TypeObject *importlib_type_for_loader(std::string_view loader_name);
        Object *new_importlib_object(std::string name,
                                     std::string module,
                                     std::initializer_list<std::pair<std::string_view, Object *>> attributes);

        friend class VirtualMachine;

        std::vector<std::unique_ptr<Object>> heap_;
        std::vector<Object *> roots_;

        TypeObject *object_type_ = nullptr;
        TypeObject *type_type_ = nullptr;
        TypeObject *none_type_ = nullptr;
        TypeObject *not_implemented_type_ = nullptr;
        TypeObject *int_type_ = nullptr;
        TypeObject *bool_type_ = nullptr;
        TypeObject *str_type_ = nullptr;
        TypeObject *bytes_type_ = nullptr;
        TypeObject *list_type_ = nullptr;
        TypeObject *tuple_type_ = nullptr;
        TypeObject *slice_type_ = nullptr;
        TypeObject *dict_type_ = nullptr;
        TypeObject *mappingproxy_type_ = nullptr;
        TypeObject *sequence_iterator_type_ = nullptr;
        TypeObject *dict_key_iterator_type_ = nullptr;
        TypeObject *reverse_iterator_type_ = nullptr;
        TypeObject *range_type_ = nullptr;
        TypeObject *range_iterator_type_ = nullptr;
        TypeObject *enumerate_type_ = nullptr;
        TypeObject *zip_type_ = nullptr;
        TypeObject *map_type_ = nullptr;
        TypeObject *filter_type_ = nullptr;
        TypeObject *text_io_type_ = nullptr;
        TypeObject *module_type_ = nullptr;
        TypeObject *native_function_type_ = nullptr;
        TypeObject *function_type_ = nullptr;
        TypeObject *cell_type_ = nullptr;
        TypeObject *super_type_ = nullptr;
        TypeObject *method_type_ = nullptr;
        TypeObject *method_descriptor_type_ = nullptr;
        TypeObject *slot_wrapper_type_ = nullptr;
        TypeObject *bound_slot_wrapper_type_ = nullptr;
        TypeObject *getset_descriptor_type_ = nullptr;
        TypeObject *property_type_ = nullptr;
        TypeObject *staticmethod_type_ = nullptr;
        TypeObject *classmethod_type_ = nullptr;
        TypeObject *code_type_ = nullptr;
        TypeObject *frame_type_ = nullptr;
        TypeObject *traceback_type_ = nullptr;
        TypeObject *base_exception_type_ = nullptr;
        TypeObject *exception_type_ = nullptr;
        TypeObject *type_error_type_ = nullptr;
        TypeObject *value_error_type_ = nullptr;
        TypeObject *index_error_type_ = nullptr;
        TypeObject *key_error_type_ = nullptr;
        TypeObject *syntax_error_type_ = nullptr;
        TypeObject *name_error_type_ = nullptr;
        TypeObject *unbound_local_error_type_ = nullptr;
        TypeObject *recursion_error_type_ = nullptr;
        TypeObject *attribute_error_type_ = nullptr;
        TypeObject *import_error_type_ = nullptr;
        TypeObject *os_error_type_ = nullptr;
        TypeObject *system_exit_type_ = nullptr;
        TypeObject *keyboard_interrupt_type_ = nullptr;
        TypeObject *stop_iteration_type_ = nullptr;

        NoneObject *none_ = nullptr;
        NotImplementedObject *not_implemented_ = nullptr;
        BoolObject *true_ = nullptr;
        BoolObject *false_ = nullptr;
        TextIOObject *stdin_object_ = nullptr;
        TextIOObject *stdout_object_ = nullptr;
        TextIOObject *stderr_object_ = nullptr;
        ModuleObject *builtins_module_ = nullptr;
        DictObject *modules_ = nullptr;
        TypeObject *builtin_importer_type_ = nullptr;
        TypeObject *source_file_loader_type_ = nullptr;
        TypeObject *extension_file_loader_type_ = nullptr;
        TypeObject *module_spec_type_ = nullptr;
        std::vector<void *> extension_handles_;
        std::vector<std::string> module_search_paths_;
        std::vector<std::string> program_arguments_;
        std::ostream *stdout_ = nullptr;
        VirtualMachine *active_vm_ = nullptr;
    };

    template <class T>
    class Rooted final
    {
    public:
        Rooted(Runtime &runtime, T *object)
            : runtime_(&runtime), object_(object)
        {
            static_assert(std::is_base_of_v<Object, T>);
            runtime_->add_root(object_);
        }

        ~Rooted()
        {
            reset();
        }

        Rooted(const Rooted &) = delete;
        Rooted &operator=(const Rooted &) = delete;

        Rooted(Rooted &&other) noexcept
            : runtime_(other.runtime_), object_(other.object_)
        {
            other.runtime_ = nullptr;
            other.object_ = nullptr;
        }

        Rooted &operator=(Rooted &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                runtime_ = other.runtime_;
                object_ = other.object_;
                other.runtime_ = nullptr;
                other.object_ = nullptr;
            }
            return *this;
        }

        T *get() const noexcept
        {
            return object_;
        }

        T &operator*() const noexcept
        {
            return *object_;
        }

        T *operator->() const noexcept
        {
            return object_;
        }

        explicit operator bool() const noexcept
        {
            return object_ != nullptr;
        }

        void reset() noexcept
        {
            if (runtime_ && object_)
                runtime_->remove_root(object_);
            runtime_ = nullptr;
            object_ = nullptr;
        }

    private:
        Runtime *runtime_;
        T *object_;
    };

    template <class T>
    Rooted<T> Runtime::root(T *object)
    {
        return Rooted<T>(*this, object);
    }
}
