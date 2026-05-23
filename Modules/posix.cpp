#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
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

        [[noreturn]] void raise_errno_error(Runtime &runtime, std::string_view function)
        {
            std::ostringstream message;
            message << function << "() failed: " << std::strerror(errno);
            throw PythonError(runtime.new_os_error(message.str()));
        }

        std::int64_t require_int_argument(Runtime &runtime, const char *function, const char *name, Object *object)
        {
            auto *value = dynamic_cast<IntObject *>(object);
            if (!value)
                raise_type_error(&runtime, std::string(function) + "() " + name + " must be int");
            return value->value;
        }

        pid_t require_pid_argument(Runtime &runtime, const char *function, const char *name, Object *object)
        {
            const std::int64_t value = require_int_argument(runtime, function, name, object);
            if (value < static_cast<std::int64_t>(std::numeric_limits<pid_t>::min()) ||
                value > static_cast<std::int64_t>(std::numeric_limits<pid_t>::max()))
                raise_value_error(&runtime, std::string(function) + "() " + name + " out of range");
            return static_cast<pid_t>(value);
        }

        int require_c_int_argument(Runtime &runtime, const char *function, const char *name, Object *object)
        {
            const std::int64_t value = require_int_argument(runtime, function, name, object);
            if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
                value > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
                raise_value_error(&runtime, std::string(function) + "() " + name + " out of range");
            return static_cast<int>(value);
        }

        std::string require_str_argument(Runtime &runtime, const char *function, const char *name, Object *object)
        {
            auto *text = dynamic_cast<StrObject *>(object);
            if (!text)
                raise_type_error(&runtime, std::string(function) + "() " + name + " must be str");
            return text->value;
        }

        std::vector<std::string> require_str_sequence(Runtime &runtime,
                                                      const char *function,
                                                      const char *name,
                                                      Object *object)
        {
            std::vector<std::string> result;
            for (Object *item : collect_iterable_items(&runtime, object))
                result.push_back(require_str_argument(runtime, function, name, item));
            if (result.empty())
                raise_value_error(&runtime, std::string(function) + "() arg list must not be empty");
            return result;
        }

        std::vector<char *> c_string_vector(std::vector<std::string> &storage)
        {
            std::vector<char *> result;
            result.reserve(storage.size() + 1);
            for (std::string &item : storage)
                result.push_back(item.data());
            result.push_back(nullptr);
            return result;
        }

        std::vector<std::string> require_env_mapping(Runtime &runtime,
                                                     const char *function,
                                                     Object *object)
        {
            auto *dict = dynamic_cast<DictObject *>(object);
            if (!dict)
                raise_type_error(&runtime, std::string(function) + "() env must be dict");

            std::vector<std::string> result;
            result.reserve(dict->entries().size());
            for (const DictObject::Entry &entry : dict->entries())
            {
                std::string key = require_str_argument(runtime, function, "env key", entry.key);
                std::string value = require_str_argument(runtime, function, "env value", entry.value);
                result.push_back(std::move(key) + "=" + std::move(value));
            }
            return result;
        }

        std::string env_path_or_default(const std::vector<std::string> &env)
        {
            for (const std::string &entry : env)
            {
                if (entry.rfind("PATH=", 0) == 0)
                    return entry.substr(5);
            }
            return "/bin:/usr/bin";
        }

        [[noreturn]] void execvpe_impl(Runtime &runtime,
                                       const char *function,
                                       const std::string &file,
                                       std::vector<std::string> &argv_storage,
                                       std::vector<std::string> &env_storage)
        {
            std::vector<char *> argv = c_string_vector(argv_storage);
            std::vector<char *> envp = c_string_vector(env_storage);
            if (file.find('/') != std::string::npos)
            {
                errno = 0;
                ::execve(file.c_str(), argv.data(), envp.data());
                raise_errno_error(runtime, function);
            }

            int saved_errno = ENOENT;
            const std::string path = env_path_or_default(env_storage);
            std::size_t start = 0;
            while (start <= path.size())
            {
                const std::size_t end = path.find(':', start);
                std::string directory = end == std::string::npos ? path.substr(start)
                                                                 : path.substr(start, end - start);
                if (directory.empty())
                    directory = ".";
                const std::string candidate = directory + "/" + file;
                errno = 0;
                ::execve(candidate.c_str(), argv.data(), envp.data());
                if (errno != ENOENT && errno != ENOTDIR)
                    saved_errno = errno;
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            errno = saved_errno;
            raise_errno_error(runtime, function);
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

        Object *posix_getpid(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "getpid", args);
            require_positional_count(&runtime, "getpid", args, 0);
            return runtime.new_int(static_cast<std::int64_t>(::getpid()));
        }

        Object *posix_kill(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "kill", args);
            require_positional_count(&runtime, "kill", args, 2);
            const pid_t pid = require_pid_argument(runtime, "kill", "pid", args.positional[0]);
            const int signum = require_c_int_argument(runtime, "kill", "signal", args.positional[1]);
            errno = 0;
            if (::kill(pid, signum) != 0)
                raise_errno_error(runtime, "kill");
            return runtime.none();
        }

        Object *posix_fork(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "fork", args);
            require_positional_count(&runtime, "fork", args, 0);
            errno = 0;
            const pid_t pid = ::fork();
            if (pid < 0)
                raise_errno_error(runtime, "fork");
            return runtime.new_int(static_cast<std::int64_t>(pid));
        }

        Object *posix_waitpid(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "waitpid", args);
            require_positional_count(&runtime, "waitpid", args, 2);
            const pid_t pid = require_pid_argument(runtime, "waitpid", "pid", args.positional[0]);
            const int options = require_c_int_argument(runtime, "waitpid", "options", args.positional[1]);
            int status = 0;
            errno = 0;
            const pid_t result = ::waitpid(pid, &status, options);
            if (result < 0)
                raise_errno_error(runtime, "waitpid");
            return runtime.new_tuple({
                runtime.new_int(static_cast<std::int64_t>(result)),
                runtime.new_int(static_cast<std::int64_t>(status)),
            });
        }

        Object *posix_exit(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_exit", args);
            require_positional_count(&runtime, "_exit", args, 1);
            const int status = require_c_int_argument(runtime, "_exit", "status", args.positional[0]);
            ::_exit(status);
        }

        Object *posix_pipe(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "pipe", args);
            require_positional_count(&runtime, "pipe", args, 0);
            int fds[2] = {-1, -1};
            errno = 0;
            if (::pipe(fds) != 0)
                raise_errno_error(runtime, "pipe");
            return runtime.new_tuple({
                runtime.new_int(static_cast<std::int64_t>(fds[0])),
                runtime.new_int(static_cast<std::int64_t>(fds[1])),
            });
        }

        Object *posix_close(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "close", args);
            require_positional_count(&runtime, "close", args, 1);
            const int fd = require_c_int_argument(runtime, "close", "fd", args.positional[0]);
            errno = 0;
            if (::close(fd) != 0)
                raise_errno_error(runtime, "close");
            return runtime.none();
        }

        Object *posix_dup(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "dup", args);
            require_positional_count(&runtime, "dup", args, 1);
            const int fd = require_c_int_argument(runtime, "dup", "fd", args.positional[0]);
            errno = 0;
            const int result = ::dup(fd);
            if (result < 0)
                raise_errno_error(runtime, "dup");
            return runtime.new_int(static_cast<std::int64_t>(result));
        }

        Object *posix_dup2(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "dup2", args);
            require_positional_count(&runtime, "dup2", args, 2);
            const int fd = require_c_int_argument(runtime, "dup2", "fd", args.positional[0]);
            const int fd2 = require_c_int_argument(runtime, "dup2", "fd2", args.positional[1]);
            errno = 0;
            const int result = ::dup2(fd, fd2);
            if (result < 0)
                raise_errno_error(runtime, "dup2");
            return runtime.new_int(static_cast<std::int64_t>(result));
        }

        Object *posix_open(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "open", args);
            require_positional_range(&runtime, "open", args, 2, 3);
            auto *path = dynamic_cast<StrObject *>(args.positional[0]);
            if (!path)
                raise_type_error(&runtime, "open() path must be str");
            const int flags = require_c_int_argument(runtime, "open", "flags", args.positional[1]);
            int mode = 0777;
            if (args.positional.size() == 3)
                mode = require_c_int_argument(runtime, "open", "mode", args.positional[2]);
            errno = 0;
            const int fd = ::open(path->value.c_str(), flags, mode);
            if (fd < 0)
                raise_errno_error(runtime, "open");
            return runtime.new_int(static_cast<std::int64_t>(fd));
        }

        Object *posix_read(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "read", args);
            require_positional_count(&runtime, "read", args, 2);
            const int fd = require_c_int_argument(runtime, "read", "fd", args.positional[0]);
            const std::int64_t count = require_int_argument(runtime, "read", "count", args.positional[1]);
            if (count < 0)
                raise_value_error(&runtime, "read() count must be non-negative");
            std::string buffer(static_cast<std::size_t>(count), '\0');
            errno = 0;
            const ssize_t result = ::read(fd, buffer.data(), buffer.size());
            if (result < 0)
                raise_errno_error(runtime, "read");
            buffer.resize(static_cast<std::size_t>(result));
            return runtime.new_bytes(std::move(buffer));
        }

        Object *posix_write(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "write", args);
            require_positional_count(&runtime, "write", args, 2);
            const int fd = require_c_int_argument(runtime, "write", "fd", args.positional[0]);
            auto *data = dynamic_cast<BytesObject *>(args.positional[1]);
            if (!data)
                raise_type_error(&runtime, "write() data must be bytes");
            errno = 0;
            const ssize_t result = ::write(fd, data->value.data(), data->value.size());
            if (result < 0)
                raise_errno_error(runtime, "write");
            return runtime.new_int(static_cast<std::int64_t>(result));
        }

        Object *posix_system(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "system", args);
            require_positional_count(&runtime, "system", args, 1);
            const std::string command = require_str_argument(runtime, "system", "command", args.positional[0]);
            errno = 0;
            const int status = std::system(command.c_str());
            if (status == -1)
                raise_errno_error(runtime, "system");
            return runtime.new_int(static_cast<std::int64_t>(status));
        }

        Object *posix_execv(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "execv", args);
            require_positional_count(&runtime, "execv", args, 2);
            const std::string path = require_str_argument(runtime, "execv", "path", args.positional[0]);
            std::vector<std::string> argv_storage =
                require_str_sequence(runtime, "execv", "arg", args.positional[1]);
            std::vector<char *> argv = c_string_vector(argv_storage);
            errno = 0;
            ::execv(path.c_str(), argv.data());
            raise_errno_error(runtime, "execv");
        }

        Object *posix_execve(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "execve", args);
            require_positional_count(&runtime, "execve", args, 3);
            const std::string path = require_str_argument(runtime, "execve", "path", args.positional[0]);
            std::vector<std::string> argv_storage =
                require_str_sequence(runtime, "execve", "arg", args.positional[1]);
            std::vector<std::string> env_storage = require_env_mapping(runtime, "execve", args.positional[2]);
            std::vector<char *> argv = c_string_vector(argv_storage);
            std::vector<char *> envp = c_string_vector(env_storage);
            errno = 0;
            ::execve(path.c_str(), argv.data(), envp.data());
            raise_errno_error(runtime, "execve");
        }

        Object *posix_execvp(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "execvp", args);
            require_positional_count(&runtime, "execvp", args, 2);
            const std::string file = require_str_argument(runtime, "execvp", "file", args.positional[0]);
            std::vector<std::string> argv_storage =
                require_str_sequence(runtime, "execvp", "arg", args.positional[1]);
            std::vector<char *> argv = c_string_vector(argv_storage);
            errno = 0;
            ::execvp(file.c_str(), argv.data());
            raise_errno_error(runtime, "execvp");
        }

        Object *posix_execvpe(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "execvpe", args);
            require_positional_count(&runtime, "execvpe", args, 3);
            const std::string file = require_str_argument(runtime, "execvpe", "file", args.positional[0]);
            std::vector<std::string> argv_storage =
                require_str_sequence(runtime, "execvpe", "arg", args.positional[1]);
            std::vector<std::string> env_storage = require_env_mapping(runtime, "execvpe", args.positional[2]);
            execvpe_impl(runtime, "execvpe", file, argv_storage, env_storage);
        }

        Object *posix_wifexited(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "WIFEXITED", args);
            require_positional_count(&runtime, "WIFEXITED", args, 1);
            const int status = require_c_int_argument(runtime, "WIFEXITED", "status", args.positional[0]);
            return WIFEXITED(status) ? static_cast<Object *>(runtime.true_value())
                                     : static_cast<Object *>(runtime.false_value());
        }

        Object *posix_wexitstatus(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "WEXITSTATUS", args);
            require_positional_count(&runtime, "WEXITSTATUS", args, 1);
            const int status = require_c_int_argument(runtime, "WEXITSTATUS", "status", args.positional[0]);
            return runtime.new_int(static_cast<std::int64_t>(WEXITSTATUS(status)));
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
                "getpid",
                posix_getpid,
                "Return the current process id.",
                "($module, /)",
            },
            {
                "kill",
                posix_kill,
                "Send a signal to a process.",
                "($module, pid, signal, /)",
            },
            {
                "fork",
                posix_fork,
                "Fork a child process.",
                "($module, /)",
            },
            {
                "waitpid",
                posix_waitpid,
                "Wait for completion of a child process.",
                "($module, pid, options, /)",
            },
            {
                "_exit",
                posix_exit,
                "Exit the process immediately without cleanup.",
                "($module, status, /)",
            },
            {
                "pipe",
                posix_pipe,
                "Create a pipe and return (read_fd, write_fd).",
                "($module, /)",
            },
            {
                "close",
                posix_close,
                "Close a file descriptor.",
                "($module, fd, /)",
            },
            {
                "dup",
                posix_dup,
                "Duplicate a file descriptor.",
                "($module, fd, /)",
            },
            {
                "dup2",
                posix_dup2,
                "Duplicate a file descriptor to another descriptor number.",
                "($module, fd, fd2, /)",
            },
            {
                "open",
                posix_open,
                "Open a file and return a file descriptor.",
                "($module, path, flags, mode=0o777, /)",
            },
            {
                "read",
                posix_read,
                "Read bytes from a file descriptor.",
                "($module, fd, count, /)",
            },
            {
                "write",
                posix_write,
                "Write bytes to a file descriptor.",
                "($module, fd, data, /)",
            },
            {
                "system",
                posix_system,
                "Execute a command in a subshell and return its wait status.",
                "($module, command, /)",
            },
            {
                "execv",
                posix_execv,
                "Execute a path with an argument vector, replacing the current process.",
                "($module, path, argv, /)",
            },
            {
                "execve",
                posix_execve,
                "Execute a path with an argument vector and environment.",
                "($module, path, argv, env, /)",
            },
            {
                "execvp",
                posix_execvp,
                "Execute a file using PATH lookup, replacing the current process.",
                "($module, file, argv, /)",
            },
            {
                "execvpe",
                posix_execvpe,
                "Execute a file using PATH lookup and an explicit environment.",
                "($module, file, argv, env, /)",
            },
            {
                "WIFEXITED",
                posix_wifexited,
                "Return true if a wait status indicates normal process exit.",
                "($module, status, /)",
            },
            {
                "WEXITSTATUS",
                posix_wexitstatus,
                "Return the process exit status from a wait status.",
                "($module, status, /)",
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
        module.dict()->set(runtime.new_str("O_RDONLY"), runtime.new_int(O_RDONLY));
        module.dict()->set(runtime.new_str("O_WRONLY"), runtime.new_int(O_WRONLY));
        module.dict()->set(runtime.new_str("O_RDWR"), runtime.new_int(O_RDWR));
        module.dict()->set(runtime.new_str("O_CREAT"), runtime.new_int(O_CREAT));
        module.dict()->set(runtime.new_str("O_TRUNC"), runtime.new_int(O_TRUNC));
        module.dict()->set(runtime.new_str("O_APPEND"), runtime.new_int(O_APPEND));
        module.dict()->set(runtime.new_str("WNOHANG"), runtime.new_int(WNOHANG));
        module.dict()->set(runtime.new_str("P_WAIT"), runtime.new_int(0));
        module.dict()->set(runtime.new_str("P_NOWAIT"), runtime.new_int(1));
    }
}
