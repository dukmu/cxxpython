#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

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

    TypeSlots make_exception_slots()
    {
        return TypeSlots{
            .repr = exception_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = exception_traverse,
        };
    }

    void raise_stop_iteration(Runtime *runtime)
    {
        throw PythonError(runtime ? runtime->new_stop_iteration() : nullptr);
    }
}
