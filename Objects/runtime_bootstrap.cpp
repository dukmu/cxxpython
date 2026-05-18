#include "cxxpython/runtime.hpp"

#include <iostream>

namespace cxxpython
{
    Runtime::Runtime()
    {
        stdout_ = &std::cout;

        object_type_ = allocate_type("object");
        type_type_ = allocate_type("type");
        none_type_ = allocate_type("NoneType");
        int_type_ = allocate_type("int");
        bool_type_ = allocate_type("bool");
        str_type_ = allocate_type("str");
        list_type_ = allocate_type("list");
        tuple_type_ = allocate_type("tuple");
        dict_type_ = allocate_type("dict");
        module_type_ = allocate_type("module");
        native_function_type_ = allocate_type("builtin_function_or_method");
        code_type_ = allocate_type("code");
        frame_type_ = allocate_type("frame");
        base_exception_type_ = allocate_type("BaseException");
        exception_type_ = allocate_type("Exception");
        type_error_type_ = allocate_type("TypeError");
        index_error_type_ = allocate_type("IndexError");
        syntax_error_type_ = allocate_type("SyntaxError");
        name_error_type_ = allocate_type("NameError");

        object_type_->slots = make_object_slots();
        type_type_->slots = make_type_slots();
        none_type_->slots = make_none_slots();
        int_type_->slots = make_int_slots();
        bool_type_->slots = make_bool_slots();
        str_type_->slots = make_str_slots();
        list_type_->slots = make_list_slots();
        tuple_type_->slots = make_tuple_slots();
        dict_type_->slots = make_dict_slots();
        module_type_->slots = make_module_slots();
        native_function_type_->slots = make_native_function_slots();
        code_type_->slots = make_code_slots();
        frame_type_->slots = make_frame_slots();
        base_exception_type_->slots = make_exception_slots();
        exception_type_->slots = make_exception_slots();
        type_error_type_->slots = make_exception_slots();
        index_error_type_->slots = make_exception_slots();
        syntax_error_type_->slots = make_exception_slots();
        name_error_type_->slots = make_exception_slots();

        object_type_->set_type(type_type_);
        type_type_->set_type(type_type_);
        none_type_->set_type(type_type_);
        int_type_->set_type(type_type_);
        bool_type_->set_type(type_type_);
        str_type_->set_type(type_type_);
        list_type_->set_type(type_type_);
        tuple_type_->set_type(type_type_);
        dict_type_->set_type(type_type_);
        module_type_->set_type(type_type_);
        native_function_type_->set_type(type_type_);
        code_type_->set_type(type_type_);
        frame_type_->set_type(type_type_);
        base_exception_type_->set_type(type_type_);
        exception_type_->set_type(type_type_);
        type_error_type_->set_type(type_type_);
        index_error_type_->set_type(type_type_);
        syntax_error_type_->set_type(type_type_);
        name_error_type_->set_type(type_type_);

        object_type_->base = nullptr;
        object_type_->bases = {};
        object_type_->mro = {object_type_};

        initialize_type(type_type_, object_type_);
        initialize_type(none_type_, object_type_);
        initialize_type(int_type_, object_type_);
        initialize_type(bool_type_, int_type_);
        initialize_type(str_type_, object_type_);
        initialize_type(list_type_, object_type_);
        initialize_type(tuple_type_, object_type_);
        initialize_type(dict_type_, object_type_);
        initialize_type(module_type_, object_type_);
        initialize_type(native_function_type_, object_type_);
        initialize_type(code_type_, object_type_);
        initialize_type(frame_type_, object_type_);
        initialize_type(base_exception_type_, object_type_);
        initialize_type(exception_type_, base_exception_type_);
        initialize_type(type_error_type_, exception_type_);
        initialize_type(index_error_type_, exception_type_);
        initialize_type(syntax_error_type_, exception_type_);
        initialize_type(name_error_type_, exception_type_);

        none_ = allocate<NoneObject>(none_type_);
        true_ = allocate<BoolObject>(bool_type_, true);
        false_ = allocate<BoolObject>(bool_type_, false);

        add_root(object_type_);
        add_root(type_type_);
        add_root(none_type_);
        add_root(int_type_);
        add_root(bool_type_);
        add_root(str_type_);
        add_root(list_type_);
        add_root(tuple_type_);
        add_root(dict_type_);
        add_root(module_type_);
        add_root(native_function_type_);
        add_root(code_type_);
        add_root(frame_type_);
        add_root(base_exception_type_);
        add_root(exception_type_);
        add_root(type_error_type_);
        add_root(index_error_type_);
        add_root(syntax_error_type_);
        add_root(name_error_type_);
        add_root(none_);
        add_root(true_);
        add_root(false_);

        initialize_builtins();
        add_root(builtins_module_);
    }
}
