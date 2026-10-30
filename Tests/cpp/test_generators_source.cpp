#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

TEST(GeneratorSourceScopes, RestrictionsAndNestedFunctions)
{
    SourceUnit unit(SourceBuffer("def outer():\n    def inner():\n        yield 1\n"));
    ASSERT_EQ(unit.scopes().size(), 3u);
    EXPECT_FALSE(unit.scopes()[1].generator);
    EXPECT_TRUE(unit.scopes()[2].generator);
    SourceUnit defaults(SourceBuffer("def outer():\n    def inner(x=(yield 1)): pass\n"));
    EXPECT_TRUE(defaults.scopes()[1].generator);
    EXPECT_FALSE(defaults.scopes()[2].generator);
}
} // namespace
