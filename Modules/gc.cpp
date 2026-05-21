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
    }

    void initialize_gc_module(Runtime &runtime, ModuleObject &module)
    {
        DictObject *dict = module.dict();
        dict->set(runtime.new_str("collect"), runtime.new_native_function("collect", gc_collect));
        dict->set(runtime.new_str("get_count"), runtime.new_native_function("get_count", gc_get_count));
        dict->set(runtime.new_str("get_objects"), runtime.new_native_function("get_objects", gc_get_objects));
        dict->set(runtime.new_str("isenabled"), runtime.new_native_function("isenabled", gc_isenabled));
        dict->set(runtime.new_str("enable"), runtime.new_native_function("enable", gc_enable));
        dict->set(runtime.new_str("disable"), runtime.new_native_function("disable", gc_disable));
    }
}
