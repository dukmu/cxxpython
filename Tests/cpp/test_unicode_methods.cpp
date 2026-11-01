#include "cxxpython/protocol.hpp"
#include "internal/unicodeobject.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class UnicodeMethods : public testing::Test {
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
    Handle number(std::string_view value)
    {
        ReturnSlot out(context);
        make_int_decimal(context, value, out);
        return roots.root(out.value());
    }
    Handle tuple(std::span<const Value> values)
    {
        ReturnSlot out(context);
        make_tuple(context, values, out);
        return roots.root(out.value());
    }
    void invoke(std::u32string name, std::span<const Value> args, ReturnSlot &out)
    {
        ReturnSlot method(context);
        get_attribute(context, roots.root(context.builtin(CoreObject::StrType)), text(name),
                      method);
        ASSERT_EQ(method.tag(), ResultTag::Ok);
        call(context, roots.root(method.value()), {args, {}}, out);
    }
    void expect(std::u32string name, std::span<const Value> args, bool result)
    {
        ReturnSlot out(context);
        invoke(name, args, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), context.builtin(result ? CoreObject::True : CoreObject::False));
    }
};

TEST_F(UnicodeMethods, UnicodeBoundsAndLazyTuple)
{
    auto s = text(U"a\u00e9\U0001f600"), empty = text(U""), prefix = text(U"a"),
         suffix = text(U"\U0001f600"), bad = number("7"), four = number("4"),
         huge = number("999999999999999999999999999999999999"),
         negative = number("-999999999999999999999999999999999999");
    std::array a{s.get(context), prefix.get(context)};
    expect(U"startswith", a, true);
    std::array b{s.get(context), suffix.get(context), negative.get(context), huge.get(context)};
    expect(U"endswith", b, true);
    std::array c{s.get(context), empty.get(context), four.get(context)};
    expect(U"startswith", c, false);
    std::array items{prefix.get(context), bad.get(context)};
    auto candidates = tuple(items);
    std::array d{s.get(context), candidates.get(context)};
    expect(U"startswith", d, true);
    ReturnSlot out(context);
    invoke(U"endswith", d, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
    std::array wrong{bad.get(context), prefix.get(context)};
    invoke(U"startswith", wrong, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
}

TEST_F(UnicodeMethods, IndexCallbackSurvivesCollection)
{
    ReturnSlot out(context);
    create_type(context, {"Index", {}}, out);
    auto cls = roots.root(out.value());
    auto result = number("1");
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    std::array captures{result.get(context)};
    make_native_function(
        context, "index", signature,
        [](Attachment &c, Value fn, std::span<const Value>, ReturnSlot &out) {
            c.collect();
            out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
        },
        captures, out);
    auto function = roots.root(out.value());
    set_attribute(context, cls, text(U"__index__"), function, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.allocate_instance<InstanceObject>(cls, out);
    auto index_value = roots.root(out.value());
    auto s = text(U"abc"), prefix = text(U"b");
    std::array args{s.get(context), prefix.get(context), index_value.get(context)};
    expect(U"startswith", args, true);
}
} // namespace
