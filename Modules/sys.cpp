#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cxxpython
{
    namespace
    {
        Object *sys_getframe(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_getframe", args);
            require_positional_at_most(&runtime, "_getframe", args, 1);
            std::size_t depth = 0;
            if (!args.positional.empty())
            {
                auto *value = dynamic_cast<IntObject *>(args.positional[0]);
                if (!value)
                    raise_type_error(&runtime, "_getframe() depth must be int");
                if (value->value < 0)
                    raise_value_error(&runtime, "_getframe() depth must be non-negative");
                depth = static_cast<std::size_t>(value->value);
            }
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().frame_at_depth(depth) : nullptr;
            if (!frame)
                raise_value_error(&runtime, "call stack is not deep enough");
            return frame;
        }

        Object *sys_implementation(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "implementation", args);
            require_positional_count(&runtime, "implementation", args, 0);
            DictObject *implementation = runtime.new_dict();
            implementation->set(runtime.new_str("name"), runtime.new_str("cxxpython"));
            implementation->set(runtime.new_str("version"), runtime.new_str("0.1.0-dev"));
            return implementation;
        }

        Object *sys_argv(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "argv", args);
            require_positional_count(&runtime, "argv", args, 0);
            std::vector<Object *> argv;
            argv.reserve(runtime.program_arguments().size());
            for (const std::string &arg : runtime.program_arguments())
                argv.push_back(runtime.new_str(arg));
            return runtime.new_list(std::move(argv));
        }

        Object *sys_path(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "path", args);
            require_positional_count(&runtime, "path", args, 0);
            std::vector<Object *> path;
            path.reserve(runtime.module_search_paths().size());
            for (const std::string &entry : runtime.module_search_paths())
                path.push_back(runtime.new_str(entry));
            return runtime.new_list(std::move(path));
        }

        Object *sys_modules(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "modules", args);
            require_positional_count(&runtime, "modules", args, 0);
            return runtime.modules_dict();
        }

        Object *sys_builtin_module_names(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "builtin_module_names", args);
            require_positional_count(&runtime, "builtin_module_names", args, 0);
            std::vector<Object *> names{
                runtime.new_str("_io"),
                runtime.new_str("_collections"),
                runtime.new_str("builtins"),
                runtime.new_str("gc"),
                runtime.new_str("posix"),
                runtime.new_str("sys"),
            };
            return runtime.new_tuple(std::move(names));
        }
    }

    void initialize_sys_module(Runtime &runtime, ModuleObject &module)
    {
        DictObject *dict = module.dict();
        DictObject *implementation = runtime.new_dict();
        implementation->set(runtime.new_str("name"), runtime.new_str("cxxpython"));
        implementation->set(runtime.new_str("version"), runtime.new_str("0.1.0-dev"));
        dict->set(runtime.new_str("implementation"), implementation);
        dict->set(runtime.new_str("name"), runtime.new_str("cxxpython"));
        dict->set(runtime.new_str("version"), runtime.new_str("0.1.0-dev"));
        dict->set(runtime.new_str("argv"), sys_argv(runtime, {{}, nullptr}));
        dict->set(runtime.new_str("path"), sys_path(runtime, {{}, nullptr}));
        dict->set(runtime.new_str("modules"), runtime.modules_dict());
        dict->set(runtime.new_str("builtin_module_names"), sys_builtin_module_names(runtime, {{}, nullptr}));
        dict->set(runtime.new_str("stdin"), runtime.stdin_object());
        dict->set(runtime.new_str("stdout"), runtime.stdout_object());
        dict->set(runtime.new_str("stderr"), runtime.stderr_object());
        dict->set(runtime.new_str("_implementation"),
                  runtime.new_native_function("_implementation", sys_implementation));
        dict->set(runtime.new_str("_argv"), runtime.new_native_function("_argv", sys_argv));
        dict->set(runtime.new_str("_path"), runtime.new_native_function("_path", sys_path));
        dict->set(runtime.new_str("_modules"), runtime.new_native_function("_modules", sys_modules));
        dict->set(runtime.new_str("_builtin_module_names"),
                  runtime.new_native_function("_builtin_module_names", sys_builtin_module_names));
        dict->set(runtime.new_str("_getframe"), runtime.new_native_function("_getframe", sys_getframe));
    }
}
