#include "internal/sysmodule.hpp"

#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
TEST(SysHooks, DisplayAndSystemExitUnderCollection)
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
import builtins
assert sys.displayhook is sys.__displayhook__
events = []
class Stream:
    def write(self, text):
        events.append((text, builtins._))
class Item:
    def __repr__(self):
        assert builtins._ is None
        return 'item'
sys.stdout = Stream()
item = Item()
builtins._ = 99
sys.displayhook(None)
assert builtins._ == 99 and events == []
sys.displayhook(item)
assert events == [('item', None), ('\n', None)]
assert builtins._ is item
class Bad:
    def write(self, text):
        raise ValueError('write')
sys.stdout = Bad()
try:
    sys.displayhook(item)
except ValueError:
    assert builtins._ is None
else:
    assert False
sys.stdout = None
try:
    sys.displayhook(item)
except RuntimeError:
    pass
else:
    assert False
assert issubclass(SystemExit, BaseException)
assert not issubclass(SystemExit, Exception)
e = SystemExit()
assert e.args == () and e.code is None
e = SystemExit(item)
assert e.args == (item,) and e.code is item
e = SystemExit(1, 2)
assert e.args == (1, 2) and e.code == (1, 2)
e.code = item
SystemExit.__init__(e)
assert e.args == () and e.code is item
SystemExit.__init__(e, 4, 5)
assert e.args == (4, 5) and e.code == (4, 5)
del e.code
assert e.code is None
try:
    sys.exit(item)
except SystemExit as error:
    assert error.code is item and error.args == (item,)
else:
    assert False
try:
    sys.exit()
except SystemExit as error:
    assert error.code is None and error.args == ()
else:
    assert False
class Tuple(tuple):
    pass
code = Tuple((1, 2))
try:
    sys.exit(code)
except SystemExit as error:
    assert error.args is code and error.code is code
else:
    assert False
)",
                                 "sys-hooks-test.py"));
    run(ctx, {&unit, globals}, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
}
TEST(SysHooks, DisplayValueUsesCurrentHook)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot out(ctx), sys(ctx), key(ctx), hook(ctx), value(ctx);
    initialize_imports(ctx, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(ctx, U"sys", sys);
    ASSERT_EQ(sys.tag(), ResultTag::Ok);
    make_str(ctx, U"displayhook", key);
    get_attribute(ctx, roots.root(sys.value()), roots.root(key.value()), hook);
    ASSERT_EQ(hook.tag(), ResultTag::Ok);
    make_str(ctx, U"missing_hook", key);
    set_attribute(ctx, roots.root(sys.value()), roots.root(key.value()), roots.root(hook.value()),
                  out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    make_str(ctx, U"displayhook", key);
    set_attribute(ctx, roots.root(sys.value()), roots.root(key.value()),
                  roots.root(ctx.builtin(CoreObject::None)), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    make_int(ctx, 7, value);
    detail::display_value(ctx, roots.root(value.value()), out);
    EXPECT_EQ(out.tag(), ResultTag::Raised);
}
TEST(SysHooks, DisplayhookEscapesUnencodableTextThroughBufferOrTextWriter)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto globals = roots.root(module_dict(context, out.value()));
    SourceUnit source{SourceBuffer(R"(
import sys
import builtins
events = []
class Item:
    def __repr__(self):
        events.append('repr')
        return '\u4f60'
class Stream:
    encoding = 'ascii'
    def write(self, text):
        if text == '\u4f60':
            raise UnicodeEncodeError('ascii', text, 0, 1, 'bad')
        events.append(text)
value = Item()
sys.stdout = Stream()
sys.displayhook(value)
assert events == ['repr', 'repr', '\\u4f60', '\n']
assert builtins._ is value
class Buffer:
    def write(self, data): events.append(data)
events = []
sys.stdout.buffer = Buffer()
sys.displayhook(value)
assert events == ['repr', 'repr', b'\\u4f60', '\n']
assert builtins._ is value
)")};
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}
} // namespace
