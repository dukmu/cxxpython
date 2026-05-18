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
