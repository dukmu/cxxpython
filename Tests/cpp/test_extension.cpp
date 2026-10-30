#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {
using namespace cxxpython::core;

CxxPyHandle failing_exec(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle, const CxxPyHandle *,
                         size_t)
{
    auto type = api->builtin(ctx, "ValueError");
    if (!type)
        return nullptr;
    auto exception = api->call(ctx, type, nullptr, 0);
    return exception ? api->raise(ctx, exception) : nullptr;
}

CxxPyHandle successful_exec(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle,
                            const CxxPyHandle *, size_t)
{
    return api->builtin(ctx, "None");
}

class Extensions : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};

    void SetUp() override
    {
        context.bootstrap_core();
    }

    void register_example()
    {
        ReturnSlot out(context);
        register_c_module(context, *CxxPy_Module_v1(), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }

    void check(std::string body, std::string expression, std::string expected)
    {
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto globals = roots.root(module_dict(context, out.value()));
        SourceUnit unit{SourceBuffer(std::move(body))};
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        context.collect();
        SourceUnit eval{SourceBuffer(std::move(expression)), InputMode::Eval};
        run(context, {&eval, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        repr(context, roots.root(out.value()), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(context.payload<StrObject>(out.value()).value), expected);
    }
};

TEST_F(Extensions, StaticRegistrationAndSourceImport)
{
    register_example();
    register_source_module(context, U"consumer",
                           std::make_shared<SourceUnit>(SourceBuffer(
                               "from _example import answer, add\nresult = add(answer(), 1)")));
    check("import consumer\nimport _example\n_example.value = 19",
          "consumer.result, _example.answer()", "(43, 19)");
}

TEST_F(Extensions, DynamicDiscoveryCachingAndFromImport)
{
    ReturnSlot out(context);
    const std::string paths[] = {TEST_EXTENSION_DIRECTORY};
    set_import_path(context, paths, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    check("import _example\nimport _example as again\nfrom _example import answer, add",
          "_example is again, answer(), add(20, 22)", "(True, 42, 42)");
}
TEST_F(Extensions, MethodInstallationUsesExistingNamespaceKeyEquality)
{
    check(R"(import builtins as b
import sys
class AliasKey:
    def __hash__(self):
        return hash('answer')
    def __eq__(self, other):
        return other == 'answer'
key = AliasKey()
b.__dict__[key] = None
del sys.modules['builtins']
)",
          "b.__dict__[key] is None", "True");
    auto definition = *CxxPy_Module_v1();
    definition.name = "builtins";
    ReturnSlot out(context);
    register_c_module(context, definition, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    check("from builtins import answer", "answer(), b.__dict__[key] is answer", "(42, True)");
}

TEST_F(Extensions, ErrorsArityAndIndexConversion)
{
    register_example();
    check("import _example\n"
          "class Index:\n    def __index__(self):\n        return 40\n"
          "try:\n    _example.answer(1)\nexcept TypeError:\n    arity1 = True\n"
          "try:\n    _example.add(1)\nexcept TypeError:\n    arity2 = True\n"
          "try:\n    _example.fail()\nexcept ValueError:\n    raised = True\n"
          "try:\n    _example.empty()\nexcept RuntimeError:\n    empty = True\n"
          "try:\n    _example.sticky()\nexcept AttributeError:\n    sticky = True\n"
          "try:\n    _example.add(9223372036854775807, 1)\n"
          "except OverflowError:\n    overflow = True\n"
          "try:\n    _example.add('bad', 1)\nexcept TypeError:\n    bad_type = True\n",
          "_example.add(Index(), 2), arity1, arity2, raised, empty, sticky, overflow, bad_type",
          "(42, True, True, True, True, True, True, True)");
}

TEST_F(Extensions, Utf32PreservesNulUnicodeAndSurrogates)
{
    register_example();
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto globals = roots.root(module_dict(context, out.value()));
    SourceUnit unit{SourceBuffer("import _example\nresult = _example.unicode_text()")};
    run(context, {&unit, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    auto value = detail::name_lookup(context, globals.get(context), U"result");
    ASSERT_TRUE(value);
    EXPECT_EQ(context.payload<StrObject>(*value).value,
              (std::u32string{U'A', U'\0', static_cast<char32_t>(0x1f642),
                              static_cast<char32_t>(0xd800)}));
    check("try:\n    _example.invalid_unicode()\nexcept ValueError:\n    invalid = True", "invalid",
          "True");
}

TEST_F(Extensions, PollAndSynchronousWaitRespectParkedBoundary)
{
    register_example();
    check("import _example\n"
          "try:\n    _example.invalid_wait()\nexcept RuntimeError:\n    rejected = True",
          "_example.poll_wait(), rejected", "(42, True)");
}

TEST_F(Extensions, AllocationFailuresRollBackAndImportCanRetry)
{
    register_example();
    ReturnSlot out(context);
    bool succeeded = false;
    bool failed = false;
    for (std::size_t quota = 0; quota < 300; ++quota) {
        SCOPED_TRACE(quota);
        context.set_allocation_limit(quota);
        import_module(context, U"_example", out);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        if (out.tag() == ResultTag::Ok) {
            succeeded = true;
            break;
        }
        failed = true;
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::MemoryErrorType));
        EXPECT_FALSE(
            detail::name_lookup(context, context.builtin(CoreObject::Modules), U"_example"));
        context.collect();
    }
    EXPECT_TRUE(failed);
    EXPECT_TRUE(succeeded);
}

TEST_F(Extensions, InvalidDefinitionsAreRejected)
{
    ReturnSlot out(context);
    auto bad = *CxxPy_Module_v1();
    bad.abi_version = CXXPY_ABI_VERSION + 1;
    register_c_module(context, bad, out);
    EXPECT_EQ(out.tag(), ResultTag::Raised);
    CxxPyMethodDef duplicates[] = {bad.methods[0], bad.methods[0]};
    bad = *CxxPy_Module_v1();
    bad.methods = duplicates;
    bad.method_count = 2;
    register_c_module(context, bad, out);
    EXPECT_EQ(out.tag(), ResultTag::Raised);
}

TEST_F(Extensions, FailedInitializationRollsBackAndRetries)
{
    CxxPyModuleDef definition{
        CXXPY_ABI_VERSION, sizeof(CxxPyModuleDef), "retry_extension", failing_exec, nullptr, 0};
    ReturnSlot out(context);
    register_c_module(context, definition, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"retry_extension", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    EXPECT_FALSE(
        detail::name_lookup(context, context.builtin(CoreObject::Modules), U"retry_extension"));
    context.collect();
    definition.exec = successful_exec;
    register_c_module(context, definition, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"retry_extension", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}

TEST_F(Extensions, CapturedFunctionKeepsModuleAlive)
{
    register_example();
    ReturnSlot out(context);
    import_module(context, U"_example", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto function = detail::name_lookup(context, module_dict(context, out.value()), U"answer");
    ASSERT_TRUE(function);
    auto saved = roots.root(*function);
    ASSERT_TRUE(detail::name_erase(context, context.builtin(CoreObject::Modules), U"_example"));
    out.ok(context.builtin(CoreObject::None));
    context.collect();
    call(context, saved, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "42");
}

TEST_F(Extensions, StateIsPerInterpreter)
{
    register_example();
    check("import _example\n_example.value = 99", "_example.answer()", "99");
    Interpreter other{{.collect_every_allocation = true}};
    Attachment attachment{other};
    attachment.bootstrap_core();
    ReturnSlot out(attachment);
    register_c_module(attachment, *CxxPy_Module_v1(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(attachment, U"_example", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    RootScope other_roots{attachment};
    auto module = other_roots.root(out.value());
    attachment.collect();
    auto value =
        detail::name_lookup(attachment, module_dict(attachment, module.get(attachment)), U"value");
    ASSERT_TRUE(value);
    EXPECT_EQ(int_decimal(attachment, *value), "42");
}
} // namespace
