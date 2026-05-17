#include "cxxpython/object.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        std::string exception_repr(const Object &object)
        {
            auto *error = static_cast<const ExceptionObject *>(&object);
            return type_qualified_name(error->type()) + "('" + escape_single_quoted(error->message()) + "')";
        }
    }

    ExceptionObject::ExceptionObject(TypeObject *type, std::string message)
        : Object(type), message_(std::move(message))
    {
    }

    const std::string &ExceptionObject::message() const noexcept
    {
        return message_;
    }

    TypeSlots make_exception_slots()
    {
        return TypeSlots{exception_repr, default_hash, default_equal, default_truth, default_traverse};
    }
}
