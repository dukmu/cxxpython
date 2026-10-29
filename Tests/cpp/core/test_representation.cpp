#include "cxxpython/core/protocol.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    class CoreRepresentation : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle builtin(CoreObject id) { return roots.root(context.builtin(id)); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle integer(std::string_view value)
        { ReturnSlot out(context); make_int_decimal(context, value, out); return roots.root(out.result().value()); }
        Handle tuple(std::span<const Value> values)
        { ReturnSlot out(context); cxxpython::core::make_tuple(context, values, out); return roots.root(out.result().value()); }
        Handle type()
        { ReturnSlot out(context); create_type(context, {"Example"}, out); return roots.root(out.result().value()); }
        Handle instance(Handle type)
        { ReturnSlot out(context); context.allocate_instance<InstanceObject>(type, out); return roots.root(out.result().value()); }
        void put(Handle type, std::u32string name, Handle value)
        { ReturnSlot out(context); set_attribute(context, type, text(std::move(name)), value, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); }
        Handle function(Handle result, std::size_t arity = 0)
        {
            SignatureLayout signature; signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
            if (arity) signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
            std::array captures{result.get(context)}; ReturnSlot out(context);
            make_native_function(context, "method", signature,
                [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                    c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
                }, captures, out);
            return roots.root(out.result().value());
        }
        void error(ReturnSlot &out, CoreObject type)
        { ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type)); }
        void expect_text(ReturnSlot &out, std::u32string_view expected)
        {
            ASSERT_EQ(out.result().tag(), ResultTag::Ok);
            EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, expected);
            context.collect(); EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, expected);
        }
        Handle cell(std::optional<Value> initial = {})
        { ReturnSlot out(context); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out, initial); return roots.root(out.result().value()); }
        std::int64_t hash_value(Handle value)
        {
            ReturnSlot out(context); cxxpython::core::hash(context, value, out);
            if (out.result().tag() != ResultTag::Ok) { ADD_FAILURE() << "unexpected hash error"; return 0; }
            std::int64_t result = 0; EXPECT_TRUE(int_fits_signed(context, out.result().value(), result)); return result;
        }
    };

    TEST(CoreHash, SipHash24MatchesReferenceVectorsIncludingTailAndBlockBoundaries)
    {
        std::array<std::byte, 64> data;
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>(i);
        constexpr std::array key{std::uint64_t{0x0706050403020100}, std::uint64_t{0x0f0e0d0c0b0a0908}};
        for (auto [size, expected] : {std::pair<std::size_t, std::uint64_t>{0, 0x726fdb47dd0e0e31},
            {1, 0x74f839c593dc67fd}, {7, 0xab0200f58b01d137}, {8, 0x93f5f5799a932462},
            {15, 0xa129ca6149be45e5}, {16, 0x3f2acc7f57c29bdb}, {63, 0x958a324ceb064572}})
            EXPECT_EQ(detail::siphash24(std::span(data).first(size), key), expected) << size;
    }

    TEST_F(CoreRepresentation, IntegerHashesShareNumericEqualityAndHandleSignedLimits)
    {
        EXPECT_EQ(hash_value(integer("0")), 0); EXPECT_EQ(hash_value(integer("-1")), -2);
        EXPECT_EQ(hash_value(integer("-9223372036854775808")), -4);
        EXPECT_EQ(hash_value(integer("18446744073709551616")), 8);
        EXPECT_EQ(hash_value(integer("-18446744073709551616")), -8);
        EXPECT_EQ(hash_value(builtin(CoreObject::True)), hash_value(integer("1")));
        EXPECT_EQ(hash_value(builtin(CoreObject::False)), hash_value(integer("0")));
    }

    TEST_F(CoreRepresentation, UserHashChecksIntegerWithoutIndexCoercionAndNormalizesMinusOne)
    {
        auto cls = type(); auto obj = instance(cls); ReturnSlot out(context);
        put(cls, U"__hash__", function(integer("-1"))); EXPECT_EQ(hash_value(obj), -2);
        put(cls, U"__hash__", function(integer("18446744073709551616"))); EXPECT_EQ(hash_value(obj), 8);
        auto indexed_type = type(); put(indexed_type, U"__index__", function(integer("8")));
        put(cls, U"__hash__", function(instance(indexed_type)));
        cxxpython::core::hash(context, obj, out); error(out, CoreObject::TypeErrorType);
        put(cls, U"__hash__", builtin(CoreObject::None));
        cxxpython::core::hash(context, obj, out); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreRepresentation, DefaultIdentityHashIsStableAndMutableNameDictAndCellsAreUnhashable)
    {
        auto obj = instance(type()); auto first = hash_value(obj); context.collect(); EXPECT_EQ(hash_value(obj), first);
        ReturnSlot out(context); context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out);
        auto dictionary = roots.root(out.result().value()); cxxpython::core::hash(context, dictionary, out); error(out, CoreObject::TypeErrorType);
        cxxpython::core::hash(context, cell(), out); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreRepresentation, StringEqualityOrderAndHashUseCodepointsNotIdentity)
    {
        auto a = text(U"equal\U0001f600"); auto b = text(U"equal\U0001f600"); ReturnSlot out(context);
        EXPECT_NE(a.get(context), b.get(context));
        rich_compare(context, a, b, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        EXPECT_EQ(hash_value(a), hash_value(b)); EXPECT_EQ(hash_value(text(U"")), 0);
        rich_compare(context, text(U"\U00010000"), text(U"\ue000"), CompareOp::Gt, out);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto surrogate = text(std::u32string{char32_t{0xd800}});
        EXPECT_EQ(hash_value(surrogate), hash_value(text(std::u32string{char32_t{0xd800}})));
    }

    TEST_F(CoreRepresentation, ReprQuotesUnicodeAndEscapesUnprintableCategoriesWithoutLocale)
    {
        ReturnSlot out(context);
        repr(context, text(U"one'two"), out); expect_text(out, U"\"one'two\"");
        repr(context, text(U"a'\"\\\n\r\t"), out); expect_text(out, U"'a\\'\"\\\\\\n\\r\\t'");
        std::u32string input{U' ', char32_t{0}, char32_t{0x7f}, char32_t{0xa0}, char32_t{0x200b},
                             char32_t{0xd800}, char32_t{0x11f02}, char32_t{0x1f600}, char32_t{0x10ffff}};
        repr(context, text(input), out);
        expect_text(out, U"' \\x00\\x7f\\xa0\\u200b\\ud800\U00011f02\U0001f600\\U0010ffff'");
    }

    TEST_F(CoreRepresentation, StrAndReprHaveDistinctResultsAndValidateManagedStringKind)
    {
        auto original = text(U"text"); ReturnSlot out(context); str(context, original, out);
        EXPECT_EQ(out.result().value(), original.get(context));
        repr(context, original, out); expect_text(out, U"'text'");
        auto cls = type(); auto obj = instance(cls); auto custom = text(U"custom repr");
        put(cls, U"__repr__", function(custom)); str(context, obj, out); EXPECT_EQ(out.result().value(), custom.get(context));
        put(cls, U"__str__", function(text(U"custom str"))); str(context, obj, out); expect_text(out, U"custom str");
        put(cls, U"__repr__", function(integer("3"))); repr(context, obj, out); error(out, CoreObject::TypeErrorType);
        put(cls, U"__str__", builtin(CoreObject::None)); str(context, obj, out); error(out, CoreObject::TypeErrorType);
        create_native_type(context, {{"NotPythonStr"}, LayoutDescriptor::of<StrObject>()}, out);
        auto fake_type = roots.root(out.result().value()); context.allocate_instance<StrObject>(fake_type, out, U"payload");
        put(cls, U"__repr__", function(roots.root(out.result().value())));
        repr(context, obj, out); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreRepresentation, SingletonAndClassReprUseRealMetadata)
    {
        ReturnSlot out(context); repr(context, builtin(CoreObject::None), out); expect_text(out, U"None");
        repr(context, builtin(CoreObject::NotImplemented), out); expect_text(out, U"NotImplemented");
        str(context, builtin(CoreObject::True), out); expect_text(out, U"True");
        repr(context, builtin(CoreObject::IntType), out); expect_text(out, U"<class 'int'>");
        auto cls = type(); put(cls, U"__module__", text(U"package")); put(cls, U"__qualname__", text(U"Outer.Inner"));
        repr(context, cls, out); expect_text(out, U"<class 'package.Outer.Inner'>");
        repr(context, instance(cls), out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto actual = context.payload<StrObject>(out.result().value()).value;
        EXPECT_TRUE(actual.starts_with(U"<package.Outer.Inner object at 0x")); EXPECT_TRUE(actual.ends_with(U">"));
    }

    TEST_F(CoreRepresentation, TupleReprComparisonAndHashUseCommonElementProtocols)
    {
        auto one = integer("1"); auto truth = builtin(CoreObject::True); auto name = text(U"x");
        std::array a{one.get(context), name.get(context)}; std::array b{truth.get(context), text(U"x").get(context)};
        auto left = tuple(a); auto right = tuple(b); ReturnSlot out(context);
        repr(context, left, out); expect_text(out, U"(1, 'x')");
        rich_compare(context, left, right, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        EXPECT_EQ(hash_value(left), hash_value(right));
        std::array single{one.get(context)}; repr(context, tuple(single), out); expect_text(out, U"(1,)");
        repr(context, builtin(CoreObject::EmptyTuple), out); expect_text(out, U"()");
        std::array suffix{one.get(context), name.get(context), one.get(context)};
        rich_compare(context, left, tuple(suffix), CompareOp::Lt, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    }

    TEST_F(CoreRepresentation, TupleComparisonKeepsIdentityShortcutAndArbitraryElementOrdering)
    {
        auto cls = type(); put(cls, U"__eq__", function(builtin(CoreObject::False), 1));
        auto obj = instance(cls); std::array a{obj.get(context)}; auto left = tuple(a); auto right = tuple(a); ReturnSlot out(context);
        rich_compare(context, left, right, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto marker = text(U"ordering"); put(cls, U"__lt__", function(marker, 1));
        std::array b{instance(cls).get(context)}; right = tuple(b);
        rich_compare(context, left, right, CompareOp::Lt, out); EXPECT_EQ(out.result().value(), marker.get(context));
    }

    TEST_F(CoreRepresentation, TupleReprGuardIsContainerSpecificAndUnwindsOnError)
    {
        auto cls = type(); auto obj = instance(cls); std::array values{obj.get(context)}; auto container = tuple(values);
        SignatureLayout signature; signature.parameters = {{U"self"}};
        std::array captures{container.get(context)}; ReturnSlot out(context);
        make_native_function(context, "recursive element repr", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                RootScope roots(c); repr(c, roots.root(c.payload<NativeFunctionObject>(fn).captures[0]), out);
            }, captures, out);
        put(cls, U"__repr__", roots.root(out.result().value()));
        repr(context, container, out); expect_text(out, U"((...),)");
        put(cls, U"__repr__", function(integer("2"))); repr(context, container, out); error(out, CoreObject::TypeErrorType);
        put(cls, U"__repr__", function(text(U"fixed"))); repr(context, container, out); expect_text(out, U"(fixed,)");
    }

    TEST_F(CoreRepresentation, CellComparisonDelegatesToContentsAndReprDoesNotCallContentsRepr)
    {
        auto empty = cell(); auto first = cell(integer("1").get(context)); auto second = cell(integer("1").get(context)); ReturnSlot out(context);
        rich_compare(context, empty, first, CompareOp::Lt, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        rich_compare(context, first, second, CompareOp::Eq, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
        auto cls = type(); put(cls, U"__repr__", function(integer("9"))); auto obj = instance(cls); auto wrapped = cell(obj.get(context));
        repr(context, wrapped, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_NE(context.payload<StrObject>(out.result().value()).value.find(U": Example object at 0x"), std::u32string::npos);
        repr(context, empty, out); EXPECT_TRUE(context.payload<StrObject>(out.result().value()).value.ends_with(U": empty>"));
    }

    TEST_F(CoreRepresentation, NativeWrappersAndImplicitEntriesShareResults)
    {
        auto obj = text(U"same"); ReturnSlot out(context); get_attribute(context, obj, text(U"__repr__"), out);
        auto wrapper = roots.root(out.result().value()); call(context, wrapper, {}, out); expect_text(out, U"'same'");
        get_attribute(context, obj, text(U"__hash__"), out); wrapper = roots.root(out.result().value());
        call(context, wrapper, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        std::int64_t explicit_hash = 0; EXPECT_TRUE(int_fits_signed(context, out.result().value(), explicit_hash));
        EXPECT_EQ(explicit_hash, hash_value(obj));
        context.set_allocation_limit(0); repr(context, obj, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
    }

    TEST_F(CoreRepresentation, StringSubclassesKeepOverridesButNativeStrReturnsAnExactString)
    {
        auto base = builtin(CoreObject::StrType); ReturnSlot out(context);
        create_type(context, {"MyStr", {&base, 1}}, out); auto cls = roots.root(out.result().value());
        context.allocate_instance<StrObject>(cls, out, U"payload"); auto obj = roots.root(out.result().value());
        str(context, obj, out); expect_text(out, U"payload"); EXPECT_EQ(context.type_of(out.result().value()), base.get(context));
        auto custom = text(U"override"); put(cls, U"__str__", function(custom));
        str(context, obj, out); EXPECT_EQ(out.result().value(), custom.get(context));
        auto producer = type(); put(producer, U"__repr__", function(obj));
        repr(context, instance(producer), out); EXPECT_EQ(out.result().value(), obj.get(context));
        EXPECT_EQ(hash_value(obj), hash_value(text(U"payload")));
    }

    TEST_F(CoreRepresentation, ExceptionReprAndStrUseArgumentProtocols)
    {
        ReturnSlot out(context);
        raise_error(context, CoreObject::ValueErrorType, U"bad input", out); auto error = roots.root(out.result().exception());
        repr(context, error, out); expect_text(out, U"ValueError('bad input')");
        str(context, error, out); expect_text(out, U"bad input");
        context.allocate_instance<ExceptionObject>(builtin(CoreObject::ExceptionType), out, context.builtin(CoreObject::EmptyTuple));
        auto empty = roots.root(out.result().value()); repr(context, empty, out); expect_text(out, U"Exception()");
        str(context, empty, out); expect_text(out, U"");
        std::array values{integer("1").get(context), text(U"x").get(context)}; auto args = tuple(values);
        context.payload<ExceptionObject>(error).args = args.get(context);
        repr(context, error, out); expect_text(out, U"ValueError(1, 'x')");
        str(context, error, out); expect_text(out, U"(1, 'x')");
    }

    TEST_F(CoreRepresentation, UserReprRecursionRaisesInsteadOfUsingTheContainerEllipsis)
    {
        auto cls = type(); auto obj = instance(cls); SignatureLayout signature; signature.parameters = {{U"self"}};
        ReturnSlot out(context);
        make_native_function(context, "recursive repr", signature,
            [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out) {
                RootScope roots(c); repr(c, roots.root(args[0]), out);
            }, {}, out);
        put(cls, U"__repr__", roots.root(out.result().value())); repr(context, obj, out); error(out, CoreObject::RecursionErrorType);
        put(cls, U"__repr__", function(text(U"recovered"))); repr(context, obj, out); expect_text(out, U"recovered");
    }

    TEST_F(CoreRepresentation, ReprIdentityGuardIsLocalToTheActiveAttachmentAcrossNativeWait)
    {
        std::array values{integer("4").get(context)}; auto container = tuple(values);
        ASSERT_TRUE(context.enter_repr(container)); std::exception_ptr failure;
        std::thread worker([&] {
            try
            {
                Attachment attached(interpreter); RootScope worker_roots(attached); ReturnSlot out(attached);
                repr(attached, worker_roots.root(container.get(attached)), out);
                ASSERT_EQ(out.result().tag(), ResultTag::Ok);
                EXPECT_EQ(attached.payload<StrObject>(out.result().value()).value, U"(4,)");
            }
            catch (...) { failure = std::current_exception(); }
        });
        { NativeWait wait(context); worker.join(); }
        context.leave_repr(container.get(context)); EXPECT_FALSE(failure);
        ReturnSlot out(context); repr(context, container, out); expect_text(out, U"(4,)");
    }
}
