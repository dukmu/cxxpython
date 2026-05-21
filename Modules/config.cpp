#include "cxxpython/runtime.hpp"
#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

extern char **environ;

namespace cxxpython
{
    namespace
    {
        bool is_identifier(std::string_view text)
        {
            if (text.empty())
                return false;
            const unsigned char first = static_cast<unsigned char>(text.front());
            if (!(std::isalpha(first) || text.front() == '_'))
                return false;
            for (char ch : text)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                if (!(std::isalnum(value) || ch == '_'))
                    return false;
            }
            return true;
        }

        std::string active_module_name(Runtime &runtime)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            DictObject *globals = frame ? frame->globals() : nullptr;
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            return "__main__";
        }

        Object *sys_getframe(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_getframe", args);
            require_positional_at_most(&runtime, "_getframe", args, 1);
            std::size_t depth = 0;
            if (!args.positional.empty())
            {
                auto *value = dynamic_cast<IntObject *>(args.positional[0]);
                if (!value)
                    raise_type_error(&runtime, "_getframe() depth must be int");
                if (value->value < 0)
                    raise_value_error(&runtime, "_getframe() depth must be non-negative");
                depth = static_cast<std::size_t>(value->value);
            }
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().frame_at_depth(depth) : nullptr;
            if (!frame)
                raise_value_error(&runtime, "call stack is not deep enough");
            return frame;
        }

        Object *gc_collect(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "collect", args);
            require_positional_at_most(&runtime, "collect", args, 1);
            if (args.positional.size() == 1 && !dynamic_cast<IntObject *>(args.positional[0]))
                raise_type_error(&runtime, "collect() generation must be int");
            return runtime.new_int(static_cast<std::int64_t>(runtime.collect_garbage()));
        }

        Object *gc_get_count(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_count", args);
            require_positional_count(&runtime, "get_count", args, 0);
            return runtime.new_tuple({
                runtime.new_int(static_cast<std::int64_t>(runtime.heap_size())),
                runtime.new_int(static_cast<std::int64_t>(runtime.root_count())),
                runtime.new_int(0),
            });
        }

        Object *gc_get_objects(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "get_objects", args);
            require_positional_count(&runtime, "get_objects", args, 0);
            std::vector<Object *> objects;
            runtime.visit_heap([&objects](Object *object) {
                if (object)
                    objects.push_back(object);
            });
            return runtime.new_list(std::move(objects));
        }

        Object *gc_isenabled(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "isenabled", args);
            require_positional_count(&runtime, "isenabled", args, 0);
            return runtime.true_value();
        }

        Object *gc_enable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "enable", args);
            require_positional_count(&runtime, "enable", args, 0);
            return runtime.none();
        }

        Object *gc_disable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "disable", args);
            require_positional_count(&runtime, "disable", args, 0);
            return runtime.none();
        }

        Object *sys_implementation(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "implementation", args);
            require_positional_count(&runtime, "implementation", args, 0);
            DictObject *implementation = runtime.new_dict();
            implementation->set(runtime.new_str("name"), runtime.new_str("cxxpython"));
            implementation->set(runtime.new_str("version"), runtime.new_str("0.1.0-dev"));
            return implementation;
        }

        Object *sys_argv(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "argv", args);
            require_positional_count(&runtime, "argv", args, 0);
            std::vector<Object *> argv;
            argv.reserve(runtime.program_arguments().size());
            for (const std::string &arg : runtime.program_arguments())
                argv.push_back(runtime.new_str(arg));
            return runtime.new_list(std::move(argv));
        }

        Object *sys_path(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "path", args);
            require_positional_count(&runtime, "path", args, 0);
            std::vector<Object *> path;
            path.reserve(runtime.module_search_paths().size());
            for (const std::string &entry : runtime.module_search_paths())
                path.push_back(runtime.new_str(entry));
            return runtime.new_list(std::move(path));
        }

        Object *sys_modules(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "modules", args);
            require_positional_count(&runtime, "modules", args, 0);
            return runtime.modules_dict();
        }

        Object *sys_builtin_module_names(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "builtin_module_names", args);
            require_positional_count(&runtime, "builtin_module_names", args, 0);
            std::vector<Object *> names{
                runtime.new_str("_io"),
                runtime.new_str("_collections"),
                runtime.new_str("builtins"),
                runtime.new_str("gc"),
                runtime.new_str("posix"),
                runtime.new_str("sys"),
            };
            return runtime.new_tuple(std::move(names));
        }

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

        Object *io_stdin(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stdin", args);
            require_positional_count(&runtime, "_stdin", args, 0);
            return runtime.stdin_object();
        }

        Object *io_stdout(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stdout", args);
            require_positional_count(&runtime, "_stdout", args, 0);
            return runtime.stdout_object();
        }

        Object *io_stderr(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stderr", args);
            require_positional_count(&runtime, "_stderr", args, 0);
            return runtime.stderr_object();
        }

        Object *io_open(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "open", args);
            if (args.positional.empty() || args.positional.size() > 2)
                raise_type_error(&runtime, "open() expected 1 or 2 positional arguments");
            auto *path = dynamic_cast<StrObject *>(args.positional[0]);
            if (!path)
                raise_type_error(&runtime, "open() path must be str");
            std::string mode = "r";
            if (args.positional.size() == 2)
            {
                auto *mode_text = dynamic_cast<StrObject *>(args.positional[1]);
                if (!mode_text)
                    raise_type_error(&runtime, "open() mode must be str");
                mode = mode_text->value;
            }
            if (mode != "r" && mode != "w" && mode != "a")
                raise_value_error(&runtime, "open() only supports text modes 'r', 'w', and 'a'");

            std::ios::openmode open_mode = std::ios::in;
            if (mode == "w")
                open_mode = std::ios::out | std::ios::trunc;
            else if (mode == "a")
                open_mode = std::ios::out | std::ios::app;

            auto stream = std::make_unique<std::fstream>(path->value, open_mode);
            if (!stream->is_open())
                throw PythonError(runtime.new_os_error("open() failed for '" + path->value + "'"));
            std::fstream *raw = stream.get();
            std::istream *input = mode == "r" ? raw : nullptr;
            std::ostream *output = mode == "r" ? nullptr : raw;
            return runtime.new_text_io(path->value, input, output, true, std::move(stream));
        }

        std::vector<std::string> parse_field_names_from_string(const std::string &raw)
        {
            std::vector<std::string> fields;
            std::string token;
            for (char ch : raw)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                if (ch == ',' || std::isspace(value))
                {
                    if (!token.empty())
                    {
                        fields.push_back(token);
                        token.clear();
                    }
                    continue;
                }
                token.push_back(ch);
            }
            if (!token.empty())
                fields.push_back(token);
            return fields;
        }

        std::vector<std::string> namedtuple_fields(Runtime &runtime, Object *field_names)
        {
            std::vector<std::string> fields;
            if (auto *text = dynamic_cast<StrObject *>(field_names))
            {
                fields = parse_field_names_from_string(text->value);
            }
            else
            {
                for (Object *item : collect_iterable_items(&runtime, field_names))
                {
                    auto item_root = runtime.root(item);
                    auto *name = dynamic_cast<StrObject *>(item_root.get());
                    if (!name)
                        raise_type_error(&runtime, "namedtuple() field names must be strings");
                    fields.push_back(name->value);
                }
            }
            if (fields.empty())
                raise_value_error(&runtime, "namedtuple() requires at least one field");
            return fields;
        }

        std::size_t namedtuple_field_count(TypeObject &type)
        {
            Runtime *runtime = type.runtime();
            if (!runtime || !type.dict)
                raise_type_error(runtime, "namedtuple type metadata is missing");
            auto *count = dynamic_cast<IntObject *>(type.dict->get(runtime->new_str("__namedtuple_field_count__")));
            if (!count || count->value < 0)
                raise_type_error(runtime, "namedtuple type metadata is invalid");
            return static_cast<std::size_t>(count->value);
        }

        void validate_namedtuple_ctor(TypeObject &type, const CallArguments &args)
        {
            Runtime *runtime = type.runtime();
            require_no_keywords(runtime, type.name, args);
            const std::size_t expected = namedtuple_field_count(type);
            if (args.positional.size() == expected)
                return;

            std::ostringstream message;
            message << type.name << "() takes exactly " << expected
                    << (expected == 1 ? " argument" : " arguments")
                    << " (" << args.positional.size() << " given)";
            raise_type_error(runtime, message.str());
        }

        Object *namedtuple_new(TypeObject &type, const CallArguments &args)
        {
            validate_namedtuple_ctor(type, args);
            return type.runtime()->allocate<TupleObject>(&type, args.positional);
        }

        void namedtuple_init(Object &object, const CallArguments &args)
        {
            TypeObject *type = object.type();
            if (!type)
                raise_type_error(object.runtime(), "namedtuple instance has no type");
            validate_namedtuple_ctor(*type, args);
        }

        void validate_namedtuple_identifiers(Runtime &runtime,
                                             const std::string &type_name,
                                             const std::vector<std::string> &fields)
        {
            if (!is_identifier(type_name))
                raise_value_error(&runtime, "namedtuple() typename must be a valid identifier");

            std::unordered_set<std::string> seen;
            for (const std::string &field : fields)
            {
                if (!is_identifier(field))
                    raise_value_error(&runtime, "namedtuple() field names must be valid identifiers");
                if (!seen.insert(field).second)
                    raise_value_error(&runtime, "namedtuple() field names must be unique");
            }
        }

        TypeObject *build_namedtuple_type(Runtime &runtime,
                                          const std::string &type_name,
                                          const std::vector<std::string> &fields)
        {
            DictObject *namespace_dict = runtime.new_dict();
            auto namespace_root = runtime.root(namespace_dict);
            TypeObject *type = runtime.new_class(
                type_name,
                std::vector<TypeObject *>{runtime.tuple_type()},
                namespace_root.get(),
                active_module_name(runtime));
            auto type_root = runtime.root(type);

            type_root->slots.new_object = namedtuple_new;
            type_root->slots.init = namedtuple_init;
            type_root->dict->set(runtime.new_str("__namedtuple_field_count__"),
                                 runtime.new_int(static_cast<std::int64_t>(fields.size())));

            std::vector<Object *> field_objects;
            std::vector<Rooted<StrObject>> field_roots;
            field_objects.reserve(fields.size());
            field_roots.reserve(fields.size());
            for (const std::string &field : fields)
            {
                StrObject *text = runtime.new_str(field);
                field_roots.push_back(runtime.root(text));
                field_objects.push_back(text);
            }

            TupleObject *field_tuple = runtime.new_tuple(field_objects);
            auto field_tuple_root = runtime.root(field_tuple);
            type_root->dict->set(runtime.new_str("_fields"), field_tuple_root.get());
            type_root->dict->set(runtime.new_str("__match_args__"), field_tuple_root.get());

            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                const std::string field_name = fields[index];
                GetSetGetter getter = [index, field_name](Runtime &getter_runtime, Object *instance, TypeObject *) -> Object * {
                    auto *tuple = dynamic_cast<TupleObject *>(instance);
                    if (!tuple)
                        raise_type_error(&getter_runtime, "namedtuple field receiver does not have tuple layout");
                    if (index >= tuple->size())
                        raise_attribute_error(&getter_runtime, "'" + type_qualified_name(instance->type()) +
                                                                  "' object has no attribute '" + field_name + "'");
                    return tuple->items()[index];
                };
                GetSetDescriptorObject *descriptor =
                    runtime.new_getset_descriptor(field_name, type_root.get(), std::move(getter), nullptr);
                type_root->dict->set(runtime.new_str(field_name), descriptor);
            }

            return type_root.get();
        }

        Object *collections_namedtuple(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "namedtuple", args);
            require_positional_count(&runtime, "namedtuple", args, 2);
            auto *typename_object = dynamic_cast<StrObject *>(args.positional[0]);
            if (!typename_object)
                raise_type_error(&runtime, "namedtuple() typename must be str");

            std::vector<std::string> fields = namedtuple_fields(runtime, args.positional[1]);
            validate_namedtuple_identifiers(runtime, typename_object->value, fields);
            return build_namedtuple_type(runtime, typename_object->value, fields);
        }
    }

    ModuleObject *Runtime::import_stdlib_module(std::string_view name)
    {
        if (name != "gc" &&
            name != "_collections" &&
            name != "_io" &&
            name != "sys" &&
            name != "posix")
            return nullptr;

        ModuleObject *module = new_module(std::string(name), "<stdlib>");
        DictObject *dict = module->dict();

        if (name == "gc")
        {
            dict->set(new_str("collect"), new_native_function("collect", gc_collect));
            dict->set(new_str("get_count"), new_native_function("get_count", gc_get_count));
            dict->set(new_str("get_objects"), new_native_function("get_objects", gc_get_objects));
            dict->set(new_str("isenabled"), new_native_function("isenabled", gc_isenabled));
            dict->set(new_str("enable"), new_native_function("enable", gc_enable));
            dict->set(new_str("disable"), new_native_function("disable", gc_disable));
            return module;
        }

        if (name == "sys")
        {
            DictObject *implementation = new_dict();
            implementation->set(new_str("name"), new_str("cxxpython"));
            implementation->set(new_str("version"), new_str("0.1.0-dev"));
            dict->set(new_str("implementation"), implementation);
            dict->set(new_str("name"), new_str("cxxpython"));
            dict->set(new_str("version"), new_str("0.1.0-dev"));
            dict->set(new_str("argv"), sys_argv(*this, {{}, nullptr}));
            dict->set(new_str("path"), sys_path(*this, {{}, nullptr}));
            dict->set(new_str("modules"), modules_dict());
            dict->set(new_str("builtin_module_names"), sys_builtin_module_names(*this, {{}, nullptr}));
            dict->set(new_str("stdin"), io_stdin(*this, {{}, nullptr}));
            dict->set(new_str("stdout"), io_stdout(*this, {{}, nullptr}));
            dict->set(new_str("stderr"), io_stderr(*this, {{}, nullptr}));
            dict->set(new_str("_implementation"), new_native_function("_implementation", sys_implementation));
            dict->set(new_str("_argv"), new_native_function("_argv", sys_argv));
            dict->set(new_str("_path"), new_native_function("_path", sys_path));
            dict->set(new_str("_modules"), new_native_function("_modules", sys_modules));
            dict->set(new_str("_builtin_module_names"),
                      new_native_function("_builtin_module_names", sys_builtin_module_names));
            dict->set(new_str("_getframe"), new_native_function("_getframe", sys_getframe));
            return module;
        }

        if (name == "_io")
        {
            dict->set(new_str("TextIOWrapper"), text_io_type());
            dict->set(new_str("_stdin"), new_native_function("_stdin", io_stdin));
            dict->set(new_str("_stdout"), new_native_function("_stdout", io_stdout));
            dict->set(new_str("_stderr"), new_native_function("_stderr", io_stderr));
            dict->set(new_str("open"), new_native_function("open", io_open));
            return module;
        }

        if (name == "posix")
        {
            dict->set(new_str("getcwd"), new_native_function("getcwd", posix_getcwd));
            dict->set(new_str("listdir"), new_native_function("listdir", posix_listdir));
            dict->set(new_str("environ"), new_native_function("environ", posix_environ));
            return module;
        }

        dict->set(new_str("namedtuple"),
                  new_native_function("namedtuple", collections_namedtuple));
        return module;
    }
}
