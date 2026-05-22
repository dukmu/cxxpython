#include "builtins.hpp"

#include "../../Objects/object_utils.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cxxpython
{
    namespace
    {
        std::string module_name_from_globals(Runtime &runtime, DictObject *globals)
        {
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            return "__main__";
        }

        std::string package_name_from_globals(Runtime &runtime, DictObject *globals)
        {
            if (!globals)
                return "";
            if (auto *package = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__package__"))))
            {
                if (!package->value.empty())
                    return package->value;
            }
            if (globals->get(runtime.new_str("__path__")))
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
            {
                const std::size_t dot = name->value.rfind('.');
                if (dot != std::string::npos)
                    return name->value.substr(0, dot);
            }
            return "";
        }

        std::string resolve_relative_import(Runtime &runtime,
                                            const std::string &name,
                                            DictObject *globals,
                                            std::int64_t level)
        {
            if (level < 0)
                raise_value_error(&runtime, "level must be >= 0");
            if (level == 0)
                return name;

            std::string package = package_name_from_globals(runtime, globals);
            if (package.empty())
                raise_import_error(&runtime, "attempted relative import with no known parent package");

            std::vector<std::string_view> components;
            std::size_t start = 0;
            while (start <= package.size())
            {
                const std::size_t dot = package.find('.', start);
                const std::size_t end = dot == std::string::npos ? package.size() : dot;
                components.push_back(std::string_view(package).substr(start, end - start));
                if (dot == std::string::npos)
                    break;
                start = dot + 1;
            }
            if (level > static_cast<std::int64_t>(components.size()))
                raise_import_error(&runtime, "attempted relative import beyond top-level package");

            const std::size_t keep = components.size() - static_cast<std::size_t>(level) + 1;
            std::string resolved;
            for (std::size_t index = 0; index < keep; ++index)
            {
                if (!resolved.empty())
                    resolved += '.';
                resolved += components[index];
            }
            if (!name.empty())
            {
                if (!resolved.empty())
                    resolved += '.';
                resolved += name;
            }
            return resolved;
        }
    }

    Object *builtin_build_class(Runtime &runtime, const CallArguments &args)
    {
        if (args.positional.size() < 2)
            raise_type_error(&runtime, "__build_class__() missing function or name");
        auto *function = dynamic_cast<PythonFunctionObject *>(args.positional[0]);
        if (!function)
            raise_type_error(&runtime, "__build_class__() argument 1 must be a function");
        auto *name = dynamic_cast<StrObject *>(args.positional[1]);
        if (!name)
            raise_type_error(&runtime, "__build_class__() argument 2 must be a str");

        std::vector<Object *> bases;
        bases.reserve(args.positional.size() - 2);
        for (std::size_t index = 2; index < args.positional.size(); ++index)
            bases.push_back(args.positional[index]);
        TupleObject *bases_tuple = runtime.new_tuple(std::move(bases));
        auto bases_root = runtime.root(bases_tuple);

        Frame *frame = current_frame_or_error(runtime, "__build_class__");
        return runtime.build_class(ClassBuildRequest{
            .name = name->value,
            .body_code = function->code(),
            .bases = bases_root.get(),
            .keywords = args.keywords,
            .globals = function->globals(),
            .builtins = frame->builtins(),
            .module_fallback = module_name_from_globals(runtime, function->globals()),
            .qualname = name->value,
        });
    }

    Object *builtin_import(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "__import__", args);
        if (args.positional.empty() || args.positional.size() > 5)
            raise_type_error(&runtime, "__import__() expected 1 to 5 arguments");
        auto *name = dynamic_cast<StrObject *>(args.positional[0]);
        if (!name)
            raise_type_error(&runtime, "__import__() name must be str");
        DictObject *globals = nullptr;
        if (args.positional.size() >= 2 && args.positional[1] != runtime.none())
        {
            globals = dynamic_cast<DictObject *>(args.positional[1]);
            if (!globals)
                raise_type_error(&runtime, "__import__() globals must be dict");
        }
        if (!globals)
            globals = current_frame_or_error(runtime, "__import__")->globals();
        std::int64_t level_value = 0;
        if (args.positional.size() >= 5 && args.positional[4] != runtime.none())
        {
            auto *level = dynamic_cast<IntObject *>(args.positional[4]);
            if (!level)
                raise_type_error(&runtime, "__import__() level must be int");
            level_value = level->value;
        }

        const std::string absolute_name = resolve_relative_import(runtime, name->value, globals, level_value);
        ModuleObject *module = runtime.import_module(absolute_name);
        auto module_root = runtime.root(module);
        if (args.positional.size() >= 4 && args.positional[3] != runtime.none() && is_true(args.positional[3]))
        {
            if (module_root->dict()->get(runtime.new_str("__path__")))
            {
                std::vector<Object *> from_items = collect_iterable_items(&runtime, args.positional[3]);
                std::vector<Rooted<Object>> from_roots;
                from_roots.reserve(from_items.size());
                for (Object *item : from_items)
                {
                    from_roots.push_back(runtime.root(item));
                    auto *item_name = dynamic_cast<StrObject *>(item);
                    if (!item_name || item_name->value == "*")
                        continue;
                    if (module_root->dict()->get(runtime.new_str(item_name->value)))
                        continue;
                    try
                    {
                        (void)runtime.import_module(absolute_name + "." + item_name->value);
                    }
                    catch (const PythonError &error)
                    {
                        if (error.type() != runtime.import_error_type())
                            throw;
                    }
                }
            }
            return module_root.get();
        }

        const std::size_t dot = absolute_name.find('.');
        if (dot == std::string::npos)
            return module_root.get();

        const std::string top_level = absolute_name.substr(0, dot);
        Object *top_level_module = runtime.modules_dict()->get(runtime.new_str(top_level));
        auto *top_level_object = dynamic_cast<ModuleObject *>(top_level_module);
        if (!top_level_object)
            raise_import_error(&runtime, "module registry entry is not a module");
        return top_level_object;
    }
}
