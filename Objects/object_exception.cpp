#include "cxxpython/object.hpp"

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
}
