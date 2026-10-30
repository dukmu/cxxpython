#include "internal/main.hpp"

#include <clocale>

int main(int argc, char **argv)
{
    // Establish libc terminal character handling before any interpreter threads start.
    std::setlocale(LC_CTYPE, "");
    return cxxpython::core::detail::pymain(argc, argv);
}
