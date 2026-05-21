#pragma once

#include <string_view>

namespace cxxpython
{
    class ModuleObject;
    class Runtime;

    bool is_builtin_extension_module(std::string_view name);
    ModuleObject *initialize_builtin_extension_module(Runtime &runtime, std::string_view name);

    void initialize_sys_module(Runtime &runtime, ModuleObject &module);
    void initialize_gc_module(Runtime &runtime, ModuleObject &module);
    void initialize_posix_module(Runtime &runtime, ModuleObject &module);
    void initialize_io_module(Runtime &runtime, ModuleObject &module);
    void initialize_collections_module(Runtime &runtime, ModuleObject &module);
}
