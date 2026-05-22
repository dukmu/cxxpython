#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <cstdint>
#include <vector>

namespace cxxpython
{
    namespace
    {
        Object *gc_collect(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "collect", args);
            require_positional_at_most(&runtime, "collect", args, 1);
            if (args.positional.size() == 1 && !dynamic_cast<IntObject *>(args.positional[0]))
                raise_type_error(&runtime, "collect() generation must be int");
            return runtime.new_int(static_cast<std::int64_t>(runtime.collect_garbage()));
        }

        Object *gc_get_count(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_count", args);
            require_positional_count(&runtime, "get_count", args, 0);
            return runtime.new_tuple({
                runtime.new_int(static_cast<std::int64_t>(runtime.heap_size())),
                runtime.new_int(static_cast<std::int64_t>(runtime.root_count())),
                runtime.new_int(0),
            });
        }

        Object *gc_get_objects(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_objects", args);
            require_positional_count(&runtime, "get_objects", args, 0);
            std::vector<Object *> objects;
            runtime.visit_heap([&objects](Object *object) {
                if (object)
                    objects.push_back(object);
            });
            return runtime.new_list(std::move(objects));
        }

        Object *gc_isenabled(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "isenabled", args);
            require_positional_count(&runtime, "isenabled", args, 0);
            return runtime.true_value();
        }

        Object *gc_enable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "enable", args);
            require_positional_count(&runtime, "enable", args, 0);
            return runtime.none();
        }

        Object *gc_disable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "disable", args);
            require_positional_count(&runtime, "disable", args, 0);
            return runtime.none();
        }

        constexpr NativeModuleFunctionDef gc_methods[] = {
            {
                "collect",
                gc_collect,
                "Run a garbage collection and return the number of collected objects.",
                "($module, generation=None, /)",
            },
            {
                "get_count",
                gc_get_count,
                "Return the current collection counters.",
                "($module, /)",
            },
            {
                "get_objects",
                gc_get_objects,
                "Return objects tracked by the collector.",
                "($module, /)",
            },
            {
                "isenabled",
                gc_isenabled,
                "Return whether automatic garbage collection is enabled.",
                "($module, /)",
            },
            {
                "enable",
                gc_enable,
                "Enable automatic garbage collection.",
                "($module, /)",
            },
            {
                "disable",
                gc_disable,
                "Disable automatic garbage collection.",
                "($module, /)",
            },
        };

        constexpr NativeModuleDef gc_module = {
            "gc",
            "Expose garbage collector controls and tracked-object introspection.",
            gc_methods,
        };
    }

    void initialize_gc_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, gc_module);
    }
}
