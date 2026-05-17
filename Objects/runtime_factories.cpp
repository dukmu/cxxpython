#include "cxxpython/runtime.hpp"

namespace cxxpython
{
    IntObject *Runtime::new_int(std::int64_t value)
    {
        return allocate<IntObject>(int_type_, value);
    }

    StrObject *Runtime::new_str(std::string value)
    {
        return allocate<StrObject>(str_type_, std::move(value));
    }

    ListObject *Runtime::new_list(std::vector<Object *> items)
    {
        return allocate<ListObject>(list_type_, std::move(items));
    }

    TupleObject *Runtime::new_tuple(std::vector<Object *> items)
    {
        return allocate<TupleObject>(tuple_type_, std::move(items));
    }

    DictObject *Runtime::new_dict()
    {
        return allocate<DictObject>(dict_type_);
    }

    TypeObject *Runtime::new_heap_type(std::string name, TypeObject *base, TypeSlots slots)
    {
        TypeObject *type = allocate_type(std::move(name));
        type->set_type(type_type_);
        TypeObject *resolved_base = base ? base : object_type_;
        type->slots = inherit_slots(resolved_base, slots);
        initialize_type(type, resolved_base);
        return type;
    }

    ExceptionObject *Runtime::new_exception(TypeObject *type, std::string message)
    {
        TypeObject *resolved_type = type ? type : exception_type_;
        return allocate<ExceptionObject>(resolved_type, std::move(message));
    }

    ExceptionObject *Runtime::new_type_error(std::string message)
    {
        return new_exception(type_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_index_error(std::string message)
    {
        return new_exception(index_error_type_, std::move(message));
    }
}
