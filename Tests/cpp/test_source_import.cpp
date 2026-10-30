#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class SourceImports : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    void source(std::u32string name, std::string body, bool package = false)
    {
        register_source_module(context, std::move(name),
                               std::make_shared<SourceUnit>(SourceBuffer(std::move(body))),
                               package);
    }
    void check(std::string body, std::string expression, std::string expected)
    {
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto globals = roots.root(module_dict(context, out.value()));
        SourceUnit unit(SourceBuffer(std::move(body)));
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit eval(SourceBuffer(std::move(expression)), InputMode::Eval);
        run(context, {&eval, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        repr(context, roots.root(out.value()), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(context.payload<StrObject>(out.value()).value), expected);
    }
};

TEST_F(SourceImports, PackagesAliasesRelativeImportsAndClosuresShareModuleIdentity)
{
    source(U"pkg", "value = 10", true);
    source(U"pkg.child", "from . import value\nanswer = value + 32");
    check("import pkg.child\nimport pkg.child as alias\n"
          "from pkg.child import (answer as result,)\n"
          "def outer():\n    import pkg.child as local\n"
          "    def inner():\n        return local\n    return inner\n"
          "class C:\n    from pkg.child import answer as __answer\n",
          "result, alias is pkg.child, outer()() is alias, C._C__answer", "(42, True, True, 42)");
}

TEST_F(SourceImports, StarImportsHonorAllAndExcludePrivateFallbackNames)
{
    source(U"public", "one = 1\n_hidden = 2");
    source(U"explicit", "__all__ = ('_hidden', 'renamed')\n_hidden = 4\nrenamed = 8");
    check("from public import *\ntry:\n    _hidden\nexcept NameError:\n    excluded = True\n"
          "from explicit import *",
          "one, excluded, _hidden, renamed", "(1, True, 4, 8)");
}

TEST_F(SourceImports, CyclesAndImportFailuresFollowNormalExceptionUnwinding)
{
    source(U"cycle_a", "value = 1\nimport cycle_b\nanswer = cycle_b.answer + value");
    source(U"cycle_b", "from cycle_a import value\nanswer = value + 40");
    check("import cycle_a\ntry:\n    from cycle_a import absent\n"
          "except ImportError:\n    failed = True\nfinally:\n    finished = True\n"
          "try:\n    import absent_cxxpython_module\nexcept ModuleNotFoundError as error:\n"
          "    missing = error.name",
          "cycle_a.answer, failed, finished, missing",
          "(42, True, True, 'absent_cxxpython_module')");
}

TEST_F(SourceImports, InvalidImportFormsAreSyntaxErrors)
{
    for (const auto *text :
         {"import a,", "import .a", "import a as", "from a import", "from a import x.y",
          "from a import x,", "from a import (*)", "from a import * as b", "from import x",
          "from . import ()", "def f():\n    from a import *", "class C:\n    from a import *",
          "import a as __debug__", "from a import __debug__", "import a\nglobal a"}) {
        SCOPED_TRACE(text);
        EXPECT_THROW(SourceUnit{SourceBuffer(text)}, SourceError);
    }
}

TEST_F(SourceImports, ClassPrivateImportNamesAreMangledButFromlistIsNot)
{
    source(U"_C__private", "_C__field = 42");
    check("class C:\n    import __private\n    from __private import __field\n",
          "C._C__field, C._C__private._C__field", "(42, 42)");
}

TEST_F(SourceImports, BuiltinsImportHookReceivesGlobalsAndActualLocalBindings)
{
    check("import builtins\nimport __main__ as main\nold = builtins.__import__\n"
          "def hook(name, globals, locals, fromlist, level):\n"
          "    global captured\n    captured = name, globals, locals, fromlist, level\n"
          "    return main\n"
          "builtins.__import__ = hook\n"
          "try:\n    def outer(cell):\n        def inner():\n"
          "            cell\n            local = 42\n            import other as alias\n"
          "            return alias\n        return inner()\n    result = outer(7)\n"
          "finally:\n    builtins.__import__ = old\n",
          "result is main, captured[0], captured[1] is main.__dict__, captured[3], captured[4]",
          "(True, 'other', True, None, 0)");
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    const auto mapping = module_dict(context, out.value());
    const auto captured = *detail::name_lookup(context, mapping, U"captured");
    const auto locals = context.payload<TupleObject>(captured).items[2];
    EXPECT_EQ(int_decimal(context, *detail::name_lookup(context, locals, U"cell")), "7");
    EXPECT_EQ(int_decimal(context, *detail::name_lookup(context, locals, U"local")), "42");
    EXPECT_FALSE(detail::name_lookup(context, locals, U"alias"));
}
} // namespace
