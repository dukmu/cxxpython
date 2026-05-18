#include "cxxpython/runtime.hpp"

namespace cxxpython
{
    TypeObject *Runtime::object_type() const noexcept
    {
        return object_type_;
    }

    TypeObject *Runtime::type_type() const noexcept
    {
        return type_type_;
    }

    TypeObject *Runtime::none_type() const noexcept
    {
        return none_type_;
    }

    TypeObject *Runtime::int_type() const noexcept
    {
        return int_type_;
    }

    TypeObject *Runtime::bool_type() const noexcept
    {
        return bool_type_;
    }

    TypeObject *Runtime::str_type() const noexcept
    {
        return str_type_;
    }

    TypeObject *Runtime::list_type() const noexcept
    {
        return list_type_;
    }

    TypeObject *Runtime::tuple_type() const noexcept
    {
        return tuple_type_;
    }

    TypeObject *Runtime::dict_type() const noexcept
    {
        return dict_type_;
    }

    TypeObject *Runtime::mappingproxy_type() const noexcept
    {
        return mappingproxy_type_;
    }

    TypeObject *Runtime::module_type() const noexcept
    {
        return module_type_;
    }

    TypeObject *Runtime::native_function_type() const noexcept
    {
        return native_function_type_;
    }

    TypeObject *Runtime::method_descriptor_type() const noexcept
    {
        return method_descriptor_type_;
    }

    TypeObject *Runtime::getset_descriptor_type() const noexcept
    {
        return getset_descriptor_type_;
    }

    TypeObject *Runtime::code_type() const noexcept
    {
        return code_type_;
    }

    TypeObject *Runtime::frame_type() const noexcept
    {
        return frame_type_;
    }

    TypeObject *Runtime::base_exception_type() const noexcept
    {
        return base_exception_type_;
    }

    TypeObject *Runtime::exception_type() const noexcept
    {
        return exception_type_;
    }

    TypeObject *Runtime::type_error_type() const noexcept
    {
        return type_error_type_;
    }

    TypeObject *Runtime::index_error_type() const noexcept
    {
        return index_error_type_;
    }

    TypeObject *Runtime::key_error_type() const noexcept
    {
        return key_error_type_;
    }

    TypeObject *Runtime::syntax_error_type() const noexcept
    {
        return syntax_error_type_;
    }

    TypeObject *Runtime::name_error_type() const noexcept
    {
        return name_error_type_;
    }

    TypeObject *Runtime::attribute_error_type() const noexcept
    {
        return attribute_error_type_;
    }

    NoneObject *Runtime::none() const noexcept
    {
        return none_;
    }

    BoolObject *Runtime::true_value() const noexcept
    {
        return true_;
    }

    BoolObject *Runtime::false_value() const noexcept
    {
        return false_;
    }

    ModuleObject *Runtime::builtins_module() const noexcept
    {
        return builtins_module_;
    }

    DictObject *Runtime::builtins_dict() const noexcept
    {
        return builtins_module_ ? builtins_module_->dict() : nullptr;
    }

    std::ostream &Runtime::stdout_stream() const noexcept
    {
        return *stdout_;
    }

    void Runtime::set_stdout(std::ostream &stream) noexcept
    {
        stdout_ = &stream;
    }
}
