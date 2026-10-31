#pragma once
#include "cxxpython/runtime.hpp"
namespace cxxpython::core::detail {
void initialize_errno(Attachment &, Handle, ReturnSlot &);
}
