#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include <array>
#include <string_view>

namespace cxxpython
{
    namespace
    {
        constexpr std::array builtin_extension_modules{
            "_collections",
            "_io",
            "_thread",
            "_tokenize",
            "gc",
            "posix",
            "signal",
            "sys",
        };
    }

    bool is_builtin_extension_module(std::string_view name)
    {
        for (std::string_view module_name : builtin_extension_modules)
        {
            if (module_name == name)
                return true;
        }
        return false;
    }

    ModuleObject *initialize_builtin_extension_module(Runtime &runtime, std::string_view name)
    {
        if (!is_builtin_extension_module(name))
            return nullptr;

        ModuleObject *module = runtime.new_module(std::string(name), "<stdlib>");
        if (name == "gc")
            initialize_gc_module(runtime, *module);
        else if (name == "sys")
            initialize_sys_module(runtime, *module);
        else if (name == "_io")
            initialize_io_module(runtime, *module);
        else if (name == "posix")
            initialize_posix_module(runtime, *module);
        else if (name == "signal")
            initialize_signal_module(runtime, *module);
        else if (name == "_collections")
            initialize_collections_module(runtime, *module);
        else if (name == "_thread")
            initialize_thread_module(runtime, *module);
        else if (name == "_tokenize")
            initialize_tokenize_module(runtime, *module);
        return module;
    }

    ModuleObject *Runtime::import_stdlib_module(std::string_view name)
    {
        return initialize_builtin_extension_module(*this, name);
    }
}
