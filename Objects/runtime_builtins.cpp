#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "object_utils.hpp"

#include <cstdint>
#include <ostream>

namespace cxxpython
{
    namespace
    {
        StrObject *keyword_as_str(Runtime &runtime, DictObject *keywords, const char *name, const char *function_name)
        {
            if (!keywords)
                return nullptr;
            Object *value = keywords->get(runtime.new_str(name));
            if (!value || value == runtime.none())
                return nullptr;
            auto *text = dynamic_cast<StrObject *>(value);
            if (!text)
                raise_type_error(&runtime, std::string(function_name) + "() " + name + " must be None or a string");
            return text;
        }

        void require_print_keywords(Runtime &runtime, const CallArguments &args)
        {
            if (!args.keywords)
                return;
            for (const DictObject::Entry &entry : args.keywords->entries())
            {
                auto *name = dynamic_cast<StrObject *>(entry.key);
                if (!name)
                    raise_type_error(&runtime, "print() keywords must be strings");
                if (name->value != "sep" && name->value != "end")
                {
                    raise_type_error(&runtime,
                                     "print() got an unexpected keyword argument '" + name->value + "'");
                }
            }
        }

        Object *builtin_print(Runtime &runtime, const CallArguments &args)
        {
            require_print_keywords(runtime, args);
            StrObject *sep_arg = keyword_as_str(runtime, args.keywords, "sep", "print");
            StrObject *end_arg = keyword_as_str(runtime, args.keywords, "end", "print");
            const std::string sep = sep_arg ? sep_arg->value : " ";
            const std::string end = end_arg ? end_arg->value : "\n";
            std::ostream &out = runtime.stdout_stream();
            for (std::size_t index = 0; index < args.positional.size(); ++index)
            {
                if (index != 0)
                    out << sep;
                out << object_str(args.positional[index]);
            }
            out << end;
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

        bool classinfo_matches(Runtime &runtime, TypeObject *type, Object *classinfo, const char *function_name)
        {
            if (auto *candidate = dynamic_cast<TypeObject *>(classinfo))
                return is_subclass(type, candidate);
            if (auto *tuple = dynamic_cast<TupleObject *>(classinfo))
            {
                for (Object *item : tuple->items())
                {
                    if (classinfo_matches(runtime, type, item, function_name))
                        return true;
                }
                return false;
            }
            raise_type_error(&runtime,
                             std::string(function_name) +
                                 "() arg 2 must be a type or tuple of types");
        }

        Object *builtin_isinstance(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "isinstance", args);
            require_positional_count(&runtime, "isinstance", args, 2);
            Object *object = args.positional[0];
            if (!object)
                raise_type_error(&runtime, "isinstance() arg 1 cannot be null");
            return classinfo_matches(runtime, object->type(), args.positional[1], "isinstance")
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_issubclass(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "issubclass", args);
            require_positional_count(&runtime, "issubclass", args, 2);
            auto *type = dynamic_cast<TypeObject *>(args.positional[0]);
            if (!type)
                raise_type_error(&runtime, "issubclass() arg 1 must be a class");
            return classinfo_matches(runtime, type, args.positional[1], "issubclass")
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_callable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "callable", args);
            require_positional_count(&runtime, "callable", args, 1);
            Object *object = args.positional[0];
            return object && object->type() && object->type()->slots.call
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_id(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "id", args);
            require_positional_count(&runtime, "id", args, 1);
            return runtime.new_int(static_cast<std::int64_t>(
                reinterpret_cast<std::uintptr_t>(args.positional[0])));
        }

        Frame *current_frame_or_error(Runtime &runtime, const char *function_name)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            if (!frame)
                raise_type_error(&runtime, std::string(function_name) + "() requires an active frame");
            return frame;
        }

        Object *builtin_globals(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "globals", args);
            require_positional_count(&runtime, "globals", args, 0);
            return current_frame_or_error(runtime, "globals")->globals();
        }

        Object *builtin_locals(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "locals", args);
            require_positional_count(&runtime, "locals", args, 0);
            return current_frame_or_error(runtime, "locals")->locals();
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
        dict->set(new_str("isinstance"), new_native_function("isinstance", builtin_isinstance));
        dict->set(new_str("issubclass"), new_native_function("issubclass", builtin_issubclass));
        dict->set(new_str("callable"), new_native_function("callable", builtin_callable));
        dict->set(new_str("id"), new_native_function("id", builtin_id));
        dict->set(new_str("globals"), new_native_function("globals", builtin_globals));
        dict->set(new_str("locals"), new_native_function("locals", builtin_locals));
    }
}
