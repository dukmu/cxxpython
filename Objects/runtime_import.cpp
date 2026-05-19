#include "cxxpython/runtime.hpp"
#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

#include <cctype>
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

        std::filesystem::path relative_module_path(std::string_view name)
        {
            std::filesystem::path path = std::string(name);
            path += ".py";
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

        const std::filesystem::path relative = relative_module_path(name);
        std::filesystem::path filename;
        std::string source;
        for (const std::string &search_path : module_search_paths_)
        {
            std::filesystem::path candidate = std::filesystem::path(search_path) / relative;
            if (read_text_file(candidate, source))
            {
                filename = candidate;
                break;
            }
        }

        if (filename.empty())
            raise_import_error(this, "No module named '" + name + "'");

        ModuleObject *module = new_module(name, filename.string());
        auto module_root = root(module);
        try
        {
            CodeObject *code = compile_exec_input(*this, source, filename.string());
            auto code_root = root(code);
            SearchPathGuard imported_directory(module_search_paths_, filename.parent_path());
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
            {
                (void)vm->execute(request);
            }
            else
            {
                VirtualMachine local_vm(*this);
                (void)local_vm.execute(request);
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
