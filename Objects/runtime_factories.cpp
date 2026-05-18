#include "cxxpython/runtime.hpp"

#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

namespace cxxpython
{
    namespace
    {
        std::string exception_message_from_args(const TupleObject *args)
        {
            if (!args || args->size() == 0)
                return "";
            if (args->size() == 1)
            {
                if (auto *message = dynamic_cast<StrObject *>(args->items()[0]))
                    return message->value;
                return object_repr(args->items()[0]);
            }
            return object_repr(args);
        }
    }

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

    CodeObject *Runtime::new_code(std::vector<Object *> constants, std::vector<Instruction> instructions)
    {
        return allocate<CodeObject>(code_type_, std::move(constants), std::move(instructions));
    }

    Frame *Runtime::new_frame(CodeObject *code)
    {
        return allocate<Frame>(frame_type_, code);
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
        return new_exception(type, {new_str(std::move(message))});
    }

    ExceptionObject *Runtime::new_exception(TypeObject *type, std::vector<Object *> args)
    {
        TypeObject *resolved_type = type ? type : exception_type_;
        TupleObject *args_tuple = new_tuple(std::move(args));
        return allocate<ExceptionObject>(resolved_type, args_tuple, exception_message_from_args(args_tuple));
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
