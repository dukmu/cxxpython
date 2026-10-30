#pragma once

#include "cxxpython/compiler.hpp"

namespace cxxpython::core::detail {
int report_exception(Attachment &, Value);
int report_source_error(const SourceError &);
int run_interactive(Attachment &, Handle globals, Handle sys, bool prompts, bool banner);
} // namespace cxxpython::core::detail
