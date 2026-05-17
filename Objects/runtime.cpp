#include "cxxpython/runtime.hpp"

#include <algorithm>

namespace cxxpython
{
    Runtime::Runtime()
    {
        object_type_ = new_type("object");
        type_type_ = new_type("type");
        none_type_ = new_type("NoneType");
        int_type_ = new_type("int");
        bool_type_ = new_type("bool");
        str_type_ = new_type("str");
        tuple_type_ = new_type("tuple");
        dict_type_ = new_type("dict");

        object_type_->set_type(type_type_);
        type_type_->set_type(type_type_);
        none_type_->set_type(type_type_);
        int_type_->set_type(type_type_);
        bool_type_->set_type(type_type_);
        str_type_->set_type(type_type_);
        tuple_type_->set_type(type_type_);
        dict_type_->set_type(type_type_);

        object_type_->base = nullptr;
        object_type_->bases = {};
        object_type_->mro = {object_type_};

        initialize_type(type_type_, object_type_);
        initialize_type(none_type_, object_type_);
        initialize_type(int_type_, object_type_);
        initialize_type(bool_type_, int_type_);
        initialize_type(str_type_, object_type_);
        initialize_type(tuple_type_, object_type_);
        initialize_type(dict_type_, object_type_);

        none_ = allocate<NoneObject>(none_type_);
        true_ = allocate<BoolObject>(bool_type_, true);
        false_ = allocate<BoolObject>(bool_type_, false);

        add_root(object_type_);
        add_root(type_type_);
        add_root(none_type_);
        add_root(int_type_);
        add_root(bool_type_);
        add_root(str_type_);
        add_root(tuple_type_);
        add_root(dict_type_);
        add_root(none_);
        add_root(true_);
        add_root(false_);
    }

    TypeObject *Runtime::object_type() const noexcept
    {
        return object_type_;
    }

    TypeObject *Runtime::type_type() const noexcept
    {
        return type_type_;
    }

    TypeObject *Runtime::none_type() const noexcept
    {
        return none_type_;
    }

    TypeObject *Runtime::int_type() const noexcept
    {
        return int_type_;
    }

    TypeObject *Runtime::bool_type() const noexcept
    {
        return bool_type_;
    }

    TypeObject *Runtime::str_type() const noexcept
    {
        return str_type_;
    }

    TypeObject *Runtime::tuple_type() const noexcept
    {
        return tuple_type_;
    }

    TypeObject *Runtime::dict_type() const noexcept
    {
        return dict_type_;
    }

    NoneObject *Runtime::none() const noexcept
    {
        return none_;
    }

    BoolObject *Runtime::true_value() const noexcept
    {
        return true_;
    }

    BoolObject *Runtime::false_value() const noexcept
    {
        return false_;
    }

    IntObject *Runtime::new_int(std::int64_t value)
    {
        return allocate<IntObject>(int_type_, value);
    }

    StrObject *Runtime::new_str(std::string value)
    {
        return allocate<StrObject>(str_type_, std::move(value));
    }

    TupleObject *Runtime::new_tuple(std::vector<Object *> items)
    {
        return allocate<TupleObject>(tuple_type_, std::move(items));
    }

    DictObject *Runtime::new_dict()
    {
        return allocate<DictObject>(dict_type_);
    }

    void Runtime::add_root(Object *object)
    {
        if (!object)
            return;
        if (std::find(roots_.begin(), roots_.end(), object) == roots_.end())
            roots_.push_back(object);
    }

    void Runtime::remove_root(Object *object)
    {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), object), roots_.end());
    }

    void Runtime::visit_roots(const std::function<void(Object *)> &visitor) const
    {
        for (Object *root : roots_)
        {
            if (root)
                visitor(root);
        }
    }

    void Runtime::visit_heap(const std::function<void(Object *)> &visitor) const
    {
        for (const auto &object : heap_)
        {
            if (object)
                visitor(object.get());
        }
    }

    std::size_t Runtime::heap_size() const noexcept
    {
        return heap_.size();
    }

    std::size_t Runtime::root_count() const noexcept
    {
        return roots_.size();
    }

    TypeObject *Runtime::new_type(std::string name)
    {
        return allocate<TypeObject>(std::move(name));
    }

    void Runtime::initialize_type(TypeObject *type, TypeObject *base)
    {
        type->base = base;
        type->bases = base ? std::vector<TypeObject *>{base} : std::vector<TypeObject *>{};
        type->mro.clear();
        type->mro.push_back(type);
        if (base)
        {
            for (TypeObject *mro_item : base->mro)
                type->mro.push_back(mro_item);
        }
    }
}
