#include "cxxpython/parser.hpp"

#include <gtest/gtest.h>

#include <future>
#include <string>

namespace {
using namespace cxxpython::syntax;
bool has_rule(const NodePtr &node, Rule rule)
{
    if (node->rule == rule)
        return true;
    for (const auto &child : node->children)
        if (has_rule(child, rule))
            return true;
    return false;
}
TEST(GeneratedParser, CompleteGrammarIsIndependentOfRuntimeCompilerSupport)
{
    const auto tree = parse("async def f[T](xs):\n"
                            "    async for x in xs:\n        yield x\n"
                            "match xs:\n    case [first, *rest]:\n        pass\n"
                            "type Alias[T] = list[T]\n");
    EXPECT_EQ(tree.root->rule, Rule::file);
    EXPECT_TRUE(has_rule(tree.root, Rule::function_def_raw));
    EXPECT_TRUE(has_rule(tree.root, Rule::yield_expr));
    EXPECT_TRUE(has_rule(tree.root, Rule::match_stmt));
    EXPECT_TRUE(has_rule(tree.root, Rule::type_alias));
}
TEST(GeneratedParser, OrderedChoiceLeftRecursionAndSoftKeywordsUseGeneratedRules)
{
    const auto tree = parse("match = case = type = 1\nvalue = (1 + 2 * 3) ** 2 ** 3\n");
    EXPECT_TRUE(has_rule(tree.root, Rule::power));
    EXPECT_TRUE(has_rule(tree.root, Rule::sum));
    EXPECT_FALSE(has_rule(tree.root, Rule::match_stmt));
    EXPECT_THROW(parse("match value:\n    case : pass\n"), Error);
    EXPECT_THROW(parse("x = 1 +\n"), Error);
}
TEST(GeneratedParser, TreesOwnTokenTextAndParsesAreReentrant)
{
    std::string source = "answer = [x * x for x in range(8) if x]\n";
    auto tree = parse(source);
    source.assign(source.size(), ' ');
    EXPECT_EQ(tree.tokens.front().text, U"answer");
    auto worker = [] {
        for (int i = 0; i < 20; ++i) {
            const auto result = parse("x = f'{ {\"key\": 3}[\"key\"]!r:>{4}}'\n");
            if (!has_rule(result.root, Rule::fstring))
                return false;
        }
        return true;
    };
    auto other = std::async(std::launch::async, worker);
    EXPECT_TRUE(worker());
    EXPECT_TRUE(other.get());
}
} // namespace
