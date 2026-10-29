#include "cxxpython/execution.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
// Serial test observations: the owning graph stays rooted and no writer races this read.
template <class T, class Owner>
std::optional<Value> read_edge(Attachment &context, Owner owner, AtomicGcEdge T::*field)
{
    const auto value = [&] {
        if constexpr (std::is_same_v<Owner, Handle>)
            return owner.get(context);
        else
            return owner;
    }();
    ReturnSlot saved(context);
    context.load(value, context.payload<T>(value).*field, saved);
    return saved.result().tag() == ResultTag::Ok ? std::optional<Value>{saved.result().value()}
                                                 : std::optional<Value>{};
}
class CoreTraceback : public testing::Test {
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
    Handle function(CodeBuilder &builder)
    {
        ReturnSlot out(context);
        builder.freeze(out);
        auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out);
        auto globals = roots.root(out.result().value());
        make_function(context, {code, globals}, out);
        return roots.root(out.result().value());
    }
    Handle raiser()
    {
        CodeSpec spec;
        spec.name = U"leaf";
        spec.first_line = 10;
        CodeBuilder builder(context, spec);
        ReturnSlot out(context);
        raise_error(context, CoreObject::ValueErrorType, U"trace me", out);
        builder.emit(Opcode::LoadConst, builder.constant(roots.root(out.result().exception())));
        builder.emit(Opcode::RaiseInstance, 0, {12, 4, 12, 9});
        return function(builder);
    }
    Handle caller(Handle target, std::uint32_t line = 30)
    {
        CodeBuilder builder(context);
        builder.emit(Opcode::LoadConst, builder.constant(target));
        builder.call(0, {}, {line, 0, line, 6});
        builder.emit(Opcode::Return);
        return function(builder);
    }
    Handle get(Handle object, std::u32string name)
    {
        ReturnSlot out(context);
        get_attribute(context, object, text(std::move(name)), out);
        EXPECT_EQ(out.result().tag(), ResultTag::Ok);
        return roots.root(out.result().value());
    }
    Handle failure(Handle fn)
    {
        ReturnSlot out(context);
        call(context, fn, {}, out);
        EXPECT_EQ(out.result().tag(), ResultTag::Raised);
        return roots.root(out.result().exception());
    }
    void error(ReturnSlot &out, CoreObject type)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
    }
    Handle construct(Handle next, Handle frame, Handle lasti, Handle line)
    {
        ReturnSlot out(context);
        std::array args{next.get(context), frame.get(context), lasti.get(context),
                        line.get(context)};
        call(context, builtin(CoreObject::TracebackType), {args, {}}, out);
        EXPECT_EQ(out.result().tag(), ResultTag::Ok);
        return roots.root(out.result().value());
    }
};

TEST_F(CoreTraceback, ScheduledErrorsKeepActualFramesInOuterToInnerOrder)
{
    auto leaf = raiser();
    auto middle = caller(leaf, 25);
    auto outer = caller(middle, 40);
    auto exception = failure(outer);
    auto trace = get(exception, U"__traceback__");
    const std::array expected{outer.get(context), middle.get(context), leaf.get(context)};
    const std::array<std::int64_t, 3> lines{40, 25, 12};
    std::optional<Value> previous;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(context.type_of(trace), context.builtin(CoreObject::TracebackType));
        const auto &node = context.payload<TracebackObject>(trace);
        auto frame = get(trace, U"tb_frame");
        const auto &data = context.payload<FrameObject>(frame);
        EXPECT_EQ(data.function, expected[i]);
        EXPECT_EQ(node.frame, frame.get(context));
        EXPECT_EQ(node.lasti, 1);
        EXPECT_EQ(node.lineno, lines[i]);
        EXPECT_EQ(int_decimal(context, get(trace, U"tb_lasti").get(context)), "1");
        EXPECT_EQ(int_decimal(context, get(trace, U"tb_lineno").get(context)),
                  std::to_string(lines[i]));
        EXPECT_EQ(data.state, FrameState::Completed);
        EXPECT_EQ(data.sp, 0U);
        EXPECT_FALSE(data.pending_call);
        EXPECT_EQ(data.back, previous);
        previous = frame.get(context);
        trace = get(trace, U"tb_next");
        context.collect();
    }
    EXPECT_EQ(trace.get(context), context.builtin(CoreObject::None));
    EXPECT_FALSE(context.current_frame());
}

TEST_F(CoreTraceback, NativeReentryAddsEachPythonFrameExactlyOnce)
{
    auto leaf = raiser();
    ReturnSlot out(context);
    std::array captures{leaf.get(context)};
    make_native_function(
        context, "reenter", {},
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
            RootScope roots(c);
            auto target = roots.root(c.payload<NativeFunctionObject>(fn).captures[0]);
            call(c, target, {}, out);
            c.collect();
        },
        captures, out);
    auto outer = caller(roots.root(out.result().value()), 60);
    auto exception = failure(outer);
    auto trace = get(exception, U"__traceback__");
    EXPECT_EQ(context.payload<FrameObject>(get(trace, U"tb_frame")).function, outer.get(context));
    trace = get(trace, U"tb_next");
    EXPECT_EQ(context.payload<FrameObject>(get(trace, U"tb_frame")).function, leaf.get(context));
    EXPECT_EQ(get(trace, U"tb_next").get(context), context.builtin(CoreObject::None));
}

TEST_F(CoreTraceback, RuntimeErrorsAndBindingErrorsUseTheFailingInstruction)
{
    CodeSpec spec;
    spec.first_line = 77;
    spec.locals = {U"missing"};
    CodeBuilder unbound(context, spec);
    unbound.emit(Opcode::LoadFast);
    unbound.emit(Opcode::Return);
    auto exception = failure(function(unbound));
    auto trace = get(exception, U"__traceback__");
    EXPECT_EQ(context.payload<TracebackObject>(trace).lasti, 0);
    EXPECT_EQ(context.payload<TracebackObject>(trace).lineno, 77);
    auto no_arguments = raiser();
    CodeBuilder bad(context);
    bad.emit(Opcode::LoadConst, bad.constant(no_arguments));
    bad.emit(Opcode::LoadConst, bad.constant(integer(5)));
    bad.call(1, {}, {90, 0, 90, 8});
    bad.emit(Opcode::Return);
    exception = failure(function(bad));
    trace = get(exception, U"__traceback__");
    EXPECT_EQ(context.type_of(exception), context.builtin(CoreObject::TypeErrorType));
    EXPECT_EQ(context.payload<TracebackObject>(trace).lasti, 2);
    EXPECT_EQ(context.payload<TracebackObject>(trace).lineno, 90);
    EXPECT_FALSE(read_edge(context, trace, &TracebackObject::next));
}

TEST_F(CoreTraceback, ExplicitRepeatedRaisePrependsWithoutCloningCodeOrReplacingOldNodes)
{
    auto fn = raiser();
    auto exception = failure(fn);
    auto first = get(exception, U"__traceback__");
    auto same = failure(fn);
    auto second = get(same, U"__traceback__");
    EXPECT_EQ(exception.get(context), same.get(context));
    EXPECT_NE(first.get(context), second.get(context));
    EXPECT_EQ(get(second, U"tb_next").get(context), first.get(context));
    auto first_frame = get(first, U"tb_frame");
    auto second_frame = get(second, U"tb_frame");
    EXPECT_NE(first_frame.get(context), second_frame.get(context));
    EXPECT_EQ(get(first_frame, U"f_code").get(context), get(second_frame, U"f_code").get(context));
}

TEST_F(CoreTraceback, TracebackMutationRejectsTypesDeletionAndLoopsTransactionally)
{
    auto exception = failure(caller(raiser()));
    auto first = get(exception, U"__traceback__");
    auto last = get(first, U"tb_next");
    ReturnSlot out(context);
    auto name = text(U"tb_next");
    for (auto invalid : {first, last}) {
        set_attribute(context, last, name, invalid, out);
        error(out, CoreObject::ValueErrorType);
    }
    set_attribute(context, first, name, integer(5), out);
    error(out, CoreObject::TypeErrorType);
    del_attribute(context, first, name, out);
    error(out, CoreObject::TypeErrorType);
    EXPECT_EQ(get(first, U"tb_next").get(context), last.get(context));
    for (const auto &field : {U"tb_frame", U"tb_lasti", U"tb_lineno"}) {
        set_attribute(context, first, text(field), builtin(CoreObject::None), out);
        error(out, CoreObject::AttributeErrorType);
    }
    set_attribute(context, first, name, builtin(CoreObject::None), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_FALSE(read_edge(context, first, &TracebackObject::next));
    name = text(U"__traceback__");
    set_attribute(context, exception, name, integer(1), out);
    error(out, CoreObject::TypeErrorType);
    del_attribute(context, exception, name, out);
    error(out, CoreObject::TypeErrorType);
    EXPECT_EQ(get(exception, U"__traceback__").get(context), first.get(context));
    set_attribute(context, exception, name, builtin(CoreObject::None), out);
    EXPECT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_FALSE(read_edge(context, exception, &ExceptionObject::traceback));
    auto method = get(exception, U"with_traceback");
    std::array args{last.get(context)};
    call(context, method, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), exception.get(context));
    EXPECT_EQ(get(exception, U"__traceback__").get(context), last.get(context));
}

TEST_F(CoreTraceback, ConstructorUsesTheSharedBinderAndIndexConversion)
{
    auto existing = get(failure(raiser()), U"__traceback__");
    auto frame = get(existing, U"tb_frame");
    auto created = construct(existing, frame, integer(1), integer(-1));
    EXPECT_EQ(get(created, U"tb_next").get(context), existing.get(context));
    EXPECT_EQ(get(created, U"tb_frame").get(context), frame.get(context));
    EXPECT_EQ(int_decimal(context, get(created, U"tb_lineno").get(context)), "12");
    auto unknown = construct(builtin(CoreObject::None), frame, integer(1000), integer(-1));
    EXPECT_EQ(get(unknown, U"tb_lineno").get(context), context.builtin(CoreObject::None));
    auto fixed = construct(builtin(CoreObject::None), frame, integer(-1), integer(99));
    EXPECT_EQ(int_decimal(context, get(fixed, U"tb_lineno").get(context)), "99");
    ReturnSlot out(context);
    std::array args{existing.get(context), frame.get(context), integer(1).get(context)};
    std::array named{KeywordArg{text(U"tb_lineno").get(context), integer(101).get(context)}};
    call(context, builtin(CoreObject::TracebackType), {args, named}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<TracebackObject>(out.result().value()).lineno, 101);
    create_type(context, {"IndexPosition"}, out);
    auto cls = roots.root(out.result().value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    make_native_function(
        context, "position index", unary,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &out) {
            c.collect();
            out.ok(c.builtin(CoreObject::IntOne));
        },
        {}, out);
    set_attribute(context, cls, text(U"__index__"), roots.root(out.result().value()), out);
    context.allocate_instance<InstanceObject>(cls, out);
    auto index = roots.root(out.result().value());
    auto via_index = construct(existing, frame, index, integer(-1));
    EXPECT_EQ(context.payload<TracebackObject>(via_index).lasti, 1);
    std::array bad{existing.get(context), frame.get(context),
                   integer(std::int64_t{1} << 40).get(context), integer(0).get(context)};
    call(context, builtin(CoreObject::TracebackType), {bad, {}}, out);
    error(out, CoreObject::OverflowErrorType);
    bad[0] = context.builtin(CoreObject::None);
    bad[1] = context.builtin(CoreObject::None);
    bad[2] = context.builtin(CoreObject::IntZero);
    call(context, builtin(CoreObject::TracebackType), {bad, {}}, out);
    error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreTraceback, IdentityHashReprAndNonSubclassableLayoutUseObjectProtocols)
{
    auto first = get(failure(raiser()), U"__traceback__");
    auto frame = get(first, U"tb_frame");
    auto second = construct(builtin(CoreObject::None), frame, integer(1), integer(12));
    ReturnSlot out(context);
    rich_compare(context, first, second, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    rich_compare(context, first, first, CompareOp::Eq, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    hash(context, first, out);
    auto expected = int_decimal(context, out.result().value());
    context.collect();
    hash(context, first, out);
    EXPECT_EQ(int_decimal(context, out.result().value()), expected);
    repr(context, first, out);
    EXPECT_EQ(
        context.payload<StrObject>(out.result().value()).value.find(U"traceback object at 0x"), 1U);
    std::array bases{builtin(CoreObject::TracebackType)};
    EXPECT_THROW(create_type(context, {"NotSubclassable", bases}, out), TypeDefinitionError);
}

TEST_F(CoreTraceback, AllocationFailureDoesNotRetainFramesOnTheEmergencySingleton)
{
    auto leaf = raiser();
    auto parent = caller(leaf);
    ReturnSlot out(context);
    context.set_allocation_limit(2);
    call(context, parent, {}, out);
    error(out, CoreObject::MemoryErrorType);
    EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
    EXPECT_FALSE(context.current_frame());
    EXPECT_FALSE(
        read_edge(context, context.builtin(CoreObject::MemoryError), &ExceptionObject::traceback));
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    out.reset();
    context.collect();
    const auto before = context.stats().objects;
    auto exception = failure(parent);
    EXPECT_EQ(context.type_of(exception), context.builtin(CoreObject::ValueErrorType));
    EXPECT_TRUE(read_edge(context, exception, &ExceptionObject::traceback));
    auto trace = text(U"__traceback__");
    set_attribute(context, exception, trace, builtin(CoreObject::None), out);
    out.reset();
    context.collect();
    EXPECT_EQ(context.stats().objects, before + 1U);
}

TEST(CoreTracebackLifecycle, TracebackFrameLocalExceptionCyclesReturnToTheBaseline)
{
    Interpreter interpreter({.collect_every_allocation = true});
    Attachment context(interpreter);
    context.bootstrap_core();
    context.collect();
    const auto baseline = context.stats().objects;
    {
        RootScope roots(context);
        ReturnSlot out(context);
        raise_error(context, CoreObject::ValueErrorType, U"cycle", out);
        auto exception = roots.root(out.result().exception());
        CodeSpec spec;
        spec.signature.parameters = {{U"error"}};
        spec.locals = {U"error"};
        CodeBuilder builder(context, spec);
        builder.emit(Opcode::LoadFast);
        builder.emit(Opcode::RaiseInstance);
        builder.freeze(out);
        auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        auto globals = roots.root(out.result().value());
        make_function(context, {code, globals}, out);
        auto fn = roots.root(out.result().value());
        std::array args{exception.get(context)};
        call(context, fn, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        const auto trace = read_edge(context, exception, &ExceptionObject::traceback);
        ASSERT_TRUE(trace);
        const auto frame = context.payload<TracebackObject>(*trace).frame;
        EXPECT_EQ(context.payload<FrameObject>(frame).locals[0], exception.get(context));
        context.collect();
        EXPECT_EQ(context.payload<FrameObject>(frame).state, FrameState::Completed);
    }
    context.collect();
    EXPECT_EQ(context.stats().objects, baseline);
}

TEST(CoreTracebackLifecycle, ThousandsOfExceptionalFramesUnwindWithoutNativeRecursion)
{
    Interpreter interpreter(
        {.minimum_gc_budget = 1024 * 1024, .max_call_depth = 8192, .max_native_depth = 16});
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    raise_error(context, CoreObject::ValueErrorType, U"deep error", out);
    auto exception = roots.root(out.result().exception());
    CodeSpec spec;
    spec.signature.parameters = {{U"function"}, {U"n"}};
    spec.locals = {U"function", U"n"};
    CodeBuilder builder(context, spec);
    auto again = builder.label();
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::LoadConst,
                 builder.constant(roots.root(context.builtin(CoreObject::IntZero))));
    builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Eq));
    builder.jump(Opcode::JumpIfFalse, again);
    builder.emit(Opcode::LoadConst, builder.constant(exception));
    builder.emit(Opcode::RaiseInstance);
    builder.bind(again);
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::LoadConst,
                 builder.constant(roots.root(context.builtin(CoreObject::IntOne))));
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Subtract));
    builder.call(2);
    builder.emit(Opcode::Return);
    builder.freeze(out);
    auto code = roots.root(out.result().value());
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    auto globals = roots.root(out.result().value());
    make_function(context, {code, globals}, out);
    auto fn = roots.root(out.result().value());
    make_int(context, 6000, out);
    auto n = roots.root(out.result().value());
    context.collect();
    const auto baseline = context.stats().objects;
    std::array args{fn.get(context), n.get(context)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), exception.get(context));
    EXPECT_FALSE(context.current_frame());
    auto &data = context.payload<ExceptionObject>(exception);
    std::size_t count = 0;
    for (auto trace = read_edge(context, exception, &ExceptionObject::traceback); trace;
         trace = read_edge(context, *trace, &TracebackObject::next)) {
        ++count;
        EXPECT_EQ(
            context.payload<FrameObject>(context.payload<TracebackObject>(*trace).frame).state,
            FrameState::Completed);
    }
    EXPECT_EQ(count, 6001U);
    context.store(exception.get(context), data.traceback, {});
    out.reset();
    context.collect();
    EXPECT_EQ(context.stats().objects, baseline);
    args[1] = context.builtin(CoreObject::IntZero);
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    const auto trace = read_edge(context, exception, &ExceptionObject::traceback);
    ASSERT_TRUE(trace);
    EXPECT_FALSE(read_edge(context, *trace, &TracebackObject::next));
}
} // namespace
