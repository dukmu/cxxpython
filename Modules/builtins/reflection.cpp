#include "builtins.hpp"

#include "../../Objects/object_utils.hpp"

#include <cstdint>
#include <ostream>
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

        StrObject *require_attribute_name(Runtime &runtime, const char *function_name, Object *object)
        {
            auto *name = dynamic_cast<StrObject *>(object);
            if (!name)
                raise_type_error(&runtime, std::string(function_name) + "() attribute name must be str");
            return name;
        }

        std::string help_name(Runtime &runtime, Object *object)
        {
            if (!object)
                return "help";
            try
            {
                Object *name = object_get_attr(object, "__name__");
                auto name_root = runtime.root(name);
                if (auto *text = dynamic_cast<StrObject *>(name_root.get()))
                    return text->value;
            }
            catch (const PythonError &error)
            {
                if (error.type() != runtime.attribute_error_type())
                    throw;
            }
            return type_qualified_name(object->type());
        }

        Object *help_doc(Runtime &runtime, Object *object)
        {
            if (!object)
                return runtime.none();
            try
            {
                return object_get_attr(object, "__doc__");
            }
            catch (const PythonError &error)
            {
                if (error.type() != runtime.attribute_error_type())
                    throw;
                return runtime.none();
            }
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
        Frame *frame = current_frame_or_error(runtime, "locals");
        frame->sync_fast_locals_to_dict();
        return frame->locals();
    }

    Object *builtin_dir(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "dir", args);
        require_positional_at_most(&runtime, "dir", args, 1);

        std::set<std::string> names;
        if (args.positional.empty())
        {
            Frame *frame = current_frame_or_error(runtime, "dir");
            frame->sync_fast_locals_to_dict();
            collect_dict_names(names, frame->locals());
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
        {
            Frame *frame = current_frame_or_error(runtime, "vars");
            frame->sync_fast_locals_to_dict();
            return frame->locals();
        }

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

    Object *builtin_getattr(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "getattr", args);
        require_positional_range(&runtime, "getattr", args, 2, 3);
        StrObject *name = require_attribute_name(runtime, "getattr", args.positional[1]);
        try
        {
            return object_get_attr(args.positional[0], name);
        }
        catch (const PythonError &error)
        {
            if (args.positional.size() == 3 && error.type() == runtime.attribute_error_type())
                return args.positional[2];
            throw;
        }
    }

    Object *builtin_setattr(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "setattr", args);
        require_positional_count(&runtime, "setattr", args, 3);
        StrObject *name = require_attribute_name(runtime, "setattr", args.positional[1]);
        object_set_attr(args.positional[0], name, args.positional[2]);
        return runtime.none();
    }

    Object *builtin_delattr(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "delattr", args);
        require_positional_count(&runtime, "delattr", args, 2);
        StrObject *name = require_attribute_name(runtime, "delattr", args.positional[1]);
        object_delete_attr(args.positional[0], name);
        return runtime.none();
    }

    Object *builtin_hasattr(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "hasattr", args);
        require_positional_count(&runtime, "hasattr", args, 2);
        StrObject *name = require_attribute_name(runtime, "hasattr", args.positional[1]);
        try
        {
            (void)object_get_attr(args.positional[0], name);
            return runtime.true_value();
        }
        catch (const PythonError &error)
        {
            if (error.type() == runtime.attribute_error_type())
                return runtime.false_value();
            throw;
        }
    }

    Object *builtin_help(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "help", args);
        require_positional_at_most(&runtime, "help", args, 1);
        std::ostream &out = runtime.stdout_stream();
        if (args.positional.empty())
        {
            out << "Type help(object) for help about object.\n";
            return runtime.none();
        }

        Object *object = args.positional[0];
        auto object_root = runtime.root(object);
        out << "Help on " << help_name(runtime, object_root.get()) << ":\n";
        Object *doc = help_doc(runtime, object_root.get());
        auto doc_root = runtime.root(doc);
        if (doc_root.get() != runtime.none())
            out << object_str(doc_root.get()) << "\n";
        return runtime.none();
    }
}
