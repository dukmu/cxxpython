#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <ostream>

namespace cxxpython
{
    namespace
    {
        Object *builtin_print(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "print", args);
            std::ostream &out = runtime.stdout_stream();
            for (std::size_t index = 0; index < args.positional.size(); ++index)
            {
                if (index != 0)
                    out << ' ';
                out << object_str(args.positional[index]);
            }
            out << '\n';
            return runtime.none();
        }

        Object *builtin_len(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "len", args);
            require_positional_count(&runtime, "len", args, 1);
            return runtime.new_int(static_cast<std::int64_t>(object_length(args.positional[0])));
        }

        Object *builtin_iter(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "iter", args);
            require_positional_count(&runtime, "iter", args, 1);
            return object_iter(args.positional[0]);
        }

        Object *builtin_next(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "next", args);
            require_positional_count(&runtime, "next", args, 1);
            return object_next(args.positional[0]);
        }

        Object *builtin_repr(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "repr", args);
            require_positional_count(&runtime, "repr", args, 1);
            return runtime.new_str(object_repr(args.positional[0]));
        }

        Object *builtin_hash(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "hash", args);
            require_positional_count(&runtime, "hash", args, 1);
            return runtime.new_int(object_hash(args.positional[0]));
        }
    }

    void Runtime::initialize_builtins()
    {
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str("builtins"));
        dict->set(new_str("__file__"), new_str(""));

        builtins_module_ = allocate<ModuleObject>(module_type_, "builtins", "", dict);
        dict->set(new_str("__builtins__"), builtins_module_);
        dict->set(new_str("NotImplemented"), not_implemented_);
        dict->set(new_str("BaseException"), base_exception_type_);
        dict->set(new_str("Exception"), exception_type_);
        dict->set(new_str("TypeError"), type_error_type_);
        dict->set(new_str("ValueError"), value_error_type_);
        dict->set(new_str("IndexError"), index_error_type_);
        dict->set(new_str("KeyError"), key_error_type_);
        dict->set(new_str("SyntaxError"), syntax_error_type_);
        dict->set(new_str("NameError"), name_error_type_);
        dict->set(new_str("UnboundLocalError"), unbound_local_error_type_);
        dict->set(new_str("AttributeError"), attribute_error_type_);
        dict->set(new_str("StopIteration"), stop_iteration_type_);
        dict->set(new_str("None"), none_);
        dict->set(new_str("True"), true_);
        dict->set(new_str("False"), false_);
        dict->set(new_str("object"), object_type_);
        dict->set(new_str("type"), type_type_);
        dict->set(new_str("int"), int_type_);
        dict->set(new_str("bool"), bool_type_);
        dict->set(new_str("str"), str_type_);
        dict->set(new_str("list"), list_type_);
        dict->set(new_str("tuple"), tuple_type_);
        dict->set(new_str("dict"), dict_type_);
        dict->set(new_str("property"), property_type_);
        dict->set(new_str("staticmethod"), staticmethod_type_);
        dict->set(new_str("classmethod"), classmethod_type_);
        dict->set(new_str("print"), new_native_function("print", builtin_print));
        dict->set(new_str("len"), new_native_function("len", builtin_len));
        dict->set(new_str("iter"), new_native_function("iter", builtin_iter));
        dict->set(new_str("next"), new_native_function("next", builtin_next));
        dict->set(new_str("repr"), new_native_function("repr", builtin_repr));
        dict->set(new_str("hash"), new_native_function("hash", builtin_hash));
    }
}
