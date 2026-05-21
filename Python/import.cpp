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
            return is_identifier_component(name);
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
        }
        catch (...)
        {
            (void)modules_->erase(name_key_root.get());
            throw;
        }

        return module_root.get();
    }
}
