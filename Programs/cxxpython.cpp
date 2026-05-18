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
        std::cout << "cxxpython runtime initialized: "
                  << cxxpython::object_repr(runtime.type_type())
                  << "\n";
        return 0;
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
            std::cerr << error.type_name() << ": " << error.what() << "\n";
            return 1;
        }
        catch (const cxxpython::BytecodeError &error)
        {
            std::cerr << "BytecodeError: " << error.what() << "\n";
            return 1;
        }
    }

    std::cerr << "usage: cxxpython [--version]\n";
    return 2;
}
