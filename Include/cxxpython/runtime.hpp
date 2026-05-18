#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

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

    class Runtime final
    {
    public:
        Runtime();
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        TypeObject *object_type() const noexcept;
        TypeObject *type_type() const noexcept;
        TypeObject *none_type() const noexcept;
        TypeObject *not_implemented_type() const noexcept;
        TypeObject *int_type() const noexcept;
        TypeObject *bool_type() const noexcept;
        TypeObject *str_type() const noexcept;
        TypeObject *list_type() const noexcept;
        TypeObject *tuple_type() const noexcept;
        TypeObject *dict_type() const noexcept;
        TypeObject *mappingproxy_type() const noexcept;
        TypeObject *module_type() const noexcept;
        TypeObject *native_function_type() const noexcept;
        TypeObject *function_type() const noexcept;
        TypeObject *method_type() const noexcept;
        TypeObject *method_descriptor_type() const noexcept;
        TypeObject *getset_descriptor_type() const noexcept;
        TypeObject *code_type() const noexcept;
        TypeObject *frame_type() const noexcept;
        TypeObject *base_exception_type() const noexcept;
        TypeObject *exception_type() const noexcept;
        TypeObject *type_error_type() const noexcept;
        TypeObject *index_error_type() const noexcept;
        TypeObject *key_error_type() const noexcept;
        TypeObject *syntax_error_type() const noexcept;
        TypeObject *name_error_type() const noexcept;
        TypeObject *attribute_error_type() const noexcept;

        NoneObject *none() const noexcept;
        NotImplementedObject *not_implemented() const noexcept;
        BoolObject *true_value() const noexcept;
        BoolObject *false_value() const noexcept;
        ModuleObject *builtins_module() const noexcept;
        DictObject *builtins_dict() const noexcept;
        std::ostream &stdout_stream() const noexcept;
        void set_stdout(std::ostream &stream) noexcept;
        VirtualMachine *active_vm() const noexcept;
        void set_active_vm(VirtualMachine *vm) noexcept;

        IntObject *new_int(std::int64_t value);
        StrObject *new_str(std::string value);
        ListObject *new_list(std::vector<Object *> items);
        TupleObject *new_tuple(std::vector<Object *> items);
        DictObject *new_dict();
        MappingProxyObject *new_mappingproxy(DictObject *mapping);
        ModuleObject *new_module(std::string name, std::string filename);
        NativeFunctionObject *new_native_function(std::string name, NativeFunctionCallback callback);
        PythonFunctionObject *new_function(std::string name, CodeObject *code, DictObject *globals);
        PythonMethodObject *new_method(PythonFunctionObject *function, Object *self);
        MethodDescriptorObject *new_method_descriptor(std::string name,
                                                       TypeObject *owner,
                                                       MethodDescriptorCallback callback);
        BoundMethodObject *new_bound_method(MethodDescriptorObject *method, Object *self);
        GetSetDescriptorObject *new_getset_descriptor(std::string name,
                                                       TypeObject *owner,
                                                       GetSetGetter getter,
                                                       GetSetSetter setter = {});
        CodeObject *new_code(std::vector<Object *> constants,
                              std::vector<Instruction> instructions);
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::vector<Object *> constants,
                              std::vector<Instruction> instructions);
        CodeObject *new_code(std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions);
        CodeObject *new_code(std::string name,
                              std::string filename,
                              std::vector<Object *> constants,
                              std::vector<Object *> names,
                              std::vector<Instruction> instructions);
        Frame *new_frame(CodeObject *code,
                         DictObject *locals = nullptr,
                         DictObject *globals = nullptr,
                         DictObject *builtins = nullptr);
        TypeObject *new_heap_type(std::string name, TypeObject *base, TypeSlots slots = {});
        ExceptionObject *new_exception(TypeObject *type, std::string message);
        ExceptionObject *new_exception(TypeObject *type, std::vector<Object *> args);
        ExceptionObject *new_type_error(std::string message);
        ExceptionObject *new_index_error(std::string message);
        ExceptionObject *new_key_error(Object *key);
        ExceptionObject *new_syntax_error(std::string message);
        ExceptionObject *new_name_error(std::string message);
        ExceptionObject *new_attribute_error(std::string message);

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
            raw->set_runtime(this);
            heap_.push_back(std::move(object));
            return raw;
        }

    private:
        TypeObject *allocate_type(std::string name);
        void initialize_type(TypeObject *type, TypeObject *base);
        void initialize_type_dict(TypeObject *type);
        void initialize_type_dicts();
        void initialize_type_descriptors();
        void initialize_type_methods();
        TypeSlots inherit_slots(TypeObject *base, TypeSlots slots) const;
        void initialize_builtins();

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
        TypeObject *list_type_ = nullptr;
        TypeObject *tuple_type_ = nullptr;
        TypeObject *dict_type_ = nullptr;
        TypeObject *mappingproxy_type_ = nullptr;
        TypeObject *module_type_ = nullptr;
        TypeObject *native_function_type_ = nullptr;
        TypeObject *function_type_ = nullptr;
        TypeObject *method_type_ = nullptr;
        TypeObject *method_descriptor_type_ = nullptr;
        TypeObject *getset_descriptor_type_ = nullptr;
        TypeObject *code_type_ = nullptr;
        TypeObject *frame_type_ = nullptr;
        TypeObject *base_exception_type_ = nullptr;
        TypeObject *exception_type_ = nullptr;
        TypeObject *type_error_type_ = nullptr;
        TypeObject *index_error_type_ = nullptr;
        TypeObject *key_error_type_ = nullptr;
        TypeObject *syntax_error_type_ = nullptr;
        TypeObject *name_error_type_ = nullptr;
        TypeObject *attribute_error_type_ = nullptr;

        NoneObject *none_ = nullptr;
        NotImplementedObject *not_implemented_ = nullptr;
        BoolObject *true_ = nullptr;
        BoolObject *false_ = nullptr;
        ModuleObject *builtins_module_ = nullptr;
        std::ostream *stdout_ = nullptr;
        VirtualMachine *active_vm_ = nullptr;
    };
}
