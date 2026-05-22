#pragma once

#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include <span>
#include <string_view>

namespace cxxpython
{
    using NativeModuleFunctionCallback = Object *(*)(Runtime &, const CallArguments &);

    struct NativeModuleFunctionDef
    {
        const char *name;
        NativeModuleFunctionCallback callback;
        const char *doc = "";
        const char *text_signature = "";
    };

    struct NativeModuleDef
    {
        const char *name;
        const char *doc;
        std::span<const NativeModuleFunctionDef> methods;
    };

    inline void add_module_function(Runtime &runtime,
                                    ModuleObject &module,
                                    const char *name,
                                    NativeModuleFunctionCallback callback,
                                    const char *doc = "",
                                    const char *text_signature = "")
    {
        module.dict()->set(runtime.new_str(name),
                           runtime.new_native_function(name,
                                                       callback,
                                                       doc,
                                                       text_signature));
    }

    inline void add_module_functions(Runtime &runtime,
                                     ModuleObject &module,
                                     std::span<const NativeModuleFunctionDef> functions)
    {
        for (const NativeModuleFunctionDef &function : functions)
        {
            add_module_function(runtime,
                                module,
                                function.name,
                                function.callback,
                                function.doc,
                                function.text_signature);
        }
    }

    inline void initialize_native_module(Runtime &runtime,
                                         ModuleObject &module,
                                         const NativeModuleDef &definition)
    {
        DictObject *dict = module.dict();
        if (definition.name && definition.name[0] != '\0')
            dict->set(runtime.new_str("__name__"), runtime.new_str(definition.name));
        Object *doc = definition.doc ? static_cast<Object *>(runtime.new_str(definition.doc))
                                     : static_cast<Object *>(runtime.none());
        dict->set(runtime.new_str("__doc__"), doc);
        add_module_functions(runtime, module, definition.methods);
    }

    bool is_builtin_extension_module(std::string_view name);
    ModuleObject *initialize_builtin_extension_module(Runtime &runtime, std::string_view name);

    void initialize_sys_module(Runtime &runtime, ModuleObject &module);
    void initialize_gc_module(Runtime &runtime, ModuleObject &module);
    void initialize_posix_module(Runtime &runtime, ModuleObject &module);
    void initialize_signal_module(Runtime &runtime, ModuleObject &module);
    void initialize_io_module(Runtime &runtime, ModuleObject &module);
    void initialize_collections_module(Runtime &runtime, ModuleObject &module);
}
