#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
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
    enum class RunMode
    {
        Repl,
        Command,
        Script,
        Module,
    };

    struct CliOptions
    {
        RunMode mode = RunMode::Repl;
        std::optional<std::string> command;
        std::optional<std::string> script_path;
        std::optional<std::string> module_name;
        std::vector<std::string> passthrough_args;
    };

    bool read_file(const std::filesystem::path &path, std::string &out)
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

    std::filesystem::path script_directory(const std::filesystem::path &path)
    {
        if (path.has_parent_path())
            return std::filesystem::absolute(path.parent_path());
        return std::filesystem::current_path();
    }

    std::vector<std::string> argv_with_program(const std::vector<std::string> &argv, const std::string &program)
    {
        std::vector<std::string> result;
        result.reserve(argv.size());
        result.push_back(program);
        for (std::size_t index = 1; index < argv.size(); ++index)
            result.push_back(argv[index]);
        return result;
    }

    std::vector<std::string> argv_for_module(const std::string &module_filename,
                                             const std::vector<std::string> &argv)
    {
        std::vector<std::string> result;
        result.reserve(argv.size());
        result.push_back(module_filename);
        for (std::size_t index = 1; index < argv.size(); ++index)
            result.push_back(argv[index]);
        return result;
    }

    void configure_sys_state(cxxpython::Runtime &runtime,
                             const std::vector<std::string> &argv,
                             const std::filesystem::path &path0,
                             bool prepend_path0)
    {
        runtime.set_program_arguments(argv);
        if (prepend_path0)
            runtime.add_module_search_path(path0.string());
    }

    CliOptions parse_cli(int argc, char **argv)
    {
        CliOptions options;
        options.passthrough_args.reserve(static_cast<std::size_t>(argc > 0 ? argc : 0));
        for (int index = 0; index < argc; ++index)
            options.passthrough_args.push_back(argv[index] ? argv[index] : "");

        if (argc == 1)
            return options;

        if (std::string_view(argv[1]) == "-c")
        {
            if (argc < 3)
                throw std::runtime_error("cxxpython: -c requires a command string");
            options.mode = RunMode::Command;
            options.command = argv[2] ? argv[2] : "";
            std::vector<std::string> command_argv;
            command_argv.push_back("-c");
            for (int index = 3; index < argc; ++index)
                command_argv.push_back(argv[index] ? argv[index] : "");
            options.passthrough_args = std::move(command_argv);
            return options;
        }

        if (std::string_view(argv[1]) == "-m")
        {
            if (argc < 3)
                throw std::runtime_error("cxxpython: -m requires a module name");
            options.mode = RunMode::Module;
            options.module_name = argv[2];
            std::vector<std::string> module_argv;
            module_argv.push_back(argv[2]);
            for (int index = 3; index < argc; ++index)
                module_argv.push_back(argv[index] ? argv[index] : "");
            options.passthrough_args = std::move(module_argv);
            return options;
        }

        options.mode = RunMode::Script;
        options.script_path = argv[1];
        std::vector<std::string> script_argv;
        script_argv.push_back(argv[1]);
        for (int index = 2; index < argc; ++index)
            script_argv.push_back(argv[index] ? argv[index] : "");
        options.passthrough_args = std::move(script_argv);
        return options;
    }

    int run_repl(cxxpython::Runtime &runtime)
    {
        std::cout << "cxxpython 0.1.0-dev\n";
        configure_sys_state(runtime, {"-"}, std::filesystem::current_path(), false);
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

        cxxpython::Object *console = nullptr;
        try
        {
            cxxpython::ModuleObject *code_module = runtime.import_module("code");
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

    int run_command(cxxpython::Runtime &runtime, const std::string &command, const std::vector<std::string> &argv)
    {
        configure_sys_state(runtime, argv, std::filesystem::current_path(), false);
        if (!import_site(runtime))
            return 1;

        try
        {
            cxxpython::VirtualMachine vm(runtime);
            cxxpython::ModuleObject *module = runtime.new_module("__main__", "<string>");
            cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, command, "<string>");
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

    int run_script(cxxpython::Runtime &runtime, const std::filesystem::path &script_path, const std::vector<std::string> &argv)
    {
        std::string source;
        if (!read_file(script_path, source))
        {
            std::cerr << "cxxpython: cannot open file: " << script_path.string() << "\n";
            return 2;
        }

        configure_sys_state(runtime, argv_with_program(argv, script_path.string()), script_directory(script_path), true);
        if (!import_site(runtime))
            return 1;

        try
        {
            cxxpython::VirtualMachine vm(runtime);
            cxxpython::ModuleObject *module = runtime.new_module("__main__", script_path.string());
            cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, source, script_path.string());
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

    int run_module(cxxpython::Runtime &runtime, const std::string &module_name, const std::vector<std::string> &argv)
    {
        std::string target_name = module_name;
        auto spec = runtime.resolve_module_spec(target_name);
        if (spec && spec->is_package)
        {
            target_name = module_name + ".__main__";
            spec = runtime.resolve_module_spec(target_name);
        }
        if (!spec.has_value())
        {
            std::cerr << "cxxpython: No module named " << module_name << "\n";
            return 1;
        }
        if (spec->kind != cxxpython::ModuleLoadKind::Source)
        {
            std::cerr << "cxxpython: module execution currently requires a source module\n";
            return 1;
        }

        try
        {
            std::vector<std::string> module_argv = argv_for_module(spec->filename, argv);
            configure_sys_state(runtime, module_argv, std::filesystem::current_path(), true);
            if (!import_site(runtime))
                return 1;

            cxxpython::VirtualMachine vm(runtime);
            cxxpython::ModuleObject *module = runtime.new_module("__main__", spec->filename);
            std::string source;
            if (!read_file(spec->filename, source))
            {
                std::cerr << "cxxpython: cannot open file: " << spec->filename << "\n";
                return 2;
            }
            cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, source, spec->filename);
            cxxpython::Object *loader = runtime.new_module_loader_object(*spec);
            auto loader_root = runtime.root(loader);
            cxxpython::Object *module_spec = runtime.new_module_spec_object(*spec, loader_root.get());
            auto module_spec_root = runtime.root(module_spec);
            runtime.initialize_module_import_metadata(module, *spec, loader_root.get(), module_spec_root.get());
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
}

int main(int argc, char **argv)
{
    cxxpython::Runtime runtime;

    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--version")
        {
            std::cout << "cxxpython 0.1.0-dev\n";
            return 0;
        }

        const CliOptions options = parse_cli(argc, argv);

        if (options.mode == RunMode::Repl)
            return run_repl(runtime);
        if (options.mode == RunMode::Command)
            return run_command(runtime, *options.command, options.passthrough_args);
        if (options.mode == RunMode::Script)
            return run_script(runtime, *options.script_path, options.passthrough_args);
        return run_module(runtime, *options.module_name, options.passthrough_args);
    }
    catch (const std::exception &error)
    {
        std::cerr << "cxxpython: " << error.what() << "\n";
        return 2;
    }
}
