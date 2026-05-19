#include <iostream>
#include <fstream>
#include <sstream>
#include <string_view>

#include "cxxpython/compiler.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

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
        if (error.value() && error.value()->has_source_location())
        {
            std::cerr << "Traceback (most recent call last):\n"
                      << "  File \"" << error.value()->source_filename()
                      << "\", line " << error.value()->source_line()
                      << ", in " << error.value()->source_name() << "\n";
        }
        std::cerr << error.type_name() << ": " << error.what() << "\n";
    }

    bool is_incomplete_input(const cxxpython::PythonError &error)
    {
        if (error.type_name() != "SyntaxError")
            return false;
        const std::string message = error.what();
        return message.find("expected indented block") != std::string::npos ||
               message.find("expected except after try block") != std::string::npos;
    }

    int run_repl(cxxpython::Runtime &runtime)
    {
        std::cout << "cxxpython 0.1.0-dev\n";
        cxxpython::VirtualMachine vm(runtime);
        cxxpython::ModuleObject *module = runtime.new_module("__main__", "<stdin>");
        std::string pending_source;
        bool pending_block = false;

        while (true)
        {
            std::cout << (pending_block ? "... " : ">>> ") << std::flush;
            std::string line;
            if (!std::getline(std::cin, line))
                break;

            pending_source += line;
            pending_source += '\n';

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
                cxxpython::Object *result = vm.execute(*code, *module->dict());
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
            cxxpython::ModuleObject *module = runtime.new_module("__main__", argv[1]);
            cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, source, argv[1]);
            (void)vm.execute(*code, *module->dict());
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
