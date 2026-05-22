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

    bool is_incomplete_input(const cxxpython::PythonError &error)
    {
        if (error.type_name() != "SyntaxError")
            return false;
        const std::string message = error.what();
        return message.find("expected indented block") != std::string::npos ||
               message.find("expected except after try block") != std::string::npos ||
               message.find("expected except or finally after try block") != std::string::npos;
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
        cxxpython::VirtualMachine vm(runtime);
        cxxpython::ModuleObject *module = runtime.new_module("__main__", "<stdin>");
        std::string pending_source;
        bool pending_block = false;
        const bool interactive = should_use_readline();

#ifdef CXXPYTHON_HAS_READLINE
        if (interactive)
        {
            using_history();
            stifle_history(1000);
        }
#endif

        while (true)
        {
            std::optional<std::string> maybe_line =
                read_repl_line(pending_block ? "... " : ">>> ", interactive);
            if (!maybe_line)
                break;
            const std::string &line = *maybe_line;

            pending_source += line;
            pending_source += '\n';
            if (pending_block && !line.empty())
                continue;

            cxxpython::CodeObject *code = nullptr;
            bool print_result = false;
            if (!pending_block)
            {
                try
                {
                    code = cxxpython::compile_eval_input(runtime, line, "<stdin>");
                    print_result = true;
                }
                catch (const cxxpython::PythonError &error)
                {
                    if (error.type_name() != "SyntaxError")
                    {
                        print_python_error(error);
                        pending_source.clear();
                        continue;
                    }
                }
            }

            if (!code)
            {
                try
                {
                    code = cxxpython::compile_exec_input(runtime, pending_source, "<stdin>");
                }
                catch (const cxxpython::PythonError &error)
                {
                    if (is_incomplete_input(error))
                    {
                        pending_block = true;
                        continue;
                    }
                    print_python_error(error);
                    pending_source.clear();
                    pending_block = false;
                    continue;
                }
            }

            try
            {
                cxxpython::Object *result = vm.execute(cxxpython::ExecutionRequest{
                    .code = code,
                    .locals = module->dict(),
                    .globals = module->dict(),
                    .builtins = runtime.builtins_dict(),
                    .module = module,
                    .closure = {},
                    .mode = print_result ? cxxpython::ExecutionMode::Eval : cxxpython::ExecutionMode::Exec,
                });
                if (print_result && result != runtime.none())
                    std::cout << cxxpython::object_repr(result) << "\n";
            }
            catch (const cxxpython::PythonError &error)
            {
                print_python_error(error);
            }
            catch (const cxxpython::BytecodeError &error)
            {
                std::cerr << "BytecodeError: " << error.what() << "\n";
            }

            pending_source.clear();
            pending_block = false;
        }

        if (pending_block && !pending_source.empty())
        {
            try
            {
                (void)cxxpython::compile_exec_input(runtime, pending_source, "<stdin>");
            }
            catch (const cxxpython::PythonError &error)
            {
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
