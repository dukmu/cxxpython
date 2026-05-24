#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <chrono>
#include <thread>

namespace cxxpython
{
    namespace
    {
        double seconds_argument(Runtime &runtime, Object *object, const char *function_name)
        {
            if (auto *floating = dynamic_cast<FloatObject *>(object))
                return floating->value;
            if (auto *integer = dynamic_cast<IntObject *>(object))
                return static_cast<double>(integer->value);
            raise_type_error(&runtime, std::string(function_name) + "() seconds must be a number");
        }

        Object *time_time(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "time", args);
            require_positional_count(&runtime, "time", args, 0);
            using clock = std::chrono::system_clock;
            const auto now = clock::now().time_since_epoch();
            return runtime.new_float(std::chrono::duration<double>(now).count());
        }

        Object *time_sleep(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "sleep", args);
            require_positional_count(&runtime, "sleep", args, 1);
            const double seconds = seconds_argument(runtime, args.positional[0], "sleep");
            if (seconds < 0.0)
                raise_value_error(&runtime, "sleep length must be non-negative");
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            return runtime.none();
        }

        Object *time_monotonic(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "monotonic", args);
            require_positional_count(&runtime, "monotonic", args, 0);
            using clock = std::chrono::steady_clock;
            const auto now = clock::now().time_since_epoch();
            return runtime.new_float(std::chrono::duration<double>(now).count());
        }

        constexpr NativeModuleFunctionDef time_methods[] = {
            {
                "time",
                time_time,
                "Return the current time in seconds since the epoch.",
                "($module, /)",
            },
            {
                "sleep",
                time_sleep,
                "Sleep for the given number of seconds.",
                "($module, seconds, /)",
            },
            {
                "monotonic",
                time_monotonic,
                "Return a monotonic clock value.",
                "($module, /)",
            },
        };

        constexpr NativeModuleDef time_module = {
            "time",
            "Time access helpers backed by the host clock.",
            time_methods,
        };
    }

    void initialize_time_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, time_module);
        module.dict()->set(runtime.new_str("__all__"), runtime.new_tuple({
                                                        runtime.new_str("time"),
                                                        runtime.new_str("sleep"),
                                                        runtime.new_str("monotonic"),
                                                    }));
    }
}
