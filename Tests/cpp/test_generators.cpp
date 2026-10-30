#include "cxxpython/compiler.hpp"
#include "cxxpython/genobject.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class GeneratorProtocol : public testing::Test {
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
        SourceUnit unit(SourceBuffer(source, "generator_protocol.py"));
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

TEST_F(GeneratorProtocol, HostFaultsAndManagedAllocationFailureCompleteTheExecutionClaim)
{
    for (bool host_fault : {false, true}) {
        SCOPED_TRACE(host_fault);
        ReturnSlot out(context);
        NativeEntry entry =
            host_fault ? +[](Attachment &, Handle, std::span<const Value>,
                             ReturnSlot &) { throw std::runtime_error("generator host fault"); }
                       : +[](Attachment &context, Handle, std::span<const Value>, ReturnSlot &out) {
                             context.set_allocation_limit(0);
                             out.ok(context.builtin(CoreObject::None));
                         };
        make_native_function(context, "fail in generator", {}, entry, {}, out);
        const auto fault = roots.root(out.value());
        CodeSpec spec;
        spec.generator = true;
        CodeBuilder builder(context, spec);
        builder.emit(Opcode::LoadConst, builder.constant(fault));
        builder.call(0);
        builder.emit(Opcode::PopTop);
        builder.emit(Opcode::LoadConst,
                     builder.constant(roots.root(context.builtin(CoreObject::EmptyTuple))));
        builder.emit(Opcode::GetIter);
        builder.emit(Opcode::YieldFrom);
        builder.emit(Opcode::Return);
        builder.freeze(out);
        const auto code = roots.root(out.value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        const auto globals = roots.root(out.value());
        make_function(context, {code, globals}, out);
        call(context, roots.root(out.value()), {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto generator = roots.root(out.value());
        context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame,
                     out);
        const auto frame = roots.root(out.value());
        if (host_fault) {
            EXPECT_THROW(iter_next(context, generator, out), std::runtime_error);
        } else {
            EXPECT_EQ(iter_next(context, generator, out), NextStatus::Raised);
            ASSERT_EQ(out.tag(), ResultTag::Raised);
            EXPECT_EQ(out.exception(), context.builtin(CoreObject::MemoryError));
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_EQ(context.payload<FrameObject>(frame).state.load(), FrameState::Completed);
        EXPECT_FALSE(context.current_frame());
        EXPECT_FALSE(context.handled_exception());
        context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame,
                     out);
        EXPECT_EQ(out.tag(), ResultTag::Empty);
        context.collect();
    }
}

TEST_F(GeneratorProtocol, FailedDelegationDropsItsIteratorBeforeYieldingFromTheHandler)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source{SourceBuffer("class Delegate:\n"
                                   "    def __iter__(self): return self\n"
                                   "    def __next__(self): return 1\n"
                                   "    def send(self, value): return value\n"
                                   "def f():\n"
                                   "    try:\n"
                                   "        yield from Delegate()\n"
                                   "    except MemoryError:\n"
                                   "        yield 'caught'\n"
                                   "g = f()\n")};
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    SourceUnit expression{SourceBuffer("g"), InputMode::Eval};
    run(context, {&expression, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto generator = roots.root(out.value());
    ASSERT_EQ(iter_next(context, generator, out), NextStatus::Item);
    make_str(context, U"send", out);
    get_attribute(context, generator, roots.root(out.value()), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto send = roots.root(out.value());
    context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame, out);
    const auto frame = roots.root(out.value());
    const Value args[] = {context.builtin(CoreObject::IntOne)};
    context.set_allocation_limit(0);
    call(context, send, {args}, out);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"caught");
    context.load(frame.get(context), context.payload<FrameObject>(frame).delegate, out);
    EXPECT_EQ(out.tag(), ResultTag::Empty);
}

TEST_F(GeneratorProtocol, SendLazinessAndTupleReturn)
{
    check("events = []\n"
          "def f():\n"
          "    events.append('started')\n"
          "    x = yield 1\n"
          "    return (x, 3)\n"
          "g = f()\n"
          "before = events[:]\n"
          "try:\n"
          "    g.send(9)\n"
          "except TypeError:\n"
          "    pass\n"
          "first = next(g)\n"
          "try:\n"
          "    g.send(2)\n"
          "except StopIteration as e:\n"
          "    result = e.value\n"
          "try:\n"
          "    next(g)\n"
          "except StopIteration as e:\n"
          "    exhausted = e.value\n",
          "(before, events, first, result, exhausted, g.gi_frame)",
          "([], ['started'], 1, (2, 3), None, None)");
}

TEST_F(GeneratorProtocol, ThrowAndCloseFinally)
{
    check("events = []\n"
          "def f():\n"
          "    try:\n"
          "        try:\n"
          "            yield 1\n"
          "        except ValueError as e:\n"
          "            yield str(e)\n"
          "    finally:\n"
          "        events.append('closed')\n"
          "g = f()\n"
          "next(g)\n"
          "value = g.throw(ValueError, 'bad')\n"
          "g.close()\n"
          "h = f()\n"
          "h.close()\n",
          "(value, events, g.gi_frame, h.gi_frame)", "('bad', ['closed'], None, None)");
}

TEST_F(GeneratorProtocol, IgnoredGeneratorExitAndPep479)
{
    check("def ignores():\n"
          "    try:\n"
          "        yield 1\n"
          "    except GeneratorExit:\n"
          "        yield 2\n"
          "g = ignores()\n"
          "next(g)\n"
          "try:\n"
          "    g.close()\n"
          "except RuntimeError as e:\n"
          "    message = str(e)\n"
          "suspended = g.gi_suspended\n"
          "g.close()\n"
          "def escapes():\n"
          "    raise StopIteration('oops')\n"
          "    yield 1\n"
          "try:\n"
          "    next(escapes())\n"
          "except RuntimeError as e:\n"
          "    cause = e.__cause__\n"
          "    linked = e.__context__ is cause\n",
          "(message, suspended, str(cause), linked)",
          "('generator ignored GeneratorExit', True, 'oops', True)");
}

TEST_F(GeneratorProtocol, MetadataAndReentry)
{
    check("def f():\n"
          "    try:\n"
          "        g.send(None)\n"
          "    except ValueError as e:\n"
          "        yield (str(e), g.gi_running)\n"
          "g = f()\n"
          "created = (g.gi_running, g.gi_suspended, g.gi_yieldfrom, g.gi_frame is None)\n"
          "g.__name__ = 'renamed'\n"
          "g.__qualname__ = 'qualified'\n"
          "value = next(g)\n"
          "suspended = (g.gi_running, g.gi_suspended, g.gi_frame is None)\n"
          "g.close()\n",
          "(created, value, suspended, g.__name__, g.__qualname__, g.gi_frame)",
          "((False, False, None, False), ('generator already executing', True), "
          "(False, True, False), 'renamed', 'qualified', None)");
}

TEST_F(GeneratorProtocol, ThrowPreservesDelegateArguments)
{
    check("events = []\n"
          "class Delegate:\n"
          "    def __iter__(self): return self\n"
          "    def __next__(self): return 1\n"
          "    def throw(self, *args):\n"
          "        events.append(args)\n"
          "        return 2\n"
          "def f():\n"
          "    yield from Delegate()\n"
          "g = f()\n"
          "next(g)\n"
          "value = g.throw(ValueError, ('a', 'b'))\n",
          "(value, events[0][0] is ValueError, events[0][1])", "(2, True, ('a', 'b'))");
}

TEST_F(GeneratorProtocol, ThrowValidationRestoresSuspension)
{
    check("def f():\n"
          "    yield 1\n"
          "    yield 2\n"
          "g = f()\n"
          "next(g)\n"
          "try:\n"
          "    g.throw(ValueError(), 'extra')\n"
          "except TypeError:\n"
          "    rejected = g.gi_suspended\n"
          "value = next(g)\n",
          "(rejected, value)", "(True, 2)");
}

TEST_F(GeneratorProtocol, DelegateThrowPrecedesExceptionNormalization)
{
    check("events = []\n"
          "class Error(Exception):\n"
          "    def __init__(self, *args):\n"
          "        events.append('constructed')\n"
          "class Delegate:\n"
          "    def __iter__(self): return self\n"
          "    def __next__(self): return 1\n"
          "    def throw(self, *args):\n"
          "        events.append(args)\n"
          "        return 2\n"
          "def f():\n"
          "    yield from Delegate()\n"
          "g = f()\n"
          "next(g)\n"
          "first = g.throw(Error, 'value')\n"
          "second = g.throw(42)\n",
          "(first, second, len(events), events[0][0] is Error, events[0][1], events[1])",
          "(2, 2, 2, True, 'value', (42,))");
}
} // namespace
