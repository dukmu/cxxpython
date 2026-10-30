#include "internal/codecs.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class Codecs : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};

    void SetUp() override
    {
        context.bootstrap_core();
    }
};

TEST_F(Codecs, Utf8RoundTripAndScalarBoundary)
{
    RootScope roots(context);
    ReturnSlot out(context);
    make_str(context, U"A\u00e9\U0010ffff", out);
    auto text = roots.root(out.value());
    auto none = roots.root(context.builtin(CoreObject::None));
    detail::encode_text(context, text, none, none, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto bytes = roots.root(out.value());
    EXPECT_EQ(context.payload<BytesObject>(bytes.get(context)).value,
              std::string("A\xc3\xa9\xf4\x8f\xbf\xbf", 7));
    detail::decode_bytes(context, bytes, none, none, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"A\u00e9\U0010ffff");
}

TEST_F(Codecs, InvalidUtf8ReportsOriginalBytesAndSpan)
{
    RootScope roots(context);
    ReturnSlot out(context);
    make_bytes(context, std::string("x\xe2\x82", 3), out);
    auto bytes = roots.root(out.value());
    auto none = roots.root(context.builtin(CoreObject::None));
    detail::decode_bytes(context, bytes, none, none, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    auto error = roots.root(out.exception());
    ASSERT_EQ(context.type_of(error.get(context)),
              context.builtin(CoreObject::UnicodeDecodeErrorType));
    ReturnSlot field(context);
    context.load(error.get(context), context.instance_slot(error.get(context), 1), field);
    ASSERT_EQ(field.tag(), ResultTag::Ok);
    EXPECT_EQ(field.value(), bytes.get(context));
    context.load(error.get(context), context.instance_slot(error.get(context), 2), field);
    ASSERT_EQ(field.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<IntObject>(field.value()).small, 1);
    context.load(error.get(context), context.instance_slot(error.get(context), 3), field);
    ASSERT_EQ(field.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<IntObject>(field.value()).small, 3);
}

TEST_F(Codecs, UnknownEncodingIsNotUtf8)
{
    RootScope roots(context);
    ReturnSlot out(context);
    make_str(context, U"text", out);
    auto text = roots.root(out.value());
    make_str(context, U"not-an-encoding", out);
    auto encoding = roots.root(out.value());
    auto none = roots.root(context.builtin(CoreObject::None));
    detail::encode_text(context, text, encoding, none, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::LookupErrorType));
}
} // namespace
