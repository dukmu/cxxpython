#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

TEST(BuiltinExec, CallerNamespacesAndVars)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(marker = 42
assert globals() is locals()
def check(value):
    captured = 17
    return locals()['value'] == value and vars()['captured'] == 17 and globals()['marker'] == 42
assert check(3)
class Example:
    pass
item = Example()
item.value = 9
assert vars(item)['value'] == 9
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}

TEST(BuiltinExec, CompileAndExecuteWithRealNamespaces)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(global_value = 30
def caller(local_value):
    assert eval('local_value + global_value') == 42
    exec('created = local_value + 1')
    assert locals()['created'] == 13
caller(12)
g = {'x': 2}
l = {'x': 40}
assert eval('x + 2', g, l) == 42
exec('result = x + 1', g, l)
assert l['result'] == 41
assert 'result' not in g
c = compile('answer = 42', 'example.py', 'exec')
exec(c, g, l)
assert l['answer'] == 42
assert eval(compile('x + 2', 'example.py', 'eval'), g, l) == 42
class Custom:
    def __dir__(self):
        return ['z', 'a', 'm']
assert dir(Custom()) == ['a', 'm', 'z']
assert 'global_value' in dir()
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}

TEST(BuiltinExec, RejectsUnsupportedOptionsAndReportsSyntaxLocation)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(try:
    compile('x =', 'bad.py', 'exec')
except SyntaxError as e:
    assert e.filename == 'bad.py'
    assert e.lineno == 1
    assert e.offset > 0
else:
    assert False
try:
    compile('1', 'x', 'eval', 1)
except ValueError:
    pass
else:
    assert False
try:
    exec('pass', {}, {}, closure=())
except TypeError:
    pass
else:
    assert False
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}
} // namespace
