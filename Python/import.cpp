#include "cxxpython/runtime.hpp"

#include <cctype>
#include <string_view>

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
    }

    ModuleObject *Runtime::import_module(std::string name)
    {
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
        if (!find_module_spec(name, spec))
            raise_import_error(this, "No module named '" + name + "'");

        ModuleObject *module = load_module_from_spec(spec);
        auto module_root = root(module);
        if (!module_root)
            raise_import_error(this, "loader returned null for module '" + name + "'");
        try
        {
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
