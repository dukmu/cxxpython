#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <charconv>
#include <utility>

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

        std::int64_t parse_int_literal(Runtime *runtime, const StrObject *text)
        {
            std::int64_t value = 0;
            const char *begin = text->value.data();
            const char *end = begin + text->value.size();
            const auto [position, error] = std::from_chars(begin, end, value);
            if (error == std::errc{} && position == end)
                return value;
            raise_value_error(runtime, "invalid literal for int() with base 10: '" + text->value + "'");
        }

        std::int64_t int_constructor_value(Runtime *runtime, Object *object)
        {
            if (auto *integer = dynamic_cast<IntObject *>(object))
                return integer->value;
            if (auto *text = dynamic_cast<StrObject *>(object))
                return parse_int_literal(runtime, text);
            raise_type_error(runtime,
                             "int() argument must be a string, a bytes-like object or a real number");
        }

        Object *int_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "int", args);
            require_positional_at_most(type.runtime(), "int", args, 1);
            const std::int64_t value = args.positional.empty()
                                           ? 0
                                           : int_constructor_value(type.runtime(), args.positional[0]);
            return type.runtime()->allocate<IntObject>(&type, value);
        }

        void int_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "int", args);
            require_positional_at_most(object.runtime(), "int", args, 1);
        }

        std::string bool_repr(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0 ? "True" : "False";
        }

        Object *bool_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "bool", args);
            require_positional_at_most(type.runtime(), "bool", args, 1);
            if (args.positional.empty())
                return type.runtime()->false_value();
            return is_true(args.positional[0])
                       ? type.runtime()->true_value()
                       : type.runtime()->false_value();
        }

        void bool_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "bool", args);
            require_positional_at_most(object.runtime(), "bool", args, 1);
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

        std::size_t str_length(const Object &object)
        {
            return static_cast<const StrObject *>(&object)->value.size();
        }

        std::int64_t str_index(Object &object, Object *key)
        {
            auto *index = dynamic_cast<IntObject *>(key);
            if (!index)
                raise_type_error(object.runtime(), "string indices must be integers");
            return index->value;
        }

        Object *str_getitem(Object &object, Object *key)
        {
            auto &text = static_cast<StrObject &>(object);
            if (auto *slice = dynamic_cast<SliceObject *>(key))
            {
                SliceRange range = normalize_slice(*object.runtime(), *slice, static_cast<std::int64_t>(text.value.size()));
                std::string result;
                result.reserve(range.length);
                std::int64_t index = range.start;
                for (std::size_t count = 0; count < range.length; ++count)
                {
                    result.push_back(text.value[static_cast<std::size_t>(index)]);
                    index += range.step;
                }
                return object.runtime()->new_str(std::move(result));
            }
            const std::int64_t size = static_cast<std::int64_t>(text.value.size());
            const std::int64_t index = str_index(object, key);
            const std::int64_t resolved = index < 0 ? size + index : index;
            if (resolved < 0 || resolved >= size)
                raise_index_error(object.runtime(), "string index out of range");
            return object.runtime()->new_str(std::string(1, text.value[static_cast<std::size_t>(resolved)]));
        }

        bool str_contains(Object &object, Object *key)
        {
            auto *needle = dynamic_cast<StrObject *>(key);
            if (!needle)
                raise_type_error(object.runtime(), "'in <string>' requires string as left operand, not " +
                                                   type_qualified_name(key ? key->type() : nullptr));
            const std::string &haystack = static_cast<StrObject *>(&object)->value;
            return haystack.find(needle->value) != std::string::npos;
        }

        Object *str_iter(Object &object)
        {
            return object.runtime()->new_sequence_iterator(&object);
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

        Object *int_bool_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int.__bool__() takes exactly zero arguments");
            return is_true(self) ? runtime.true_value() : runtime.false_value();
        }

        Object *int_identity_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int conversion method takes exactly zero arguments");
            auto *integer = dynamic_cast<IntObject *>(self);
            if (!integer)
                raise_type_error(&runtime, "int conversion receiver does not have int layout");
            return integer;
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
            .new_object = int_new,
            .init = int_init,
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
            .new_object = bool_new,
            .init = bool_init,
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
            .length = str_length,
            .getitem = str_getitem,
            .contains = str_contains,
            .iter = str_iter,
            .new_object = str_new,
            .init = str_init,
        };
    }

    void initialize_none_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
    }

    void initialize_not_implemented_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
    }

    void initialize_int_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__bool__"),
                        runtime.new_method_descriptor("__bool__", type, int_bool_method));
        type->dict->set(runtime.new_str("__int__"),
                        runtime.new_method_descriptor("__int__", type, int_identity_method));
        type->dict->set(runtime.new_str("__index__"),
                        runtime.new_method_descriptor("__index__", type, int_identity_method));
    }

    void initialize_int_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_numeric_slot_wrappers(type);
    }

    void initialize_bool_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_numeric_slot_wrappers(type);
    }

    void initialize_str_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_slot_wrapper(type, "__add__", SlotWrapperKind::BinaryOp, BinaryOp::Add);
        add_sequence_slot_wrappers(type, false);
    }
}
