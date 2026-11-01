#include "cxxpython/moduleobject.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class Modules : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.value());
    }
};

TEST_F(Modules, NamespaceAndReadOnlyDescriptor)
{
    ReturnSlot out(context);
    make_module(context, U"example", out);
    auto module = roots.root(out.value());
    EXPECT_TRUE(is_module(context, module.get(context)));
    auto dictionary = roots.root(module_dict(context, module.get(context)));
    get_attribute(context, module, text(U"__dict__"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), dictionary.get(context));
    auto value = text(U"value");
    set_attribute(context, module, text(U"answer"), value, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    get_attribute(context, module, text(U"answer"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));
    set_attribute(context, module, text(U"__dict__"), dictionary, out);
    EXPECT_EQ(out.tag(), ResultTag::Raised);
}

TEST_F(Modules, SharedDictionaryAndConstruction)
{
    ReturnSlot out(context);
    auto builtins = roots.root(context.builtin(CoreObject::Builtins));
    make_module(context, U"builtins", out, builtins);
    EXPECT_EQ(module_dict(context, out.value()), builtins.get(context));
    auto type = roots.root(context.builtin(CoreObject::ModuleType));
    auto name = text(U"constructed");
    std::array args{name.get(context)};
    call(context, type, {args, {}}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto module = roots.root(out.value());
    get_attribute(context, module, text(U"__name__"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), name.get(context));
}

TEST_F(Modules, NamespaceGetattrReceivesOnlyName)
{
    ReturnSlot out(context);
    make_module(context, U"example", out);
    auto module = roots.root(out.value());
    SignatureLayout signature;
    signature.parameters = {{U"name", ParameterKind::PositionalOnly}};
    context.allocate_instance<NativeFunctionObject>(
        roots.root(context.builtin(CoreObject::NativeFunctionType)), out, "fallback", signature,
        +[](Attachment &, Value, std::span<const Value> args, ReturnSlot &result) {
            result.ok(args[0]);
        },
        std::vector<Value>{});
    auto callback = roots.root(out.value());
    set_attribute(context, module, text(U"__getattr__"), callback, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto missing = text(U"missing");
    get_attribute(context, module, missing, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), missing.get(context));
}

TEST_F(Modules, SubclassUsesModuleLayout)
{
    auto base = roots.root(context.builtin(CoreObject::ModuleType));
    std::array bases{base};
    TypeSpec spec{U"Child", bases};
    ReturnSlot out(context);
    create_type(context, spec, out);
    auto type = roots.root(out.value());
    auto name = text(U"child");
    std::array args{name.get(context)};
    call(context, type, {args, {}}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto module = roots.root(out.value());
    EXPECT_TRUE(is_module(context, module.get(context)));
    context.collect();
    get_attribute(context, module, text(U"__name__"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), name.get(context));
}
} // namespace
