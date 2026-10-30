#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
TEST(InteractiveExecution, SingleUsesLiveDisplayhookAndDoesNotWriteModuleDocstring)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto globals = roots.root(module_dict(context, out.value()));
    auto execute = [&](const char *text, InputMode mode = InputMode::Exec) {
        SourceUnit source{SourceBuffer(text, "<stdin>"), mode};
        run(context, {&source, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    };
    execute("import sys\nevents = []\n__doc__ = 'unchanged'\n"
            "sys.displayhook = lambda value: events.append(value)\n");
    execute("'shown'\n", InputMode::Single);
    execute("if True:\n    None; 42\n", InputMode::Single);
    execute("def f():\n    99\n    return 7\n", InputMode::Single);
    execute("f()\n", InputMode::Single);
    execute("exec(compile('8; 9', '<single>', 'single'))\n");
    execute("assert events == ['shown', None, 42, 7, 8, 9]\n"
            "assert __doc__ == 'unchanged'\n"
            "def bad(value):\n    raise ValueError('hook')\n"
            "sys.displayhook = bad\n");
    SourceUnit fail{SourceBuffer("1\n", "<stdin>"), InputMode::Single};
    run(context, {&fail, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    EXPECT_FALSE(context.current_frame());
}
} // namespace
