#include "cxxpython/core/compiler.hpp"

#include <gtest/gtest.h>

#include <future>
#include <latch>

namespace
{
    using namespace cxxpython::core;
    class CoreSource : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter}; RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle dictionary()
        { ReturnSlot out(context); context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out); return roots.root(out.result().value()); }
        void exec(std::string source, Handle globals, ReturnSlot &out)
        { SourceUnit unit(SourceBuffer(std::move(source), "fixture.py")); run(context, {&unit, globals}, out); }
        void eval(std::string source, ReturnSlot &out, std::optional<Handle> globals = {})
        { SourceUnit unit(SourceBuffer(std::move(source), "expression.py"), InputMode::Eval); run(context, {&unit, globals}, out); }
        std::string result(ReturnSlot &out)
        {
            if (out.result().tag() != ResultTag::Ok) return "<raised>";
            ReturnSlot text(context); repr(context, roots.root(out.result().value()), text);
            return source_utf8(context.payload<StrObject>(text.result().value()).value);
        }
        void raised(ReturnSlot &out, CoreObject type)
        { ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type)); }
    };

    TEST_F(CoreSource, LiteralArithmeticBooleanAndComparisonProgramsUseOrdinaryProtocols)
    {
        const std::pair<const char *, const char *> cases[] = {
            {"1 + 2 * 3", "7"}, {"-(7 // 2) + ~0", "-4"}, {"-7 % 3", "2"}, {"+True", "1"},
            {"0x_FF + 0b101 + 0o7 + 1_000", "1267"}, {"18446744073709551616 * 2", "36893488147419103232"},
            {"'a\\n' 'b'", "'a\\nb'"}, {"'\\u4f60\\u597d'", "'\xe4\xbd\xa0\xe5\xa5\xbd'"},
            {"r'\\n'", "'\\\\n'"}, {"None is None", "True"}, {"1 is not None", "True"},
            {"0 and missing", "0"}, {"'ok' or missing", "'ok'"}, {"not 0", "True"},
            {"1 < 2 < 3", "True"}, {"3 < 2 < missing", "False"}, {"len('abc')", "3"},
        };
        ReturnSlot out(context);
        for (auto [source, expected] : cases) { SCOPED_TRACE(source); eval(source, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(result(out), expected); }
    }

    TEST_F(CoreSource, FunctionsLoopsDefaultsKeywordsAndRuntimeCodeIdentityCompose)
    {
        auto globals = dictionary(); ReturnSlot out(context);
        exec("def sum_to(n, /, step=1, *, extra=0):\n"
             "    total = 0\n"
             "    while n:\n"
             "        total += n\n"
             "        n -= step\n"
             "    else:\n"
             "        total += extra\n"
             "    return total\n"
             "result = sum_to(100, extra=2)\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("result", out, globals); EXPECT_EQ(result(out), "5052");
        eval("sum_to.__name__", out, globals); EXPECT_EQ(result(out), "'sum_to'");
        const auto fn = *detail::name_lookup(context, globals.get(context), U"sum_to"); const auto code = context.payload<FunctionObject>(fn).code;
        eval("sum_to.__code__", out, globals); EXPECT_EQ(out.result().value(), code);
        exec("def factory():\n    def child(x=42):\n        return x\n    return child\n"
             "a = factory()\nb = factory()\nresult = a is not b and a.__code__ is b.__code__\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("result", out, globals); EXPECT_EQ(result(out), "True");
    }

    TEST_F(CoreSource, ScopeClassificationIsFrozenBeforeEmissionAndRejectsUnloweredClosures)
    {
        SourceUnit unit(SourceBuffer("x = 1\ndef f(arg):\n    before = later\n    later = arg\n    return before\n"));
        ASSERT_EQ(unit.scopes().size(), 2U); const auto &scope = unit.scopes()[1];
        EXPECT_EQ(scope.locals, (std::vector<std::u32string>{U"arg", U"before", U"later"}));
        for (const auto &symbol : scope.symbols) EXPECT_EQ(symbol.classification, NameClass::Local);
        ReturnSlot out(context); auto globals = dictionary(); run(context, {&unit, globals}, out);
        eval("f(2)", out, globals); raised(out, CoreObject::UnboundLocalErrorType);
        SourceUnit closure(SourceBuffer("def outer(x):\n    def middle():\n        def inner():\n            return x\n        return inner\n    return middle\n"));
        EXPECT_EQ(closure.scopes()[1].symbols[0].classification, NameClass::Cell);
        bool free = false; for (const auto &symbol : closure.scopes()[2].symbols) free |= symbol.name == U"x" && symbol.classification == NameClass::Free;
        EXPECT_TRUE(free);
        try { closure.freeze(context, out); FAIL(); }
        catch (const SourceError &error) { EXPECT_EQ(error.kind, SourceErrorKind::Capability); }
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    }

    TEST_F(CoreSource, GlobalsAreExplicitAndDefaultsEvaluateAtDefinitionTime)
    {
        auto globals = dictionary(); ReturnSlot out(context);
        exec("count = 0\ndef next_value():\n    global count\n    count += 1\n    return count\n"
             "def f(a=next_value(), *, b=next_value()):\n    return a * 10 + b\n"
             "first = f()\nsecond = f()\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("count * 100 + first + second", out, globals); EXPECT_EQ(result(out), "224");
        exec("def f(*args, **kwargs):\n    return len(args) * 10 + len(kwargs)\nresult = f(1, 2, x=3)\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("result", out, globals); EXPECT_EQ(result(out), "21");
    }

    TEST_F(CoreSource, ControlFlowKeepsBreakContinueElseAndComparisonEvaluationOrder)
    {
        auto globals = dictionary(); ReturnSlot out(context);
        exec("i = 0\ntotal = 0\nwhile i < 10:\n    i += 1\n    if i == 3:\n        continue\n    elif i == 7:\n        break\n    else:\n        total += i\nelse:\n    total = 999\n"
             "count = 0\ndef bump():\n    global count\n    count += 1\n    return count\n"
             "chain = 0 < bump() < 2\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("total * 10 + count", out, globals); EXPECT_EQ(result(out), "181");
        eval("chain", out, globals); EXPECT_EQ(result(out), "True");
    }

    TEST_F(CoreSource, AttributeTargetsAndInplaceDoNotReevaluateTheReceiver)
    {
        auto globals = dictionary(); ReturnSlot out(context); create_type(context, {U"Receiver"}, out); const auto cls = roots.root(out.result().value());
        context.allocate_instance<InstanceObject>(cls, out); const auto self = roots.root(out.result().value());
        make_str(context, U"obj", out); detail::name_store(context, globals.get(context), out.result().value(), self.get(context));
        exec("count = 0\ndef receiver():\n    global count\n    count += 1\n    return obj\n"
             "receiver().x = 40\nreceiver().x += 2\nresult = obj.x\ndel obj.x\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("result + count", out, globals); EXPECT_EQ(result(out), "44");
        eval("obj.x", out, globals); raised(out, CoreObject::AttributeErrorType);
    }

    TEST_F(CoreSource, NestedBooleanAndCompareConditionsDoNotRepeatTruthCallbacks)
    {
        auto globals = dictionary(); ReturnSlot out(context);
        context.allocate_instance<CellObject>(roots.root(context.builtin(CoreObject::CellType)), out, context.builtin(CoreObject::IntZero));
        const auto counter = roots.root(out.result().value()); create_type(context, {U"Truth"}, out); const auto truth_type = roots.root(out.result().value());
        SignatureLayout signature; signature.parameters = {{U"self"}}; std::array captures{counter.get(context)};
        make_native_function(context, "count bool", signature,
            [](Attachment &context, Handle fn, std::span<const Value>, ReturnSlot &out) {
                const auto cell = context.payload<NativeFunctionObject>(fn).captures[0]; ReturnSlot previous(context); read_cell(context, cell, previous);
                std::int64_t value = 0; int_fits_signed(context, previous.result().value(), value); make_int(context, value + 1, out);
                write_cell(context, cell, out.result().value()); out.ok(context.builtin(CoreObject::False));
            }, captures, out);
        const auto method = roots.root(out.result().value()); make_str(context, U"__bool__", out); set_attribute(context, truth_type, roots.root(out.result().value()), method, out);
        context.allocate_instance<InstanceObject>(truth_type, out); const auto flag = roots.root(out.result().value());
        make_str(context, U"flag", out); detail::name_store(context, globals.get(context), out.result().value(), flag.get(context));
        exec("result = flag and flag and missing\nif flag and missing:\n    result = 99\nresult = (flag and flag) or 42\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); ReturnSlot count(context); read_cell(context, counter.get(context), count);
        EXPECT_EQ(int_decimal(context, count.result().value()), "3");
        eval("result", out, globals); EXPECT_EQ(result(out), "42");
    }

    TEST_F(CoreSource, SourceLocationsAndStructuredDiagnosticsPreserveTheOriginalLine)
    {
        try { SourceUnit unit(SourceBuffer("def f():\n  return (1 + )\n", "broken.py")); FAIL(); }
        catch (const SourceError &error)
        { EXPECT_EQ(error.filename, "broken.py"); EXPECT_EQ(error.position.line, 2U); EXPECT_EQ(error.position.column, 14U); EXPECT_EQ(error.source_line, "  return (1 + )\n"); }
        SourceUnit valid(SourceBuffer("def f():\n    return 1 // 0\nresult = f()\n", "trace.py")); auto globals = dictionary(); ReturnSlot out(context);
        run(context, {&valid, globals}, out); raised(out, CoreObject::ZeroDivisionErrorType);
        ReturnSlot trace(context); context.load(out.result().exception(), context.payload<ExceptionObject>(out.result().exception()).traceback, trace);
        ASSERT_EQ(trace.result().tag(), ResultTag::Ok); EXPECT_EQ(context.payload<TracebackObject>(trace.result().value()).lineno, 3);
        const auto node = trace.result().value(); context.load(node, context.payload<TracebackObject>(node).next, trace);
        EXPECT_EQ(context.payload<TracebackObject>(trace.result().value()).lineno, 2);
    }

    TEST(SourceDiagnostics, InvalidProgramsDoNotSilentlyCompileThroughAnotherLanguagePath)
    {
        for (auto source : {"return 1", "break", "continue", "def f(a, a): pass", "def f(a=1, b): pass", "1 = 2", "True = 1", "x = 09",
                            "x = 1__2", "if 1:\nx = 2", "  x = 1", "x = (1", "x = 1 x", "global x\nnonlocal x", "nonlocal x", "x = 1\nglobal x"})
        { SCOPED_TRACE(source); EXPECT_THROW(SourceUnit{SourceBuffer(source)}, SourceError); }
        for (auto source : {"import x", "class X: pass", "for x in y: pass", "f'{x}'", "x = 1.5", "x = [1]"})
        {
            SCOPED_TRACE(source);
            try { SourceUnit unit{SourceBuffer(source)}; FAIL(); }
            catch (const SourceError &error) { EXPECT_EQ(error.kind, SourceErrorKind::Capability); }
        }
        EXPECT_THROW(SourceBuffer(std::string("x\0y", 3)), SourceError);
        EXPECT_THROW(SourceBuffer("\xc0\x80"), SourceError);
        std::string nested(1000, '-'); nested += '1'; EXPECT_THROW(SourceUnit{SourceBuffer(nested)}, SourceError);
    }

    TEST_F(CoreSource, ModuleAndFunctionDocstringsAndDefaultEnvironmentAreRealObjects)
    {
        auto globals = dictionary(); ReturnSlot out(context);
        exec("'module doc'\ndef f():\n    'function doc'\n    return __debug__\n", globals, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); eval("__doc__", out, globals); EXPECT_EQ(result(out), "'module doc'");
        eval("f.__doc__", out, globals); EXPECT_EQ(result(out), "'function doc'");
        eval("f()", out, globals); EXPECT_EQ(result(out), "True");
        eval("__name__", out); EXPECT_EQ(result(out), "'__main__'");
    }

    TEST_F(CoreSource, FreezeFailureAndAliasedRunInputsUseCallerOwnedRoots)
    {
        SourceUnit unit(SourceBuffer("40 + 2"), InputMode::Eval); ReturnSlot out(context);
        context.set_allocation_limit(0); unit.freeze(context, out); raised(out, CoreObject::MemoryErrorType);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max()); unit.freeze(context, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        RootScope temporary(context); const auto code = temporary.root(out.result().value());
        run(context, {code}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(result(out), "42");
    }

    TEST(SourceParallel, ImmutableNativeGraphsFreezeConcurrentlyWithoutSharedPythonState)
    {
        const SourceUnit unit(SourceBuffer("def f(n=40):\n    return n + 2\nresult = f()\n"));
        Interpreter interpreter; Attachment owner(interpreter); owner.bootstrap_core(); std::latch ready(2), start(1);
        std::array<std::future<Transfer>, 2> tasks;
        {
            NativeWait wait(owner);
            for (auto &task : tasks) task = std::async(std::launch::async, [&] {
                Attachment context(interpreter); RootScope roots(context); ReturnSlot globals(context), result(context);
                ready.count_down(); start.wait(); context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), globals);
                const auto mapping = roots.root(globals.result().value()); run(context, {&unit, mapping}, result);
                if (result.result().tag() != ResultTag::Ok) throw std::runtime_error("concurrent source freeze failed");
                return context.make_transfer(roots.root(*detail::name_lookup(context, mapping.get(context), U"result")));
            });
            ready.wait(); EXPECT_EQ(interpreter.gc_state().active, 2U); start.count_down();
        }
        ReturnSlot out(owner);
        for (auto &task : tasks)
        {
            Transfer token; { NativeWait wait(owner); token = task.get(); }
            owner.receive(token, out); EXPECT_EQ(int_decimal(owner, out.result().value()), "42");
        }
    }

    TEST(SourceParallel, OneFrozenSourceAndFunctionRunInOneSharedInterpreter)
    {
        const SourceUnit unit(SourceBuffer("def total(n):\n    result = 0\n    while n:\n        result += n\n        n -= 1\n    return result\n"));
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        source.allocate_instance<DictObject>(roots.root(source.builtin(CoreObject::DictType)), out); const auto globals = roots.root(out.result().value());
        run(source, {&unit, globals}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        const auto fn = roots.root(*detail::name_lookup(source, globals.get(source), U"total"));
        std::array transfers{source.make_transfer(fn), source.make_transfer(fn)}; std::latch ready(2), start(1); std::array<std::future<Transfer>, 2> tasks;
        {
            NativeWait wait(source);
            for (std::size_t i = 0; i < tasks.size(); ++i)
                tasks[i] = std::async(std::launch::async, [&, token = std::move(transfers[i])]() mutable {
                    Attachment context(interpreter); RootScope roots(context); ReturnSlot function(context), argument(context), result(context);
                    context.receive(token, function); const auto fn = roots.root(function.result().value());
                    ready.count_down(); start.wait(); make_int(context, 20000, argument); std::array args{argument.result().value()};
                    call(context, fn, {args, {}}, result); if (result.result().tag() != ResultTag::Ok) throw std::runtime_error("source call failed");
                    context.collect(); return context.make_transfer(roots.root(result.result().value()));
                });
            ready.wait(); EXPECT_EQ(interpreter.gc_state().active, 2U); start.count_down();
        }
        for (auto &task : tasks)
        {
            Transfer result; { NativeWait wait(source); result = task.get(); }
            source.receive(result, out); EXPECT_EQ(int_decimal(source, out.result().value()), "200010000");
        }
    }
}
