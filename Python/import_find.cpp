#include "cxxpython/runtime.hpp"

#include "../Modules/module_registry.hpp"

#include <filesystem>
#include <string_view>

namespace cxxpython
{
    namespace
    {
        std::filesystem::path relative_module_path(std::string_view name)
        {
            std::filesystem::path path = std::string(name);
            path += ".py";
            return path;
        }

        std::filesystem::path relative_extension_path(std::string_view name)
        {
            std::filesystem::path path = std::string(name);
            path += ".so";
            return path;
        }

        std::string package_name_for(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            if (dot == std::string_view::npos)
                return "";
            return std::string(name.substr(0, dot));
        }
    }

    ModuleLoadSpec *Runtime::find_module_spec(std::string_view name, ModuleLoadSpec &storage)
    {
        if (is_builtin_extension_module(name))
        {
            storage = ModuleLoadSpec{
                .name = std::string(name),
                .filename = "<built-in>",
                .origin = "built-in",
                .loader_name = "BuiltinImporter",
                .package = package_name_for(name),
                .kind = ModuleLoadKind::Builtin,
            };
            return &storage;
        }

        const std::filesystem::path source_relative = relative_module_path(name);
        const std::filesystem::path extension_relative = relative_extension_path(name);
        for (const std::string &search_path : module_search_paths_)
        {
            std::filesystem::path source_candidate = std::filesystem::path(search_path) / source_relative;
            if (std::filesystem::is_regular_file(source_candidate))
            {
                storage = ModuleLoadSpec{
                    .name = std::string(name),
                    .filename = source_candidate.string(),
                    .origin = source_candidate.string(),
                    .loader_name = "SourceFileLoader",
                    .package = package_name_for(name),
                    .kind = ModuleLoadKind::Source,
                };
                return &storage;
            }

            std::filesystem::path extension_candidate = std::filesystem::path(search_path) / extension_relative;
            if (std::filesystem::is_regular_file(extension_candidate))
            {
                storage = ModuleLoadSpec{
                    .name = std::string(name),
                    .filename = extension_candidate.string(),
                    .origin = extension_candidate.string(),
                    .loader_name = "ExtensionFileLoader",
                    .package = package_name_for(name),
                    .kind = ModuleLoadKind::Extension,
                };
                return &storage;
            }
        }

        return nullptr;
    }
}
