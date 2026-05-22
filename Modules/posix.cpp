#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <filesystem>
#include <sstream>
#include <string_view>
#include <vector>

extern char **environ;

namespace cxxpython
{
    namespace
    {
        [[noreturn]] void raise_posix_error(Runtime &runtime,
                                            std::string_view function,
                                            const std::filesystem::path &path,
                                            const std::error_code &error)
        {
            std::ostringstream message;
            message << function << "() failed";
            if (!path.empty())
                message << " for '" << path.string() << "'";
            message << ": " << error.message();
            throw PythonError(runtime.new_os_error(message.str()));
        }

        Object *posix_getcwd(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "getcwd", args);
            require_positional_count(&runtime, "getcwd", args, 0);
            std::error_code error;
            const std::filesystem::path current = std::filesystem::current_path(error);
            if (error)
                raise_posix_error(runtime, "getcwd", {}, error);
            return runtime.new_str(current.string());
        }

        Object *posix_listdir(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "listdir", args);
            require_positional_at_most(&runtime, "listdir", args, 1);
            std::filesystem::path path = ".";
            if (args.positional.size() == 1)
            {
                auto *text = dynamic_cast<StrObject *>(args.positional[0]);
                if (!text)
                    raise_type_error(&runtime, "listdir() path must be str");
                path = text->value;
            }

            std::vector<Object *> entries;
            std::vector<Rooted<StrObject>> roots;
            std::error_code error;
            std::filesystem::directory_iterator iterator(path, error);
            if (error)
                raise_posix_error(runtime, "listdir", path, error);
            for (const std::filesystem::directory_entry &entry : iterator)
            {
                StrObject *name = runtime.new_str(entry.path().filename().string());
                roots.push_back(runtime.root(name));
                entries.push_back(name);
            }
            return runtime.new_list(std::move(entries));
        }

        Object *posix_environ(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "environ", args);
            require_positional_count(&runtime, "environ", args, 0);
            DictObject *dict = runtime.new_dict();
            auto dict_root = runtime.root(dict);
            for (char **item = environ; item && *item; ++item)
            {
                std::string entry = *item;
                const std::size_t separator = entry.find('=');
                if (separator == std::string::npos)
                    continue;
                StrObject *key = runtime.new_str(entry.substr(0, separator));
                auto key_root = runtime.root(key);
                StrObject *value = runtime.new_str(entry.substr(separator + 1));
                auto value_root = runtime.root(value);
                dict_root->set(key_root.get(), value_root.get());
            }
            return dict_root.get();
        }

        constexpr NativeModuleFunctionDef posix_methods[] = {
            {
                "getcwd",
                posix_getcwd,
                "Return a unicode string representing the current working directory.",
                "($module, /)",
            },
            {
                "listdir",
                posix_listdir,
                "Return a list containing the names of entries in the directory.",
                "($module, path='.', /)",
            },
            {
                "environ",
                posix_environ,
                "Return a dictionary containing the process environment.",
                "($module, /)",
            },
        };

        constexpr NativeModuleDef posix_module = {
            "posix",
            "Access operating-system services implemented by the POSIX substrate.",
            posix_methods,
        };
    }

    void initialize_posix_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, posix_module);
    }
}
