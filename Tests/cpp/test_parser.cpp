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
TEST(GeneratedParser, LiteralDiagnosticsUseSecondPass)
{
    const auto check = [](std::string_view source, std::string_view message,
                          std::string_view location = {}) {
        try {
            parse(source);
            FAIL() << "expected syntax error";
        } catch (const Error &error) {
            EXPECT_EQ(error.kind, ErrorKind::Syntax);
            EXPECT_EQ(error.what(), message);
            if (!location.empty()) {
                EXPECT_EQ(source.substr(error.span.begin, error.span.end - error.span.begin),
                          location);
            }
        }
    };
    constexpr auto mixed = "cannot have both 'except' and 'except*' on the same 'try'";
    check("try:\n pass\nexcept ValueError:\n pass\nexcept* TypeError:\n pass\n", mixed, "except*");
    check("try:\n pass\nexcept* ValueError:\n pass\nexcept TypeError:\n pass\n", mixed, "except");
    check("try:\n pass\nx = 1\n", "expected 'except' or 'finally' block");
    check("try:\n pass\nexcept*:\n pass\n", "expected one or more exception types");
    check("x = 1 +\n", "invalid syntax");
    EXPECT_NO_THROW(parse("try:\n try:\n  pass\n except* ValueError:\n  pass\n"
                          "except TypeError:\n pass\n"));
}
} // namespace
