#include "cxxpython/parser.hpp"

#include <gtest/gtest.h>

namespace cxxpython::syntax {
std::u32string normalize_identifier(std::u32string_view input);
}

namespace {
using namespace cxxpython::syntax;

TEST(ParserLiterals, RejectsMalformedStringEscapes)
{
    for (const char *source : {"x='\\x0'", "x='\\u123'", "x='\\U00110000'", "x='\\U0001234'",
                               "x=f'\\x0'", "x=b'\\x0'"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(parse(source), Error);
    }
    for (const char *source :
         {"x=r'\\x0\\u123\\U00110000'", "x=b'\\u123\\U\\N'", "x='\\x00\\u1234\\U0010ffff'"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(parse(source));
    }
}

TEST(ParserLiterals, ValidatesFStringConversions)
{
    for (const char *source :
         {"x=f'{x!q}'", "x=f'{x! r}'", "x=f'{x!rr}'", "x=f'{x!}'", "x=f'{x!R}'"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(parse(source), Error);
    }
    for (const char *source :
         {"x=f'{x!s}'", "x=f'{x!r:>5}'", "x=f'{x!a}'", "x=f'{x != y}'", "x=f'{x:=3}'"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(parse(source));
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
TEST(TokenizerStream, ReadsLazilyAndSharesLongUnicodeLines)
{
    TokenStream stream(true);
    unsigned reads = 0;
    auto reader = [&] { return ++reads == 1 ? std::string("\xcf\x80 = x + y\n") : std::string{}; };
    auto first = stream.next(reader);
    ASSERT_TRUE(first);
    EXPECT_EQ(reads, 1u);
    EXPECT_EQ(first->column, 0);
    EXPECT_EQ(first->end_column, 1);
    auto second = stream.next(reader);
    ASSERT_TRUE(second);
    EXPECT_EQ(reads, 1u);
    EXPECT_EQ(second->column, 2);
    EXPECT_EQ(second->end_column, 3);
    EXPECT_EQ(first->line.get(), second->line.get());
    EXPECT_EQ(first->line_revision, second->line_revision);
    while (stream.next(reader)) {
    }
    EXPECT_EQ(reads, 2u);
    EXPECT_FALSE(stream.next(reader));
    EXPECT_EQ(reads, 2u);
}

TEST(TokenizerStream, BorrowsOnlyTheCurrentCallsReader)
{
    TokenStream stream;
    unsigned reads = 0;
    {
        const auto reader = [&] {
            ++reads;
            return std::string("x\n");
        };
        ASSERT_EQ(stream.next(reader)->token.text, U"x");
    }
    const auto reader = [&] {
        ++reads;
        return reads == 2 ? std::string("y\n") : std::string{};
    };
    EXPECT_EQ(stream.next(reader)->token.kind, TokenKind::NEWLINE);
    EXPECT_EQ(reads, 1u);
    EXPECT_EQ(stream.next(reader)->token.text, U"y");
    EXPECT_EQ(reads, 2u);
}

TEST(TokenizerStream, PollAndReadFailuresReleaseBorrowedCallbacksAndTerminate)
{
    TokenStream stream;
    unsigned polls = 0;
    const auto reader = [] { return std::string(50000, 'x') + '\n'; };
    EXPECT_THROW(stream.next(reader,
                             [&] {
                                 ++polls;
                                 throw std::runtime_error("interrupted");
                             }),
                 std::runtime_error);
    EXPECT_GT(polls, 0u);
    EXPECT_FALSE(stream.next(reader));
    TokenStream broken;
    EXPECT_THROW(broken.next([]() -> std::string { throw std::runtime_error("read"); }),
                 std::runtime_error);
    EXPECT_FALSE(broken.next(reader));
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

TEST(InteractiveSyntax, CompletenessAndErrors)
{
    for (const char *source :
         {"", "# comment\n", "\n", "x\n", "x; y\n", "if x:\n    if y:\n        pass\n\n",
          "for x in y:\n    pass\n\n", "try:\n    pass\nexcept:\n    pass\n\n", "x = (\n  1\n)\n",
          "x = '''a\nb'''\n", "x = 1 + \\\n  2\n"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(parse(source, InputMode::Interactive, true));
    }
    for (const char *source :
         {"if x:\n", "if x:\n    pass\n", "if x:\n    if y:\n        pass\n",
          "for x in y:\n    pass\n", "try:\n    pass\n", "x = (\n", "x = '''a\n", "x = 1 + \\\n"}) {
        SCOPED_TRACE(source);
        try {
            parse(source, InputMode::Interactive, true);
            ADD_FAILURE() << "expected incomplete input";
        } catch (const Error &error) {
            EXPECT_EQ(error.kind, ErrorKind::Incomplete);
        }
    }
    for (const char *source : {"x=\n", "x+\n", "x = 'abc\n", "if x:\n  pass\n pass\n"}) {
        SCOPED_TRACE(source);
        try {
            parse(source, InputMode::Interactive, true);
            ADD_FAILURE() << "expected syntax error";
        } catch (const Error &error) {
            EXPECT_NE(error.kind, ErrorKind::Incomplete);
        }
    }
    EXPECT_NO_THROW(parse("if x:\n    pass\n", InputMode::Interactive));
}

TEST(InteractiveSyntax, UnterminatedStringsAtEof)
{
    for (const char *source :
         {"x = '''text", "x = '''text\\", "x = r'''text\\", "x = f'''text\\"}) {
        SCOPED_TRACE(source);
        for (bool allow_incomplete : {false, true}) {
            try {
                parse(source, InputMode::Interactive, allow_incomplete);
                ADD_FAILURE() << "expected unterminated string error";
            } catch (const Error &error) {
                EXPECT_EQ(error.kind, allow_incomplete ? ErrorKind::Incomplete : ErrorKind::Syntax);
            }
        }
    }
    for (const char *source : {"x = 'text\\", "x = f'text\\"}) {
        SCOPED_TRACE(source);
        try {
            parse(source, InputMode::Interactive, true);
            ADD_FAILURE() << "expected unterminated string error";
        } catch (const Error &error) {
            EXPECT_EQ(error.kind, ErrorKind::Syntax);
        }
    }
}
} // namespace
