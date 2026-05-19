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

    TypeObject *Runtime::not_implemented_type() const noexcept
    {
        return not_implemented_type_;
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

    TypeObject *Runtime::slice_type() const noexcept
    {
        return slice_type_;
    }

    TypeObject *Runtime::dict_type() const noexcept
    {
        return dict_type_;
    }

    TypeObject *Runtime::mappingproxy_type() const noexcept
    {
        return mappingproxy_type_;
    }

    TypeObject *Runtime::sequence_iterator_type() const noexcept
    {
        return sequence_iterator_type_;
    }

    TypeObject *Runtime::dict_key_iterator_type() const noexcept
    {
        return dict_key_iterator_type_;
    }

    TypeObject *Runtime::range_type() const noexcept
    {
        return range_type_;
    }

    TypeObject *Runtime::range_iterator_type() const noexcept
    {
        return range_iterator_type_;
    }

    TypeObject *Runtime::enumerate_type() const noexcept
    {
        return enumerate_type_;
    }

    TypeObject *Runtime::zip_type() const noexcept
    {
        return zip_type_;
    }

    TypeObject *Runtime::map_type() const noexcept
    {
        return map_type_;
    }

    TypeObject *Runtime::filter_type() const noexcept
    {
        return filter_type_;
    }

    TypeObject *Runtime::module_type() const noexcept
    {
        return module_type_;
    }

    TypeObject *Runtime::native_function_type() const noexcept
    {
        return native_function_type_;
    }

    TypeObject *Runtime::function_type() const noexcept
    {
        return function_type_;
    }

    TypeObject *Runtime::cell_type() const noexcept
    {
        return cell_type_;
    }

    TypeObject *Runtime::super_type() const noexcept
    {
        return super_type_;
    }

    TypeObject *Runtime::method_type() const noexcept
    {
        return method_type_;
    }

    TypeObject *Runtime::method_descriptor_type() const noexcept
    {
        return method_descriptor_type_;
    }

    TypeObject *Runtime::slot_wrapper_type() const noexcept
    {
        return slot_wrapper_type_;
    }

    TypeObject *Runtime::bound_slot_wrapper_type() const noexcept
    {
        return bound_slot_wrapper_type_;
    }

    TypeObject *Runtime::getset_descriptor_type() const noexcept
    {
        return getset_descriptor_type_;
    }

    TypeObject *Runtime::property_type() const noexcept
    {
        return property_type_;
    }

    TypeObject *Runtime::staticmethod_type() const noexcept
    {
        return staticmethod_type_;
    }

    TypeObject *Runtime::classmethod_type() const noexcept
    {
        return classmethod_type_;
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

    TypeObject *Runtime::value_error_type() const noexcept
    {
        return value_error_type_;
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

    TypeObject *Runtime::unbound_local_error_type() const noexcept
    {
        return unbound_local_error_type_;
    }

    TypeObject *Runtime::attribute_error_type() const noexcept
    {
        return attribute_error_type_;
    }

    TypeObject *Runtime::import_error_type() const noexcept
    {
        return import_error_type_;
    }

    TypeObject *Runtime::stop_iteration_type() const noexcept
    {
        return stop_iteration_type_;
    }

    NoneObject *Runtime::none() const noexcept
    {
        return none_;
    }

    NotImplementedObject *Runtime::not_implemented() const noexcept
    {
        return not_implemented_;
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

    DictObject *Runtime::modules_dict() const noexcept
    {
        return modules_;
    }

    void Runtime::add_module_search_path(std::string path)
    {
        module_search_paths_.insert(module_search_paths_.begin(), std::move(path));
    }

    std::ostream &Runtime::stdout_stream() const noexcept
    {
        return *stdout_;
    }

    void Runtime::set_stdout(std::ostream &stream) noexcept
    {
        stdout_ = &stream;
    }

    VirtualMachine *Runtime::active_vm() const noexcept
    {
        return active_vm_;
    }

    void Runtime::set_active_vm(VirtualMachine *vm) noexcept
    {
        active_vm_ = vm;
    }
}
