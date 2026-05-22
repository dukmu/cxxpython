#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

namespace cxxpython
{
    namespace
    {
        std::string exception_args_repr(const TupleObject *args)
        {
            std::string result;
            if (!args)
                return result;
            for (std::size_t i = 0; i < args->items().size(); ++i)
            {
                if (i != 0)
                    result += ", ";
                result += object_repr(args->items()[i]);
            }
            return result;
        }

        std::string exception_repr(const Object &object)
        {
            auto *error = static_cast<const ExceptionObject *>(&object);
            return type_qualified_name(error->type()) + "(" + exception_args_repr(error->args()) + ")";
        }

        void exception_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *error = static_cast<const ExceptionObject *>(&object);
            if (error->args())
                visitor(error->args());
        }

        ExceptionObject *require_exception(Runtime &runtime, Object *instance, const char *name)
        {
            auto *error = dynamic_cast<ExceptionObject *>(instance);
            if (!error)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have exception layout");
            return error;
        }

        Object *exception_args_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            ExceptionObject *error = require_exception(runtime, instance, "args");
            return error->args() ? static_cast<Object *>(error->args()) : static_cast<Object *>(runtime.new_tuple({}));
        }

        Object *exception_traceback_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            ExceptionObject *error = require_exception(runtime, instance, "__traceback__");
            std::vector<Object *> entries;
            entries.reserve(error->traceback_entries().size());
            std::vector<Rooted<Object>> roots;
            for (const ExceptionObject::TracebackEntry &entry : error->traceback_entries())
            {
                Object *filename = runtime.new_str(entry.filename);
                Object *line = runtime.new_int(static_cast<std::int64_t>(entry.line));
                Object *name = runtime.new_str(entry.name);
                roots.push_back(runtime.root(filename));
                roots.push_back(runtime.root(line));
                roots.push_back(runtime.root(name));
                Object *tuple = runtime.new_tuple({filename, line, name});
                roots.push_back(runtime.root(tuple));
                entries.push_back(tuple);
            }
            return runtime.new_list(std::move(entries));
        }

        Object *exception_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), type_qualified_name(&type), args);
            return type.runtime()->new_exception(&type, args.positional);
        }

        void exception_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), type_qualified_name(object.type()), args);
        }
    }

    ExceptionObject::ExceptionObject(TypeObject *type, TupleObject *args, std::string message)
        : Object(type), args_(args), message_(std::move(message))
    {
    }

    TupleObject *ExceptionObject::args() const noexcept
    {
        return args_;
    }

    const std::string &ExceptionObject::message() const noexcept
    {
        return message_;
    }

    bool ExceptionObject::has_source_location() const noexcept
    {
        return !source_filename_.empty() && source_line_ != 0;
    }

    const std::string &ExceptionObject::source_filename() const noexcept
    {
        return source_filename_;
    }

    const std::string &ExceptionObject::source_name() const noexcept
    {
        return source_name_;
    }

    std::uint32_t ExceptionObject::source_line() const noexcept
    {
        return source_line_;
    }

    void ExceptionObject::set_source_location(std::string filename, std::uint32_t line, std::string name)
    {
        source_filename_ = std::move(filename);
        source_name_ = std::move(name);
        source_line_ = line;
    }

    void ExceptionObject::add_traceback_entry(std::string filename, std::uint32_t line, std::string name)
    {
        if (line == 0)
            return;
        if (!traceback_.empty())
        {
            const TracebackEntry &last = traceback_.back();
            if (last.filename == filename && last.line == line && last.name == name)
                return;
        }
        if (!has_source_location())
            set_source_location(filename, line, name);
        traceback_.push_back(TracebackEntry{
            .filename = std::move(filename),
            .line = line,
            .name = std::move(name),
        });
    }

    const std::vector<ExceptionObject::TracebackEntry> &ExceptionObject::traceback_entries() const noexcept
    {
        return traceback_;
    }

    TypeSlots make_exception_slots()
    {
        return TypeSlots{
            .repr = exception_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = exception_traverse,
            .new_object = exception_new,
            .init = exception_init,
        };
    }

    void initialize_exception_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("args"),
                        runtime.new_getset_descriptor("args",
                                                      type,
                                                      exception_args_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__traceback__"),
                        runtime.new_getset_descriptor("__traceback__",
                                                      type,
                                                      exception_traceback_getter,
                                                      nullptr));
    }

    void raise_stop_iteration(Runtime *runtime)
    {
        throw PythonError(runtime ? runtime->new_stop_iteration() : nullptr);
    }
}
