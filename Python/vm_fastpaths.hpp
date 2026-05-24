#pragma once

#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython::vm_fastpath
{
    Object *try_binary_op(Runtime &runtime, Object *left, Object *right, BinaryOp op);
    Object *try_compare_op(Runtime &runtime, Object *left, Object *right, RichCompareOp op);
    bool try_truth(Runtime &runtime, Object *object, bool &truth) noexcept;
    Object *try_builtin_call(Runtime &runtime, Object *callable, ObjectSpan args);
}
