#include "vm_fastpaths.hpp"

#include "../Objects/object_utils.hpp"

#include <cmath>

namespace cxxpython::vm_fastpath
{
    namespace
    {
        bool is_exact_int_layout(Runtime &runtime, const Object *object) noexcept
        {
            if (!object)
                return false;
            TypeObject *type = object->type();
            return type == runtime.int_type() || type == runtime.bool_type();
        }

        bool is_exact_float_layout(Runtime &runtime, const Object *object) noexcept
        {
            return object && object->type() == runtime.float_type();
        }

        std::int64_t exact_int_value(const Object *object) noexcept
        {
            return static_cast<const IntObject *>(object)->value;
        }

        double exact_float_value(const Object *object) noexcept
        {
            return static_cast<const FloatObject *>(object)->value;
        }

        Object *bool_result(Runtime &runtime, bool value) noexcept
        {
            return value ? runtime.true_value() : runtime.false_value();
        }

        std::int64_t floor_div_int(std::int64_t left, std::int64_t right)
        {
            std::int64_t quotient = left / right;
            std::int64_t remainder = left % right;
            if (remainder != 0 && ((remainder > 0) != (right > 0)))
                --quotient;
            return quotient;
        }

        std::int64_t modulo_int(std::int64_t left, std::int64_t right)
        {
            std::int64_t remainder = left % right;
            if (remainder != 0 && ((remainder > 0) != (right > 0)))
                remainder += right;
            return remainder;
        }

        Object *fast_int_int_binary_op(Runtime &runtime,
                                       std::int64_t left,
                                       std::int64_t right,
                                       BinaryOp op)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return runtime.new_int(left + right);
            case BinaryOp::Subtract:
                return runtime.new_int(left - right);
            case BinaryOp::Multiply:
                return runtime.new_int(left * right);
            case BinaryOp::TrueDivide:
                if (right == 0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(static_cast<double>(left) / static_cast<double>(right));
            case BinaryOp::FloorDivide:
                if (right == 0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_int(floor_div_int(left, right));
            case BinaryOp::Modulo:
                if (right == 0)
                    raise_value_error(&runtime, "integer modulo by zero");
                return runtime.new_int(modulo_int(left, right));
            case BinaryOp::Power:
                return runtime.new_int(static_cast<std::int64_t>(
                    std::pow(static_cast<double>(left), static_cast<double>(right))));
            case BinaryOp::ShiftLeft:
                if (right < 0)
                    raise_value_error(&runtime, "negative shift count");
                return runtime.new_int(left << right);
            case BinaryOp::ShiftRight:
                if (right < 0)
                    raise_value_error(&runtime, "negative shift count");
                return runtime.new_int(left >> right);
            case BinaryOp::BitAnd:
                return runtime.new_int(left & right);
            case BinaryOp::BitXor:
                return runtime.new_int(left ^ right);
            case BinaryOp::BitOr:
                return runtime.new_int(left | right);
            }
            return nullptr;
        }

        Object *fast_float_binary_op(Runtime &runtime,
                                     double left,
                                     double right,
                                     BinaryOp op)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return runtime.new_float(left + right);
            case BinaryOp::Subtract:
                return runtime.new_float(left - right);
            case BinaryOp::Multiply:
                return runtime.new_float(left * right);
            case BinaryOp::TrueDivide:
                if (right == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(left / right);
            case BinaryOp::FloorDivide:
                if (right == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(std::floor(left / right));
            case BinaryOp::Modulo:
                if (right == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(std::fmod(left, right));
            case BinaryOp::Power:
                return runtime.new_float(std::pow(left, right));
            case BinaryOp::ShiftLeft:
            case BinaryOp::ShiftRight:
            case BinaryOp::BitAnd:
            case BinaryOp::BitXor:
            case BinaryOp::BitOr:
                return nullptr;
            }
            return nullptr;
        }
    }

    Object *try_binary_op(Runtime &runtime, Object *left, Object *right, BinaryOp op)
    {
        const bool left_is_int = is_exact_int_layout(runtime, left);
        const bool right_is_int = is_exact_int_layout(runtime, right);
        if (left_is_int && right_is_int)
            return fast_int_int_binary_op(runtime, exact_int_value(left), exact_int_value(right), op);

        const bool left_is_float = is_exact_float_layout(runtime, left);
        const bool right_is_float = is_exact_float_layout(runtime, right);
        if (left_is_float && (right_is_float || right_is_int))
        {
            const double right_value = right_is_float
                                           ? exact_float_value(right)
                                           : static_cast<double>(exact_int_value(right));
            return fast_float_binary_op(runtime, exact_float_value(left), right_value, op);
        }
        if (left_is_int && right_is_float)
        {
            const double left_value = static_cast<double>(exact_int_value(left));
            const double right_value = exact_float_value(right);
            switch (op)
            {
            case BinaryOp::Add:
                return runtime.new_float(left_value + right_value);
            case BinaryOp::Subtract:
                return runtime.new_float(left_value - right_value);
            case BinaryOp::Multiply:
                return runtime.new_float(left_value * right_value);
            case BinaryOp::TrueDivide:
                if (right_value == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(left_value / right_value);
            case BinaryOp::FloorDivide:
                if (right_value == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_int(static_cast<std::int64_t>(std::floor(left_value / right_value)));
            case BinaryOp::Modulo:
                if (right_value == 0.0)
                    raise_value_error(&runtime, "division by zero");
                return runtime.new_float(std::fmod(left_value, right_value));
            case BinaryOp::Power:
                return runtime.new_float(std::pow(left_value, right_value));
            case BinaryOp::ShiftLeft:
            case BinaryOp::ShiftRight:
            case BinaryOp::BitAnd:
            case BinaryOp::BitXor:
            case BinaryOp::BitOr:
                return nullptr;
            }
        }
        return nullptr;
    }

    Object *try_compare_op(Runtime &runtime, Object *left, Object *right, RichCompareOp op)
    {
        const bool left_is_int = is_exact_int_layout(runtime, left);
        const bool right_is_int = is_exact_int_layout(runtime, right);
        const bool left_is_float = is_exact_float_layout(runtime, left);
        const bool right_is_float = is_exact_float_layout(runtime, right);

        if (!(left_is_int || left_is_float) || !(right_is_int || right_is_float))
            return nullptr;

        if (left_is_int && right_is_int)
        {
            const std::int64_t left_value = exact_int_value(left);
            const std::int64_t right_value = exact_int_value(right);
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(runtime, left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(runtime, left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(runtime, left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(runtime, left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(runtime, left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(runtime, left_value >= right_value);
            }
        }

        const double left_value = left_is_float
                                      ? exact_float_value(left)
                                      : static_cast<double>(exact_int_value(left));
        const double right_value = right_is_float
                                       ? exact_float_value(right)
                                       : static_cast<double>(exact_int_value(right));
        switch (op)
        {
        case RichCompareOp::Equal:
            return bool_result(runtime, left_value == right_value);
        case RichCompareOp::NotEqual:
            return bool_result(runtime, left_value != right_value);
        case RichCompareOp::Less:
            return bool_result(runtime, left_value < right_value);
        case RichCompareOp::LessEqual:
            return bool_result(runtime, left_value <= right_value);
        case RichCompareOp::Greater:
            return bool_result(runtime, left_value > right_value);
        case RichCompareOp::GreaterEqual:
            return bool_result(runtime, left_value >= right_value);
        }
        return nullptr;
    }

    bool try_truth(Runtime &runtime, Object *object, bool &truth) noexcept
    {
        if (!object)
        {
            truth = false;
            return true;
        }
        if (object == runtime.true_value())
        {
            truth = true;
            return true;
        }
        if (object == runtime.false_value() || object == runtime.none())
        {
            truth = false;
            return true;
        }
        if (is_exact_int_layout(runtime, object))
        {
            truth = exact_int_value(object) != 0;
            return true;
        }
        if (is_exact_float_layout(runtime, object))
        {
            truth = exact_float_value(object) != 0.0;
            return true;
        }
        return false;
    }

    Object *try_builtin_call(Runtime &runtime, Object *callable, ObjectSpan args)
    {
        if (callable == runtime.int_type())
        {
            if (args.size() > 1)
                return nullptr;
            if (args.empty())
                return runtime.new_int(0);
            Object *argument = args[0];
            if (is_exact_int_layout(runtime, argument))
                return runtime.new_int(exact_int_value(argument));
            if (is_exact_float_layout(runtime, argument))
                return runtime.new_int(static_cast<std::int64_t>(
                    std::trunc(exact_float_value(argument))));
            return nullptr;
        }

        if (callable == runtime.float_type())
        {
            if (args.size() > 1)
                return nullptr;
            if (args.empty())
                return runtime.new_float(0.0);
            Object *argument = args[0];
            if (is_exact_float_layout(runtime, argument))
                return runtime.new_float(exact_float_value(argument));
            if (is_exact_int_layout(runtime, argument))
                return runtime.new_float(static_cast<double>(exact_int_value(argument)));
            return nullptr;
        }

        if (callable == runtime.range_type())
        {
            if (args.size() < 1 || args.size() > 3)
                return nullptr;
            for (Object *argument : args)
            {
                if (!is_exact_int_layout(runtime, argument))
                    return nullptr;
            }
            const std::int64_t start = args.size() == 1 ? 0 : exact_int_value(args[0]);
            const std::int64_t stop = args.size() == 1 ? exact_int_value(args[0]) : exact_int_value(args[1]);
            const std::int64_t step = args.size() == 3 ? exact_int_value(args[2]) : 1;
            return runtime.new_range(start, stop, step);
        }

        return nullptr;
    }
}
