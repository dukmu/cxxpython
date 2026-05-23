#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"

#include <iostream>
#include <limits>
#include <sstream>
#include <thread>

namespace cxxpython
{
    namespace
    {
        ThreadLockObject *require_lock(Runtime &runtime, Object *self, const char *name)
        {
            auto *lock = dynamic_cast<ThreadLockObject *>(self);
            if (!lock)
                raise_type_error(&runtime, std::string(name) + " receiver does not have lock layout");
            return lock;
        }

        std::string lock_repr(const Object &object)
        {
            auto *lock = static_cast<const ThreadLockObject *>(&object);
            return lock->locked() ? "<locked _thread.lock object>"
                                  : "<unlocked _thread.lock object>";
        }

        std::int64_t lock_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool lock_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool lock_truth(const Object &)
        {
            return true;
        }

        Object *lock_acquire_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() > 1)
                raise_type_error(&runtime, "lock.acquire() takes at most one argument");
            bool blocking = true;
            if (args.size() == 1)
                blocking = is_true(args[0]);
            return require_lock(runtime, self, "lock.acquire")->acquire(blocking)
                       ? static_cast<Object *>(runtime.true_value())
                       : static_cast<Object *>(runtime.false_value());
        }

        Object *lock_release_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "lock.release() takes exactly zero arguments");
            require_lock(runtime, self, "lock.release")->release();
            return runtime.none();
        }

        Object *lock_locked_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "lock.locked() takes exactly zero arguments");
            return require_lock(runtime, self, "lock.locked")->locked()
                       ? static_cast<Object *>(runtime.true_value())
                       : static_cast<Object *>(runtime.false_value());
        }

        Object *lock_enter_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "lock.__enter__() takes exactly zero arguments");
            require_lock(runtime, self, "lock.__enter__")->acquire(true);
            return self;
        }

        Object *lock_exit_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 3)
                raise_type_error(&runtime, "lock.__exit__() takes exactly three arguments");
            require_lock(runtime, self, "lock.__exit__")->release();
            return runtime.false_value();
        }

        Object *thread_allocate_lock(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "allocate_lock", args);
            require_positional_count(&runtime, "allocate_lock", args, 0);
            return runtime.new_thread_lock();
        }

        std::int64_t thread_id_value(std::thread::id id)
        {
            const auto value = std::hash<std::thread::id>{}(id);
            return static_cast<std::int64_t>(value & static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()));
        }

        Object *thread_get_ident(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_ident", args);
            require_positional_count(&runtime, "get_ident", args, 0);
            return runtime.new_int(thread_id_value(std::this_thread::get_id()));
        }

        Object *thread_exit(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "exit", args);
            require_positional_count(&runtime, "exit", args, 0);
            throw PythonError(runtime.new_exception(runtime.system_exit_type(), std::vector<Object *>{}));
        }

        void print_unraisable_thread_error(const PythonError &error)
        {
            std::cerr << "Exception ignored in thread started by _thread.start_new_thread: "
                      << error.type_name() << ": " << error.what() << "\n";
        }

        Object *thread_start_new_thread(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "start_new_thread", args);
            require_positional_range(&runtime, "start_new_thread", args, 2, 3);
            Object *callable = args.positional[0];
            auto *thread_args = dynamic_cast<TupleObject *>(args.positional[1]);
            if (!thread_args)
                raise_type_error(&runtime, "start_new_thread() args must be tuple");
            DictObject *thread_kwargs = nullptr;
            if (args.positional.size() == 3)
            {
                thread_kwargs = dynamic_cast<DictObject *>(args.positional[2]);
                if (!thread_kwargs)
                    raise_type_error(&runtime, "start_new_thread() kwargs must be dict");
            }

            runtime.add_root(callable);
            runtime.add_root(thread_args);
            runtime.add_root(thread_kwargs);

            std::thread thread([&runtime, callable, thread_args, thread_kwargs]() {
                try
                {
                    std::vector<Object *> positional = thread_args->items();
                    (void)object_call(callable, CallArguments{std::move(positional), thread_kwargs});
                }
                catch (const PythonError &error)
                {
                    if (!is_subclass(error.type(), runtime.system_exit_type()))
                        print_unraisable_thread_error(error);
                }
                catch (const std::exception &error)
                {
                    std::cerr << "Exception ignored in thread started by _thread.start_new_thread: "
                              << error.what() << "\n";
                }
                runtime.remove_root(thread_kwargs);
                runtime.remove_root(thread_args);
                runtime.remove_root(callable);
            });

            const std::int64_t ident = thread_id_value(thread.get_id());
            thread.detach();
            return runtime.new_int(ident);
        }

        constexpr NativeModuleFunctionDef thread_methods[] = {
            {
                "allocate_lock",
                thread_allocate_lock,
                "Create a new primitive lock object.",
                "($module, /)",
            },
            {
                "get_ident",
                thread_get_ident,
                "Return the current thread identifier.",
                "($module, /)",
            },
            {
                "start_new_thread",
                thread_start_new_thread,
                "Start a new OS thread and call function(*args, **kwargs).",
                "($module, function, args, kwargs=None, /)",
            },
            {
                "exit",
                thread_exit,
                "Raise SystemExit to exit the current thread.",
                "($module, /)",
            },
        };

        constexpr NativeModuleDef thread_module = {
            "_thread",
            "Low-level primitives for real OS threads.",
            thread_methods,
        };
    }

    ThreadLockObject::ThreadLockObject(TypeObject *type)
        : Object(type)
    {
    }

    bool ThreadLockObject::acquire(bool blocking)
    {
        std::unique_lock lock(mutex_);
        if (!blocking)
        {
            if (locked_)
                return false;
            locked_ = true;
            return true;
        }
        VirtualMachine *vm = runtime()->active_vm();
        ThreadState *thread = vm ? &vm->thread_state() : nullptr;
        bool parked_for_gc = false;
        if (thread && locked_)
        {
            thread->set_gc_safepoint(true);
            parked_for_gc = true;
        }
        condition_.wait(lock, [this] { return !locked_; });
        if (parked_for_gc)
            thread->set_gc_safepoint(false);
        locked_ = true;
        return true;
    }

    void ThreadLockObject::release()
    {
        {
            std::scoped_lock lock(mutex_);
            if (!locked_)
                raise_value_error(runtime(), "release unlocked lock");
            locked_ = false;
        }
        condition_.notify_one();
    }

    bool ThreadLockObject::locked() const
    {
        std::scoped_lock lock(mutex_);
        return locked_;
    }

    TypeSlots make_thread_lock_slots()
    {
        return TypeSlots{
            .repr = lock_repr,
            .hash = lock_hash,
            .equal = lock_equal,
            .truth = lock_truth,
        };
    }

    void initialize_thread_lock_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("acquire"),
                        runtime.new_method_descriptor("acquire",
                                                      type,
                                                      lock_acquire_method,
                                                      "Acquire the lock, blocking by default.",
                                                      "($self, blocking=True, /)"));
        type->dict->set(runtime.new_str("release"),
                        runtime.new_method_descriptor("release",
                                                      type,
                                                      lock_release_method,
                                                      "Release the lock.",
                                                      "($self, /)"));
        type->dict->set(runtime.new_str("locked"),
                        runtime.new_method_descriptor("locked",
                                                      type,
                                                      lock_locked_method,
                                                      "Return whether the lock is acquired.",
                                                      "($self, /)"));
        type->dict->set(runtime.new_str("__enter__"),
                        runtime.new_method_descriptor("__enter__",
                                                      type,
                                                      lock_enter_method,
                                                      "Acquire the lock and return it.",
                                                      "($self, /)"));
        type->dict->set(runtime.new_str("__exit__"),
                        runtime.new_method_descriptor("__exit__",
                                                      type,
                                                      lock_exit_method,
                                                      "Release the lock.",
                                                      "($self, exc_type, exc, traceback, /)"));
    }

    void initialize_thread_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, thread_module);
        module.dict()->set(runtime.new_str("LockType"), runtime.thread_lock_type());
    }
}
