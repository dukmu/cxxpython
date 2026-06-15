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
    std::int64_t identity_hash(const Object &object);
    bool identity_equal(const Object &left, const Object &right);
    bool always_true(const Object &object);
    void default_traverse(const Object &object, const VisitChildren &visitor);
    std::vector<Object *> collect_iterable_items(Runtime *runtime, Object *iterable);
    bool has_keyword_arguments(const CallArguments &args) noexcept;
    void require_no_keywords(Runtime *runtime, std::string_view callable, const CallArguments &args);
    void require_no_positional(Runtime *runtime, std::string_view callable, const CallArguments &args);
    void require_positional_count(Runtime *runtime,
                                  std::string_view callable,
                                  const CallArguments &args,
                                  std::size_t expected);
    void require_positional_at_most(Runtime *runtime,
                                    std::string_view callable,
                                    const CallArguments &args,
                                    std::size_t maximum);
    void require_positional_range(Runtime *runtime,
                                  std::string_view callable,
                                  const CallArguments &args,
                                  std::size_t minimum,
                                  std::size_t maximum);
    const char *binary_op_symbol(BinaryOp op);
    std::string unsupported_binary_op_message(BinaryOp op, const Object *left, const Object *right);
    const char *unary_op_symbol(UnaryOp op);
    std::string unsupported_unary_op_message(UnaryOp op, const Object *operand);
}
