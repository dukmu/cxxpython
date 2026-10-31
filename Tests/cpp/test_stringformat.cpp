#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/stringformat.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class StringFormat : public testing::Test {
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
    Handle integer(std::int64_t value)
    {
        ReturnSlot out(context);
        make_int(context, value, out);
        return roots.root(out.value());
    }
    Handle floating(double value)
    {
        ReturnSlot out(context);
        make_float(context, value, out);
        return roots.root(out.value());
    }
    void invoke(Handle self, std::u32string method, CallArgsView args, ReturnSlot &out)
    {
        get_attribute(context, self, text(std::move(method)), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto callable = roots.root(out.value());
        call(context, callable, args, out);
    }
    void percent(std::u32string pattern, std::initializer_list<Handle> values, ReturnSlot &out)
    {
        std::vector<Value> items;
        for (auto value : values)
            items.push_back(value.get(context));
        ReturnSlot tuple(context);
        cxxpython::core::make_tuple(context, items, tuple);
        auto arguments = roots.root(tuple.value());
        auto format = text(std::move(pattern));
        std::array args{arguments.get(context)};
        invoke(format, U"__mod__", {args, {}}, out);
    }
    void expect_text(ReturnSlot &out, std::u32string_view expected)
    {
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.value()).value, expected);
        context.collect();
        EXPECT_EQ(context.payload<StrObject>(out.value()).value, expected);
    }
    void expect_error(ReturnSlot &out, CoreObject type, std::u32string_view message)
    {
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(type));
        ReturnSlot args(context);
        context.load(out.exception(), context.payload<ExceptionObject>(out.exception()).args, args);
        ASSERT_EQ(args.tag(), ResultTag::Ok);
        const auto &items = context.payload<TupleObject>(args.value()).items;
        ASSERT_EQ(items.size(), 1U);
        EXPECT_EQ(context.payload<StrObject>(items[0]).value, message);
    }
};

TEST_F(StringFormat, PositionalKeywordAndEscapedFields)
{
    auto pattern = text(U"{}: {name} {{ok}}");
    auto first = text(U"hello");
    auto name = text(U"name");
    auto value = text(U"world");
    std::array positional{first.get(context)};
    std::array keywords{KeywordArg{name.get(context), value.get(context)}};
    ReturnSlot out(context);
    invoke(pattern, U"format", {positional, keywords}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"hello: world {ok}");
}

TEST_F(StringFormat, PercentStringAndRepr)
{
    auto pattern = text(U"%s: %r");
    auto value = text(U"hello");
    std::array values{value.get(context), value.get(context)};
    ReturnSlot tuple(context);
    cxxpython::core::make_tuple(context, values, tuple);
    auto argument = roots.root(tuple.value());
    std::array args{argument.get(context)};
    ReturnSlot out(context);
    invoke(pattern, U"__mod__", {args, {}}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"hello: 'hello'");
}

TEST_F(StringFormat, MixedFieldNumberingRaises)
{
    auto pattern = text(U"{} {0}");
    auto value = text(U"x");
    std::array args{value.get(context)};
    ReturnSlot out(context);
    invoke(pattern, U"format", {args, {}}, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
}

TEST_F(StringFormat, ExplicitMethodsRejectNonStringReceiver)
{
    auto type = roots.root(context.builtin(CoreObject::StrType));
    auto receiver = integer(42);
    auto argument = text(U"value");
    std::array args{receiver.get(context), argument.get(context)};
    ReturnSlot out(context);
    invoke(type, U"format", {args, {}}, out);
    expect_error(out, CoreObject::TypeErrorType,
                 U"descriptor 'format' for 'str' objects doesn't apply to a 'int' object");
    invoke(type, U"__mod__", {args, {}}, out);
    expect_error(out, CoreObject::TypeErrorType,
                 U"descriptor '__mod__' for 'str' objects doesn't apply to a 'int' object");
}

TEST_F(StringFormat, PercentIntegerTypesFlagsAndPrecision)
{
    auto positive = integer(42);
    auto negative = integer(-42);
    auto eight = integer(8);
    ReturnSlot out(context);
    percent(U"%d %i %u %o %x %X", {negative, positive, negative, positive, positive, positive},
            out);
    expect_text(out, U"-42 42 -42 52 2a 2A");
    percent(U"%+08d|%#08x|%#o|%.5d|%-06i", {positive, positive, eight, negative, positive}, out);
    expect_text(out, U"+0000042|0x00002a|0o10|-00042|42    ");
}

TEST_F(StringFormat, PercentFloatingConversionsAndIntegerCoercion)
{
    auto number = floating(1.25);
    auto large = floating(1234.0);
    auto one = floating(1.0);
    auto positive = floating(1.9);
    auto negative = floating(-1.9);
    ReturnSlot out(context);
    percent(U"%f|%.2F|%.2e|%.3E|%.3g|%#.3G", {number, number, number, number, large, one}, out);
    expect_text(out, U"1.250000|1.25|1.25e+00|1.250E+00|1.23e+03|1.00");
    percent(U"%d|%i|%u", {positive, negative, positive}, out);
    expect_text(out, U"1|-1|1");
    percent(U"%#.0f|%.0g", {one, one}, out);
    expect_text(out, U"1.|1");
}

TEST_F(StringFormat, PercentStarWidthPrecisionAndCharacters)
{
    auto minus_eight = integer(-8);
    auto two = integer(2);
    auto number = floating(1.25);
    auto six = integer(6);
    auto three = integer(3);
    auto value = text(U"abcdef");
    ReturnSlot out(context);
    percent(U"%*.*f|%*.*s", {minus_eight, two, number, six, three, value}, out);
    expect_text(out, U"1.25    |   abc");
    auto negative = integer(-1);
    percent(U"%.*f", {negative, number}, out);
    expect_text(out, U"1");
    auto ordinal = integer(65);
    auto unicode = text(U"\u754c");
    percent(U"%c|%c", {ordinal, unicode}, out);
    expect_text(out, U"A|\u754c");
}

TEST_F(StringFormat, PercentErrorsMatchReference)
{
    auto text_value = text(U"bad");
    auto real = floating(1.5);
    auto negative = integer(-1);
    auto oversized = integer(0x110000);
    ReturnSlot out(context);
    percent(U"%*s", {text_value, text_value}, out);
    expect_error(out, CoreObject::TypeErrorType, U"* wants int");
    percent(U"%.*s", {text_value, text_value}, out);
    expect_error(out, CoreObject::TypeErrorType, U"* wants int");
    percent(U"%*s", {negative}, out);
    expect_error(out, CoreObject::TypeErrorType, U"not enough arguments for format string");
    percent(U"%d", {text_value}, out);
    expect_error(out, CoreObject::TypeErrorType, U"%d format: a real number is required, not str");
    percent(U"%x", {real}, out);
    expect_error(out, CoreObject::TypeErrorType, U"%x format: an integer is required, not float");
    percent(U"%f", {text_value}, out);
    expect_error(out, CoreObject::TypeErrorType, U"must be real number, not str");
    percent(U"%c", {text_value}, out);
    expect_error(out, CoreObject::TypeErrorType, U"%c requires int or char");
    percent(U"%c", {oversized}, out);
    expect_error(out, CoreObject::OverflowErrorType, U"%c arg not in range(0x110000)");
    percent(U"%q", {negative}, out);
    expect_error(out, CoreObject::ValueErrorType,
                 U"unsupported format character 'q' (0x71) at index 1");
}

TEST_F(StringFormat, PercentUsesIndexProtocolForBaseAndCharacter)
{
    ReturnSlot out(context);
    create_type(context, {"Indexed"}, out);
    auto type = roots.root(out.value());
    auto ordinal = integer(65);
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    const std::array captures{ordinal.get(context)};
    make_native_function(
        context, "index", signature,
        [](Attachment &ctx, Handle function, std::span<const Value>, ReturnSlot &result) {
            ctx.collect();
            result.ok(ctx.payload<NativeFunctionObject>(function).captures[0]);
        },
        captures, out);
    auto function = roots.root(out.value());
    set_attribute(context, type, text(U"__index__"), function, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.allocate_instance<InstanceObject>(type, out);
    auto object = roots.root(out.value());
    percent(U"%x %c", {object, object}, out);
    expect_text(out, U"41 A");
}

TEST_F(StringFormat, NestedSpecsShareNumberingAndEnforceRecursionLimit)
{
    auto one = integer(1);
    auto two = integer(2);
    auto three = integer(3);
    std::array args{one.get(context), two.get(context), three.get(context)};
    ReturnSlot out(context);
    invoke(text(U"{} {:{}}"), U"format", {args, {}}, out);
    expect_text(out, U"1   2");
    invoke(text(U"{0:{}}"), U"format", {args, {}}, out);
    expect_error(out, CoreObject::ValueErrorType,
                 U"cannot switch from manual field specification to automatic field numbering");
    invoke(text(U"{:{1}}"), U"format", {args, {}}, out);
    expect_error(out, CoreObject::ValueErrorType,
                 U"cannot switch from automatic field numbering to manual field specification");
    invoke(text(U"{0:{1:{2}}}"), U"format", {args, {}}, out);
    expect_error(out, CoreObject::ValueErrorType, U"Max string recursion exceeded");
}
} // namespace
