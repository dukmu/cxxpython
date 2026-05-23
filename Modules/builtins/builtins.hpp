#pragma once

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

namespace cxxpython
{
    Frame *current_frame_or_error(Runtime &runtime, const char *function_name);

    Object *builtin_print(Runtime &runtime, const CallArguments &args);
    Object *builtin_len(Runtime &runtime, const CallArguments &args);
    Object *builtin_iter(Runtime &runtime, const CallArguments &args);
    Object *builtin_next(Runtime &runtime, const CallArguments &args);
    Object *builtin_reversed(Runtime &runtime, const CallArguments &args);
    Object *builtin_repr(Runtime &runtime, const CallArguments &args);
    Object *builtin_hash(Runtime &runtime, const CallArguments &args);
    Object *builtin_all(Runtime &runtime, const CallArguments &args);
    Object *builtin_any(Runtime &runtime, const CallArguments &args);
    Object *builtin_sum(Runtime &runtime, const CallArguments &args);
    Object *builtin_min(Runtime &runtime, const CallArguments &args);
    Object *builtin_max(Runtime &runtime, const CallArguments &args);
    Object *builtin_sorted(Runtime &runtime, const CallArguments &args);
    Object *builtin_format(Runtime &runtime, const CallArguments &args);

    Object *builtin_isinstance(Runtime &runtime, const CallArguments &args);
    Object *builtin_issubclass(Runtime &runtime, const CallArguments &args);
    Object *builtin_callable(Runtime &runtime, const CallArguments &args);
    Object *builtin_id(Runtime &runtime, const CallArguments &args);
    Object *builtin_globals(Runtime &runtime, const CallArguments &args);
    Object *builtin_locals(Runtime &runtime, const CallArguments &args);
    Object *builtin_dir(Runtime &runtime, const CallArguments &args);
    Object *builtin_vars(Runtime &runtime, const CallArguments &args);
    Object *builtin_getattr(Runtime &runtime, const CallArguments &args);
    Object *builtin_setattr(Runtime &runtime, const CallArguments &args);
    Object *builtin_delattr(Runtime &runtime, const CallArguments &args);
    Object *builtin_hasattr(Runtime &runtime, const CallArguments &args);
    Object *builtin_help(Runtime &runtime, const CallArguments &args);

    Object *builtin_compile(Runtime &runtime, const CallArguments &args);
    Object *builtin_eval(Runtime &runtime, const CallArguments &args);
    Object *builtin_exec(Runtime &runtime, const CallArguments &args);

    Object *builtin_build_class(Runtime &runtime, const CallArguments &args);
    Object *builtin_import(Runtime &runtime, const CallArguments &args);
}
