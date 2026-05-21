#include "cxxpython/runtime.hpp"
#include "cxxpython/extension_api.h"

#include <dlfcn.h>
#include <string>

namespace cxxpython
{
    namespace
    {
        thread_local std::string pending_extension_import_error;

        void set_pending_extension_import_error(const char *message)
        {
            pending_extension_import_error =
                message ? message : "extension module initialization failed";
        }

        CxxPyRuntime *to_extension_runtime(Runtime *runtime)
        {
            return reinterpret_cast<CxxPyRuntime *>(runtime);
        }

        Runtime *from_extension_runtime(CxxPyRuntime *runtime)
        {
            return reinterpret_cast<Runtime *>(runtime);
        }

        CxxPyObject *to_extension_object(Object *object)
        {
            return reinterpret_cast<CxxPyObject *>(object);
        }

        Object *from_extension_object(CxxPyObject *object)
        {
            return reinterpret_cast<Object *>(object);
        }

        CxxPyObject *extension_create_module(CxxPyRuntime *runtime, const char *name)
        {
            try
            {
                Runtime *core_runtime = from_extension_runtime(runtime);
                if (!core_runtime || !name)
                {
                    set_pending_extension_import_error("extension create_module() received invalid arguments");
                    return nullptr;
                }
                return to_extension_object(core_runtime->new_module(name, ""));
            }
            catch (const PythonError &error)
            {
                set_pending_extension_import_error(error.what());
                return nullptr;
            }
            catch (...)
            {
                set_pending_extension_import_error("extension create_module() failed");
                return nullptr;
            }
        }

        CxxPyObject *extension_create_string(CxxPyRuntime *runtime, const char *value)
        {
            try
            {
                Runtime *core_runtime = from_extension_runtime(runtime);
                if (!core_runtime || !value)
                {
                    set_pending_extension_import_error("extension create_string() received invalid arguments");
                    return nullptr;
                }
                return to_extension_object(core_runtime->new_str(value));
            }
            catch (const PythonError &error)
            {
                set_pending_extension_import_error(error.what());
                return nullptr;
            }
            catch (...)
            {
                set_pending_extension_import_error("extension create_string() failed");
                return nullptr;
            }
        }

        CxxPyObject *extension_create_int(CxxPyRuntime *runtime, int64_t value)
        {
            try
            {
                Runtime *core_runtime = from_extension_runtime(runtime);
                if (!core_runtime)
                {
                    set_pending_extension_import_error("extension create_int() received invalid arguments");
                    return nullptr;
                }
                return to_extension_object(core_runtime->new_int(value));
            }
            catch (const PythonError &error)
            {
                set_pending_extension_import_error(error.what());
                return nullptr;
            }
            catch (...)
            {
                set_pending_extension_import_error("extension create_int() failed");
                return nullptr;
            }
        }

        int extension_set_attr(CxxPyRuntime *runtime, CxxPyObject *object, const char *name, CxxPyObject *value)
        {
            try
            {
                Runtime *core_runtime = from_extension_runtime(runtime);
                if (!core_runtime || !object || !name || !value)
                {
                    set_pending_extension_import_error("extension set_attr() received invalid arguments");
                    return -1;
                }
                object_set_attr(from_extension_object(object), name, from_extension_object(value));
                return 0;
            }
            catch (const PythonError &error)
            {
                set_pending_extension_import_error(error.what());
                return -1;
            }
            catch (...)
            {
                set_pending_extension_import_error("extension set_attr() failed");
                return -1;
            }
        }

        void extension_set_import_error(CxxPyRuntime *runtime, const char *message)
        {
            (void)runtime;
            set_pending_extension_import_error(message);
        }

        const CxxPyExtensionApi extension_api{
            .abi_version = CXXPYTHON_EXTENSION_ABI_VERSION,
            .create_module = extension_create_module,
            .create_string = extension_create_string,
            .create_int = extension_create_int,
            .set_attr = extension_set_attr,
            .set_import_error = extension_set_import_error,
        };
    }

    ModuleObject *Runtime::load_extension_module_from_spec(const ModuleLoadSpec &spec)
    {
        void *handle = dlopen(spec.filename.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            raise_import_error(this, "cannot load extension module '" + spec.name + "': " + dlerror());

        const std::string symbol = "CxxPyInit_" + spec.name;
        dlerror();
        auto *init = reinterpret_cast<CxxPyInitFunc>(dlsym(handle, symbol.c_str()));
        const char *error = dlerror();
        if (error)
        {
            dlclose(handle);
            raise_import_error(this, "extension module '" + spec.name + "' does not define " + symbol);
        }

        pending_extension_import_error.clear();
        CxxPyObject *result = nullptr;
        try
        {
            result = init(to_extension_runtime(this), &extension_api);
        }
        catch (...)
        {
            dlclose(handle);
            throw;
        }

        if (!pending_extension_import_error.empty())
        {
            (void)modules_->erase(root(new_str(spec.name)).get());
            const std::string message = pending_extension_import_error;
            pending_extension_import_error.clear();
            dlclose(handle);
            raise_import_error(this, message);
        }

        auto *module = dynamic_cast<ModuleObject *>(from_extension_object(result));
        if (!module)
        {
            (void)modules_->erase(root(new_str(spec.name)).get());
            dlclose(handle);
            raise_import_error(this, "extension module '" + spec.name + "' initialization did not return a module");
        }

        extension_handles_.push_back(handle);
        return module;
    }
}
