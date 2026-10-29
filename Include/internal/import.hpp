#pragma once

#include "cxxpython/import.hpp"

#include <condition_variable>
#include <map>

namespace cxxpython::core::detail {
struct ModuleDefinition {
    std::variant<std::shared_ptr<const SourceUnit>, ModuleInitializer> loader;
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
};
} // namespace cxxpython::core::detail
