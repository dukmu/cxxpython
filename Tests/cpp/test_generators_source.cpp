#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class GeneratorSource : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};

    void SetUp() override
    {
        context.bootstrap_core();
    }
    void check(const char *source, const char *expression, const char *expected)
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto globals = roots.root(out.value());
        SourceUnit unit(SourceBuffer(source, "generators.py"));
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit result(SourceBuffer(expression, "result.py"), InputMode::Eval);
        run(context, {&result, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        repr(context, roots.root(out.value()), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(context.payload<StrObject>(out.value()).value), expected);
    }
};

TEST_F(GeneratorSource, YieldSendReturnAndDelegation)
{
    check("def inner():\n"
          "    x = yield 1\n"
          "    return x\n"
          "def outer():\n"
          "    result = yield from inner()\n"
          "    yield result\n"
          "g = outer()\n"
          "first = next(g)\n"
          "second = g.send(42)\n",
          "(first, second)", "(1, 42)");
}

TEST_F(GeneratorSource, GeneratorExpressionIsLazyAndCapturesClosure)
{
    check("events = []\n"
          "def source():\n"
          "    events.append(1)\n"
          "    return [2, 3]\n"
          "def make():\n"
          "    offset = 10\n"
          "    return (events.append(x) or x + offset for x in source() if x > 2)\n"
          "g = make()\n"
          "before = events[:]\n"
          "value = next(g)\n"
          "",
          "(before, events, value)", "([1], [1, 3], 13)");
}

TEST_F(GeneratorSource, YieldInExceptionCleanupAndWith)
{
    check("events = []\n"
          "class C:\n"
          "    def __enter__(self): return self\n"
          "    def __exit__(self, a, b, c): events.append(3)\n"
          "def f():\n"
          "    with C():\n"
          "        try:\n"
          "            raise ValueError()\n"
          "        except ValueError:\n"
          "            yield 1\n"
          "        finally:\n"
          "            events.append(2)\n"
          "g = f()\n"
          "next(g)\n"
          "g.close()\n"
          "",
          "events", "[2, 3]");
}

TEST_F(GeneratorSource, OutermostIteratorErrorsAtCreation)
{
    ReturnSlot out(context);
    SourceUnit unit(SourceBuffer("g = (x for x in 1)\n"));
    run(context, {&unit}, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::TypeErrorType));
}

TEST(GeneratorSourceScopes, RestrictionsAndNestedFunctions)
{
    for (const char *source :
         {"yield 1\n", "class C:\n    yield 1\n", "def f():\n    return [(yield x) for x in ()]\n",
          "def f(x=(yield 1)): pass\n"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(SourceUnit{SourceBuffer(source)}, SourceError);
    }
    SourceUnit unit(SourceBuffer("def outer():\n    def inner():\n        yield 1\n"));
    ASSERT_EQ(unit.scopes().size(), 3u);
    EXPECT_FALSE(unit.scopes()[1].generator);
    EXPECT_TRUE(unit.scopes()[2].generator);
    SourceUnit defaults(SourceBuffer("def outer():\n    def inner(x=(yield 1)): pass\n"));
    EXPECT_TRUE(defaults.scopes()[1].generator);
    EXPECT_FALSE(defaults.scopes()[2].generator);
}
} // namespace
