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

    inline bool custom_false(const cxxpython::Object &object)
    {
        (void)object;
        return false;
    }
}
