#pragma once

#include "cxxpython/object.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cxxpython
{
    std::string pointer_hex(const void *ptr);
    std::string escape_single_quoted(std::string_view value);
    std::string default_repr(const Object &object);
    std::int64_t default_hash(const Object &object);
    bool default_equal(const Object &left, const Object &right);
    bool default_truth(const Object &object);
    void default_traverse(const Object &object, const VisitChildren &visitor);
}
