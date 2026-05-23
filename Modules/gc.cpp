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
                runtime.new_int(static_cast<std::int64_t>(runtime.garbage_collection_count())),
                runtime.new_int(static_cast<std::int64_t>(runtime.root_count())),
                runtime.new_int(0),
            });
        }

        Object *gc_get_threshold(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_threshold", args);
            require_positional_count(&runtime, "get_threshold", args, 0);
            return runtime.new_tuple({
                runtime.new_int(static_cast<std::int64_t>(runtime.garbage_collection_threshold())),
                runtime.new_int(0),
                runtime.new_int(0),
            });
        }

        Object *gc_set_threshold(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "set_threshold", args);
            require_positional_range(&runtime, "set_threshold", args, 1, 3);
            auto *threshold = dynamic_cast<IntObject *>(args.positional[0]);
            if (!threshold)
                raise_type_error(&runtime, "set_threshold() threshold0 must be int");
            if (threshold->value < 0)
                raise_value_error(&runtime, "threshold must be non-negative");
            runtime.set_garbage_collection_threshold(static_cast<std::size_t>(threshold->value));
            return runtime.none();
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
            return runtime.garbage_collection_enabled() ? runtime.true_value() : runtime.false_value();
        }

        Object *gc_enable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "enable", args);
            require_positional_count(&runtime, "enable", args, 0);
            runtime.enable_garbage_collection();
            return runtime.none();
        }

        Object *gc_disable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "disable", args);
            require_positional_count(&runtime, "disable", args, 0);
            runtime.disable_garbage_collection();
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
                "get_threshold",
                gc_get_threshold,
                "Return the automatic collection thresholds.",
                "($module, /)",
            },
            {
                "set_threshold",
                gc_set_threshold,
                "Set the automatic collection threshold.",
                "($module, threshold0, threshold1=0, threshold2=0, /)",
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
