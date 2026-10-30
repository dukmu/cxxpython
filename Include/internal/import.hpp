#pragma once

#include "cxxpython/import.hpp"

#include <condition_variable>
#include <list>
#include <map>

namespace cxxpython::core::detail {
struct NativeLibrary;
struct NativeModule {
    struct Method {
        std::u32string name;
        std::uint32_t arity;
        CxxPyEntry entry;
    };
    std::u32string name;
    CxxPyEntry exec;
    std::vector<Method> methods;
    std::shared_ptr<const NativeLibrary> library;
};
struct ModuleDefinition {
    std::variant<std::shared_ptr<const SourceUnit>, ModuleInitializer,
                 std::shared_ptr<const NativeModule>>
        loader;
    bool package = false;
    std::string origin;
    std::vector<std::string> paths;
};
struct ModuleLoad {
    const std::thread::id owner = std::this_thread::get_id();
    bool complete = false;
};
struct ImportState {
    ImportState();
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::u32string, std::shared_ptr<const ModuleDefinition>> definitions;
    std::map<std::u32string, std::shared_ptr<ModuleLoad>> loading;
    std::map<std::thread::id, std::shared_ptr<ModuleLoad>> waiting;
    // Library destructors may block. Keep a pin until State destruction, outside GC.
    std::list<std::shared_ptr<const NativeLibrary>> libraries;
};
void register_definition(Attachment &, std::u32string, std::shared_ptr<const ModuleDefinition>);
std::shared_ptr<const NativeModule> load_extension(Attachment &, const std::string &,
                                                   const std::u32string &, ReturnSlot &);
void exec_extension(Attachment &, const std::shared_ptr<const NativeModule> &, Handle,
                    ReturnSlot &);
} // namespace cxxpython::core::detail
