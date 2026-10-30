#include "internal/stdio.hpp"

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
    // Warm import policy; only stream reconstruction below forces every allocation to collect.
    ctx.set_gc_enabled(false);
    initialize_imports(ctx, result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    ctx.set_gc_enabled(true);
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

TEST(Stdio, NativePrintLazilyInitializesTheInterpreterSystemNamespace)
{
    Interpreter interpreter;
    Attachment ctx(interpreter);
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot out(ctx);
    ctx.load_sys_dictionary(out);
    ASSERT_EQ(out.tag(), ResultTag::Empty);
    const auto function =
        roots.root(*detail::name_lookup(ctx, ctx.builtin(CoreObject::Builtins), U"print"));
    call(ctx, function, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    ctx.load_sys_dictionary(out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_TRUE(detail::name_lookup(ctx, out.value(), U"stdout"));
}

} // namespace
