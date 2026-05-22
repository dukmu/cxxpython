#include "builtins.hpp"

#include "../../Objects/object_utils.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace cxxpython
{
    namespace
    {
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

        void collect_dict_names(std::set<std::string> &names, DictObject *dict)
        {
            if (!dict)
                return;
            for (const DictObject::Entry &entry : dict->entries())
            {
                if (auto *name = dynamic_cast<StrObject *>(entry.key))
                    names.insert(name->value);
            }
        }

        void collect_type_names(std::set<std::string> &names, TypeObject *type)
        {
            if (!type)
                return;
            if (type->mro.empty())
            {
                collect_dict_names(names, type->dict);
                return;
            }
            for (TypeObject *item : type->mro)
                collect_dict_names(names, item ? item->dict : nullptr);
        }

        void filter_inaccessible_instance_names(std::set<std::string> &names, Object *object)
        {
            if (!object || dynamic_cast<TypeObject *>(object) || dynamic_cast<ModuleObject *>(object))
                return;
            if ((object->type()->flags & TypeFlagHasInstanceDict) == 0)
                names.erase("__dict__");
        }

        std::vector<Object *> names_as_sorted_str_objects(Runtime &runtime, const std::set<std::string> &names)
        {
            std::vector<Object *> result;
            result.reserve(names.size());
            std::vector<Rooted<StrObject>> roots;
            for (const std::string &name : names)
            {
                StrObject *text = runtime.new_str(name);
                roots.push_back(runtime.root(text));
                result.push_back(text);
            }
            return result;
        }
    }

    Frame *current_frame_or_error(Runtime &runtime, const char *function_name)
    {
        VirtualMachine *vm = runtime.active_vm();
        Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
        if (!frame)
            raise_type_error(&runtime, std::string(function_name) + "() requires an active frame");
        return frame;
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

    Object *builtin_dir(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "dir", args);
        require_positional_at_most(&runtime, "dir", args, 1);

        std::set<std::string> names;
        if (args.positional.empty())
        {
            collect_dict_names(names, current_frame_or_error(runtime, "dir")->locals());
        }
        else
        {
            Object *object = args.positional[0];
            if (auto *module = dynamic_cast<ModuleObject *>(object))
            {
                collect_dict_names(names, module->dict());
            }
            else if (auto *type = dynamic_cast<TypeObject *>(object))
            {
                collect_type_names(names, type);
            }
            else
            {
                collect_dict_names(names, object ? object->instance_dict() : nullptr);
                collect_type_names(names, object ? object->type() : nullptr);
                filter_inaccessible_instance_names(names, object);
            }
        }
        return runtime.new_list(names_as_sorted_str_objects(runtime, names));
    }

    Object *builtin_vars(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "vars", args);
        require_positional_at_most(&runtime, "vars", args, 1);
        if (args.positional.empty())
            return current_frame_or_error(runtime, "vars")->locals();

        Object *object = args.positional[0];
        auto object_root = runtime.root(object);
        try
        {
            return object_get_attr(object_root.get(), "__dict__");
        }
        catch (const PythonError &error)
        {
            if (error.type() == runtime.attribute_error_type())
                raise_type_error(&runtime, "vars() argument must have __dict__ attribute");
            throw;
        }
    }
}
