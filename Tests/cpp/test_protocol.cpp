#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace cxxpython::core;
class CoreProtocols : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle builtin(CoreObject id)
    {
        return roots.root(context.builtin(id));
    }
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.result().value());
    }
    Handle integer(std::int64_t value)
    {
        ReturnSlot out(context);
        make_int(context, value, out);
        return roots.root(out.result().value());
    }
    Handle type(std::span<const Handle> bases = {})
    {
        ReturnSlot out(context);
        create_type(context, {"Custom", bases}, out);
        return roots.root(out.result().value());
    }
    Handle instance(Handle type)
    {
        ReturnSlot out(context);
        context.allocate_instance<InstanceObject>(type, out);
        return roots.root(out.result().value());
    }
    void put(Handle type, std::u32string name, Handle value)
    {
        ReturnSlot out(context);
        set_attribute(context, type, text(std::move(name)), value, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    }
    Handle function(Handle result, std::size_t arity)
    {
        SignatureLayout signature;
        signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
        if (arity)
            signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
        std::array captures{result.get(context)};
        ReturnSlot out(context);
        make_native_function(
            context, "protocol", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                c.collect();
                out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
            },
            captures, out);
        return roots.root(out.result().value());
    }
    void method(Handle type, std::u32string name, Handle result, std::size_t arity = 0)
    {
        put(type, std::move(name), function(result, arity));
    }
    void error(ReturnSlot &out, CoreObject type)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
    }
};

class ProtocolNativeDepth : public testing::TestWithParam<bool> {};

struct UnpublishedProtocolProbe {
    ObjectHeader header;
    UnpublishedProtocolProbe(InitialType head, Attachment &context, RootScope &roots) : header(head)
    {
        EXPECT_THROW(context.cached_protocol(Value(header), Protocol::Add, roots),
                     std::logic_error);
    }
    void trace(Tracer &) const noexcept {}
};

TEST_P(ProtocolNativeDepth, SpecialCallsConsumeAndUnwindDepth)
{
    HeapConfig config;
    config.collect_every_allocation = true;
    if (GetParam())
        config.max_native_depth = 1;
    else
        config.max_call_depth = 1;
    Interpreter interpreter{config};
    Attachment context{interpreter};
    context.bootstrap_core();
    RootScope roots{context};
    ReturnSlot out(context);

    make_int(context, 1, out);
    auto one = roots.root(out.result().value());
    make_int(context, 2, out);
    auto two = roots.root(out.result().value());

    create_type(context, {"DepthTruth", {}}, out);
    auto cls = roots.root(out.result().value());
    context.allocate_instance<InstanceObject>(cls, out);
    auto obj = roots.root(out.result().value());
    SignatureLayout bool_signature;
    bool_signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    make_native_function(
        context, "depth bool", bool_signature,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
            result.ok(c.builtin(CoreObject::True));
        },
        {}, out);
    auto bool_fn = roots.root(out.result().value());
    make_str(context, U"__bool__", out);
    auto bool_name = roots.root(out.result().value());
    ASSERT_EQ(set_attribute(context, cls, bool_name, bool_fn, out).tag(), ResultTag::Ok);

    std::array captures{one.get(context), two.get(context), obj.get(context)};
    make_native_function(
        context, "outer add", {},
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &result) {
            RootScope scope(c);
            const auto &captured = c.payload<NativeFunctionObject>(fn).captures;
            auto left = scope.root(captured[0]);
            auto right = scope.root(captured[1]);
            binary(c, left, right, BinaryOp::Add, result);
        },
        captures, out);
    auto outer_add = roots.root(out.result().value());
    make_native_function(
        context, "outer truth", {},
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &result) {
            RootScope scope(c);
            auto object = scope.root(c.payload<NativeFunctionObject>(fn).captures[2]);
            truth(c, object, result);
        },
        captures, out);
    auto outer_truth = roots.root(out.result().value());

    call(context, outer_add, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::RecursionErrorType));
    binary(context, one, two, BinaryOp::Add, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "3");

    call(context, outer_truth, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::RecursionErrorType));
    truth(context, obj, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));

    make_native_function(
        context, "host fault bool", bool_signature,
        [](Attachment &, Handle, std::span<const Value>, ReturnSlot &) {
            throw std::runtime_error("host fault");
        },
        {}, out);
    auto host_fault = roots.root(out.result().value());
    ASSERT_EQ(set_attribute(context, cls, bool_name, host_fault, out).tag(), ResultTag::Ok);
    EXPECT_THROW(truth(context, obj, out), std::runtime_error);
    EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    ASSERT_EQ(set_attribute(context, cls, bool_name, bool_fn, out).tag(), ResultTag::Ok);
    truth(context, obj, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
}

INSTANTIATE_TEST_SUITE_P(CallAndNativeLimits, ProtocolNativeDepth, testing::Bool());

TEST_F(CoreProtocols, TruthUsesTypeMethodsAndChecksBooleanBeforeLength)
{
    auto cls = type();
    auto obj = instance(cls);
    ReturnSlot out(context);
    truth(context, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    method(cls, U"__len__", integer(0));
    truth(context, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    method(cls, U"__bool__", builtin(CoreObject::True));
    truth(context, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    method(cls, U"__bool__", integer(1));
    truth(context, obj, out);
    error(out, CoreObject::TypeErrorType);
    auto empty = text(U"");
    truth(context, empty, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    truth(context, builtin(CoreObject::None), out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
}

TEST_F(CoreProtocols, LengthAcceptsIndexButRejectsNegativeAndUnboundedResults)
{
    auto cls = type();
    auto obj = instance(cls);
    ReturnSlot out(context);
    length(context, obj, out);
    error(out, CoreObject::TypeErrorType);
    method(cls, U"__len__", integer(-1));
    length(context, obj, out);
    error(out, CoreObject::ValueErrorType);
    make_int_decimal(context, "9223372036854775808", out);
    auto big = roots.root(out.result().value());
    method(cls, U"__len__", big);
    length(context, obj, out);
    error(out, CoreObject::OverflowErrorType);
    auto index_type = type();
    method(index_type, U"__index__", integer(3));
    auto indexed = instance(index_type);
    method(cls, U"__len__", indexed);
    length(context, obj, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "3");
    method(index_type, U"__index__", builtin(CoreObject::None));
    index(context, indexed, out);
    error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreProtocols, RichComparisonKeepsArbitraryResultsAndContainerIdentityIsSeparate)
{
    auto cls = type();
    auto obj = instance(cls);
    ReturnSlot out(context);
    method(cls, U"__eq__", builtin(CoreObject::False), 1);
    rich_compare(context, obj, obj, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    container_equal(context, obj, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    auto marker = text(U"result");
    method(cls, U"__eq__", marker, 1);
    auto other = instance(cls);
    rich_compare(context, obj, other, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), marker.get(context));
    container_equal(context, obj, other, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    rich_compare(context, obj, other, CompareOp::Ne, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
}

TEST_F(CoreProtocols, ContainsUsesTruthAndNotInNegates)
{
    auto cls = type();
    auto container = instance(cls);
    auto item = integer(1);
    method(cls, U"__contains__", integer(2), 1);
    ReturnSlot out(context);
    rich_compare(context, item, container, CompareOp::In, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    rich_compare(context, item, container, CompareOp::NotIn, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
}

TEST_F(CoreProtocols, StrictSubtypeReflectedComparisonPrecedesLeftEvenForSameDescriptor)
{
    auto base = type();
    auto subtype = type({&base, 1});
    auto left = instance(base);
    auto right = instance(subtype);
    auto marker = text(U"right");
    method(base, U"__lt__", builtin(CoreObject::False), 1);
    method(base, U"__gt__", marker, 1);
    ReturnSlot out(context);
    rich_compare(context, left, right, CompareOp::Lt, out);
    EXPECT_EQ(out.result().value(), marker.get(context));
}

TEST_F(CoreProtocols, BinaryReflectedPriorityDependsOnDescriptorIdentityNotThunk)
{
    auto base = type();
    auto subtype = type({&base, 1});
    auto left = instance(base);
    auto right = instance(subtype);
    auto l = text(U"left");
    auto r = text(U"right");
    auto inherited = text(U"inherited");
    method(base, U"__add__", l, 1);
    method(base, U"__radd__", inherited, 1);
    ReturnSlot out(context);
    binary(context, left, right, BinaryOp::Add, out);
    EXPECT_EQ(out.result().value(), l.get(context));
    method(subtype, U"__radd__", r, 1);
    binary(context, left, right, BinaryOp::Add, out);
    EXPECT_EQ(out.result().value(), r.get(context));
    method(subtype, U"__radd__", builtin(CoreObject::NotImplemented), 1);
    binary(context, left, right, BinaryOp::Add, out);
    EXPECT_EQ(out.result().value(), l.get(context));
    method(base, U"__iadd__", inherited, 1);
    inplace(context, left, right, BinaryOp::Add, out);
    EXPECT_EQ(out.result().value(), inherited.get(context));
    method(base, U"__iadd__", builtin(CoreObject::NotImplemented), 1);
    inplace(context, left, right, BinaryOp::Add, out);
    EXPECT_EQ(out.result().value(), l.get(context));
}

TEST_F(CoreProtocols, NotImplementedAndNoneAndMissingHaveDistinctDispatchMeaning)
{
    auto cls = type();
    auto a = instance(cls);
    auto b = instance(cls);
    ReturnSlot out(context);
    method(cls, U"__eq__", builtin(CoreObject::NotImplemented), 1);
    rich_compare(context, a, b, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    rich_compare(context, a, a, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    rich_compare(context, a, b, CompareOp::Lt, out);
    error(out, CoreObject::TypeErrorType);
    put(cls, U"__add__", builtin(CoreObject::None));
    binary(context, a, b, BinaryOp::Add, out);
    error(out, CoreObject::TypeErrorType);
    put(cls, U"__hash__", builtin(CoreObject::None));
    auto slot = resolve_protocol(context, cls, Protocol::Hash, roots);
    EXPECT_EQ(slot.state, SlotState::Disabled);
    auto missing = resolve_protocol(context, cls, Protocol::Len, roots);
    EXPECT_EQ(missing.state, SlotState::Missing);
    invoke_protocol(context, a, slot, {}, out);
    error(out, CoreObject::TypeErrorType);
    invoke_protocol(context, a, missing, {}, out);
    error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreProtocols, ReflectedOverrideUsesClassDescriptorBindingAndRichInequality)
{
    auto base = type();
    auto subtype = type({&base, 1});
    auto a = instance(base);
    auto b = instance(subtype);
    auto marker = text(U"left wins");
    method(base, U"__add__", marker, 1);
    method(base, U"__radd__", builtin(CoreObject::NotImplemented), 1);
    auto equality = type();
    method(equality, U"__ne__", builtin(CoreObject::False), 1);
    auto compared = instance(equality);
    auto descriptor_type = type();
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
    std::array captures{compared.get(context)};
    ReturnSlot out(context);
    make_native_function(
        context, "__get__", signature,
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
            c.collect();
            out.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
        },
        captures, out);
    put(descriptor_type, U"__get__", roots.root(out.result().value()));
    put(subtype, U"__radd__", instance(descriptor_type));
    binary(context, a, b, BinaryOp::Add, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), marker.get(context));
}

TEST_F(CoreProtocols, ReflectedFallbackResolvesAgainAfterLeftCallbackMutatesRightType)
{
    auto left_type = type();
    auto right_type = type();
    auto a = instance(left_type);
    auto b = instance(right_type);
    auto replacement = function(text(U"fresh reflected result"), 1);
    auto name = text(U"__radd__");
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"other"}};
    std::array captures{right_type.get(context), name.get(context), replacement.get(context)};
    ReturnSlot out(context);
    make_native_function(
        context, "mutate", signature,
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
            RootScope roots(c);
            auto captures = c.payload<NativeFunctionObject>(fn).captures;
            set_attribute(c, roots.root(captures[0]), roots.root(captures[1]),
                          roots.root(captures[2]), out);
            c.collect();
            out.ok(c.builtin(CoreObject::NotImplemented));
        },
        captures, out);
    put(left_type, U"__add__", roots.root(out.result().value()));
    binary(context, a, b, BinaryOp::Add, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"fresh reflected result");
}

TEST_F(CoreProtocols, ClassDefinitionHashRuleDoesNotRerunOnLaterMutation)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out);
    auto namespace_ = roots.root(out.result().value());
    auto equality = function(builtin(CoreObject::True), 1);
    detail::name_store(context, namespace_.get(context), text(U"__eq__").get(context),
                       equality.get(context));
    TypeSpec spec{"DefinesEquality"};
    spec.dictionary = namespace_;
    create_type(context, spec, out);
    auto cls = roots.root(out.result().value());
    EXPECT_EQ(resolve_protocol(context, cls, Protocol::Hash, roots).state, SlotState::Disabled);
    auto late = type();
    put(late, U"__eq__", equality);
    EXPECT_FALSE(detail::name_lookup(context, *TypeView(context, late).dictionary(), U"__hash__"));
    EXPECT_FALSE(detail::name_lookup(context, namespace_.get(context), U"__hash__"));
}

TEST_F(CoreProtocols, SameTypeArithmeticDoesNotTryReflectedButEqualityDoes)
{
    auto cls = type();
    auto a = instance(cls);
    auto b = instance(cls);
    ReturnSlot out(context);
    method(cls, U"__add__", builtin(CoreObject::NotImplemented), 1);
    method(cls, U"__radd__", text(U"must not run"), 1);
    binary(context, a, b, BinaryOp::Add, out);
    error(out, CoreObject::TypeErrorType);
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"other"}};
    std::array captures{b.get(context)};
    make_native_function(
        context, "eq", signature,
        [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
            out.ok(c.builtin(args[0] == c.payload<NativeFunctionObject>(fn).captures[0]
                                 ? CoreObject::True
                                 : CoreObject::NotImplemented));
        },
        captures, out);
    put(cls, U"__eq__", roots.root(out.result().value()));
    rich_compare(context, a, b, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
}

TEST_F(CoreProtocols, FullMroAndEpochRevalidationUseCurrentClassDefinitions)
{
    auto first = type();
    auto second = type();
    auto marker = integer(4);
    method(second, U"__len__", marker);
    std::array bases{first, second};
    auto cls = type(bases);
    auto obj = instance(cls);
    auto slot = resolve_protocol(context, cls, Protocol::Len, roots);
    EXPECT_EQ(slot.owner.get(context), second.get(context));
    auto replacement = integer(5);
    method(second, U"__len__", replacement);
    ReturnSlot out(context);
    invoke_protocol(context, obj, slot, {}, out);
    EXPECT_EQ(out.result().value(), replacement.get(context));
}

TEST_F(CoreProtocols, ResolvedDescriptorSurvivesReplacementDuringNativeCallback)
{
    for (bool stale : {false, true}) {
        auto cls = type();
        auto obj = instance(cls);
        auto name = text(U"__len__");
        RootScope snapshots(context);
        ReturnSlot out(context);
        if (stale)
            method(cls, U"__len__", integer(1));
        auto slot = resolve_protocol(context, cls, Protocol::Len, snapshots);

        RootScope temporary(context);
        make_str(context, U"selected result", out);
        auto marker = temporary.root(out.value());
        SignatureLayout signature;
        signature.parameters = {{U"self"}};
        std::array captures{cls.get(context), name.get(context), marker.get(context)};
        make_native_function(
            context, "replace selected protocol", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &result) {
                RootScope local(c);
                const auto &captured = c.payload<NativeFunctionObject>(fn).captures;
                auto owner = local.root(captured[0]);
                auto name = local.root(captured[1]);
                auto none = local.root(c.builtin(CoreObject::None));
                ReturnSlot mutation(c);
                set_attribute(c, owner, name, none, mutation);
                ASSERT_EQ(mutation.tag(), ResultTag::Ok);
                c.collect();
                result.ok(c.payload<NativeFunctionObject>(fn).captures[2]);
            },
            captures, out);
        auto selected = temporary.root(out.value());
        set_attribute(context, cls, name, selected, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        temporary.release(marker);
        temporary.release(selected);
        if (!stale)
            slot = resolve_protocol(context, cls, Protocol::Len, snapshots);

        invoke_protocol(context, obj, slot, {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        context.collect();
        EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"selected result");
        if (!stale) {
            const auto &captured = context.payload<NativeFunctionObject>(slot.descriptor).captures;
            EXPECT_EQ(captured[2], out.value());
        }
        invoke_protocol(context, obj, slot, {}, out);
        error(out, CoreObject::TypeErrorType);
    }
}

TEST_F(CoreProtocols, ResolvedSlotUsesReceiverCurrentClass)
{
    auto original = type();
    auto replacement = type();
    method(original, U"__len__", integer(3));
    method(replacement, U"__len__", integer(8));
    auto obj = instance(original);
    auto slot = resolve_protocol(context, original, Protocol::Len, roots);
    ReturnSlot out(context);
    set_attribute(context, obj, text(U"__class__"), replacement, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    invoke_protocol(context, obj, slot, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "8");
}

TEST_F(CoreProtocols, ThreadPrivateCacheIncludesMissingSlotsAndInvalidatesOnBaseMutation)
{
    auto base = type();
    auto cls = type({&base, 1});
    context.set_gc_enabled(false);
    EXPECT_FALSE(context.cached_protocol(cls, Protocol::Len, roots));
    EXPECT_EQ(resolve_protocol(context, cls, Protocol::Len, roots).state, SlotState::Missing);
    ASSERT_TRUE(context.cached_protocol(cls, Protocol::Len, roots));
    method(base, U"__len__", integer(7));
    EXPECT_FALSE(context.cached_protocol(cls, Protocol::Len, roots));
    auto slot = resolve_protocol(context, cls, Protocol::Len, roots);
    ASSERT_EQ(slot.state, SlotState::Native);
    EXPECT_EQ(slot.owner.get(context), base.get(context));
    auto cached = context.cached_protocol(cls, Protocol::Len, roots);
    ASSERT_TRUE(cached);
    EXPECT_EQ(cached->descriptor.get(context), slot.descriptor.get(context));
    context.collect();
    EXPECT_FALSE(context.cached_protocol(cls, Protocol::Len, roots));
    ReturnSlot out(context);
    invoke_protocol(context, instance(cls), slot, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "7");
}

TEST_F(CoreProtocols, SequenceNativeRoleSurvivesCacheAndTracksDescriptorChanges)
{
    context.set_gc_enabled(false);
    auto list = builtin(CoreObject::ListType);
    auto integer_type = builtin(CoreObject::IntType);
    for (auto protocol : {Protocol::Add, Protocol::Mul}) {
        auto slot = resolve_protocol(context, list, protocol, roots);
        ASSERT_EQ(slot.state, SlotState::Native);
        EXPECT_EQ(slot.native_kind, NativeSlotKind::Sequence);
        auto cached = context.cached_protocol(list, protocol, roots);
        ASSERT_TRUE(cached);
        EXPECT_EQ(cached->native_kind, NativeSlotKind::Sequence);
    }
    auto int_add = resolve_protocol(context, integer_type, Protocol::Add, roots);
    EXPECT_EQ(int_add.native_kind, NativeSlotKind::Ordinary);
    auto int_cached = context.cached_protocol(integer_type, Protocol::Add, roots);
    ASSERT_TRUE(int_cached);
    EXPECT_EQ(int_cached->native_kind, NativeSlotKind::Ordinary);

    auto add = resolve_protocol(context, list, Protocol::Add, roots).descriptor;
    auto subclass = type({&list, 1});
    auto unrelated = type();
    EXPECT_EQ(resolve_protocol(context, subclass, Protocol::Add, roots).native_kind,
              NativeSlotKind::Sequence);
    put(subclass, U"__add__", add);
    EXPECT_EQ(resolve_protocol(context, subclass, Protocol::Add, roots).native_kind,
              NativeSlotKind::Sequence);
    auto copied = context.cached_protocol(subclass, Protocol::Add, roots);
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied->native_kind, NativeSlotKind::Sequence);
    put(unrelated, U"__add__", add);
    EXPECT_EQ(resolve_protocol(context, unrelated, Protocol::Add, roots).native_kind,
              NativeSlotKind::Ordinary);
    auto unrelated_cached = context.cached_protocol(unrelated, Protocol::Add, roots);
    ASSERT_TRUE(unrelated_cached);
    EXPECT_EQ(unrelated_cached->native_kind, NativeSlotKind::Ordinary);

    method(subclass, U"__add__", builtin(CoreObject::NotImplemented), 1);
    EXPECT_FALSE(context.cached_protocol(subclass, Protocol::Add, roots));
    EXPECT_EQ(resolve_protocol(context, subclass, Protocol::Add, roots).native_kind,
              NativeSlotKind::Ordinary);
    auto replacement = context.cached_protocol(subclass, Protocol::Add, roots);
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->native_kind, NativeSlotKind::Ordinary);
    put(subclass, U"__add__", add);
    EXPECT_FALSE(context.cached_protocol(subclass, Protocol::Add, roots));
    EXPECT_EQ(resolve_protocol(context, subclass, Protocol::Add, roots).native_kind,
              NativeSlotKind::Sequence);
    auto restored = context.cached_protocol(subclass, Protocol::Add, roots);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->native_kind, NativeSlotKind::Sequence);
}

TEST_F(CoreProtocols, CachedNativeCallKindsPreserveBindingAndReceiverChecks)
{
    for (const auto binding :
         {NativeFunctionBinding::Function, NativeFunctionBinding::Descriptor}) {
        auto cls = type();
        auto obj = instance(cls);
        auto marker = integer(17);
        SignatureLayout signature;
        if (binding == NativeFunctionBinding::Descriptor)
            signature.parameters = {{U"self"}};
        const std::array captures{marker.get(context),
                                  context.builtin(binding == NativeFunctionBinding::Descriptor
                                                      ? CoreObject::True
                                                      : CoreObject::False)};
        ReturnSlot out(context);
        make_native_function(
            context, "cached binding", signature,
            [](Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &result) {
                const auto &captured = c.payload<NativeFunctionObject>(function).captures;
                EXPECT_EQ(args.size(), captured[1] == c.builtin(CoreObject::True) ? 1U : 0U);
                c.collect();
                result.ok(c.payload<NativeFunctionObject>(function).captures[0]);
            },
            captures, out, binding);
        put(cls, U"__len__", roots.root(out.value()));
        context.set_gc_enabled(false);
        auto slot = resolve_protocol(context, cls, Protocol::Len, roots);
        const auto kind = binding == NativeFunctionBinding::Descriptor ? NativeCallKind::Descriptor
                                                                       : NativeCallKind::Function;
        EXPECT_EQ(slot.native_call, kind);
        const auto cached = context.cached_protocol(cls, Protocol::Len, roots);
        ASSERT_TRUE(cached);
        EXPECT_EQ(cached->native_call, kind);
        invoke_protocol(context, obj, slot, {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), marker.get(context));
        detail::invoke_protocol(context, obj, slot, {}, out, roots);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), marker.get(context));

        auto tuple_len =
            resolve_protocol(context, builtin(CoreObject::TupleType), Protocol::Len, roots);
        EXPECT_EQ(tuple_len.native_call, NativeCallKind::MethodDescriptor);
        put(cls, U"__len__", tuple_len.descriptor);
        invoke_protocol(context, obj, slot, {}, out);
        error(out, CoreObject::TypeErrorType);
        EXPECT_EQ(resolve_protocol(context, cls, Protocol::Len, roots).native_call,
                  NativeCallKind::MethodDescriptor);

        put(cls, U"__len__", builtin(CoreObject::None));
        EXPECT_EQ(resolve_protocol(context, cls, Protocol::Len, roots).native_call,
                  NativeCallKind::Generic);
        invoke_protocol(context, obj, slot, {}, out);
        error(out, CoreObject::TypeErrorType);
    }
}

TEST_F(CoreProtocols, ResolvedTypeAndSnapshotHaveIndependentRootsOnMissAndHit)
{
    context.set_gc_enabled(false);
    auto cls = type();
    method(cls, U"__len__", integer(7));
    RootScope snapshots(context);
    auto input = snapshots.root(cls.get(context));
    ASSERT_FALSE(context.cached_protocol(input, Protocol::Len, snapshots));
    auto first = resolve_protocol(context, input, Protocol::Len, snapshots);
    ASSERT_TRUE(context.cached_protocol(input, Protocol::Len, snapshots));
    auto second = resolve_protocol(context, input, Protocol::Len, snapshots);
    snapshots.release(input);
    EXPECT_EQ(first.type.get(context), cls.get(context));
    EXPECT_EQ(second.type.get(context), cls.get(context));
    snapshots.release(first.type);
    snapshots.release(first.owner);
    snapshots.release(first.descriptor);
    context.collect();
    EXPECT_EQ(second.type.get(context), cls.get(context));
    EXPECT_EQ(second.owner.get(context), cls.get(context));
    ReturnSlot out(context);
    invoke_protocol(context, instance(cls), second, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "7");
}

TEST_F(CoreProtocols, ValueCacheLookupChecksOwnershipStateAndRootGrowth)
{
    context.set_gc_enabled(false);
    auto cls = type();
    method(cls, U"__len__", integer(7));
    const auto value = cls.get(context);
    resolve_protocol(context, cls, Protocol::Len, roots);
    auto cached = context.cached_protocol(value, Protocol::Len, roots);
    ASSERT_TRUE(cached);
    const auto descriptor = cached->descriptor.get(context);
    Interpreter other;
    Attachment foreign(other);
    foreign.bootstrap_core();
    EXPECT_THROW(
        context.cached_protocol(foreign.builtin(CoreObject::IntType), Protocol::Len, roots),
        std::logic_error);
    {
        NativeWait wait(context);
        EXPECT_THROW(context.cached_protocol(value, Protocol::Len, roots), std::logic_error);
    }
    {
        NoSafepoint guard(context);
        EXPECT_THROW(context.cached_protocol(value, Protocol::Len, roots), std::logic_error);
        EXPECT_EQ(cls.get(context), value);
        EXPECT_EQ(cached->type.get(context), value);
        EXPECT_EQ(cached->descriptor.get(context), descriptor);
    }
    ASSERT_TRUE(context.cached_protocol(value, Protocol::Len, roots));
    ReturnSlot out(context);
    context.allocate<UnpublishedProtocolProbe>(out, context, roots);
    EXPECT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(cls.get(context), value);
}

TEST(ProtocolCache, CachedClassesDoNotBecomePermanentRoots)
{
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap_core();
    context.collect();
    const auto baseline = context.stats().objects;
    {
        RootScope roots(context);
        ReturnSlot out(context);
        create_type(context, {U"Ephemeral"}, out);
        const auto cls = roots.root(out.result().value());
        resolve_protocol(context, cls, Protocol::Len, roots);
        ASSERT_TRUE(context.cached_protocol(cls, Protocol::Len, roots));
    }
    context.collect();
    EXPECT_EQ(context.stats().objects, baseline);
}

TEST_F(CoreProtocols, OperandsSurviveAliasedOutputAndMemoryErrorIsRooted)
{
    ReturnSlot out(context);
    make_int(context, 42, out);
    RootScope temporary(context);
    auto only = temporary.root(out.result().value());
    truth(context, only, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    auto cls = type();
    auto obj = instance(cls);
    context.set_allocation_limit(0);
    length(context, obj, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
}

TEST_F(CoreProtocols, ValueOperandsSurviveOutputReuseDuringCollectingProtocols)
{
    auto cls = type();
    ReturnSlot out(context);
    auto install = [&](std::u32string name, Handle result, std::size_t arity) {
        SignatureLayout signature;
        signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
        if (arity)
            signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
        if (arity == 2)
            signature.parameters.push_back({U"value", ParameterKind::PositionalOnly});
        std::array captures{cls.get(context), result.get(context)};
        make_native_function(
            context, "collecting protocol", signature,
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &result) {
                c.collect();
                const auto &captured = c.payload<NativeFunctionObject>(fn).captures;
                for (auto arg : args)
                    EXPECT_EQ(c.type_of(arg), captured[0]);
                result.ok(captured[1]);
            },
            captures, out);
        put(cls, std::move(name), roots.root(out.value()));
    };
    install(U"__neg__", integer(17), false);
    install(U"__bool__", builtin(CoreObject::True), false);
    install(U"__len__", integer(4), false);
    install(U"__eq__", builtin(CoreObject::True), true);
    install(U"__getitem__", integer(19), true);
    install(U"__add__", integer(23), 1);
    install(U"__iadd__", integer(29), 1);
    install(U"__setitem__", builtin(CoreObject::None), 2);
    install(U"__delitem__", builtin(CoreObject::None), 1);

    context.allocate_instance<InstanceObject>(cls, out);
    unary(context, out.value(), UnaryOp::Negative, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "17");
    context.allocate_instance<InstanceObject>(cls, out);
    truth(context, out.value(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    context.allocate_instance<InstanceObject>(cls, out);
    length(context, out.value(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "4");

    auto receiver = instance(cls);
    context.allocate_instance<InstanceObject>(cls, out);
    rich_compare(context, receiver.get(context), out.value(), CompareOp::Eq, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    context.allocate_instance<InstanceObject>(cls, out);
    get_item(context, receiver.get(context), out.value(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "19");
    context.allocate_instance<InstanceObject>(cls, out);
    binary(context, receiver.get(context), out.value(), BinaryOp::Add, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "23");
    context.allocate_instance<InstanceObject>(cls, out);
    inplace(context, out.value(), receiver.get(context), BinaryOp::Add, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "29");
    context.allocate_instance<InstanceObject>(cls, out);
    set_item(context, receiver.get(context), receiver.get(context), out.value(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::None));
    context.allocate_instance<InstanceObject>(cls, out);
    del_item(context, out.value(), receiver.get(context), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::None));
}

TEST_F(CoreProtocols, OwnedProtocolsKeepIndependentOperandsDuringCollection)
{
    auto cls = type();
    auto left = instance(cls);
    auto right = instance(cls);
    method(cls, U"__index__", integer(7));
    method(cls, U"__bool__", builtin(CoreObject::True));
    method(cls, U"__eq__", builtin(CoreObject::True), 1);
    ReturnSlot out(context);

    detail::index_owned(context, left, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "7");
    detail::truth_owned(context, left, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    detail::rich_compare_owned(context, left, right, CompareOp::Eq, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    detail::container_equal_owned(context, left, right, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    context.collect();
    EXPECT_EQ(context.type_of(left.get(context)), cls.get(context));
    EXPECT_EQ(context.type_of(right.get(context)), cls.get(context));

    // Public operations continue to accept an operand independently rooted from out.
    out.ok(left.get(context));
    auto reused = roots.root(out.value());
    truth(context, reused, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    EXPECT_EQ(context.type_of(reused.get(context)), cls.get(context));
}

TEST_F(CoreProtocols, NativeHashNormalizesAndKeepsOperandRooted)
{
    auto cls = type();
    auto obj = instance(cls);
    ReturnSlot out(context);
    method(cls, U"__hash__", integer(-1));
    out.ok(obj.get(context));
    auto hash = detail::hash_word(context, obj, out);
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, -2);
    EXPECT_EQ(out.tag(), ResultTag::Empty);

    make_int_decimal(context, "1267650600228229401496703205376", out);
    auto big = roots.root(out.value());
    method(cls, U"__hash__", big);
    hash = detail::hash_word(context, obj, out);
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, 549755813888LL);
    EXPECT_EQ(out.tag(), ResultTag::Empty);
    EXPECT_EQ(context.type_of(obj.get(context)), cls.get(context));

    method(cls, U"__hash__", builtin(CoreObject::None));
    hash = detail::hash_word(context, obj, out);
    EXPECT_FALSE(hash.has_value());
    error(out, CoreObject::TypeErrorType);
    context.collect();
    EXPECT_EQ(context.type_of(obj.get(context)), cls.get(context));
}

TEST_F(CoreProtocols, ImplicitLookupIgnoresInstanceMethodsAndAttributeHooks)
{
    auto cls = type();
    method(cls, U"__len__", integer(0));
    auto obj = instance(cls);
    ReturnSlot out(context);
    auto shadow = function(integer(8), 0);
    set_attribute(context, obj, text(U"__len__"), shadow, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    method(cls, U"__getattribute__", integer(9), 1);
    truth(context, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    length(context, obj, out);
    EXPECT_EQ(int_decimal(context, out.result().value()), "0");
}

TEST_F(CoreProtocols, RecursiveProtocolReentryUsesTheExistingCallDepthLimit)
{
    auto cls = type();
    auto obj = instance(cls);
    SignatureLayout signature;
    signature.parameters = {{U"self"}};
    ReturnSlot out(context);
    make_native_function(
        context, "recursive bool", signature,
        [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out) {
            RootScope roots(c);
            truth(c, roots.root(args[0]), out);
        },
        {}, out);
    put(cls, U"__bool__", roots.root(out.result().value()));
    truth(context, obj, out);
    error(out, CoreObject::RecursionErrorType);
    del_attribute(context, cls, text(U"__bool__"), out);
    truth(context, obj, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
}

TEST_F(CoreProtocols, ForeignOperandsResultsAndEvenStaleResolutionAreRejected)
{
    Interpreter other;
    Attachment foreign(other);
    foreign.bootstrap_core();
    RootScope foreign_roots(foreign);
    auto foreign_type = foreign_roots.root(foreign.builtin(CoreObject::IntType));
    auto foreign_value = foreign_roots.root(foreign.builtin(CoreObject::IntOne));
    auto slot = resolve_protocol(foreign, foreign_type, Protocol::Bool, foreign_roots);
    context.invalidate_types();
    auto value = integer(3);
    ReturnSlot out(context);
    ReturnSlot foreign_out(foreign);
    EXPECT_THROW(truth(context, foreign_value, out), std::logic_error);
    EXPECT_THROW(truth(context, foreign_value.get(foreign), out), std::logic_error);
    EXPECT_THROW(
        binary(context, value.get(context), foreign_value.get(foreign), BinaryOp::Add, out),
        std::logic_error);
    EXPECT_THROW(invoke_protocol(context, value, slot, {}, out), std::logic_error);
    foreign_out.ok(foreign.builtin(CoreObject::None));
    EXPECT_THROW(truth(context, value, foreign_out), std::logic_error);
    EXPECT_EQ(foreign_out.result().value(), foreign.builtin(CoreObject::None));
    EXPECT_THROW(truth(context, value.get(context), foreign_out), std::logic_error);
    EXPECT_EQ(foreign_out.result().value(), foreign.builtin(CoreObject::None));
    {
        NoSafepoint guard(context);
        EXPECT_THROW(truth(context, value.get(context), out), std::logic_error);
    }
    EXPECT_NE(context.builtin(CoreObject::IntOne), foreign.builtin(CoreObject::IntOne));
}

TEST_F(CoreProtocols, FormatUsesTypeLookupAndValidatesCallbackResultsUnderCollection)
{
    auto cls = type();
    auto obj = instance(cls);
    auto marker = text(U"formatted");
    method(cls, U"__format__", marker, 1);
    ReturnSlot out(context);
    set_attribute(context, obj, text(U"__format__"), function(integer(99), 1), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    method(cls, U"__getattribute__", integer(99), 1);
    format(context, obj, text(U"custom"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), marker.get(context));
    method(cls, U"__format__", integer(9), 1);
    format(context, obj, text(U""), out);
    error(out, CoreObject::TypeErrorType);
    format(context, obj, integer(0), out);
    error(out, CoreObject::TypeErrorType);
    put(cls, U"__format__", builtin(CoreObject::None));
    format(context, obj, text(U""), out);
    error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreProtocols, FormatBindsCustomDescriptorsBeforeCallingWithOnlySpec)
{
    auto cls = type();
    auto obj = instance(cls);
    SignatureLayout callable_signature;
    callable_signature.parameters = {{U"spec", ParameterKind::PositionalOnly}};
    ReturnSlot out(context);
    make_native_function(
        context, "format callback", callable_signature,
        [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out) {
            c.collect();
            out.ok(args[0]);
        },
        {}, out);
    auto callback = roots.root(out.result().value());
    auto descriptor_type = type();
    SignatureLayout get_signature;
    get_signature.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
    std::array captures{callback.get(context), obj.get(context), cls.get(context)};
    make_native_function(
        context, "format descriptor", get_signature,
        [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
            c.collect();
            const auto &captured = c.payload<NativeFunctionObject>(fn).captures;
            EXPECT_EQ(args[1], captured[1]);
            EXPECT_EQ(args[2], captured[2]);
            out.ok(captured[0]);
        },
        captures, out);
    put(descriptor_type, U"__get__", roots.root(out.result().value()));
    put(cls, U"__format__", instance(descriptor_type));
    format(context, obj, text(U"bound spec"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"bound spec");
}

TEST_F(CoreProtocols, FormatObjectStringAndBooleanSemantics)
{
    ReturnSlot out(context);
    auto cls = type();
    auto obj = instance(cls);
    method(cls, U"__str__", text(U"plain"));
    format(context, obj, text(U""), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"plain");
    format(context, obj, text(U">8"), out);
    error(out, CoreObject::TypeErrorType);
    struct Sample {
        const char32_t *value;
        const char32_t *spec;
        const char32_t *expected;
    };
    for (auto sample : {Sample{U"abc", U"*^8.2s", U"***ab***"},
                        {U"abc", U"05", U"abc00"},
                        {U"\U0001f600xy", U"\u4e2d>4.2", U"\u4e2d\u4e2d\U0001f600x"}}) {
        format(context, text(sample.value), text(sample.spec), out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, sample.expected);
    }
    format(context, builtin(CoreObject::True), text(U""), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"True");
    format(context, builtin(CoreObject::True), text(U"d"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"1");
    for (auto spec : {U"+s", U"=5", U"#s", U"zs", U",s", U".s"}) {
        format(context, text(U"x"), text(spec), out);
        error(out, CoreObject::ValueErrorType);
    }
}

TEST_F(CoreProtocols, FormatIntegersPreservesPrecisionPrefixesAndGrouping)
{
    ReturnSlot out(context);
    struct Sample {
        std::int64_t value;
        const char32_t *spec;
        const char32_t *expected;
    };
    for (auto sample : {Sample{-42, U"+08d", U"-0000042"},
                        {42, U"#08x", U"0x00002a"},
                        {0, U"#b", U"0b0"},
                        {1234, U"010,d", U"00,001,234"},
                        {-42, U"*=8d", U"-*****42"},
                        {0x12345, U"#_X", U"0X1_2345"},
                        {65, U"4c", U"   A"},
                        {12, U".1f", U"12.0"}}) {
        format(context, integer(sample.value), text(sample.spec), out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, sample.expected);
    }
    make_int_decimal(context, "1267650600228229401496703205376", out);
    auto big = roots.root(out.result().value());
    format(context, big, text(U"_d"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value,
              U"1_267_650_600_228_229_401_496_703_205_376");
    format(context, big, text(U"#x"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value,
              U"0x10000000000000000000000000");
    for (auto spec : {U".2d", U"zd", U",x", U"_,d", U"+c", U"q", U"n"}) {
        format(context, integer(1), text(spec), out);
        error(out, CoreObject::ValueErrorType);
    }
    format(context, integer(-1), text(U"c"), out);
    error(out, CoreObject::OverflowErrorType);
}

TEST_F(CoreProtocols, FormatFloatsHandlesRoundingSpecialValuesAndDefaultType)
{
    ReturnSlot out(context);
    struct Sample {
        double value;
        const char32_t *spec;
        const char32_t *expected;
    };
    for (auto sample : {Sample{2.5, U".0f", U"2"},
                        {3.5, U".0f", U"4"},
                        {1.0, U"#.0f", U"1."},
                        {1.0, U"#.4g", U"1.000"},
                        {1234.5, U",.2f", U"1,234.50"},
                        {0.125, U".1%", U"12.5%"},
                        {-0.0, U".1f", U"-0.0"},
                        {-0.01, U"z.1f", U"0.0"},
                        {-0.01, U"+z.1f", U"+0.0"},
                        {100.0, U".3", U"1e+02"},
                        {100.0, U".3g", U"100"},
                        {1e16, U">1", U"1e+16"},
                        {1e-5, U".2e", U"1.00e-05"},
                        {std::numeric_limits<double>::infinity(), U"+F", U"+INF"},
                        {-std::numeric_limits<double>::quiet_NaN(), U"G", U"NAN"}}) {
        make_float(context, sample.value, out);
        auto value = roots.root(out.result().value());
        format(context, value, text(sample.spec), out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, sample.expected);
    }
}

TEST_F(CoreProtocols, AsciiEscapesUnicodeAfterCustomReprAndBuiltinsHaveDefaults)
{
    auto cls = type();
    auto obj = instance(cls);
    method(cls, U"__repr__", text(U"\u00e9\u4e2d\U0001f600"));
    ReturnSlot out(context);
    ascii(context, obj, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"\\xe9\\u4e2d\\U0001f600");
    method(cls, U"__repr__", integer(1));
    ascii(context, obj, out);
    error(out, CoreObject::TypeErrorType);
    auto mapping = context.builtin(CoreObject::Builtins);
    auto format_function = detail::name_lookup(context, mapping, U"format");
    auto ascii_function = detail::name_lookup(context, mapping, U"ascii");
    ASSERT_TRUE(format_function);
    ASSERT_TRUE(ascii_function);
    const auto formatter = roots.root(*format_function);
    const auto ascii_builtin = roots.root(*ascii_function);
    const std::array args{context.builtin(CoreObject::True)};
    call(context, formatter, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"True");
    call(context, ascii_builtin, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"True");
}
} // namespace
