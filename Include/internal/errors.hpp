#pragma once

#include "cxxpython/objects.hpp"

#include <optional>

namespace cxxpython::core::detail {
// Native layout is exactly tuple; item order is the CPython struct-sequence order.
using UnraisableHookArgs = TupleObject;

void write_unraisable(Attachment &ctx, Handle exception, std::optional<Handle> object,
                      std::optional<Handle> err_msg = {});
void install_error_hooks(Attachment &ctx, Handle sys, ReturnSlot &out);
void install_error_types(Attachment &, std::span<const Handle>);
void display_exception(Attachment &ctx, Handle exception);
} // namespace cxxpython::core::detail
