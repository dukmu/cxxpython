#pragma once

#include "cxxpython/compiler.hpp"

namespace cxxpython::core::detail {
int report_exception(Attachment &, Value);
// Last-resort host diagnostics; never consults user exception hooks.
int report_exception_fallback(Attachment &, Value);
int report_source_error(const SourceError &);
int run_interactive(Attachment &, Handle globals, Handle sys, bool prompts, bool banner,
                    std::uint32_t future_flags = 0);
} // namespace cxxpython::core::detail
