#include "cxxpython/object.hpp"

#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string cell_repr(const Object &object)
        {
            auto *cell = static_cast<const CellObject *>(&object);
            std::ostringstream out;
            out << "<cell at " << &object;
            if (cell->is_empty())
                out << ": empty";
            else
                out << ": " << object_repr(cell->value());
            out << ">";
            return out.str();
        }

        void cell_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *cell = static_cast<const CellObject *>(&object);
            if (cell->value())
                visitor(cell->value());
        }
    }

    CellObject::CellObject(TypeObject *type, Object *value)
        : Object(type), value_(value)
    {
    }

    Object *CellObject::value() const noexcept
    {
        return value_;
    }

    void CellObject::set_value(Object *value) noexcept
    {
        value_ = value;
    }

    bool CellObject::is_empty() const noexcept
    {
        return value_ == nullptr;
    }

    TypeSlots make_cell_slots()
    {
        return TypeSlots{
            .repr = cell_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = cell_traverse,
        };
    }
}
