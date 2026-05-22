#include "cxxpython/runtime.hpp"
#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace cxxpython
{
    namespace
    {
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
            ModuleObject *module = load_extension_module_from_spec(spec);
            auto module_root = root(module);
            Object *loader = new_module_loader_object(spec);
            auto loader_root = root(loader);
            Object *module_spec = new_module_spec_object(spec, loader_root.get());
            auto spec_root = root(module_spec);
            initialize_module_import_metadata(module_root.get(), spec, loader_root.get(), spec_root.get());
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
            modules_->set(root(new_str(spec.name)).get(), module_root.get());
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
}
