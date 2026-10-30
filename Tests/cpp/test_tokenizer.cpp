#include "cxxpython/parser.hpp"

#include <gtest/gtest.h>

namespace cxxpython::syntax {
std::u32string normalize_identifier(std::u32string_view input);
}

namespace {
using namespace cxxpython::syntax;

TEST(Tokenizer, RejectsMalformedStringEscapes)
{
    for (const char *source : {"x='\\x0'", "x='\\u123'", "x='\\U00110000'", "x='\\U0001234'",
                               "x=f'\\x0'", "x=b'\\x0'"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(tokenize(source), Error);
    }
    for (const char *source :
         {"x=r'\\x0\\u123\\U00110000'", "x=b'\\u123\\U\\N'", "x='\\x00\\u1234\\U0010ffff'"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(tokenize(source));
    }
}

TEST(Tokenizer, ValidatesFStringConversions)
{
    for (const char *source :
         {"x=f'{x!q}'", "x=f'{x! r}'", "x=f'{x!rr}'", "x=f'{x!}'", "x=f'{x!R}'"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(tokenize(source), Error);
    }
    for (const char *source :
         {"x=f'{x!s}'", "x=f'{x!r:>5}'", "x=f'{x!a}'", "x=f'{x != y}'", "x=f'{x:=3}'"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(tokenize(source));
    }
    auto tokens = tokenize("f'{x:=3}'");
    bool colon = false, format = false;
    for (const auto &token : tokens) {
        colon |= token.kind == TokenKind::COLON;
        format |= token.kind == TokenKind::FSTRING_MIDDLE && token.text == U"=3";
    }
    EXPECT_TRUE(colon);
    EXPECT_TRUE(format);
}

TEST(Tokenizer, IdentifiersUseXidAndKeepRawLexeme)
{
    auto tokens = tokenize("\xef\xbc\xa1\xcc\x8a = 1");
    ASSERT_EQ(tokens[0].kind, TokenKind::NAME);
    EXPECT_EQ(tokens[0].text, U"\uff21\u030a");
    EXPECT_EQ(tokens[0].span.begin, 0);
    EXPECT_EQ(tokens[0].span.end, 5);
    EXPECT_EQ(normalize_identifier(tokens[0].text), U"\u00c5");
    EXPECT_EQ(normalize_identifier(U"\u1100\u1161\u11a8"), U"\uac01");
    EXPECT_THROW(tokenize("\xf0\x9f\x98\x80 = 1"), Error);
    EXPECT_THROW(tokenize("a\xf0\x9f\x98\x80 = 1"), Error);
    EXPECT_THROW(tokenize("\xcc\x81"
                          "a = 1"),
                 Error);
}
TEST(Tokenizer, ContinuationLinesDoNotCreateIndentation)
{
    for (const char *source : {"x = (\n  1\n)\ny = 2\n", "x = call(\n  1,\n) + 2\ny = 3\n",
                               "def f(\n  x,\n) -> int:\n    return x\n",
                               "x = f\"{x # comment\n} tail\" + 1\ny = 2\n"}) {
        SCOPED_TRACE(source);
        auto tokens = tokenize(source);
        unsigned indents = 0;
        for (const auto &token : tokens)
            indents += token.kind == TokenKind::INDENT;
        EXPECT_EQ(indents, source[0] == 'd' ? 1u : 0u);
    }
}
} // namespace
