#include "cxxpython/object.hpp"

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

        std::string default_repr(const Object &object)
        {
            return "<" + type_qualified_name(object.type()) + " object at " + pointer_hex(&object) + ">";
        }

        std::int64_t default_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool default_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool default_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void default_traverse(const Object &object, const VisitChildren &visitor)
        {
            (void)object;
            (void)visitor;
        }

        std::string type_repr(const Object &object)
        {
            return "<class '" + type_qualified_name(static_cast<const TypeObject *>(&object)) + "'>";
        }

        void type_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *type = static_cast<const TypeObject *>(&object);
            if (type->base)
                visitor(type->base);
            for (TypeObject *item : type->bases)
            {
                if (item)
                    visitor(item);
            }
            for (TypeObject *item : type->mro)
            {
                if (item)
                    visitor(item);
            }
        }

        std::string none_repr(const Object &object)
        {
            (void)object;
            return "None";
        }

        std::int64_t none_hash(const Object &object)
        {
            (void)object;
            return 0;
        }

        bool none_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool none_truth(const Object &object)
        {
            (void)object;
            return false;
        }

        std::string int_repr(const Object &object)
        {
            return std::to_string(static_cast<const IntObject *>(&object)->value);
        }

        std::int64_t int_hash(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value;
        }

        bool int_equal(const Object &left, const Object &right)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            return right_int && static_cast<const IntObject *>(&left)->value == right_int->value;
        }

        bool int_truth(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0;
        }

        std::string bool_repr(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0 ? "True" : "False";
        }

        std::string str_repr(const Object &object)
        {
            return "'" + static_cast<const StrObject *>(&object)->value + "'";
        }

        std::int64_t str_hash(const Object &object)
        {
            return static_cast<std::int64_t>(std::hash<std::string>{}(static_cast<const StrObject *>(&object)->value));
        }

        bool str_equal(const Object &left, const Object &right)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            return right_str && static_cast<const StrObject *>(&left)->value == right_str->value;
        }

        bool str_truth(const Object &object)
        {
            return !static_cast<const StrObject *>(&object)->value.empty();
        }

        std::string tuple_repr(const Object &object)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            std::ostringstream out;
            out << "(";
            for (std::size_t i = 0; i < tuple->items().size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(tuple->items()[i]);
            }
            if (tuple->items().size() == 1)
                out << ",";
            out << ")";
            return out.str();
        }

        std::int64_t tuple_hash(const Object &object)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            std::uint64_t result = 0x345678UL;
            for (const Object *item : tuple->items())
                result = (result ^ static_cast<std::uint64_t>(object_hash(item))) * 1000003UL;
            result ^= tuple->items().size();
            return static_cast<std::int64_t>(result);
        }

        bool tuple_equal(const Object &left, const Object &right)
        {
            auto *left_tuple = static_cast<const TupleObject *>(&left);
            auto *right_tuple = dynamic_cast<const TupleObject *>(&right);
            if (!right_tuple || left_tuple->items().size() != right_tuple->items().size())
                return false;
            for (std::size_t i = 0; i < left_tuple->items().size(); ++i)
            {
                if (!object_equal(left_tuple->items()[i], right_tuple->items()[i]))
                    return false;
            }
            return true;
        }

        bool tuple_truth(const Object &object)
        {
            return static_cast<const TupleObject *>(&object)->size() != 0;
        }

        void tuple_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *tuple = static_cast<const TupleObject *>(&object);
            for (Object *item : tuple->items())
            {
                if (item)
                    visitor(item);
            }
        }

        std::string dict_repr(const Object &object)
        {
            auto *dict = static_cast<const DictObject *>(&object);
            std::ostringstream out;
            out << "{";
            const auto &entries = dict->entries();
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (i != 0)
                    out << ", ";
                out << object_repr(entries[i].key) << ": " << object_repr(entries[i].value);
            }
            out << "}";
            return out.str();
        }

        std::int64_t dict_hash(const Object &object)
        {
            (void)object;
            throw PythonError("TypeError", "unhashable type: 'dict'");
        }

        bool dict_equal(const Object &left, const Object &right)
        {
            auto *left_dict = static_cast<const DictObject *>(&left);
            auto *right_dict = dynamic_cast<const DictObject *>(&right);
            if (!right_dict || left_dict->entries().size() != right_dict->entries().size())
                return false;
            for (const DictObject::Entry &entry : left_dict->entries())
            {
                Object *right_value = right_dict->get(entry.key);
                if (!right_value || !object_equal(entry.value, right_value))
                    return false;
            }
            return true;
        }

        bool dict_truth(const Object &object)
        {
            return static_cast<const DictObject *>(&object)->size() != 0;
        }

        void dict_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *dict = static_cast<const DictObject *>(&object);
            for (const DictObject::Entry &entry : dict->entries())
            {
                if (entry.key)
                    visitor(entry.key);
                if (entry.value)
                    visitor(entry.value);
            }
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
        if (type_ && type_->slots.repr)
            return type_->slots.repr(*this);
        return default_repr(*this);
    }

    std::int64_t Object::hash() const
    {
        if (type_ && type_->slots.hash)
            return type_->slots.hash(*this);
        return default_hash(*this);
    }

    bool Object::equals(const Object &other) const
    {
        if (type_ && type_->slots.equal)
            return type_->slots.equal(*this, other);
        return default_equal(*this, other);
    }

    bool Object::truth() const
    {
        if (type_ && type_->slots.truth)
            return type_->slots.truth(*this);
        return default_truth(*this);
    }

    void Object::visit_children(const VisitChildren &visitor) const
    {
        if (type_ && type_->slots.traverse)
            type_->slots.traverse(*this, visitor);
        else
            default_traverse(*this, visitor);
    }

    TypeObject::TypeObject(std::string name)
        : Object(nullptr), name(std::move(name))
    {
    }

    NoneObject::NoneObject(TypeObject *type)
        : Object(type)
    {
    }

    IntObject::IntObject(TypeObject *type, std::int64_t value)
        : Object(type), value(value)
    {
    }

    BoolObject::BoolObject(TypeObject *type, bool value)
        : IntObject(type, value ? 1 : 0)
    {
    }

    StrObject::StrObject(TypeObject *type, std::string value)
        : Object(type), value(std::move(value))
    {
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

    bool is_true(const Object *object)
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

    TypeSlots make_object_slots()
    {
        return TypeSlots{default_repr, default_hash, default_equal, default_truth, default_traverse};
    }

    TypeSlots make_type_slots()
    {
        return TypeSlots{type_repr, default_hash, default_equal, default_truth, type_traverse};
    }

    TypeSlots make_none_slots()
    {
        return TypeSlots{none_repr, none_hash, none_equal, none_truth, default_traverse};
    }

    TypeSlots make_int_slots()
    {
        return TypeSlots{int_repr, int_hash, int_equal, int_truth, default_traverse};
    }

    TypeSlots make_bool_slots()
    {
        return TypeSlots{bool_repr, int_hash, int_equal, int_truth, default_traverse};
    }

    TypeSlots make_str_slots()
    {
        return TypeSlots{str_repr, str_hash, str_equal, str_truth, default_traverse};
    }

    TypeSlots make_tuple_slots()
    {
        return TypeSlots{tuple_repr, tuple_hash, tuple_equal, tuple_truth, tuple_traverse};
    }

    TypeSlots make_dict_slots()
    {
        return TypeSlots{dict_repr, dict_hash, dict_equal, dict_truth, dict_traverse};
    }
}
