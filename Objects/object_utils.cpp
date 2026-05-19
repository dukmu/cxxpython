#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
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

    bool has_keyword_arguments(const CallArguments &args) noexcept
    {
        return args.keywords && args.keywords->size() != 0;
    }

    namespace
    {
        std::string callable_name(std::string_view callable)
        {
            return std::string(callable) + "()";
        }

        std::string argument_count_phrase(std::size_t count)
        {
            if (count == 1)
                return "one argument";
            std::ostringstream message;
            message << count << " arguments";
            return message.str();
        }
    }

    void require_no_keywords(Runtime *runtime, std::string_view callable, const CallArguments &args)
    {
        if (!has_keyword_arguments(args))
            return;
        raise_type_error(runtime, callable_name(callable) + " takes no keyword arguments");
    }

    void require_no_positional(Runtime *runtime, std::string_view callable, const CallArguments &args)
    {
        if (args.positional.empty())
            return;
        raise_type_error(runtime, callable_name(callable) + " takes no arguments");
    }

    void require_positional_count(Runtime *runtime,
                                  std::string_view callable,
                                  const CallArguments &args,
                                  std::size_t expected)
    {
        if (args.positional.size() == expected)
            return;
        std::ostringstream message;
        message << callable_name(callable) << " takes exactly "
                << argument_count_phrase(expected)
                << " (" << args.positional.size() << " given)";
        raise_type_error(runtime, message.str());
    }

    void require_positional_at_most(Runtime *runtime,
                                    std::string_view callable,
                                    const CallArguments &args,
                                    std::size_t maximum)
    {
        if (args.positional.size() <= maximum)
            return;
        std::ostringstream message;
        message << callable_name(callable) << " takes at most "
                << argument_count_phrase(maximum);
        raise_type_error(runtime, message.str());
    }

    const char *binary_op_symbol(BinaryOp op)
    {
        switch (op)
        {
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Subtract:
            return "-";
        case BinaryOp::Multiply:
            return "*";
        case BinaryOp::TrueDivide:
            return "/";
        case BinaryOp::FloorDivide:
            return "//";
        case BinaryOp::Modulo:
            return "%";
        case BinaryOp::Power:
            return "**";
        }
        return "?";
    }

    std::string unsupported_binary_op_message(BinaryOp op, const Object *left, const Object *right)
    {
        std::ostringstream message;
        message << "unsupported operand type(s) for "
                << binary_op_symbol(op)
                << ": '"
                << type_qualified_name(left ? left->type() : nullptr)
                << "' and '"
                << type_qualified_name(right ? right->type() : nullptr)
                << "'";
        return message.str();
    }

    const char *unary_op_symbol(UnaryOp op)
    {
        switch (op)
        {
        case UnaryOp::Positive:
            return "+";
        case UnaryOp::Negative:
            return "-";
        case UnaryOp::Invert:
            return "~";
        }
        return "?";
    }

    std::string unsupported_unary_op_message(UnaryOp op, const Object *operand)
    {
        std::ostringstream message;
        message << "bad operand type for unary "
                << unary_op_symbol(op)
                << ": '"
                << type_qualified_name(operand ? operand->type() : nullptr)
                << "'";
        return message.str();
    }

    std::string pointer_hex(const void *ptr)
    {
        std::ostringstream out;
        out << ptr;
        return out.str();
    }

    std::string escape_single_quoted(std::string_view value)
    {
        std::ostringstream out;
        for (unsigned char ch : value)
        {
            switch (ch)
            {
            case '\\':
                out << "\\\\";
                break;
            case '\'':
                out << "\\'";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\t':
                out << "\\t";
                break;
            case '\r':
                out << "\\r";
                break;
            default:
                if (ch < 0x20 || ch == 0x7f)
                {
                    static constexpr char digits[] = "0123456789abcdef";
                    out << "\\x" << digits[(ch >> 4) & 0x0f] << digits[ch & 0x0f];
                }
                else
                {
                    out << static_cast<char>(ch);
                }
                break;
            }
        }
        return out.str();
    }
}
