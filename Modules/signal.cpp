#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"

#include <array>
#include <csignal>
#include <cstddef>
#include <string>

namespace cxxpython
{
    namespace
    {
        constexpr int signal_default = 0;
        constexpr int signal_ignore = 1;

        std::array<volatile std::sig_atomic_t, 128> pending_signals{};

        void signal_trampoline(int signum)
        {
            if (signum >= 0 && static_cast<std::size_t>(signum) < pending_signals.size())
                pending_signals[static_cast<std::size_t>(signum)] = 1;
        }

        int require_signal_number(Runtime &runtime, const char *callable, Object *object)
        {
            auto *number = dynamic_cast<IntObject *>(object);
            if (!number)
                raise_type_error(&runtime, std::string(callable) + "() signal number must be int");
#ifdef NSIG
            if (number->value <= 0 || number->value >= NSIG)
#else
            if (number->value <= 0 || number->value >= static_cast<std::int64_t>(pending_signals.size()))
#endif
                raise_value_error(&runtime, "signal number out of range");
            return static_cast<int>(number->value);
        }

        ModuleObject *signal_module(Runtime &runtime)
        {
            return dynamic_cast<ModuleObject *>(runtime.modules_dict()->get(runtime.new_str("signal")));
        }

        DictObject *signal_handlers(Runtime &runtime)
        {
            ModuleObject *module = signal_module(runtime);
            if (!module)
                raise_value_error(&runtime, "signal module is not initialized");
            auto *handlers = dynamic_cast<DictObject *>(module->dict()->get(runtime.new_str("_handlers")));
            if (!handlers)
                raise_value_error(&runtime, "signal handler table is not initialized");
            return handlers;
        }

        Object *signal_constant(Runtime &runtime, int value)
        {
            return runtime.new_int(value);
        }

        Object *get_stored_handler(Runtime &runtime, int signum)
        {
            Object *handler = signal_handlers(runtime)->get(runtime.new_int(signum));
            return handler ? handler : signal_constant(runtime, signal_default);
        }

        bool is_callable(Object *object)
        {
            return object && object->type() && object->type()->slots.call;
        }

        void install_native_handler(Runtime &runtime, int signum, Object *handler)
        {
            if (auto *number = dynamic_cast<IntObject *>(handler))
            {
                if (number->value == signal_default)
                {
                    std::signal(signum, SIG_DFL);
                    return;
                }
                if (number->value == signal_ignore)
                {
                    std::signal(signum, SIG_IGN);
                    return;
                }
            }
            if (is_callable(handler))
            {
                std::signal(signum, signal_trampoline);
                return;
            }
            raise_type_error(&runtime, "signal handler must be SIG_DFL, SIG_IGN, or a callable object");
        }

        Object *signal_signal(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "signal", args);
            require_positional_count(&runtime, "signal", args, 2);
            const int signum = require_signal_number(runtime, "signal", args.positional[0]);
            Object *handler = args.positional[1];
            auto handler_root = runtime.root(handler);
            install_native_handler(runtime, signum, handler_root.get());

            DictObject *handlers = signal_handlers(runtime);
            Object *previous = get_stored_handler(runtime, signum);
            auto previous_root = runtime.root(previous);
            handlers->set(runtime.new_int(signum), handler_root.get());
            return previous_root.get();
        }

        Object *signal_getsignal(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "getsignal", args);
            require_positional_count(&runtime, "getsignal", args, 1);
            const int signum = require_signal_number(runtime, "getsignal", args.positional[0]);
            return get_stored_handler(runtime, signum);
        }

        Object *signal_raise_signal(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "raise_signal", args);
            require_positional_count(&runtime, "raise_signal", args, 1);
            const int signum = require_signal_number(runtime, "raise_signal", args.positional[0]);
            Object *handler = get_stored_handler(runtime, signum);
            auto handler_root = runtime.root(handler);

            if (is_callable(handler_root.get()))
            {
                Frame *frame = nullptr;
                if (VirtualMachine *vm = runtime.active_vm())
                    frame = vm->thread_state().frame_at_depth(0);
                Object *frame_object = frame ? static_cast<Object *>(frame) : static_cast<Object *>(runtime.none());
                (void)object_call(handler_root.get(), {runtime.new_int(signum), frame_object});
                if (signum >= 0 && static_cast<std::size_t>(signum) < pending_signals.size())
                    pending_signals[static_cast<std::size_t>(signum)] = 0;
                return runtime.none();
            }

            std::raise(signum);
            return runtime.none();
        }

        constexpr NativeModuleFunctionDef signal_methods[] = {
            {
                "signal",
                signal_signal,
                "Set the action for a signal and return the previous action.",
                "($module, signalnum, handler, /)",
            },
            {
                "getsignal",
                signal_getsignal,
                "Return the current signal action for a signal number.",
                "($module, signalnum, /)",
            },
            {
                "raise_signal",
                signal_raise_signal,
                "Raise a signal in the current process.",
                "($module, signalnum, /)",
            },
        };

        constexpr NativeModuleDef signal_module_def = {
            "signal",
            "Set handlers for asynchronous events.",
            signal_methods,
        };

        void set_signal_constant(Runtime &runtime, ModuleObject &module, const char *name, int value)
        {
            module.dict()->set(runtime.new_str(name), runtime.new_int(value));
        }
    }

    void initialize_signal_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, signal_module_def);
        DictObject *dict = module.dict();
        dict->set(runtime.new_str("_handlers"), runtime.new_dict());
        set_signal_constant(runtime, module, "SIG_DFL", signal_default);
        set_signal_constant(runtime, module, "SIG_IGN", signal_ignore);
#ifdef SIGHUP
        set_signal_constant(runtime, module, "SIGHUP", SIGHUP);
#endif
#ifdef SIGINT
        set_signal_constant(runtime, module, "SIGINT", SIGINT);
#endif
#ifdef SIGQUIT
        set_signal_constant(runtime, module, "SIGQUIT", SIGQUIT);
#endif
#ifdef SIGTERM
        set_signal_constant(runtime, module, "SIGTERM", SIGTERM);
#endif
#ifdef SIGPIPE
        set_signal_constant(runtime, module, "SIGPIPE", SIGPIPE);
#endif
#ifdef SIGALRM
        set_signal_constant(runtime, module, "SIGALRM", SIGALRM);
#endif
#ifdef SIGCHLD
        set_signal_constant(runtime, module, "SIGCHLD", SIGCHLD);
#endif
#ifdef SIGUSR1
        set_signal_constant(runtime, module, "SIGUSR1", SIGUSR1);
#endif
#ifdef SIGUSR2
        set_signal_constant(runtime, module, "SIGUSR2", SIGUSR2);
#endif
    }
}
