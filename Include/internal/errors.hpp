#pragma once

#include "cxxpython/runtime.hpp"

#include <optional>
#include <string>

namespace cxxpython::core {
struct TupleObject;
}

namespace cxxpython::core::detail {
// Native layout is exactly tuple; item order is the CPython struct-sequence order.
using UnraisableHookArgs = TupleObject;
void raise_os_error(Attachment &, int, ReturnSlot &, std::optional<Value> filename = {},
                    std::optional<Value> filename2 = {});
std::u32string diagnostic_type_name(Attachment &, Handle type);
bool conversion_warning(Attachment &, const std::u32string &message, ReturnSlot &);

void write_unraisable(Attachment &ctx, Handle exception, std::optional<Handle> object,
                      std::optional<Handle> err_msg = {});
void install_error_hooks(Attachment &ctx, Handle sys, ReturnSlot &out);
void install_error_types(Attachment &, std::span<const Handle>);
void display_exception(Attachment &ctx, Handle exception);
} // namespace cxxpython::core::detail
