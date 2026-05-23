#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>
#include <unistd.h>

#include "cxxpython/compiler.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#ifdef CXXPYTHON_HAS_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace
{
    bool read_file(const char *path, std::string &out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = buffer.str();
        return true;
    }

    void print_python_error(const cxxpython::PythonError &error)
    {
        if (error.value() && !error.value()->traceback_entries().empty())
        {
            std::cerr << "Traceback (most recent call last):\n";
            const auto &entries = error.value()->traceback_entries();
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            {
                std::cerr << "  File \"" << it->filename
                          << "\", line " << it->line
                          << ", in " << it->name << "\n";
            }
        }
        std::cerr << error.type_name() << ": " << error.what() << "\n";
    }

    bool is_system_exit(cxxpython::Runtime &runtime, const cxxpython::PythonError &error)
    {
        return error.type() && cxxpython::is_subclass(error.type(), runtime.system_exit_type());
    }

    int system_exit_status(cxxpython::Runtime &runtime, const cxxpython::PythonError &error)
    {
        cxxpython::ExceptionObject *exception = error.value();
        if (!exception || !exception->args() || exception->args()->items().empty())
            return 0;
        cxxpython::Object *code = exception->args()->items()[0];
        if (!code || code == runtime.none())
            return 0;
        if (auto *integer = dynamic_cast<cxxpython::IntObject *>(code))
            return static_cast<int>(integer->value);
        std::cerr << cxxpython::object_str(code) << "\n";
        return 1;
    }

    bool import_site(cxxpython::Runtime &runtime)
    {
        try
        {
            (void)runtime.import_module("site");
            return true;
        }
        catch (const cxxpython::PythonError &error)
        {
            if (is_system_exit(runtime, error))
                return false;
            print_python_error(error);
            return false;
        }
        catch (const cxxpython::BytecodeError &error)
        {
            std::cerr << "BytecodeError: " << error.what() << "\n";
            return false;
        }
    }

    bool should_use_readline() noexcept
    {
        return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
    }

    std::optional<std::string> read_repl_line(std::string_view prompt, bool interactive)
    {
#ifdef CXXPYTHON_HAS_READLINE
        if (interactive)
        {
            char *raw_line = readline(std::string(prompt).c_str());
            if (!raw_line)
                return std::nullopt;
            std::string line(raw_line);
            if (!line.empty())
                add_history(raw_line);
            std::free(raw_line);
            return line;
        }
#else
        (void)interactive;
#endif

        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line))
            return std::nullopt;
        return line;
    }

    int run_repl(cxxpython::Runtime &runtime)
    {
        std::cout << "cxxpython 0.1.0-dev\n";
        runtime.add_module_search_path(std::filesystem::current_path().string());
        if (!import_site(runtime))
            return 1;
        cxxpython::ModuleObject *module = runtime.new_module("__main__", "<stdin>");
        auto module_root = runtime.root(module);
        const bool interactive = should_use_readline();

#ifdef CXXPYTHON_HAS_READLINE
        if (interactive)
        {
            using_history();
            stifle_history(1000);
        }
#endif

        cxxpython::ModuleObject *code_module = nullptr;
        cxxpython::Object *console = nullptr;
        try
        {
            code_module = runtime.import_module("code");
            auto code_module_root = runtime.root(code_module);
            cxxpython::Object *console_type = cxxpython::object_get_attr(code_module_root.get(), "InteractiveConsole");
            auto console_type_root = runtime.root(console_type);
            console = cxxpython::object_call(console_type_root.get(), {module_root->dict()});
        }
        catch (const cxxpython::PythonError &error)
        {
            print_python_error(error);
            return 1;
        }
        catch (const cxxpython::BytecodeError &error)
        {
            std::cerr << "BytecodeError: " << error.what() << "\n";
            return 1;
        }

        auto console_root = runtime.root(console);
        bool pending_block = false;
        while (true)
        {
            std::optional<std::string> maybe_line =
                read_repl_line(pending_block ? "... " : ">>> ", interactive);
            if (!maybe_line)
                break;

            try
            {
                cxxpython::Object *push = cxxpython::object_get_attr(console_root.get(), "push");
                auto push_root = runtime.root(push);
                cxxpython::StrObject *line = runtime.new_str(*maybe_line);
                auto line_root = runtime.root(line);
                cxxpython::Object *more = cxxpython::object_call(push_root.get(), {line_root.get()});
                auto more_root = runtime.root(more);
                pending_block = cxxpython::is_true(more_root.get());
            }
            catch (const cxxpython::PythonError &error)
            {
                if (is_system_exit(runtime, error))
                    return system_exit_status(runtime, error);
                print_python_error(error);
                pending_block = false;
            }
            catch (const cxxpython::BytecodeError &error)
            {
                std::cerr << "BytecodeError: " << error.what() << "\n";
                pending_block = false;
            }
        }

        if (pending_block)
        {
            try
            {
                cxxpython::Object *finalize = cxxpython::object_get_attr(console_root.get(), "finalize");
                auto finalize_root = runtime.root(finalize);
                (void)cxxpython::object_call(finalize_root.get(), {});
            }
            catch (const cxxpython::PythonError &error)
            {
                if (is_system_exit(runtime, error))
                    return system_exit_status(runtime, error);
                print_python_error(error);
                return 1;
            }
        }

        return 0;
    }
}

int main(int argc, char **argv)
{
    cxxpython::Runtime runtime;
    std::vector<std::string> program_args;
    program_args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
        program_args.push_back(argv[index] ? argv[index] : "");
    runtime.set_program_arguments(std::move(program_args));

    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::cout << "cxxpython 0.1.0-dev\n";
        return 0;
    }

    if (argc == 1)
    {
        return run_repl(runtime);
    }

    if (argc == 2)
    {
        std::string source;
        if (!read_file(argv[1], source))
        {
            std::cerr << "cxxpython: cannot open file: " << argv[1] << "\n";
            return 2;
        }

        try
        {
            cxxpython::VirtualMachine vm(runtime);
            runtime.add_module_search_path(std::filesystem::path(argv[1]).parent_path().string());
            if (!import_site(runtime))
                return 1;
            cxxpython::ModuleObject *module = runtime.new_module("__main__", argv[1]);
            cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, source, argv[1]);
            (void)vm.execute(cxxpython::ExecutionRequest{
                .code = code,
                .locals = module->dict(),
                .globals = module->dict(),
                .builtins = runtime.builtins_dict(),
                .module = module,
                .closure = {},
                .mode = cxxpython::ExecutionMode::Module,
            });
            return 0;
        }
        catch (const cxxpython::PythonError &error)
        {
            if (is_system_exit(runtime, error))
                return system_exit_status(runtime, error);
            print_python_error(error);
            return 1;
        }
        catch (const cxxpython::BytecodeError &error)
        {
            std::cerr << "BytecodeError: " << error.what() << "\n";
            return 1;
        }
    }

    std::cerr << "usage: cxxpython [--version] [script.py]\n";
    return 2;
}
