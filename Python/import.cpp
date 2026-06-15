#include "cxxpython/runtime.hpp"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cxxpython
{
    namespace
    {
        bool is_identifier_component(std::string_view text)
        {
            if (text.empty())
                return false;
            const auto first = static_cast<unsigned char>(text.front());
            if (!(std::isalpha(first) || text.front() == '_'))
                return false;
            for (char ch : text)
            {
                const auto value = static_cast<unsigned char>(ch);
                if (!(std::isalnum(value) || ch == '_'))
                    return false;
            }
            return true;
        }

        bool is_supported_module_name(std::string_view name)
        {
            if (name.empty())
                return false;
            std::size_t start = 0;
            while (start < name.size())
            {
                const std::size_t dot = name.find('.', start);
                const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
                if (!is_identifier_component(name.substr(start, end - start)))
                    return false;
                if (dot == std::string_view::npos)
                    return true;
                start = dot + 1;
            }
            return false;
        }

        std::string parent_module_name(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            if (dot == std::string_view::npos)
                return "";
            return std::string(name.substr(0, dot));
        }

        std::string child_module_name(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            if (dot == std::string_view::npos)
                return std::string(name);
            return std::string(name.substr(dot + 1));
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

        std::string resolve_import_name(Runtime &runtime, const ImportRequest &request)
        {
            if (request.level < 0)
                raise_value_error(&runtime, "level must be >= 0");
            if (request.level == 0)
                return request.name;

            std::string package = package_name_from_globals(runtime, request.globals);
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
            if (request.level > static_cast<std::int64_t>(components.size()))
                raise_import_error(&runtime, "attempted relative import beyond top-level package");

            const std::size_t keep = components.size() - static_cast<std::size_t>(request.level) + 1;
            std::string resolved;
            for (std::size_t index = 0; index < keep; ++index)
            {
                if (!resolved.empty())
                    resolved += '.';
                resolved += components[index];
            }
            if (!request.name.empty())
            {
                if (!resolved.empty())
                    resolved += '.';
                resolved += request.name;
            }
            return resolved;
        }

        std::vector<std::string> package_paths(Runtime &runtime, ModuleObject *package)
        {
            std::vector<std::string> paths;
            if (!package)
                return paths;
            Object *path_object = package->dict()->get(runtime.new_str("__path__"));
            if (!path_object)
                return paths;
            Object *iterator = object_iter(path_object);
            auto iterator_root = runtime.root(iterator);
            while (true)
            {
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = runtime.root(item);
                    auto *text = dynamic_cast<StrObject *>(item_root.get());
                    if (!text)
                        raise_import_error(&runtime, "__path__ entries must be str");
                    paths.push_back(text->value);
                }
                catch (const PythonError &error)
                {
                    if (error.type() && is_subclass(error.type(), runtime.stop_iteration_type()))
                        break;
                    throw;
                }
            }
            return paths;
        }

        void load_fromlist_children(Runtime &runtime, std::string_view module_name, ModuleObject *module, Object *fromlist)
        {
            if (!module->dict()->get(runtime.new_str("__path__")))
                return;

            Object *iterator = object_iter(fromlist);
            auto iterator_root = runtime.root(iterator);
            while (true)
            {
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = runtime.root(item);
                    auto *item_name = dynamic_cast<StrObject *>(item_root.get());
                    if (!item_name || item_name->value == "*")
                        continue;
                    if (module->dict()->get(runtime.new_str(item_name->value)))
                        continue;
                    try
                    {
                        (void)runtime.import_module(std::string(module_name) + "." + item_name->value);
                    }
                    catch (const PythonError &error)
                    {
                        if (error.type() != runtime.import_error_type())
                            throw;
                    }
                }
                catch (const PythonError &error)
                {
                    if (error.type() && is_subclass(error.type(), runtime.stop_iteration_type()))
                        break;
                    throw;
                }
            }
        }
    }

    Object *Runtime::import_name(const ImportRequest &request)
    {
        auto fromlist_root = root(request.fromlist);
        auto globals_root = root(request.globals);
        const std::string absolute_name = resolve_import_name(*this, request);
        ModuleObject *module = import_module(absolute_name);
        auto module_root = root(module);
        if (fromlist_root.get() && fromlist_root.get() != none() && is_true(fromlist_root.get()))
        {
            load_fromlist_children(*this, absolute_name, module_root.get(), fromlist_root.get());
            return module_root.get();
        }

        const std::size_t dot = absolute_name.find('.');
        if (dot == std::string::npos)
            return module_root.get();

        const std::string top_level = absolute_name.substr(0, dot);
        Object *top_level_module = modules_->get(new_str(top_level));
        auto *top_level_object = dynamic_cast<ModuleObject *>(top_level_module);
        if (!top_level_object)
            raise_import_error(this, "module registry entry is not a module");
        return top_level_object;
    }

    ModuleObject *Runtime::import_module(std::string name)
    {
        std::scoped_lock import_lock(import_mutex_);
        if (!modules_)
            raise_import_error(this, "module registry is not initialized");
        if (!is_supported_module_name(name))
            raise_import_error(this, "invalid module name '" + name + "'");

        StrObject *name_key = new_str(name);
        auto name_key_root = root(name_key);
        if (Object *cached = modules_->get(name_key_root.get()))
        {
            auto *module = dynamic_cast<ModuleObject *>(cached);
            if (!module)
                raise_import_error(this, "module registry entry is not a module");
            return module;
        }

        const std::string parent_name = parent_module_name(name);
        ModuleObject *parent_module = nullptr;
        if (!parent_name.empty())
        {
            parent_module = import_module(parent_name);
            auto parent_root = root(parent_module);
            Object *path = parent_root->dict()->get(new_str("__path__"));
            if (!path)
                raise_import_error(this, "No module named '" + name + "'");
        }

        ModuleLoadSpec spec;
        if (parent_module)
        {
            std::vector<std::string> paths = package_paths(*this, parent_module);
            if (paths.empty() || !find_module_spec(name, spec, &paths, child_module_name(name)))
                raise_import_error(this, "No module named '" + name + "'");
        }
        else if (!find_module_spec(name, spec))
        {
            raise_import_error(this, "No module named '" + name + "'");
        }

        ModuleObject *module = load_module_from_spec(spec);
        auto module_root = root(module);
        if (!module_root)
            raise_import_error(this, "loader returned null for module '" + name + "'");
        const bool already_registered = modules_->get(name_key_root.get()) == module_root.get();
        try
        {
            if (!already_registered)
                modules_->set(name_key_root.get(), module_root.get());
            if (parent_module)
            {
                auto parent_root = root(parent_module);
                object_set_attr(parent_root.get(), child_module_name(name), module_root.get());
            }
        }
        catch (...)
        {
            (void)modules_->erase(name_key_root.get());
            throw;
        }

        return module_root.get();
    }
}
