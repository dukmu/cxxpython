#pragma once

#include "cxxpython/runtime.hpp"

namespace cxxpython::core::detail {
void install_binascii(Attachment &, Handle module, ReturnSlot &);
}
