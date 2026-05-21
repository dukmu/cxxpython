#include "cxxpython/runtime.hpp"
#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

#include <cctype>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
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
            return is_identifier_component(name);
        }

        bool is_builtin_module_name(std::string_view name)
        {
            return name == "gc" ||
                   name == "_collections" ||
                   name == "_io" ||
                   name == "sys" ||
                   name == "posix";
        }

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

        bool read_text_file(const std::filesystem::path &path, std::string &out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;
            std::ostringstream buffer;
            buffer << file.rdbuf();
            out = buffer.str();
            return true;
        }

        class SearchPathGuard final
        {
        public:
            SearchPathGuard(std::vector<std::string> &paths, std::filesystem::path directory)
                : paths_(paths)
            {
                if (!directory.empty())
                {
                    paths_.insert(paths_.begin(), directory.string());
                    inserted_ = true;
                }
            }

            ~SearchPathGuard()
            {
                if (inserted_ && !paths_.empty())
                    paths_.erase(paths_.begin());
            }

            SearchPathGuard(const SearchPathGuard &) = delete;
            SearchPathGuard &operator=(const SearchPathGuard &) = delete;

        private:
            std::vector<std::string> &paths_;
            bool inserted_ = false;
        };

        std::string package_name_for(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            if (dot == std::string_view::npos)
                return "";
            return std::string(name.substr(0, dot));
        }
    }

    TypeObject *Runtime::importlib_type_for_loader(std::string_view loader_name)
    {
        TypeObject **slot = nullptr;
        if (loader_name == "BuiltinImporter")
            slot = &builtin_importer_type_;
        else if (loader_name == "SourceFileLoader")
            slot = &source_file_loader_type_;
        else if (loader_name == "ExtensionFileLoader")
            slot = &extension_file_loader_type_;
        else if (loader_name == "ModuleSpec")
            slot = &module_spec_type_;
        else
            raise_import_error(this, "unknown importlib object type '" + std::string(loader_name) + "'");

        if (*slot)
            return *slot;

        DictObject *namespace_dict = new_dict();
        auto namespace_root = root(namespace_dict);
        namespace_root->set(new_str("__module__"), new_str("_frozen_importlib"));
        namespace_root->set(new_str("__qualname__"), new_str(std::string(loader_name)));
        *slot = new_class(std::string(loader_name), object_type(), namespace_root.get(), "_frozen_importlib");
        add_root(*slot);
        return *slot;
    }

    Object *Runtime::new_importlib_object(
        std::string name,
        std::string module,
        std::initializer_list<std::pair<std::string_view, Object *>> attributes)
    {
        (void)module;
        TypeObject *type = importlib_type_for_loader(name);
        auto type_root = root(type);
        Object *instance = new_plain_instance(type_root.get());
        auto instance_root = root(instance);
        DictObject *dict = new_dict();
        auto dict_root = root(dict);
        for (const auto &[key, value] : attributes)
            dict_root->set(new_str(std::string(key)), value ? value : none());
        instance_root->set_instance_dict(dict_root.get());
        return instance_root.get();
    }

    ModuleLoadSpec *Runtime::find_module_spec(std::string_view name, ModuleLoadSpec &storage)
    {
        if (is_builtin_module_name(name))
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

    Object *Runtime::new_module_loader_object(const ModuleLoadSpec &spec)
    {
        return new_importlib_object(
            spec.loader_name,
            "_frozen_importlib",
            {
                {"name", new_str(spec.loader_name)},
                {"module_name", new_str(spec.name)},
                {"path", new_str(spec.filename)},
            });
    }

    Object *Runtime::new_module_spec_object(const ModuleLoadSpec &spec, Object *loader)
    {
        return new_importlib_object(
            "ModuleSpec",
            "_frozen_importlib",
            {
                {"name", new_str(spec.name)},
                {"loader", loader},
                {"origin", new_str(spec.origin)},
                {"has_location", spec.kind == ModuleLoadKind::Source || spec.kind == ModuleLoadKind::Extension
                                     ? static_cast<Object *>(true_value())
                                     : static_cast<Object *>(false_value())},
            });
    }

    void Runtime::initialize_module_import_metadata(ModuleObject *module,
                                                    const ModuleLoadSpec &spec,
                                                    Object *loader,
                                                    Object *module_spec)
    {
        if (!module)
            raise_import_error(this, "loader returned null for module '" + spec.name + "'");
        module->dict()->set(new_str("__loader__"), loader);
        module->dict()->set(new_str("__spec__"), module_spec);
        module->dict()->set(new_str("__package__"), new_str(spec.package));
        module->dict()->set(new_str("__cached__"), none());
    }

    ModuleObject *Runtime::load_module_from_spec(const ModuleLoadSpec &spec)
    {
        if (spec.kind == ModuleLoadKind::Builtin)
        {
            ModuleObject *module = import_stdlib_module(spec.name);
            auto module_root = root(module);
            Object *loader = new_module_loader_object(spec);
            auto loader_root = root(loader);
            Object *module_spec = new_module_spec_object(spec, loader_root.get());
            auto spec_root = root(module_spec);
            initialize_module_import_metadata(module_root.get(), spec, loader_root.get(), spec_root.get());
            return module_root.get();
        }

        if (spec.kind == ModuleLoadKind::Extension)
        {
            void *handle = dlopen(spec.filename.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle)
                raise_import_error(this, "cannot load extension module '" + spec.name + "': " + dlerror());
            using InitFunction = ModuleObject *(*)(Runtime &);
            const std::string symbol = "CxxPyInit_" + spec.name;
            dlerror();
            auto *init = reinterpret_cast<InitFunction>(dlsym(handle, symbol.c_str()));
            const char *error = dlerror();
            if (error)
            {
                dlclose(handle);
                raise_import_error(this, "extension module '" + spec.name + "' does not define " + symbol);
            }
            ModuleObject *module = init(*this);
            if (!module)
            {
                dlclose(handle);
                raise_import_error(this, "extension module '" + spec.name + "' initialization returned null");
            }
            auto module_root = root(module);
            Object *loader = new_module_loader_object(spec);
            auto loader_root = root(loader);
            Object *module_spec = new_module_spec_object(spec, loader_root.get());
            auto spec_root = root(module_spec);
            initialize_module_import_metadata(module_root.get(), spec, loader_root.get(), spec_root.get());
            extension_handles_.push_back(handle);
            return module;
        }

        std::string source;
        if (!read_text_file(spec.filename, source))
            raise_import_error(this, "cannot read source module '" + spec.name + "'");

        ModuleObject *module = new_module(spec.name, spec.filename);
        auto module_root = root(module);
        Object *loader = new_module_loader_object(spec);
        auto loader_root = root(loader);
        Object *module_spec = new_module_spec_object(spec, loader_root.get());
        auto spec_root = root(module_spec);
        initialize_module_import_metadata(module_root.get(), spec, loader_root.get(), spec_root.get());
        try
        {
            CodeObject *code = compile_exec_input(*this, source, spec.filename);
            auto code_root = root(code);
            SearchPathGuard imported_directory(module_search_paths_, std::filesystem::path(spec.filename).parent_path());
            ExecutionRequest request{
                .code = code_root.get(),
                .locals = module_root->dict(),
                .globals = module_root->dict(),
                .builtins = builtins_dict(),
                .module = module_root.get(),
                .closure = {},
                .mode = ExecutionMode::Module,
            };
            if (VirtualMachine *vm = active_vm())
                (void)vm->execute(request);
            else
            {
                VirtualMachine local_vm(*this);
                (void)local_vm.execute(request);
            }
        }
        catch (...)
        {
            (void)modules_->erase(root(new_str(spec.name)).get());
            throw;
        }

        return module_root.get();
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
