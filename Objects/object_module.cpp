#include "cxxpython/object.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string module_repr(const Object &object)
        {
            auto *module = static_cast<const ModuleObject *>(&object);
            std::ostringstream out;
            out << "<module '" << module->name() << "'";
            if (!module->filename().empty())
                out << " from '" << module->filename() << "'";
            out << ">";
            return out.str();
        }

        std::int64_t module_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool module_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool module_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void module_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *module = static_cast<const ModuleObject *>(&object);
            if (module->dict())
                visitor(module->dict());
        }
    }

    ModuleObject::ModuleObject(TypeObject *type, std::string name, std::string filename, DictObject *dict)
        : Object(type), name_(std::move(name)), filename_(std::move(filename)), dict_(dict)
    {
        if (dict_)
            dict_->incref();
    }

    ModuleObject::~ModuleObject()
    {
        clear_references();
    }

    void ModuleObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(dict_);
        Object::clear_references();
    }

    const std::string &ModuleObject::name() const noexcept
    {
        return name_;
    }

    const std::string &ModuleObject::filename() const noexcept
    {
        return filename_;
    }

    DictObject *ModuleObject::dict() const noexcept
    {
        return dict_;
    }

    TypeSlots make_module_slots()
    {
        return TypeSlots{
            .repr = module_repr,
            .hash = module_hash,
            .equal = module_equal,
            .truth = module_truth,
            .traverse = module_traverse,
        };
    }
}
