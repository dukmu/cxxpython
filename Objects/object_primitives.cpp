#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
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

        [[noreturn]] void raise_unsupported_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            raise_type_error(left.runtime(), unsupported_binary_op_message(op, &left, &right));
        }

        Object *int_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            if (!right_int)
                raise_unsupported_binary_op(left, right, op);
            auto *left_int = static_cast<const IntObject *>(&left);
            switch (op)
            {
            case BinaryOp::Add:
                return left.runtime()->new_int(left_int->value + right_int->value);
            case BinaryOp::Subtract:
                return left.runtime()->new_int(left_int->value - right_int->value);
            case BinaryOp::Multiply:
                return left.runtime()->new_int(left_int->value * right_int->value);
            case BinaryOp::TrueDivide:
            case BinaryOp::FloorDivide:
            case BinaryOp::Modulo:
            case BinaryOp::Power:
                break;
            }
            raise_unsupported_binary_op(left, right, op);
        }

        std::string bool_repr(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0 ? "True" : "False";
        }

        std::string str_repr(const Object &object)
        {
            return "'" + escape_single_quoted(static_cast<const StrObject *>(&object)->value) + "'";
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

        Object *str_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            if (!right_str || op != BinaryOp::Add)
                raise_unsupported_binary_op(left, right, op);
            auto *left_str = static_cast<const StrObject *>(&left);
            return left.runtime()->new_str(left_str->value + right_str->value);
        }
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

    TypeSlots make_none_slots()
    {
        return TypeSlots{
            .repr = none_repr,
            .hash = none_hash,
            .equal = none_equal,
            .truth = none_truth,
            .traverse = default_traverse,
        };
    }

    TypeSlots make_int_slots()
    {
        return TypeSlots{
            .repr = int_repr,
            .hash = int_hash,
            .equal = int_equal,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
        };
    }

    TypeSlots make_bool_slots()
    {
        return TypeSlots{
            .repr = bool_repr,
            .hash = int_hash,
            .equal = int_equal,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
        };
    }

    TypeSlots make_str_slots()
    {
        return TypeSlots{
            .repr = str_repr,
            .hash = str_hash,
            .equal = str_equal,
            .truth = str_truth,
            .traverse = default_traverse,
            .binary_op = str_binary_op,
        };
    }
}
