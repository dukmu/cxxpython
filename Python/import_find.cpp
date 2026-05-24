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
            std::filesystem::path path;
            std::size_t start = 0;
            while (start < name.size())
            {
                const std::size_t dot = name.find('.', start);
                const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
                path /= std::string(name.substr(start, end - start));
                if (dot == std::string_view::npos)
                    break;
                start = dot + 1;
            }
            path += ".py";
            return path;
        }

        std::filesystem::path relative_extension_path(std::string_view name)
        {
            std::filesystem::path path;
            std::size_t start = 0;
            while (start < name.size())
            {
                const std::size_t dot = name.find('.', start);
                const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
                path /= std::string(name.substr(start, end - start));
                if (dot == std::string_view::npos)
                    break;
                start = dot + 1;
            }
            path += ".so";
            return path;
        }

        std::filesystem::path relative_package_init_path(std::string_view name)
        {
            std::filesystem::path path;
            std::size_t start = 0;
            while (start < name.size())
            {
                const std::size_t dot = name.find('.', start);
                const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
                path /= std::string(name.substr(start, end - start));
                if (dot == std::string_view::npos)
                    break;
                start = dot + 1;
            }
            path /= "__init__.py";
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

    ModuleLoadSpec *Runtime::find_module_spec(std::string_view name,
                                              ModuleLoadSpec &storage,
                                              const std::vector<std::string> *package_paths,
                                              std::string_view search_name)
    {
        if (is_builtin_extension_module(name))
        {
            storage = ModuleLoadSpec{
                .name = std::string(name),
                .filename = "<built-in>",
                .origin = "built-in",
                .loader_name = "BuiltinImporter",
                .package = package_name_for(name),
                .package_dir = "",
                .is_package = false,
                .kind = ModuleLoadKind::Builtin,
            };
            return &storage;
        }

        const std::string_view relative_lookup = search_name.empty() ? name : search_name;
        const std::filesystem::path source_relative = relative_module_path(relative_lookup);
        const std::filesystem::path extension_relative = relative_extension_path(relative_lookup);
        const std::filesystem::path package_relative = relative_package_init_path(relative_lookup);
        const std::vector<std::string> &search_paths = package_paths ? *package_paths : module_search_paths_;
        for (const std::string &search_path : search_paths)
        {
            std::filesystem::path package_candidate = std::filesystem::path(search_path) / package_relative;
            if (std::filesystem::is_regular_file(package_candidate))
            {
                const std::filesystem::path absolute = std::filesystem::absolute(package_candidate).lexically_normal();
                storage = ModuleLoadSpec{
                    .name = std::string(name),
                    .filename = absolute.string(),
                    .origin = absolute.string(),
                    .loader_name = "SourceFileLoader",
                    .package = std::string(name),
                    .package_dir = absolute.parent_path().string(),
                    .is_package = true,
                    .kind = ModuleLoadKind::Source,
                };
                return &storage;
            }

            std::filesystem::path source_candidate = std::filesystem::path(search_path) / source_relative;
            if (std::filesystem::is_regular_file(source_candidate))
            {
                const std::filesystem::path absolute = std::filesystem::absolute(source_candidate).lexically_normal();
                storage = ModuleLoadSpec{
                    .name = std::string(name),
                    .filename = absolute.string(),
                    .origin = absolute.string(),
                    .loader_name = "SourceFileLoader",
                    .package = package_name_for(name),
                    .package_dir = "",
                    .is_package = false,
                    .kind = ModuleLoadKind::Source,
                };
                return &storage;
            }

            std::filesystem::path extension_candidate = std::filesystem::path(search_path) / extension_relative;
            if (std::filesystem::is_regular_file(extension_candidate))
            {
                const std::filesystem::path absolute = std::filesystem::absolute(extension_candidate).lexically_normal();
                storage = ModuleLoadSpec{
                    .name = std::string(name),
                    .filename = absolute.string(),
                    .origin = absolute.string(),
                    .loader_name = "ExtensionFileLoader",
                    .package = package_name_for(name),
                    .package_dir = "",
                    .is_package = false,
                    .kind = ModuleLoadKind::Extension,
                };
                return &storage;
            }
        }

        return nullptr;
    }

    std::optional<ModuleLoadSpec> Runtime::resolve_module_spec(std::string_view name,
                                                                const std::vector<std::string> *package_paths,
                                                                std::string_view search_name)
    {
        ModuleLoadSpec storage;
        if (ModuleLoadSpec *spec = find_module_spec(name, storage, package_paths, search_name))
            return *spec;
        return std::nullopt;
    }
}
