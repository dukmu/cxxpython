#pragma once

#include <string_view>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython
{
    CodeObject *compile_eval_input(Runtime &runtime, std::string_view source);
    CodeObject *compile_exec_input(Runtime &runtime, std::string_view source);
}
