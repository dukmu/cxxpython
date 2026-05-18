#pragma once

#include <cstdint>
#include <string>

#include "cxxpython/runtime.hpp"

namespace test_support
{
    inline std::string custom_repr(const cxxpython::Object &object)
    {
        (void)object;
        return "<custom>";
    }

    inline std::int64_t custom_hash(const cxxpython::Object &object)
    {
        (void)object;
        return 42;
    }

    inline bool custom_equal(const cxxpython::Object &left, const cxxpython::Object &right)
    {
        return left.type() == right.type();
    }

    inline cxxpython::Object *custom_rich_compare(const cxxpython::Object &left,
                                                  const cxxpython::Object &right,
                                                  cxxpython::RichCompareOp op)
    {
        (void)right;
        if (op == cxxpython::RichCompareOp::Equal)
            return left.runtime()->true_value();
        if (op == cxxpython::RichCompareOp::NotEqual)
            return left.runtime()->false_value();
        cxxpython::raise_type_error(left.runtime(), "custom rich comparison does not support ordering");
    }

    inline cxxpython::Object *custom_binary_op(const cxxpython::Object &left,
                                               const cxxpython::Object &right,
                                               cxxpython::BinaryOp op)
    {
        (void)right;
        if (op == cxxpython::BinaryOp::Add)
            return left.runtime()->new_str("custom-add");
        if (op == cxxpython::BinaryOp::Subtract)
            return left.runtime()->new_str("custom-subtract");
        cxxpython::raise_type_error(left.runtime(), "custom binary operator does not support this operation");
    }

    inline cxxpython::Object *custom_unary_op(const cxxpython::Object &operand,
                                              cxxpython::UnaryOp op)
    {
        if (op == cxxpython::UnaryOp::Negative)
            return operand.runtime()->new_str("custom-negative");
        if (op == cxxpython::UnaryOp::Invert)
            return operand.runtime()->new_str("custom-invert");
        cxxpython::raise_type_error(operand.runtime(), "custom unary operator does not support this operation");
    }

    inline bool custom_false(const cxxpython::Object &object)
    {
        (void)object;
        return false;
    }

    inline std::size_t custom_length(const cxxpython::Object &object)
    {
        (void)object;
        return 7;
    }

    inline cxxpython::Object *custom_getitem(cxxpython::Object &object, cxxpython::Object *key)
    {
        (void)object;
        return key;
    }

    inline void custom_setitem(cxxpython::Object &object, cxxpython::Object *key, cxxpython::Object *value)
    {
        (void)object;
        (void)key;
        (void)value;
    }

    inline bool custom_contains(cxxpython::Object &object, cxxpython::Object *key)
    {
        (void)object;
        return key != nullptr;
    }
}
