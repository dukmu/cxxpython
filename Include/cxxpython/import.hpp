#pragma once

#include "cxxpython/extension.h"
#include "cxxpython/moduleobject.hpp"

namespace cxxpython::core {
class SourceUnit;
// Internal C++ embedding interface, not a C extension ABI. Initializers use
// ordinary rooted calls and return None/exception through the caller's slot.
// Entrypoint code must outlive its registration and every function it creates.
using ModuleInitializer = void (*)(Attachment &, Handle module, ReturnSlot &);
void register_source_module(Attachment &context, std::u32string name,
                            std::shared_ptr<const SourceUnit> source, bool package = false);
void register_native_module(Attachment &context, std::u32string name, ModuleInitializer entry);
// Copies metadata; statically registered entrypoint code must remain loaded.
ResultView register_c_module(Attachment &context, const CxxPyModuleDef &definition,
                             ReturnSlot &out);
ResultView import_module(Attachment &context, const std::u32string &name, ReturnSlot &out);
ResultView import_name(Attachment &context, Handle name, Handle globals, Handle fromlist,
                       Handle level, ReturnSlot &out);
ResultView import_from(Attachment &context, Handle module, Handle name, ReturnSlot &out);
void set_import_path(Attachment &context, std::span<const std::string> paths, ReturnSlot &out);

} // namespace cxxpython::core
