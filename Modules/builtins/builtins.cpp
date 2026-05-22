#include "builtins.hpp"

#include <utility>

namespace cxxpython
{
    namespace
    {
        void install_builtin_constant(Runtime &runtime, DictObject *dict, const char *name, Object *value)
        {
            dict->set(runtime.new_str(name), value);
        }

        void install_builtin_function(Runtime &runtime,
                                      DictObject *dict,
                                      const char *name,
                                      NativeFunctionCallback callback)
        {
            dict->set(runtime.new_str(name), runtime.new_native_function(name, std::move(callback)));
        }
    }

    void Runtime::initialize_builtins()
    {
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str("builtins"));
        dict->set(new_str("__file__"), new_str(""));

        builtins_module_ = allocate<ModuleObject>(module_type_, "builtins", "", dict);
        dict->set(new_str("__builtins__"), builtins_module_);

        install_builtin_constant(*this, dict, "NotImplemented", not_implemented_);
        install_builtin_constant(*this, dict, "BaseException", base_exception_type_);
        install_builtin_constant(*this, dict, "Exception", exception_type_);
        install_builtin_constant(*this, dict, "TypeError", type_error_type_);
        install_builtin_constant(*this, dict, "ValueError", value_error_type_);
        install_builtin_constant(*this, dict, "IndexError", index_error_type_);
        install_builtin_constant(*this, dict, "KeyError", key_error_type_);
        install_builtin_constant(*this, dict, "SyntaxError", syntax_error_type_);
        install_builtin_constant(*this, dict, "NameError", name_error_type_);
        install_builtin_constant(*this, dict, "UnboundLocalError", unbound_local_error_type_);
        install_builtin_constant(*this, dict, "RecursionError", recursion_error_type_);
        install_builtin_constant(*this, dict, "AttributeError", attribute_error_type_);
        install_builtin_constant(*this, dict, "ImportError", import_error_type_);
        install_builtin_constant(*this, dict, "OSError", os_error_type_);
        install_builtin_constant(*this, dict, "StopIteration", stop_iteration_type_);
        install_builtin_constant(*this, dict, "None", none_);
        install_builtin_constant(*this, dict, "True", true_);
        install_builtin_constant(*this, dict, "False", false_);
        install_builtin_constant(*this, dict, "object", object_type_);
        install_builtin_constant(*this, dict, "type", type_type_);
        install_builtin_constant(*this, dict, "int", int_type_);
        install_builtin_constant(*this, dict, "bool", bool_type_);
        install_builtin_constant(*this, dict, "str", str_type_);
        install_builtin_constant(*this, dict, "list", list_type_);
        install_builtin_constant(*this, dict, "tuple", tuple_type_);
        install_builtin_constant(*this, dict, "slice", slice_type_);
        install_builtin_constant(*this, dict, "dict", dict_type_);
        install_builtin_constant(*this, dict, "range", range_type_);
        install_builtin_constant(*this, dict, "enumerate", enumerate_type_);
        install_builtin_constant(*this, dict, "zip", zip_type_);
        install_builtin_constant(*this, dict, "map", map_type_);
        install_builtin_constant(*this, dict, "filter", filter_type_);
        install_builtin_constant(*this, dict, "property", property_type_);
        install_builtin_constant(*this, dict, "staticmethod", staticmethod_type_);
        install_builtin_constant(*this, dict, "classmethod", classmethod_type_);
        install_builtin_constant(*this, dict, "super", super_type_);

        install_builtin_function(*this, dict, "print", builtin_print);
        install_builtin_function(*this, dict, "len", builtin_len);
        install_builtin_function(*this, dict, "iter", builtin_iter);
        install_builtin_function(*this, dict, "next", builtin_next);
        install_builtin_function(*this, dict, "repr", builtin_repr);
        install_builtin_function(*this, dict, "hash", builtin_hash);
        install_builtin_function(*this, dict, "all", builtin_all);
        install_builtin_function(*this, dict, "any", builtin_any);
        install_builtin_function(*this, dict, "sum", builtin_sum);
        install_builtin_function(*this, dict, "min", builtin_min);
        install_builtin_function(*this, dict, "max", builtin_max);
        install_builtin_function(*this, dict, "sorted", builtin_sorted);
        install_builtin_function(*this, dict, "isinstance", builtin_isinstance);
        install_builtin_function(*this, dict, "issubclass", builtin_issubclass);
        install_builtin_function(*this, dict, "callable", builtin_callable);
        install_builtin_function(*this, dict, "id", builtin_id);
        install_builtin_function(*this, dict, "globals", builtin_globals);
        install_builtin_function(*this, dict, "locals", builtin_locals);
        install_builtin_function(*this, dict, "dir", builtin_dir);
        install_builtin_function(*this, dict, "vars", builtin_vars);
        install_builtin_function(*this, dict, "format", builtin_format);
        install_builtin_function(*this, dict, "compile", builtin_compile);
        install_builtin_function(*this, dict, "eval", builtin_eval);
        install_builtin_function(*this, dict, "exec", builtin_exec);
        install_builtin_function(*this, dict, "__build_class__", builtin_build_class);
        install_builtin_function(*this, dict, "__import__", builtin_import);
    }
}
