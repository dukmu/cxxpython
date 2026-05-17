#include <iostream>
#include <string_view>

#include "cxxpython/runtime.hpp"

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

    std::cerr << "usage: cxxpython [--version]\n";
    return 2;
}
