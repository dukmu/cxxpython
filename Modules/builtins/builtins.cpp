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
                                      NativeFunctionCallback callback,
                                      const char *doc = "",
                                      const char *text_signature = "")
        {
            dict->set(runtime.new_str(name),
                      runtime.new_native_function(name,
                                                  std::move(callback),
                                                  doc,
                                                  text_signature));
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

        install_builtin_function(*this,
                                 dict,
                                 "print",
                                 builtin_print,
                                 "Print objects to the text stream, separated by sep and followed by end.",
                                 "($module, /, *args, sep=' ', end='\\n')");
        install_builtin_function(*this,
                                 dict,
                                 "len",
                                 builtin_len,
                                 "Return the number of items in a container.",
                                 "($module, object, /)");
        install_builtin_function(*this, dict, "iter", builtin_iter, "Return an iterator.", "($module, object, /)");
        install_builtin_function(*this, dict, "next", builtin_next, "Return the next item from an iterator.", "($module, iterator, /)");
        install_builtin_function(*this, dict, "repr", builtin_repr, "Return the canonical string representation of an object.", "($module, object, /)");
        install_builtin_function(*this, dict, "hash", builtin_hash, "Return the hash value for an object.", "($module, object, /)");
        install_builtin_function(*this, dict, "all", builtin_all, "Return True if bool(x) is True for all values x in the iterable.", "($module, iterable, /)");
        install_builtin_function(*this, dict, "any", builtin_any, "Return True if bool(x) is True for any value x in the iterable.", "($module, iterable, /)");
        install_builtin_function(*this, dict, "sum", builtin_sum, "Return the sum of a start value and an iterable of numbers.", "($module, iterable, start=0, /)");
        install_builtin_function(*this, dict, "min", builtin_min, "Return the smallest item.", "($module, iterable, /)");
        install_builtin_function(*this, dict, "max", builtin_max, "Return the largest item.", "($module, iterable, /)");
        install_builtin_function(*this, dict, "sorted", builtin_sorted, "Return a new sorted list from the items in iterable.", "($module, iterable, /)");
        install_builtin_function(*this, dict, "isinstance", builtin_isinstance, "Return whether an object is an instance of a class or tuple of classes.", "($module, object, classinfo, /)");
        install_builtin_function(*this, dict, "issubclass", builtin_issubclass, "Return whether cls is a subclass of classinfo.", "($module, cls, classinfo, /)");
        install_builtin_function(*this, dict, "callable", builtin_callable, "Return whether the object appears callable.", "($module, object, /)");
        install_builtin_function(*this, dict, "id", builtin_id, "Return the identity of an object.", "($module, object, /)");
        install_builtin_function(*this, dict, "globals", builtin_globals, "Return the current global namespace.", "($module, /)");
        install_builtin_function(*this, dict, "locals", builtin_locals, "Return the current local namespace.", "($module, /)");
        install_builtin_function(*this, dict, "dir", builtin_dir, "Return a sorted list of names in the current scope or object.", "($module, object=None, /)");
        install_builtin_function(*this, dict, "vars", builtin_vars, "Return the __dict__ attribute for a module, class, instance, or current locals.", "($module, object=None, /)");
        install_builtin_function(*this, dict, "format", builtin_format, "Return value.__format__(format_spec).", "($module, value, format_spec='', /)");
        install_builtin_function(*this, dict, "compile", builtin_compile, "Compile source into a code object.", "($module, source, filename, mode, /)");
        install_builtin_function(*this, dict, "eval", builtin_eval, "Evaluate source in the given global and local namespaces.", "($module, source, globals=None, locals=None, /)");
        install_builtin_function(*this, dict, "exec", builtin_exec, "Execute source in the given global and local namespaces.", "($module, source, globals=None, locals=None, /)");
        install_builtin_function(*this, dict, "__build_class__", builtin_build_class, "Internal helper used by class statements.", "($module, func, name, /, *bases, **kwds)");
        install_builtin_function(*this, dict, "__import__", builtin_import, "Import a module.", "($module, name, globals=None, locals=None, fromlist=(), level=0, /)");
    }
}
