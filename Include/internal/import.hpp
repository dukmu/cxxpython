#pragma once

#include "cxxpython/import.hpp"

#include <condition_variable>
#include <list>
#include <map>

namespace cxxpython::core::detail {
void initialize_gc(Attachment &, Handle, ReturnSlot &);
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
    enum class File { Source };
    std::variant<std::shared_ptr<const SourceUnit>, ModuleInitializer,
                 std::shared_ptr<const NativeModule>, File>
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
    enum class Bootstrap { Cold, Loading, Ready };
    Bootstrap bootstrap = Bootstrap::Cold;
    std::thread::id bootstrap_owner;
};
void initialize_imp(Attachment &, Handle, ReturnSlot &);
void loader_arguments(Attachment &, const std::u32string &,
                      const std::shared_ptr<const ModuleDefinition> &, ReturnSlot &);
std::shared_ptr<const ModuleDefinition> find_registered_definition(Attachment &,
                                                                   const std::u32string &);
void execute_definition(Attachment &, const std::shared_ptr<const ModuleDefinition> &, Handle,
                        ReturnSlot &);
std::shared_ptr<const SourceUnit> definition_source(Attachment &, const ModuleDefinition &,
                                                    ReturnSlot &);
void register_definition(Attachment &, std::u32string, std::shared_ptr<const ModuleDefinition>);
std::shared_ptr<const NativeModule> load_extension(Attachment &, const std::string &,
                                                   const std::u32string &, ReturnSlot &);
void exec_extension(Attachment &, const std::shared_ptr<const NativeModule> &, Handle,
                    ReturnSlot &);
} // namespace cxxpython::core::detail
