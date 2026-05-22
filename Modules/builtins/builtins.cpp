#include "builtins.hpp"

#include "../module_registry.hpp"

namespace cxxpython
{
    namespace
    {
        void install_builtin_constant(Runtime &runtime, DictObject *dict, const char *name, Object *value)
        {
            dict->set(runtime.new_str(name), value);
        }

        constexpr NativeModuleFunctionDef builtin_methods[] = {
            {
                "print",
                builtin_print,
                "Print objects to the text stream, separated by sep and followed by end.",
                "($module, /, *args, sep=' ', end='\\n')",
            },
            {
                "len",
                builtin_len,
                "Return the number of items in a container.",
                "($module, object, /)",
            },
            {
                "iter",
                builtin_iter,
                "Return an iterator.",
                "($module, object, /)",
            },
            {
                "next",
                builtin_next,
                "Return the next item from an iterator.",
                "($module, iterator, /)",
            },
            {
                "repr",
                builtin_repr,
                "Return the canonical string representation of an object.",
                "($module, object, /)",
            },
            {
                "hash",
                builtin_hash,
                "Return the hash value for an object.",
                "($module, object, /)",
            },
            {
                "all",
                builtin_all,
                "Return True if bool(x) is True for all values x in the iterable.",
                "($module, iterable, /)",
            },
            {
                "any",
                builtin_any,
                "Return True if bool(x) is True for any value x in the iterable.",
                "($module, iterable, /)",
            },
            {
                "sum",
                builtin_sum,
                "Return the sum of a start value and an iterable of numbers.",
                "($module, iterable, start=0, /)",
            },
            {
                "min",
                builtin_min,
                "Return the smallest item.",
                "($module, iterable, /)",
            },
            {
                "max",
                builtin_max,
                "Return the largest item.",
                "($module, iterable, /)",
            },
            {
                "sorted",
                builtin_sorted,
                "Return a new sorted list from the items in iterable.",
                "($module, iterable, /)",
            },
            {
                "isinstance",
                builtin_isinstance,
                "Return whether an object is an instance of a class or tuple of classes.",
                "($module, object, classinfo, /)",
            },
            {
                "issubclass",
                builtin_issubclass,
                "Return whether cls is a subclass of classinfo.",
                "($module, cls, classinfo, /)",
            },
            {
                "callable",
                builtin_callable,
                "Return whether the object appears callable.",
                "($module, object, /)",
            },
            {
                "id",
                builtin_id,
                "Return the identity of an object.",
                "($module, object, /)",
            },
            {
                "globals",
                builtin_globals,
                "Return the current global namespace.",
                "($module, /)",
            },
            {
                "locals",
                builtin_locals,
                "Return the current local namespace.",
                "($module, /)",
            },
            {
                "dir",
                builtin_dir,
                "Return a sorted list of names in the current scope or object.",
                "($module, object=None, /)",
            },
            {
                "vars",
                builtin_vars,
                "Return the __dict__ attribute for a module, class, instance, or current locals.",
                "($module, object=None, /)",
            },
            {
                "getattr",
                builtin_getattr,
                "Return the named attribute from an object, or a default value if provided.",
                "($module, object, name, default=<unrepresentable>, /)",
            },
            {
                "setattr",
                builtin_setattr,
                "Set the named attribute on an object.",
                "($module, object, name, value, /)",
            },
            {
                "delattr",
                builtin_delattr,
                "Delete the named attribute from an object.",
                "($module, object, name, /)",
            },
            {
                "hasattr",
                builtin_hasattr,
                "Return whether an object has the named attribute.",
                "($module, object, name, /)",
            },
            {
                "help",
                builtin_help,
                "Print a brief help summary for an object.",
                "($module, object=None, /)",
            },
            {
                "format",
                builtin_format,
                "Return value.__format__(format_spec).",
                "($module, value, format_spec='', /)",
            },
            {
                "compile",
                builtin_compile,
                "Compile source into a code object.",
                "($module, source, filename, mode, /)",
            },
            {
                "eval",
                builtin_eval,
                "Evaluate source in the given global and local namespaces.",
                "($module, source, globals=None, locals=None, /)",
            },
            {
                "exec",
                builtin_exec,
                "Execute source in the given global and local namespaces.",
                "($module, source, globals=None, locals=None, /)",
            },
            {
                "__build_class__",
                builtin_build_class,
                "Internal helper used by class statements.",
                "($module, func, name, /, *bases, **kwds)",
            },
            {
                "__import__",
                builtin_import,
                "Import a module.",
                "($module, name, globals=None, locals=None, fromlist=(), level=0, /)",
            },
        };

        constexpr NativeModuleDef builtin_module = {
            "builtins",
            "Built-in functions, types, exceptions, and other objects.",
            builtin_methods,
        };
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

        initialize_native_module(*this, *builtins_module_, builtin_module);
    }
}
