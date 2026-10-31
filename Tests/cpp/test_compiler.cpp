#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

#include <future>
#include <latch>

namespace {
using namespace cxxpython::core;
class CoreSource : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle dictionary()
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        return roots.root(out.result().value());
    }
    void exec(std::string source, Handle globals, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(std::move(source), "fixture.py"));
        run(context, {&unit, globals}, out);
    }
    void eval(std::string source, ReturnSlot &out, std::optional<Handle> globals = {})
    {
        SourceUnit unit(SourceBuffer(std::move(source), "expression.py"), InputMode::Eval);
        run(context, {&unit, globals}, out);
    }
    std::string result(ReturnSlot &out)
    {
        if (out.result().tag() != ResultTag::Ok)
            return "<raised>";
        ReturnSlot text(context);
        repr(context, roots.root(out.result().value()), text);
        return source_utf8(context.payload<StrObject>(text.result().value()).value);
    }
    void raised(ReturnSlot &out, CoreObject type)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
    }
};

TEST_F(CoreSource, LiteralArithmeticBooleanAndComparisonProgramsUseOrdinaryProtocols)
{
    const std::pair<const char *, const char *> cases[] = {
        {"1 + 2 * 3", "7"},
        {"-(7 // 2) + ~0", "-4"},
        {"-7 % 3", "2"},
        {"+True", "1"},
        {"0x_FF + 0b101 + 0o7 + 1_000", "1267"},
        {"18446744073709551616 * 2", "36893488147419103232"},
        {"'a\\n' 'b'", "'a\\nb'"},
        {"'\\u4f60\\u597d'", "'\xe4\xbd\xa0\xe5\xa5\xbd'"},
        {"r'\\n'", "'\\\\n'"},
        {"None is None", "True"},
        {"1 is not None", "True"},
        {"0 and missing", "0"},
        {"'ok' or missing", "'ok'"},
        {"not 0", "True"},
        {"1 < 2 < 3", "True"},
        {"3 < 2 < missing", "False"},
        {"len('abc')", "3"},
    };
    ReturnSlot out(context);
    for (auto [source, expected] : cases) {
        SCOPED_TRACE(source);
        eval(source, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(result(out), expected);
    }
}

TEST_F(CoreSource, ListDisplaysAndTargetsUseNormalExpressionAndBindingPaths)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("[a, (b, c)] = [1, [2, 3]]\n"
         "def f(x=[a, [b]]): return x\n"
         "class C:\n    [__private] = [c]\n"
         "del [a, (b, c)]\n"
         "[] = []\ndel []\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("f(), C._C__private, f() is f(), [] is not []", out, globals);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "([1, [2]], 3, True, True)");
    for (auto source : {"[a] += [1]", "[1] = [2]", "del [1]"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(SourceUnit{SourceBuffer(source)}, SourceError);
    }
}

TEST_F(CoreSource, SetDisplaysAndComprehensionsKeepTheirOrdinaryScopeAndRoots)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(
outer = 99
set = None
literal = {1, *range(2, 6), 2}
result = {outer % 3 for outer in range(100) if outer > 4}
nested = {(x, y) for x in range(3) for y in range(x)}
def f(n):
    return {x + n for x in range(3)}
named = {(last := x) for x in range(3)}
failed = False
try:
    {[]}
except TypeError:
    failed = True
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("literal == {1, 2, 3, 4, 5}, result == {0, 1, 2}, outer, "
         "nested == {(1, 0), (2, 0), (2, 1)}, f(10) == {10, 11, 12}, "
         "named == {0, 1, 2}, last, failed",
         out, globals);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "(True, True, 99, True, True, True, 2, True)");
}

TEST_F(CoreSource, FloatLiteralsAndArithmeticUseOrdinaryNumericProtocolsDuringGc)
{
    ReturnSlot out(context);
    for (auto [source, expected] : {
             std::pair{".5, 1., 1.e2, 001.25, 1_2.3_4e+0_2", "(0.5, 1.0, 100.0, 1.25, 1234.0)"},
             {"1e309, -1e-9999, 0x1e + 2", "(inf, -0.0, 32)"},
             {"1.25 + 2, 3 - .5, .25 * 2, 7.5 / 2", "(3.25, 2.5, 0.5, 3.75)"},
             {"7.5 // 2, -7.5 % 2, 7.5 % -2", "(3.0, 0.5, -0.5)"},
             {"0.0 / -3, -0.0 // 3, 0.0 % -3", "(-0.0, -0.0, -0.0)"},
             {"9007199254740992.0 < 9007199254740993, -1.5 < -1, -.5 < 0", "(True, True, True)"},
             {"float('nan') < 1, float('nan') >= 1, float('nan') != 1", "(False, False, True)"},
         }) {
        SCOPED_TRACE(source);
        eval(source, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(result(out), expected);
        context.collect();
        EXPECT_EQ(result(out), expected);
    }
    for (auto source : {"1e", "1e+", "1._0", "1e_2", "1.2.3", ".1__2", "1.0_", "1e2x"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(SourceUnit{SourceBuffer(source)}, SourceError);
    }
}

TEST_F(CoreSource, TrueDivisionInplaceAndFloatSubclassReflectionKeepCallbackOrder)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(events = 0
class Left:
    def __itruediv__(self, other):
        global events
        events = events * 10 + 1
        return NotImplemented
    def __truediv__(self, other):
        global events
        events = events * 10 + 2
        return NotImplemented
class Right:
    def __rtruediv__(self, other):
        global events
        events = events * 10 + 3
        return 42
class Child(float):
    def __radd__(self, other): return 97
value = Left()
value /= Right()
number = 8
number /= 4
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("value, events, number, 1.0 + Child(2.0)", out, globals);
    EXPECT_EQ(result(out), "(42, 123, 2.0, 97)");
    for (auto source : {"1.0 / 0", "1.0 // 0.0", "float('nan') % 0.0"}) {
        SCOPED_TRACE(source);
        eval(source, out);
        raised(out, CoreObject::ZeroDivisionErrorType);
    }
    eval("1.0 + '2'", out);
    raised(out, CoreObject::TypeErrorType);
}

TEST_F(CoreSource, ForLoopsTupleTargetsAndSubscriptsComposeThroughOrdinaryProtocols)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("def total(n):\n    value = 0\n    for i in range(n):\n        if i == 2:\n            "
         "continue\n        value += i\n    else:\n        value += 100\n    return value\n"
         "def nested():\n    value = 0\n    for a, b in ((1, 2), (3, 4)):\n        for n in "
         "range(a, b + 1):\n            value += n\n    return value\n"
         "left, (middle, right) = 1, (2, 3)\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("total(5)", out, globals);
    EXPECT_EQ(result(out), "108");
    eval("nested()", out, globals);
    EXPECT_EQ(result(out), "10");
    eval("left * 100 + middle * 10 + right", out, globals);
    EXPECT_EQ(result(out), "123");
    eval("(10, 20, 30)[-1]", out);
    EXPECT_EQ(result(out), "30");
    eval("'a\\U0001f600b'[1]", out);
    EXPECT_EQ(result(out), "'\xf0\x9f\x98\x80'");
    eval("(1,)[2]", out);
    raised(out, CoreObject::IndexErrorType);
    exec("a, b = (1,)", globals, out);
    raised(out, CoreObject::ValueErrorType);
    exec("a, b = (1, 2, 3)", globals, out);
    raised(out, CoreObject::ValueErrorType);
}

TEST_F(CoreSource, RangeIterationHandlesBigBoundsBreakElseAndIteratorDefaults)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("value = 0\nfor i in range(10, 0, -2):\n    if i == 4:\n        break\n    value += "
         "i\nelse:\n    value = -1\n"
         "iterator = iter(range(18446744073709551616, 18446744073709551619))\n"
         "first = next(iterator)\nsecond = next(iterator)\nthird = next(iterator)\nlast = "
         "next(iterator, None)\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("value", out, globals);
    EXPECT_EQ(result(out), "24");
    eval("third - first", out, globals);
    EXPECT_EQ(result(out), "2");
    eval("last is None", out, globals);
    EXPECT_EQ(result(out), "True");
    eval("iter(iterator) is iterator", out, globals);
    EXPECT_EQ(result(out), "True");
    eval("range(10)[-2]", out);
    EXPECT_EQ(result(out), "8");
    eval("range(0, 18446744073709551616, 2)[3]", out);
    EXPECT_EQ(result(out), "6");
    eval("range(0, 3, 0)", out);
    raised(out, CoreObject::ValueErrorType);
    eval("len(range(18446744073709551616))", out);
    raised(out, CoreObject::OverflowErrorType);
    eval("not range(0)", out);
    EXPECT_EQ(result(out), "True");
    eval("range(0, 3, 2) == range(0, 4, 2)", out);
    EXPECT_EQ(result(out), "True");
    eval("hash(range(0, 3, 2)) == hash(range(0, 4, 2))", out);
    EXPECT_EQ(result(out), "True");
}

TEST_F(CoreSource, ItemMutationAndUserIterationUseOneEvaluatedReceiverAndNormalPythonCalls)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("calls = 0\nstored = 10\n"
         "def owner():\n    global calls\n    calls = calls * 10 + 1\n    return instance\n"
         "def key():\n    global calls\n    calls = calls * 10 + 3\n    return 0\n"
         "def rhs():\n    global calls\n    calls = calls * 10 + 4\n    return 5\n"
         "def get(self, key):\n    global calls\n    calls = calls * 10 + 2\n    return stored\n"
         "def set(self, key, value):\n    global calls, stored\n    calls = calls * 10 + 5\n    "
         "stored = value\n"
         "def erase(self, key):\n    global calls\n    calls = calls * 10 + 6\n"
         "def iterator(self):\n    return self\n"
         "def step(self):\n    return next(self.cursor)\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    TypeSpec spec{U"Indexed"};
    spec.slots = std::vector<std::u32string>{U"cursor"};
    create_type(context, spec, out);
    const auto type = roots.root(out.value());
    context.allocate_instance<InstanceObject>(type, out);
    const auto instance = roots.root(out.value());
    make_str(context, U"instance", out);
    detail::name_store(context, globals.get(context), out.value(), instance.get(context));
    for (auto [name, function] : {std::pair{U"__getitem__", U"get"},
                                  {U"__setitem__", U"set"},
                                  {U"__delitem__", U"erase"},
                                  {U"__iter__", U"iterator"},
                                  {U"__next__", U"step"}}) {
        const auto target =
            roots.root(*detail::name_lookup(context, globals.get(context), function));
        make_str(context, name, out);
        set_attribute(context, type, roots.root(out.value()), target, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
    exec("owner()[key()] += rhs()", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("calls", out, globals);
    EXPECT_EQ(result(out), "13245");
    eval("stored", out, globals);
    EXPECT_EQ(result(out), "15");
    exec("calls = 0\nowner()[key()] = rhs()", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("calls", out, globals);
    EXPECT_EQ(result(out), "4135");
    exec("calls = 0\ndel owner()[key()]", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("calls", out, globals);
    EXPECT_EQ(result(out), "136");
    exec("instance.cursor = iter(range(3))\nvalue = 0\nfor item in instance:\n    value += item\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("value", out, globals);
    EXPECT_EQ(result(out), "3");
    exec("(1, 2)[0] = 5", globals, out);
    raised(out, CoreObject::TypeErrorType);
}

TEST_F(CoreSource, ExceptionConstructionAndStopIterationValueUseIndependentManagedFields)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("error = StopIteration(73)\nargs = (1, 2)\nerror.args = args\n", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.args is args", out, globals);
    EXPECT_EQ(result(out), "True");
    eval("error.value", out, globals);
    EXPECT_EQ(result(out), "73");
    exec("error.value = 'changed'", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.value", out, globals);
    EXPECT_EQ(result(out), "'changed'");
    exec("BaseException.__init__(error, 5)", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.value", out, globals);
    EXPECT_EQ(result(out), "'changed'");
    exec("error.__init__(9)\nerror.args = range(3)", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.value", out, globals);
    EXPECT_EQ(result(out), "9");
    eval("error.args", out, globals);
    EXPECT_EQ(result(out), "(0, 1, 2)");
    exec("del error.value", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.value is None", out, globals);
    EXPECT_EQ(result(out), "True");
    eval("ValueError(1, 'message').args", out);
    EXPECT_EQ(result(out), "(1, 'message')");
    eval("StopIteration.__new__(StopIteration, 1).value", out);
    EXPECT_EQ(result(out), "None");
}

TEST_F(CoreSource, ExceptionHandlersClearAliasesAndReuseCapturedCellsOnEveryExit)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(events = 0
def f():
    global events
    error = 'old'
    def read():
        return error
    try:
        raise ValueError(7)
    except TypeError:
        events = 99
    except (TypeError, ValueError) as error:
        value = read().args[0]
    else:
        value = 99
    finally:
        events = events * 10 + 1
    try:
        read()
    except NameError:
        cleared = True
    return value, cleared, events
def returning():
    try:
        raise ValueError(8)
    except Exception as error:
        return error
saved = returning()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("f()", out, globals);
    EXPECT_EQ(result(out), "(7, True, 1)");
    eval("saved.args", out, globals);
    EXPECT_EQ(result(out), "(8,)");
    exec("error = 'previous'\ntry:\n    raise ValueError\nexcept Exception as error:\n    del "
         "error\n",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error", out, globals);
    raised(out, CoreObject::NameErrorType);
}

TEST_F(CoreSource, FinallyComposesWithLoopControlReturnsAndSeparateElsePaths)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(def total():
    value = 0
    for item in range(5):
        try:
            if item == 2:
                continue
            if item == 4:
                break
            value += item
        finally:
            value += 10
    else:
        value = 999
    return value
def normal():
    value = 0
    try:
        try:
            value = 1
        except TypeError:
            value = 99
        else:
            raise TypeError(3)
    except TypeError as error:
        value += error.args[0]
    finally:
        value += 10
    return value
def override():
    try:
        return 1
    finally:
        return 2
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("total(), normal(), override()", out, globals);
    EXPECT_EQ(result(out), "(54, 14, 2)");
}

TEST_F(CoreSource, RaiseNormalizationReraiseAndChainingUseManagedExceptionFields)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(inner = ValueError('inner')
def reraiser():
    raise
try:
    raise inner
except ValueError:
    try:
        reraiser()
    except ValueError as error:
        same = error is inner
    try:
        raise TypeError('outer') from None
    except TypeError as error:
        outer = error
try:
    raise ValueError from TypeError
except ValueError as error:
    explicit = error
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("same, outer.__context__ is inner, outer.__cause__ is None, outer.__suppress_context__, "
         "type(explicit.__cause__) is TypeError",
         out, globals);
    EXPECT_EQ(result(out), "(True, True, True, True, True)");
    exec("raise", globals, out);
    raised(out, CoreObject::RuntimeErrorType);
    exec("raise 1", globals, out);
    raised(out, CoreObject::TypeErrorType);
    exec("raise ValueError from 1", globals, out);
    raised(out, CoreObject::TypeErrorType);
}

TEST_F(CoreSource, WithUsesOrdinaryPythonMethodsAndReversesNestedExitsIncludingSuppression)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(events = 0
count = 0
suppress = False
def __enter__(self):
    global events, count
    count += 1
    self.tag = count
    events = events * 10 + self.tag
    return 4, 5
def __exit__(self, kind, error, traceback):
    global events
    events = events * 10 + self.tag
    return suppress
Manager = type('Manager', (), __enter__.__globals__)
with (Manager(), Manager() as (a, b),):
    value = a + b
normal = events
events = 0
count = 0
suppress = True
with Manager(), (Manager()) as value:
    raise ValueError('suppressed')
suppressed = events
events = 0
count = 0
def returned():
    with Manager() as value:
        return value
saved = returned()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("normal, suppressed, saved, events", out, globals);
    EXPECT_EQ(result(out), "(1221, 1221, (4, 5), 11)");
    exec("suppress = False\nwith Manager() as (a, b, c):\n    pass", globals, out);
    raised(out, CoreObject::ValueErrorType);
    exec("with None:\n    pass", globals, out);
    raised(out, CoreObject::TypeErrorType);
}

TEST_F(CoreSource, ExceptionMatcherFailuresKeepTheirContextAndOuterHandler)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(original = ValueError(1)
try:
    try:
        raise original
    except None:
        pass
except TypeError as error:
    saved = error
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("saved.__context__ is original", out, globals);
    EXPECT_EQ(result(out), "True");
}

TEST_F(CoreSource, ExceptionChainDescriptorsValidateAndPublishTheirActualFields)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(error = ValueError()
other = TypeError()
error.__context__ = other
error.__cause__ = other
error.__suppress_context__ = False
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.__context__ is other, error.__cause__ is other, error.__suppress_context__", out,
         globals);
    EXPECT_EQ(result(out), "(True, True, False)");
    exec("error.__cause__ = None\nerror.__context__ = None", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("error.__context__, error.__cause__, error.__suppress_context__", out, globals);
    EXPECT_EQ(result(out), "(None, None, True)");
    for (auto source :
         {"error.__cause__ = ValueError", "error.__context__ = 1", "error.__suppress_context__ = 1",
          "del error.__cause__", "del error.__context__", "del error.__suppress_context__"}) {
        SCOPED_TRACE(source);
        exec(source, globals, out);
        raised(out, CoreObject::TypeErrorType);
    }
}

TEST_F(CoreSource, ClassesExecuteInTheirOwnNamespaceAndBindOrdinaryMethods)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(value = 90
class Base:
    'base documentation'
    value = 3
    def __init__(self, n=4):
        self.n = n
    def total(self):
        return self.n + self.value
class Child(Base):
    @staticmethod
    def scale(n):
        return n * 2
    @classmethod
    def identify(cls):
        return cls
instance = Child(7)
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("instance.total(), Child.scale(5), instance.identify() is Child, value, Base.__doc__, "
         "Child.__qualname__",
         out, globals);
    EXPECT_EQ(result(out), "(10, 10, True, 90, 'base documentation', 'Child')");
}

TEST_F(CoreSource, ClassBindingsDoNotCaptureMethodsButForwardOuterClosureCells)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(def factory(x):
    class Outer:
        x = 99
        captured = x
        def read(self):
            return x, __class__
        class Inner:
            value = x
    return Outer
A = factory(7)
B = factory(8)
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("A.captured, A.Inner.value, A().read()[0], B().read()[0], A().read()[1] is A, "
         "A.read.__code__ is B.read.__code__",
         out, globals);
    EXPECT_EQ(result(out), "(99, 7, 7, 8, True, True)");
}

TEST_F(CoreSource, ClassCellsCanCoexistWithAnOuterCellOfTheSameName)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class A:
    def make(self):
        class B:
            owner = __class__
            def who(self):
                return __class__
        return B
B = A().make()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("B.owner is A, B().who() is B, B.who.__qualname__", out, globals);
    EXPECT_EQ(result(out), "(True, True, 'A.make.<locals>.B.who')");
    eval("A.make.__code__.co_consts", out, globals);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    bool found = false;
    for (auto value : context.payload<TupleObject>(out.value()).items) {
        if (context.type_of(value) != context.builtin(CoreObject::CodeType))
            continue;
        const auto &code = context.payload<CodeObject>(value);
        if (context.payload<StrObject>(code.name).value != U"B")
            continue;
        found = true;
        EXPECT_EQ(code.program.kind(), CodeKind::ClassBody);
        EXPECT_TRUE(context.payload<TupleObject>(code.varnames).items.empty());
        ASSERT_EQ(code.program.cell_names().size(), 1U);
        ASSERT_EQ(code.program.free_names().size(), 1U);
        EXPECT_EQ(code.program.cell_names()[0], U"__class__");
        EXPECT_EQ(code.program.free_names()[0], U"__class__");
        ASSERT_EQ(code.program.cell_locals().size(), 1U);
        EXPECT_FALSE(code.program.cell_locals()[0]);
    }
    EXPECT_TRUE(found);
}

TEST_F(CoreSource, PrivateNamesAndDecoratorOrderPreserveSourceVisibleIdentities)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(events = 0
def decorator(n):
    global events
    events = events * 10 + n
    def apply(value):
        global events
        events = events * 10 + n + 2
        return value
    return apply
@decorator(1)
@decorator(2)
class C:
    __slots__ = ('__value',)
    @decorator(5)
    def __init__(self, __value=7):
        self.__value = __value
    def read(self):
        return self.__value
    def __hidden(self):
        return 42
c = C()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("c.read(), c._C__hidden(), C._C__hidden.__name__, C._C__hidden.__qualname__, "
         "C.__init__.__code__.co_varnames, events",
         out, globals);
    EXPECT_EQ(result(out), "(7, 42, '__hidden', 'C.__hidden', ('self', '_C__value'), 125743)");
}

TEST_F(CoreSource, ClassTypeHooksAndMroEntriesRunThroughNormalCalls)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(events = 0
class Descriptor:
    def __set_name__(self, owner, name):
        global events
        events = events * 10 + 1
        self.owner = owner
    def __get__(self, instance, owner):
        return self.owner
class Base:
    def __init_subclass__(cls, *, flag=2):
        global events
        events = events * 10 + flag
class Alias:
    def __mro_entries__(self, bases):
        return (Base,)
alias = Alias()
class Child(alias, flag=3):
    owner = Descriptor()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("Child.owner is Child, Child.__bases__[0] is Base, Child.__orig_bases__[0] is alias, "
         "events",
         out, globals);
    EXPECT_EQ(result(out), "(True, True, True, 13)");
    exec("bases = Child.__bases__\nmro = Child.__mro__\nChild.other = 42", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("Child.__bases__ is bases, Child.__mro__ is mro, Child.__base__ is Base", out, globals);
    EXPECT_EQ(result(out), "(True, True, True)");
}

TEST_F(CoreSource, MetaclassPrepareUsesGenericNamespaceProtocolsAndPreservesLookupErrors)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class Namespace:
    def __getitem__(self, key):
        if key == 'injected':
            return 17
        raise KeyError(key)
    def __setitem__(self, key, value):
        if key == 'value':
            self.saved = value
class Meta(type):
    @classmethod
    def __prepare__(cls, name, bases, *, flag):
        return Namespace()
    def __new__(cls, name, bases, namespace, *, flag):
        return namespace
class C(metaclass=Meta, flag=8):
    value = injected
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("C.saved", out, globals);
    EXPECT_EQ(result(out), "17");
    eval("str(KeyError('x')), str(KeyError(1, 2)), str(KeyError()), "
         "BaseException.__str__(KeyError('x'))",
         out);
    EXPECT_EQ(result(out), "(\"'x'\", '(1, 2)', '', 'x')");
    eval("str(), str(None), str(object=42), str(encoding='utf8')", out);
    EXPECT_EQ(result(out), "('', 'None', '42', '')");
    exec(R"(def bad(self, key):
    raise ValueError('lookup')
Namespace.__getitem__ = bad
class D(metaclass=Meta, flag=8):
    pass
)",
         globals, out);
    raised(out, CoreObject::ValueErrorType);
}

TEST_F(CoreSource, ClassCellPropagationFailuresArePythonExceptionsNotHostFaults)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class Drop(type):
    def __new__(cls, name, bases, namespace):
        return type.__new__(cls, name, bases, type.__prepare__(name, bases))
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    exec(R"(class C(metaclass=Drop):
    def who(self):
        return __class__
)",
         globals, out);
    raised(out, CoreObject::RuntimeErrorType);
    exec(R"(class Wrong(type):
    def __new__(cls, name, bases, namespace):
        result = type.__new__(cls, name, bases, namespace)
        result.who.__closure__[0].cell_contents = None
        return result
class D(metaclass=Wrong):
    def who(self):
        return __class__
)",
         globals, out);
    raised(out, CoreObject::TypeErrorType);
}

TEST_F(CoreSource, ClassFunctionCellAndMroCyclesReturnToTheOriginalLiveSet)
{
    const SourceUnit unit(SourceBuffer(R"(class C:
    def who(self):
        return __class__
result = C().who()
)"));
    context.collect();
    const auto baseline = context.stats().objects;
    for (int i = 0; i < 10; ++i) {
        {
            ReturnSlot out(context);
            run(context, {&unit}, out);
            ASSERT_EQ(out.tag(), ResultTag::Ok);
        }
        context.collect();
        EXPECT_EQ(context.stats().objects, baseline);
    }
}

TEST_F(CoreSource, FunctionsLoopsDefaultsKeywordsAndRuntimeCodeIdentityCompose)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec("def sum_to(n, /, step=1, *, extra=0):\n"
         "    total = 0\n"
         "    while n:\n"
         "        total += n\n"
         "        n -= step\n"
         "    else:\n"
         "        total += extra\n"
         "    return total\n"
         "result = sum_to(100, extra=2)\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("result", out, globals);
    EXPECT_EQ(result(out), "5052");
    eval("sum_to.__name__", out, globals);
    EXPECT_EQ(result(out), "'sum_to'");
    const auto fn = *detail::name_lookup(context, globals.get(context), U"sum_to");
    const auto code = context.payload<FunctionObject>(fn).code;
    eval("sum_to.__code__", out, globals);
    EXPECT_EQ(out.result().value(), code);
    exec("def factory():\n    def child(x=42):\n        return x\n    return child\n"
         "a = factory()\nb = factory()\nresult = a is not b and a.__code__ is b.__code__\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("result", out, globals);
    EXPECT_EQ(result(out), "True");
}

TEST_F(CoreSource, ScopeClassificationAndClosureLayoutsAreFrozenBeforeEmission)
{
    SourceUnit unit(SourceBuffer(
        "x = 1\ndef f(arg):\n    before = later\n    later = arg\n    return before\n"));
    ASSERT_EQ(unit.scopes().size(), 2U);
    const auto &scope = unit.scopes()[1];
    EXPECT_EQ(scope.locals, (std::vector<std::u32string>{U"arg", U"before", U"later"}));
    for (const auto &symbol : scope.symbols)
        EXPECT_EQ(symbol.classification, NameClass::Local);
    ReturnSlot out(context);
    auto globals = dictionary();
    run(context, {&unit, globals}, out);
    eval("f(2)", out, globals);
    raised(out, CoreObject::UnboundLocalErrorType);
    SourceUnit closure(SourceBuffer("def outer(x):\n    def middle():\n        def inner():\n      "
                                    "      return x\n        return inner\n    return middle\n"));
    EXPECT_EQ(closure.scopes()[1].symbols[0].classification, NameClass::Cell);
    bool free = false;
    for (const auto &symbol : closure.scopes()[2].symbols)
        free |= symbol.name == U"x" && symbol.classification == NameClass::Free;
    EXPECT_TRUE(free);
    closure.freeze(context, out);
    EXPECT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(closure.scopes()[1].cellvars, (std::vector<std::u32string>{U"x"}));
    EXPECT_EQ(closure.scopes()[2].freevars, (std::vector<std::u32string>{U"x"}));
    run(context, {&closure, globals}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("outer(42)()()", out, globals);
    EXPECT_EQ(result(out), "42");
}

TEST_F(CoreSource, ClosuresShareSiblingCellsButNotOtherFactoryInvocations)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("def outer(start):\n    global writer, eraser\n    value = start\n"
         "    def read():\n        return value\n"
         "    def write(new):\n        nonlocal value\n        value = new\n        return value\n"
         "    def erase():\n        nonlocal value\n        del value\n"
         "    writer = write\n    eraser = erase\n    return read\n"
         "first = outer(7)\nsecond = outer(40)\nwriter(42)\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("first() * 100 + second()", out, globals);
    EXPECT_EQ(result(out), "742");
    const auto second = *detail::name_lookup(context, globals.get(context), U"second");
    const auto writer = *detail::name_lookup(context, globals.get(context), U"writer");
    const auto closure = *context.payload<FunctionObject>(second).closure;
    const auto &cells = context.payload<TupleObject>(closure).items;
    ASSERT_EQ(cells.size(), 1U);
    EXPECT_EQ(
        cells[0],
        context.payload<TupleObject>(*context.payload<FunctionObject>(writer).closure).items[0]);
    eval("second.__closure__", out, globals);
    EXPECT_EQ(out.result().value(), closure);
    eval("eraser()", out, globals);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("second()", out, globals);
    raised(out, CoreObject::NameErrorType);
    eval("eraser()", out, globals);
    raised(out, CoreObject::NameErrorType);
    eval("writer(99)", out, globals);
    EXPECT_EQ(result(out), "99");
    eval("second()", out, globals);
    EXPECT_EQ(result(out), "99");
    eval("writer('changed')", out, globals);
    EXPECT_EQ(result(out), "'changed'");
    eval("second()", out, globals);
    EXPECT_EQ(result(out), "'changed'");
    eval("writer(None)", out, globals);
    EXPECT_EQ(result(out), "None");
    eval("second() is None", out, globals);
    EXPECT_EQ(result(out), "True");
}

TEST_F(CoreSource, CapturedParametersDefaultsAndIntermediateScopesUseOneCellAuthority)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("x = 100\ndef outer(x, /, *args, flag=2, **kw):\n"
         "    def middle():\n        def inner(step=x, *, extra=flag):\n"
         "            return x + flag + len(args) + len(kw) + step + extra\n"
         "        return inner\n    x += 1\n    return middle\n"
         "a = outer(10, 1, 2, flag=3, key=1)()\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("a()", out, globals);
    EXPECT_EQ(result(out), "31");
    eval("a(1, extra=2)", out, globals);
    EXPECT_EQ(result(out), "20");
    eval("a.__code__.co_freevars", out, globals);
    EXPECT_EQ(result(out), "('args', 'flag', 'kw', 'x')");
    eval("outer.__code__.co_cellvars", out, globals);
    EXPECT_EQ(result(out), "('x', 'flag', 'args', 'kw')");
    eval("outer.__code__.co_varnames", out, globals);
    EXPECT_EQ(result(out), "('x', 'flag', 'args', 'kw', 'middle')");
    exec("def local():\n    value = 42\n    def inner():\n        return value\n    return inner\n",
         globals, out);
    eval("local.__code__.co_varnames", out, globals);
    EXPECT_EQ(result(out), "('inner',)");
    eval("x", out, globals);
    EXPECT_EQ(result(out), "100");
}

TEST_F(CoreSource, EmptyCapturedLocalsAndFreeVariablesRaiseDifferentPythonErrors)
{
    const auto globals = dictionary();
    ReturnSlot out(context);
    exec("def outer(flag):\n    if flag:\n        x = 1\n    def inner():\n        return x\n    "
         "return inner\n"
         "def local():\n    def inner():\n        return x\n    return x\n    x = 1\n"
         "def deleted():\n    x = 1\n    def inner():\n        return x\n    del x\n    del x\n"
         "def recursive():\n    def fact(n):\n        if n <= 1:\n            return 1\n        "
         "return n * fact(n - 1)\n    return fact\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("outer(0)()", out, globals);
    raised(out, CoreObject::NameErrorType);
    eval("local()", out, globals);
    raised(out, CoreObject::UnboundLocalErrorType);
    eval("deleted()", out, globals);
    raised(out, CoreObject::UnboundLocalErrorType);
    eval("recursive()(10)", out, globals);
    EXPECT_EQ(result(out), "3628800");
    eval("outer(1)()", out, globals);
    EXPECT_EQ(result(out), "1");
}

TEST_F(CoreSource, GlobalsAreExplicitAndDefaultsEvaluateAtDefinitionTime)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec("count = 0\ndef next_value():\n    global count\n    count += 1\n    return count\n"
         "def f(a=next_value(), *, b=next_value()):\n    return a * 10 + b\n"
         "first = f()\nsecond = f()\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("count * 100 + first + second", out, globals);
    EXPECT_EQ(result(out), "224");
    exec(
        "def f(*args, **kwargs):\n    return len(args) * 10 + len(kwargs)\nresult = f(1, 2, x=3)\n",
        globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("result", out, globals);
    EXPECT_EQ(result(out), "21");
}

TEST_F(CoreSource, ControlFlowKeepsBreakContinueElseAndComparisonEvaluationOrder)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec("i = 0\ntotal = 0\nwhile i < 10:\n    i += 1\n    if i == 3:\n        continue\n    elif "
         "i == 7:\n        break\n    else:\n        total += i\nelse:\n    total = 999\n"
         "count = 0\ndef bump():\n    global count\n    count += 1\n    return count\n"
         "chain = 0 < bump() < 2\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("total * 10 + count", out, globals);
    EXPECT_EQ(result(out), "181");
    eval("chain", out, globals);
    EXPECT_EQ(result(out), "True");
}

TEST_F(CoreSource, AttributeTargetsAndInplaceDoNotReevaluateTheReceiver)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    create_type(context, {U"Receiver"}, out);
    const auto cls = roots.root(out.result().value());
    context.allocate_instance<InstanceObject>(cls, out);
    const auto self = roots.root(out.result().value());
    make_str(context, U"obj", out);
    detail::name_store(context, globals.get(context), out.result().value(), self.get(context));
    exec("count = 0\ndef receiver():\n    global count\n    count += 1\n    return obj\n"
         "receiver().x = 40\nreceiver().x += 2\nresult = obj.x\ndel obj.x\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("result + count", out, globals);
    EXPECT_EQ(result(out), "44");
    eval("obj.x", out, globals);
    raised(out, CoreObject::AttributeErrorType);
}

TEST_F(CoreSource, NestedBooleanAndCompareConditionsDoNotRepeatTruthCallbacks)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    context.allocate_instance<CellObject>(roots.root(context.builtin(CoreObject::CellType)), out,
                                          context.builtin(CoreObject::IntZero));
    const auto counter = roots.root(out.result().value());
    create_type(context, {U"Truth"}, out);
    const auto truth_type = roots.root(out.result().value());
    SignatureLayout signature;
    signature.parameters = {{U"self"}};
    std::array captures{counter.get(context)};
    make_native_function(
        context, "count bool", signature,
        [](Attachment &context, Handle fn, std::span<const Value>, ReturnSlot &out) {
            const auto cell = context.payload<NativeFunctionObject>(fn).captures[0];
            ReturnSlot previous(context);
            read_cell(context, cell, previous);
            std::int64_t value = 0;
            int_fits_signed(context, previous.result().value(), value);
            make_int(context, value + 1, out);
            write_cell(context, cell, out.result().value());
            out.ok(context.builtin(CoreObject::False));
        },
        captures, out);
    const auto method = roots.root(out.result().value());
    make_str(context, U"__bool__", out);
    set_attribute(context, truth_type, roots.root(out.result().value()), method, out);
    context.allocate_instance<InstanceObject>(truth_type, out);
    const auto flag = roots.root(out.result().value());
    make_str(context, U"flag", out);
    detail::name_store(context, globals.get(context), out.result().value(), flag.get(context));
    exec("result = flag and flag and missing\nif flag and missing:\n    result = 99\nresult = "
         "(flag and flag) or 42\n",
         globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    ReturnSlot count(context);
    read_cell(context, counter.get(context), count);
    EXPECT_EQ(int_decimal(context, count.result().value()), "3");
    eval("result", out, globals);
    EXPECT_EQ(result(out), "42");
}

TEST_F(CoreSource, SourceLocationsAndStructuredDiagnosticsPreserveTheOriginalLine)
{
    try {
        SourceUnit unit(SourceBuffer("def f():\n  return (1 + )\n", "broken.py"));
        FAIL();
    } catch (const SourceError &error) {
        EXPECT_EQ(error.filename, "broken.py");
        EXPECT_EQ(error.position.line, 2U);
        EXPECT_EQ(error.position.column, 14U);
        EXPECT_EQ(error.source_line, "  return (1 + )\n");
    }
    SourceUnit valid(SourceBuffer("def f():\n    return 1 // 0\nresult = f()\n", "trace.py"));
    auto globals = dictionary();
    ReturnSlot out(context);
    run(context, {&valid, globals}, out);
    raised(out, CoreObject::ZeroDivisionErrorType);
    ReturnSlot trace(context);
    context.load(out.result().exception(),
                 context.payload<ExceptionObject>(out.result().exception()).traceback, trace);
    ASSERT_EQ(trace.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<TracebackObject>(trace.result().value()).lineno, 3);
    const auto node = trace.result().value();
    context.load(node, context.payload<TracebackObject>(node).next, trace);
    EXPECT_EQ(context.payload<TracebackObject>(trace.result().value()).lineno, 2);
}

TEST(CompilerDiagnostics, ScopeAndControlFlowErrorsRemainCompileTimeErrors)
{
    for (auto source :
         {"return 1", "break", "continue", "def f(a, a): pass", "global x\nnonlocal x",
          "nonlocal x", "x = 1\nglobal x", "try: pass\nexcept: pass\nexcept ValueError: pass",
          "try: pass\nexcept ValueError as __debug__: pass", "try: pass\nfinally: break"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(SourceUnit{SourceBuffer(source)}, SourceError);
    }
}
TEST(CompilerDiagnostics, RecognizedUnimplementedFeaturesAreExplicitCapabilities)
{
    for (auto source : {"class X[T]: pass"}) {
        SCOPED_TRACE(source);
        try {
            SourceUnit unit{SourceBuffer(source)};
            FAIL();
        } catch (const SourceError &error) {
            EXPECT_EQ(error.kind, SourceErrorKind::Capability);
        }
    }
}
TEST(CompilerDiagnostics, SourceBufferAndCompilationLimitsDoNotUseAnotherParser)
{
    EXPECT_THROW(SourceBuffer(std::string("x\0y", 3)), SourceError);
    EXPECT_THROW(SourceBuffer("\xc0\x80"), SourceError);
    std::string nested(1000, '-');
    nested += '1';
    EXPECT_THROW(SourceUnit{SourceBuffer(nested)}, SourceError);
}

TEST_F(CoreSource, ModuleAndFunctionDocstringsAndDefaultEnvironmentAreRealObjects)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec("'module doc'\ndef f():\n    'function doc'\n    return __debug__\n", globals, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    eval("__doc__", out, globals);
    EXPECT_EQ(result(out), "'module doc'");
    eval("f.__doc__", out, globals);
    EXPECT_EQ(result(out), "'function doc'");
    eval("f()", out, globals);
    EXPECT_EQ(result(out), "True");
    eval("__name__", out);
    EXPECT_EQ(result(out), "'__main__'");
}

TEST_F(CoreSource, FreezeFailureAndAliasedRunInputsUseCallerOwnedRoots)
{
    SourceUnit unit(SourceBuffer("40 + 2"), InputMode::Eval);
    ReturnSlot out(context);
    context.set_allocation_limit(0);
    unit.freeze(context, out);
    raised(out, CoreObject::MemoryErrorType);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    unit.freeze(context, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    RootScope temporary(context);
    const auto code = temporary.root(out.result().value());
    run(context, {code}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "42");
}

TEST_F(CoreSource, RecursiveClosureCyclesReturnToTheOriginalLiveSet)
{
    const SourceUnit unit(SourceBuffer(
        "def outer():\n    def recurse(n):\n        if n:\n            return recurse(n - 1)\n     "
        "   return 42\n    return recurse\nresult = outer()(7)\n"));
    context.collect();
    const auto baseline = context.stats().objects;
    for (int i = 0; i < 10; ++i) {
        {
            ReturnSlot out(context);
            run(context, {&unit}, out);
            ASSERT_EQ(out.tag(), ResultTag::Ok);
        }
        context.collect();
        EXPECT_EQ(context.stats().objects, baseline);
    }
}

TEST(SourceParallel, ImmutableNativeGraphsFreezeConcurrentlyWithoutSharedPythonState)
{
    const SourceUnit unit(SourceBuffer("def f(n=40.5):\n    try:\n        return int(n + 1.5)\n    "
                                       "finally:\n        pass\nresult = f()\n"));
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    std::latch ready(2), start(1);
    std::array<std::future<Transfer>, 2> tasks;
    {
        NativeWait wait(owner);
        for (auto &task : tasks)
            task = std::async(std::launch::async, [&] {
                Attachment context(interpreter);
                RootScope roots(context);
                ReturnSlot globals(context), result(context);
                ready.count_down();
                start.wait();
                context.allocate_instance<DictObject>(
                    roots.root(context.builtin(CoreObject::DictType)), globals);
                const auto mapping = roots.root(globals.result().value());
                run(context, {&unit, mapping}, result);
                if (result.result().tag() != ResultTag::Ok)
                    throw std::runtime_error("concurrent source freeze failed");
                return context.make_transfer(
                    roots.root(*detail::name_lookup(context, mapping.get(context), U"result")));
            });
        ready.wait();
        EXPECT_EQ(interpreter.gc_state().active, 2U);
        start.count_down();
    }
    ReturnSlot out(owner);
    for (auto &task : tasks) {
        Transfer token;
        {
            NativeWait wait(owner);
            token = task.get();
        }
        owner.receive(token, out);
        EXPECT_EQ(int_decimal(owner, out.result().value()), "42");
    }
}

TEST(SourceParallel, OneFrozenSourceAndFunctionRunInOneSharedInterpreter)
{
    for (unsigned variant : {0U, 1U, 2U, 3U, 4U, 5U}) {
        const bool closure = variant == 1;
        SCOPED_TRACE(variant);
        const SourceUnit unit(SourceBuffer(
            variant == 5   ? R"(def total(n):
    result = 0.0
    for item in range(n, 0, -1):
        result += item / 2.0
    return int(result * 2.0)
)"
            : variant == 4 ? R"(def total(n):
    for unused in range(8):
        class Operation:
            @staticmethod
            def item(value):
                return value
    result = 0
    for item in range(n, 0, -1):
        result += Operation.item(item)
    return result
)"
            : variant == 3 ? R"(def total(n):
    result = 0
    for item in range(n, 0, -1):
        try:
            if item % 7 == 0:
                raise ValueError(item)
            result += item
        except ValueError as error:
            result += error.args[0]
        finally:
            pass
    return result
)"
            : variant == 2
                ? "def total(n):\n    result = 0\n    for item in range(n, 0, -1):\n      "
                  "  result += item\n    return result\n"
            : closure ? "def outer(step):\n    shared = 0\n    def total(n):\n        "
                        "nonlocal shared\n        result = 0\n"
                        "        while n:\n            shared = n\n            result "
                        "+= n * step\n            n -= 1\n"
                        "        return result\n    return total\ntotal = outer(1)\n"
                      : "def total(n):\n    result = 0\n    while n:\n        result += "
                        "n\n        n -= 1\n    return result\n"));
        Interpreter interpreter;
        Attachment source(interpreter);
        source.bootstrap_core();
        RootScope roots(source);
        ReturnSlot out(source);
        source.allocate_instance<DictObject>(roots.root(source.builtin(CoreObject::DictType)), out);
        const auto globals = roots.root(out.value());
        run(source, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto fn = roots.root(*detail::name_lookup(source, globals.get(source), U"total"));
        std::array transfers{source.make_transfer(fn), source.make_transfer(fn)};
        std::latch ready(2), start(1);
        std::array<std::future<Transfer>, 2> tasks;
        {
            NativeWait wait(source);
            for (std::size_t i = 0; i < tasks.size(); ++i)
                tasks[i] =
                    std::async(std::launch::async, [&, token = std::move(transfers[i])]() mutable {
                        bool announced = false;
                        try {
                            Attachment context(interpreter);
                            RootScope roots(context);
                            ReturnSlot function(context), argument(context), result(context);
                            context.receive(token, function);
                            const auto fn = roots.root(function.value());
                            announced = true;
                            ready.count_down();
                            start.wait();
                            make_int(context, 20000, argument);
                            std::array args{argument.value()};
                            call(context, fn, {args, {}}, result);
                            if (result.tag() != ResultTag::Ok)
                                throw std::runtime_error("source call failed");
                            context.collect();
                            return context.make_transfer(roots.root(result.value()));
                        } catch (...) {
                            if (!announced)
                                ready.count_down();
                            throw;
                        }
                    });
            ready.wait();
            EXPECT_EQ(interpreter.gc_state().active, 2U);
            start.count_down();
        }
        for (auto &task : tasks) {
            Transfer result;
            {
                NativeWait wait(source);
                EXPECT_NO_THROW(result = task.get());
            }
            if (!result)
                continue;
            source.receive(result, out);
            EXPECT_EQ(int_decimal(source, out.value()), "200010000");
        }
        if (closure) {
            const auto tuple = *source.payload<FunctionObject>(fn).closure;
            const auto cell = source.payload<TupleObject>(tuple).items[0];
            ASSERT_TRUE(read_cell(source, cell, out));
            EXPECT_EQ(int_decimal(source, out.value()), "1");
        }
    }
}
} // namespace
