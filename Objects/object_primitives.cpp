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

        std::string not_implemented_repr(const Object &object)
        {
            (void)object;
            return "NotImplemented";
        }

        bool not_implemented_equal(const Object &left, const Object &right)
        {
            return &left == &right;
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

        Object *bool_result(Runtime *runtime, bool value)
        {
            return value ? runtime->true_value() : runtime->false_value();
        }

        Object *int_rich_compare(const Object &left, const Object &right, RichCompareOp op)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            if (!right_int)
                return left.runtime()->not_implemented();
            const std::int64_t left_value = static_cast<const IntObject *>(&left)->value;
            const std::int64_t right_value = right_int->value;
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(left.runtime(), left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(left.runtime(), left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(left.runtime(), left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(left.runtime(), left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(left.runtime(), left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(left.runtime(), left_value >= right_value);
            }
            return left.runtime()->not_implemented();
        }

        [[noreturn]] void raise_unsupported_unary_op(const Object &operand, UnaryOp op)
        {
            raise_type_error(operand.runtime(), unsupported_unary_op_message(op, &operand));
        }

        Object *int_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            if (!right_int)
                return left.runtime()->not_implemented();
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
            return left.runtime()->not_implemented();
        }

        Object *int_unary_op(const Object &operand, UnaryOp op)
        {
            auto *value = static_cast<const IntObject *>(&operand);
            switch (op)
            {
            case UnaryOp::Positive:
                return operand.runtime()->new_int(value->value);
            case UnaryOp::Negative:
                return operand.runtime()->new_int(-value->value);
            case UnaryOp::Invert:
                return operand.runtime()->new_int(~value->value);
            }
            raise_unsupported_unary_op(operand, op);
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

        Object *str_rich_compare(const Object &left, const Object &right, RichCompareOp op)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            if (!right_str)
                return left.runtime()->not_implemented();
            const std::string &left_value = static_cast<const StrObject *>(&left)->value;
            const std::string &right_value = right_str->value;
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(left.runtime(), left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(left.runtime(), left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(left.runtime(), left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(left.runtime(), left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(left.runtime(), left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(left.runtime(), left_value >= right_value);
            }
            return left.runtime()->not_implemented();
        }

        Object *str_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            if (!right_str || op != BinaryOp::Add)
                return left.runtime()->not_implemented();
            auto *left_str = static_cast<const StrObject *>(&left);
            return left.runtime()->new_str(left_str->value + right_str->value);
        }

        void validate_str_constructor_args(Runtime *runtime, const CallArguments &args)
        {
            require_no_keywords(runtime, "str", args);
            require_positional_at_most(runtime, "str", args, 1);
        }

        Object *str_new(TypeObject &type, const CallArguments &args)
        {
            validate_str_constructor_args(type.runtime(), args);
            if (args.positional.empty())
                return type.runtime()->allocate<StrObject>(&type, "");
            return type.runtime()->allocate<StrObject>(&type, object_str(args.positional[0]));
        }

        void str_init(Object &object, const CallArguments &args)
        {
            validate_str_constructor_args(object.runtime(), args);
        }
    }

    NoneObject::NoneObject(TypeObject *type)
        : Object(type)
    {
    }

    NotImplementedObject::NotImplementedObject(TypeObject *type)
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

    TypeSlots make_not_implemented_slots()
    {
        return TypeSlots{
            .repr = not_implemented_repr,
            .hash = default_hash,
            .equal = not_implemented_equal,
            .truth = default_truth,
            .traverse = default_traverse,
        };
    }

    TypeSlots make_int_slots()
    {
        return TypeSlots{
            .repr = int_repr,
            .hash = int_hash,
            .equal = int_equal,
            .rich_compare = int_rich_compare,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
            .unary_op = int_unary_op,
        };
    }

    TypeSlots make_bool_slots()
    {
        return TypeSlots{
            .repr = bool_repr,
            .hash = int_hash,
            .equal = int_equal,
            .rich_compare = int_rich_compare,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
            .unary_op = int_unary_op,
        };
    }

    TypeSlots make_str_slots()
    {
        return TypeSlots{
            .repr = str_repr,
            .hash = str_hash,
            .equal = str_equal,
            .rich_compare = str_rich_compare,
            .truth = str_truth,
            .traverse = default_traverse,
            .binary_op = str_binary_op,
            .new_object = str_new,
            .init = str_init,
        };
    }
}
