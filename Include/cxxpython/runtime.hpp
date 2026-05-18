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
    struct Instruction;
    class CodeObject;
    class Frame;
    class ModuleObject;

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
        TypeObject *list_type() const noexcept;
        TypeObject *tuple_type() const noexcept;
        TypeObject *dict_type() const noexcept;
        TypeObject *module_type() const noexcept;
        TypeObject *code_type() const noexcept;
        TypeObject *frame_type() const noexcept;
        TypeObject *base_exception_type() const noexcept;
        TypeObject *exception_type() const noexcept;
        TypeObject *type_error_type() const noexcept;
        TypeObject *index_error_type() const noexcept;
        TypeObject *syntax_error_type() const noexcept;
        TypeObject *name_error_type() const noexcept;

        NoneObject *none() const noexcept;
        BoolObject *true_value() const noexcept;
        BoolObject *false_value() const noexcept;

        IntObject *new_int(std::int64_t value);
        StrObject *new_str(std::string value);
        ListObject *new_list(std::vector<Object *> items);
        TupleObject *new_tuple(std::vector<Object *> items);
        DictObject *new_dict();
        ModuleObject *new_module(std::string name, std::string filename);
        CodeObject *new_code(std::vector<Object *> constants,
                              std::vector<Instruction> instructions);
        CodeObject *new_code(std::vector<Object *> constants,
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
        ExceptionObject *new_syntax_error(std::string message);
        ExceptionObject *new_name_error(std::string message);

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
        TypeSlots inherit_slots(TypeObject *base, TypeSlots slots) const;

        std::vector<std::unique_ptr<Object>> heap_;
        std::vector<Object *> roots_;

        TypeObject *object_type_ = nullptr;
        TypeObject *type_type_ = nullptr;
        TypeObject *none_type_ = nullptr;
        TypeObject *int_type_ = nullptr;
        TypeObject *bool_type_ = nullptr;
        TypeObject *str_type_ = nullptr;
        TypeObject *list_type_ = nullptr;
        TypeObject *tuple_type_ = nullptr;
        TypeObject *dict_type_ = nullptr;
        TypeObject *module_type_ = nullptr;
        TypeObject *code_type_ = nullptr;
        TypeObject *frame_type_ = nullptr;
        TypeObject *base_exception_type_ = nullptr;
        TypeObject *exception_type_ = nullptr;
        TypeObject *type_error_type_ = nullptr;
        TypeObject *index_error_type_ = nullptr;
        TypeObject *syntax_error_type_ = nullptr;
        TypeObject *name_error_type_ = nullptr;

        NoneObject *none_ = nullptr;
        BoolObject *true_ = nullptr;
        BoolObject *false_ = nullptr;
    };
}
