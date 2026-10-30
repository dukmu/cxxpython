#include "internal/stdio.hpp"

#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
TEST(Stdio, InitialStreamsAreIdenticalToTheirSavedValues)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot result(ctx), key(ctx), stream(ctx), original(ctx);
    make_module(ctx, U"sys", result);
    auto sys = roots.root(result.value());
    detail::initialize_stdio(ctx, sys, result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    make_str(ctx, U"stdout", key);
    get_attribute(ctx, sys, roots.root(key.value()), stream);
    ASSERT_EQ(stream.tag(), ResultTag::Ok);
    make_str(ctx, U"__stdout__", key);
    get_attribute(ctx, sys, roots.root(key.value()), original);
    ASSERT_EQ(original.tag(), ResultTag::Ok);
    EXPECT_EQ(stream.value(), original.value());
}

TEST(Stdio, PrintUsesCurrentStdoutAndPreservesCallbackOrder)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot out(ctx);
    initialize_imports(ctx, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(ctx, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto globals = roots.root(module_dict(ctx, out.value()));
    SourceUnit unit(SourceBuffer(R"(
import sys
events = []
class Stream:
    def write(self, text):
        events.append(text)
    def flush(self):
        events.append('flush')
class Item:
    def __str__(self):
        events.append('str')
        return 'item'
class Bad:
    def __str__(self):
        events.append('bad')
        raise ValueError('bad')
sys.stdout = Stream()
print(Item(), Item(), sep=None, end=None, flush=True)
assert events == ['str', 'item', ' ', 'str', 'item', '\n', 'flush']
events = []
try:
    print(Item(), Bad(), flush=True)
except ValueError:
    pass
assert events == ['str', 'item', ' ', 'bad']
events = []
try:
    print(Item(), sep=42)
except TypeError:
    pass
assert events == []
class FailingStream:
    def write(self, text):
        events.append('write')
        raise ValueError('write')
    def flush(self):
        events.append('flush')
events = []
try:
    print(Item(), Item(), file=FailingStream(), flush=True)
except ValueError:
    pass
assert events == ['str', 'write']
class FailingFlush:
    def write(self, text):
        events.append(text)
    def flush(self):
        events.append('flush')
        raise ValueError('flush')
events = []
try:
    print('x', file=FailingFlush(), flush=True)
except ValueError:
    pass
assert events == ['x', '\n', 'flush']
sys.stdout = None
print(Bad())
print(Bad(), sep=42)
)",
                                 "stdio-test.py"));
    run(ctx, {&unit, globals}, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
}
TEST(Stdio, EncodingErrorsAndArgumentConversionRemainPythonExceptions)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot out(ctx);
    initialize_imports(ctx, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(ctx, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto globals = roots.root(module_dict(ctx, out.value()));
    SourceUnit unit(SourceBuffer(R"(
import sys
try:
    sys.stdout.write(chr(0xd800))
except UnicodeEncodeError as error:
    assert error.object == chr(0xd800)
    assert error.start == 0 and error.end == 1
else:
    assert False
events = []
class Flag:
    def __bool__(self):
        events.append('bool')
        raise ValueError('flush')
try:
    print('never written', sep=42, flush=Flag())
except ValueError:
    pass
else:
    assert False
assert events == ['bool']
)",
                                 "stdio-errors.py"));
    run(ctx, {&unit, globals}, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
}
} // namespace
