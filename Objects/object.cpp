#include "cxxpython/object.hpp"

#include <functional>
#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string pointer_hex(const void *ptr)
        {
            std::ostringstream out;
            out << ptr;
            return out.str();
        }
    }

    PythonError::PythonError(std::string type_name, std::string message)
        : type_name_(std::move(type_name)), message_(std::move(message))
    {
    }

    const std::string &PythonError::type_name() const noexcept
    {
        return type_name_;
    }

    const char *PythonError::what() const noexcept
    {
        return message_.c_str();
    }

    Object::Object(TypeObject *type)
        : type_(type)
    {
    }

    TypeObject *Object::type() const noexcept
    {
        return type_;
    }

    void Object::set_type(TypeObject *type) noexcept
    {
        type_ = type;
    }

    std::string Object::repr() const
    {
        return "<" + type_qualified_name(type_) + " object at " + pointer_hex(this) + ">";
    }

    std::int64_t Object::hash() const
    {
        return reinterpret_cast<std::intptr_t>(this);
    }

    bool Object::equals(const Object &other) const
    {
        return this == &other;
    }

    bool Object::truth() const noexcept
    {
        return true;
    }

    void Object::visit_children(const std::function<void(Object *)> &visitor) const
    {
        (void)visitor;
    }

    TypeObject::TypeObject(std::string name)
        : Object(nullptr), name(std::move(name))
    {
    }

    std::string TypeObject::repr() const
    {
        return "<class '" + type_qualified_name(this) + "'>";
    }

    std::int64_t TypeObject::hash() const
    {
        return reinterpret_cast<std::intptr_t>(this);
    }

    bool TypeObject::equals(const Object &other) const
    {
        return this == &other;
    }

    bool TypeObject::truth() const noexcept
    {
        return true;
    }

    void TypeObject::visit_children(const std::function<void(Object *)> &visitor) const
    {
        if (base)
            visitor(base);
        for (TypeObject *item : bases)
        {
            if (item)
                visitor(item);
        }
        for (TypeObject *item : mro)
        {
            if (item)
                visitor(item);
        }
    }

    NoneObject::NoneObject(TypeObject *type)
        : Object(type)
    {
    }

    std::string NoneObject::repr() const
    {
        return "None";
    }

    std::int64_t NoneObject::hash() const
    {
        return 0;
    }

    bool NoneObject::equals(const Object &other) const
    {
        return this == &other;
    }

    bool NoneObject::truth() const noexcept
    {
        return false;
    }

    IntObject::IntObject(TypeObject *type, std::int64_t value)
        : Object(type), value(value)
    {
    }

    std::string IntObject::repr() const
    {
        return std::to_string(value);
    }

    std::int64_t IntObject::hash() const
    {
        return value;
    }

    bool IntObject::equals(const Object &other) const
    {
        auto *other_int = dynamic_cast<const IntObject *>(&other);
        return other_int && value == other_int->value;
    }

    bool IntObject::truth() const noexcept
    {
        return value != 0;
    }

    BoolObject::BoolObject(TypeObject *type, bool value)
        : IntObject(type, value ? 1 : 0)
    {
    }

    bool BoolObject::truth() const noexcept
    {
        return value != 0;
    }

    std::string BoolObject::repr() const
    {
        return truth() ? "True" : "False";
    }

    StrObject::StrObject(TypeObject *type, std::string value)
        : Object(type), value(std::move(value))
    {
    }

    std::string StrObject::repr() const
    {
        return "'" + value + "'";
    }

    std::int64_t StrObject::hash() const
    {
        return static_cast<std::int64_t>(std::hash<std::string>{}(value));
    }

    bool StrObject::equals(const Object &other) const
    {
        auto *other_str = dynamic_cast<const StrObject *>(&other);
        return other_str && value == other_str->value;
    }

    bool StrObject::truth() const noexcept
    {
        return !value.empty();
    }

    TupleObject::TupleObject(TypeObject *type, std::vector<Object *> items)
        : Object(type), items_(std::move(items))
    {
    }

    const std::vector<Object *> &TupleObject::items() const noexcept
    {
        return items_;
    }

    std::size_t TupleObject::size() const noexcept
    {
        return items_.size();
    }

    std::string TupleObject::repr() const
    {
        std::ostringstream out;
        out << "(";
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (i != 0)
                out << ", ";
            out << object_repr(items_[i]);
        }
        if (items_.size() == 1)
            out << ",";
        out << ")";
        return out.str();
    }

    std::int64_t TupleObject::hash() const
    {
        std::uint64_t result = 0x345678UL;
        for (const Object *item : items_)
            result = (result ^ static_cast<std::uint64_t>(object_hash(item))) * 1000003UL;
        result ^= items_.size();
        return static_cast<std::int64_t>(result);
    }

    bool TupleObject::equals(const Object &other) const
    {
        auto *other_tuple = dynamic_cast<const TupleObject *>(&other);
        if (!other_tuple || items_.size() != other_tuple->items_.size())
            return false;
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (!object_equal(items_[i], other_tuple->items_[i]))
                return false;
        }
        return true;
    }

    bool TupleObject::truth() const noexcept
    {
        return !items_.empty();
    }

    void TupleObject::visit_children(const std::function<void(Object *)> &visitor) const
    {
        for (Object *item : items_)
        {
            if (item)
                visitor(item);
        }
    }

    DictObject::DictObject(TypeObject *type)
        : Object(type)
    {
    }

    bool DictObject::contains(Object *key) const
    {
        return get(key) != nullptr;
    }

    Object *DictObject::get(Object *key) const
    {
        if (!key)
            throw PythonError("TypeError", "dict key cannot be null");

        const std::int64_t key_hash = object_hash(key);
        for (const Entry &entry : entries_)
        {
            if (entry.hash == key_hash && object_equal(entry.key, key))
                return entry.value;
        }
        return nullptr;
    }

    void DictObject::set(Object *key, Object *value)
    {
        if (!key)
            throw PythonError("TypeError", "dict key cannot be null");
        if (!value)
            throw PythonError("TypeError", "dict value cannot be null");

        const std::int64_t key_hash = object_hash(key);
        for (Entry &entry : entries_)
        {
            if (entry.hash == key_hash && object_equal(entry.key, key))
            {
                entry.value = value;
                return;
            }
        }
        entries_.push_back(Entry{key, value, key_hash});
    }

    std::size_t DictObject::size() const noexcept
    {
        return entries_.size();
    }

    const std::vector<DictObject::Entry> &DictObject::entries() const noexcept
    {
        return entries_;
    }

    std::string DictObject::repr() const
    {
        std::ostringstream out;
        out << "{";
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            if (i != 0)
                out << ", ";
            out << object_repr(entries_[i].key) << ": " << object_repr(entries_[i].value);
        }
        out << "}";
        return out.str();
    }

    std::int64_t DictObject::hash() const
    {
        throw PythonError("TypeError", "unhashable type: 'dict'");
    }

    bool DictObject::equals(const Object &other) const
    {
        auto *other_dict = dynamic_cast<const DictObject *>(&other);
        if (!other_dict || entries_.size() != other_dict->entries_.size())
            return false;
        for (const Entry &entry : entries_)
        {
            Object *other_value = other_dict->get(entry.key);
            if (!other_value || !object_equal(entry.value, other_value))
                return false;
        }
        return true;
    }

    bool DictObject::truth() const noexcept
    {
        return !entries_.empty();
    }

    void DictObject::visit_children(const std::function<void(Object *)> &visitor) const
    {
        for (const Entry &entry : entries_)
        {
            if (entry.key)
                visitor(entry.key);
            if (entry.value)
                visitor(entry.value);
        }
    }

    bool is_subclass(const TypeObject *derived, const TypeObject *base) noexcept
    {
        if (!derived || !base)
            return false;
        if (derived == base)
            return true;
        for (const TypeObject *type : derived->mro)
        {
            if (type == base)
                return true;
        }
        return false;
    }

    bool is_instance(const Object *object, const TypeObject *type) noexcept
    {
        return object && type && is_subclass(object->type(), type);
    }

    bool is_true(const Object *object) noexcept
    {
        return object && object->truth();
    }

    std::int64_t object_hash(const Object *object)
    {
        if (!object)
            throw PythonError("TypeError", "hash() argument cannot be null");
        return object->hash();
    }

    bool object_equal(const Object *left, const Object *right)
    {
        if (left == right)
            return true;
        if (!left || !right)
            return false;
        return left->equals(*right);
    }

    std::string type_qualified_name(const TypeObject *type)
    {
        if (!type)
            return "<null>";
        if (type->module.empty() || type->module == "builtins")
            return type->name;
        return type->module + "." + type->name;
    }

    std::string object_repr(const Object *object)
    {
        if (!object)
            return "<null>";
        return object->repr();
    }
}
