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
class CoreUnwind : public testing::Test {
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
    Handle integer(std::int64_t value)
    {
        ReturnSlot out(context);
        make_int(context, value, out);
        return roots.root(out.result().value());
    }
    Handle exception(CoreObject kind = CoreObject::ValueErrorType)
    {
        ReturnSlot out(context);
        raise_error(context, kind, U"unwind", out);
        return roots.root(out.result().exception());
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
    Handle log()
    {
        ReturnSlot out(context);
        context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out,
                                              context.builtin(CoreObject::EmptyTuple));
        return roots.root(out.result().value());
    }
    Handle probe(Handle log, std::int64_t event, std::optional<Handle> handled = {})
    {
        ReturnSlot out(context);
        std::array captures{log.get(context), integer(event).get(context),
                            handled ? handled->get(context) : context.builtin(CoreObject::None)};
        make_native_function(
            context, "unwind probe", {},
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                const auto &captures = c.payload<NativeFunctionObject>(fn).captures;
                EXPECT_EQ(c.handled_exception().value_or(c.builtin(CoreObject::None)), captures[2]);
                const auto cell = captures[0];
                ReturnSlot previous(c);
                ASSERT_TRUE(read_cell(c, cell, previous));
                auto items = c.payload<TupleObject>(previous.result().value()).items;
                items.push_back(captures[1]);
                cxxpython::core::make_tuple(c, items, out);
                write_cell(c, cell, out.result().value());
                c.collect();
                out.ok(c.builtin(CoreObject::None));
            },
            captures, out);
        return roots.root(out.result().value());
    }
    std::vector<std::string> events(Handle log)
    {
        std::vector<std::string> result;
        ReturnSlot snapshot(context);
        read_cell(context, log.get(context), snapshot);
        for (auto event : context.payload<TupleObject>(snapshot.result().value()).items)
            result.push_back(int_decimal(context, event));
        return result;
    }
    void load(CodeBuilder &builder, Handle value)
    {
        builder.emit(Opcode::LoadConst, builder.constant(value));
    }
    void invoke(CodeBuilder &builder, Handle callable)
    {
        load(builder, callable);
        builder.call(0);
        builder.emit(Opcode::PopTop);
    }
    void throw_instance(CodeBuilder &builder, Handle error)
    {
        load(builder, error);
        builder.emit(Opcode::RaiseInstance);
    }
    void invalid(CodeBuilder &builder, CodeErrorKind expected)
    {
        ReturnSlot out(context);
        try {
            builder.freeze(out);
            FAIL() << "invalid regions accepted";
        } catch (const CodeValidationError &error) {
            EXPECT_EQ(error.kind, expected) << error.what();
        }
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    }
};

TEST_F(CoreUnwind, BuilderRejectsIncompleteForeignAndMisnestedRegions)
{
    CodeBuilder first(context), second(context);
    auto region = first.region(RegionKind::Finally);
    EXPECT_THROW(second.enter(region), CodeValidationError);
    first.enter(region);
    EXPECT_THROW(first.enter(region), CodeValidationError);
    invalid(first, CodeErrorKind::RegionStructure);
    first.leave(region);
    invalid(first, CodeErrorKind::RegionStructure);
    first.handler(region);
    EXPECT_THROW(first.leave(region), CodeValidationError);
    first.end_handler(region);
    load(first, builtin(CoreObject::None));
    first.emit(Opcode::Return);
    EXPECT_NO_THROW(function(first));
    CodeBuilder forged(context);
    forged.emit(Opcode::EnterRegion, 0);
    invalid(forged, CodeErrorKind::RegionBounds);
}

TEST_F(CoreUnwind, VerifierRejectsCrossRegionJumpsAndProtectedPrefixConsumption)
{
    CodeBuilder jump(context);
    auto outside = jump.label();
    auto region = jump.region(RegionKind::Finally);
    jump.enter(region);
    jump.jump(Opcode::Jump, outside);
    jump.leave(region);
    jump.handler(region);
    jump.end_handler(region);
    jump.bind(outside);
    load(jump, builtin(CoreObject::None));
    jump.emit(Opcode::Return);
    invalid(jump, CodeErrorKind::RegionJump);
    CodeBuilder prefix(context);
    load(prefix, integer(99));
    region = prefix.region(RegionKind::Finally);
    prefix.enter(region);
    prefix.emit(Opcode::PopTop);
    prefix.leave(region);
    prefix.handler(region);
    prefix.end_handler(region);
    prefix.emit(Opcode::Return);
    invalid(prefix, CodeErrorKind::StackBase);
    CodeBuilder extra(context);
    region = extra.region(RegionKind::Finally);
    extra.enter(region);
    load(extra, integer(1));
    extra.leave(region);
    extra.handler(region);
    extra.end_handler(region);
    extra.emit(Opcode::Return);
    invalid(extra, CodeErrorKind::StackBase);
    CodeBuilder control(context);
    auto continued = control.label();
    auto broken = control.label();
    auto loop = control.loop(continued, broken);
    control.enter(loop);
    control.bind(continued);
    control.leave(loop);
    control.bind(broken);
    control.control(Opcode::BreakLoop, loop);
    invalid(control, CodeErrorKind::LoopTarget);
}

TEST_F(CoreUnwind, NormalExitSkipsExceptButRunsFinallyAndPreservesFastLocals)
{
    auto recorded = log();
    CodeSpec spec;
    spec.locals = {U"value"};
    CodeBuilder builder(context, spec);
    auto cleanup = builder.region(RegionKind::Finally);
    auto caught = builder.region(RegionKind::Except);
    builder.enter(cleanup);
    builder.enter(caught);
    load(builder, integer(42));
    builder.emit(Opcode::StoreFast);
    builder.leave(caught);
    builder.handler(caught);
    builder.emit(Opcode::PopTop);
    invoke(builder, probe(recorded, 99));
    builder.end_handler(caught);
    builder.leave(cleanup);
    builder.handler(cleanup);
    invoke(builder, probe(recorded, 1));
    builder.end_handler(cleanup);
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::Return);
    auto fn = function(builder);
    ReturnSlot out(context);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "42");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1"}));
    EXPECT_FALSE(context.handled_exception());
    const auto &program =
        context.payload<CodeObject>(context.payload<FunctionObject>(fn).code).program;
    EXPECT_EQ(program.max_regions(), 2U);
    EXPECT_NE(disassemble(context, roots.root(context.payload<FunctionObject>(fn).code))
                  .find("END_HANDLER"),
              std::string::npos);
}

TEST_F(CoreUnwind, ExplicitNormalDestinationSeparatesTryElseFromHandledExit)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"fail"}};
    spec.locals = {U"fail"};
    CodeBuilder builder(context, spec), foreign(context);
    const auto normal = builder.label(), body = builder.label();
    const auto region = builder.region(RegionKind::Except);
    builder.enter(region);
    builder.emit(Opcode::LoadFast);
    builder.jump(Opcode::JumpIfFalse, body);
    throw_instance(builder, exception());
    builder.bind(body);
    EXPECT_THROW(builder.leave(region, foreign.label()), CodeValidationError);
    builder.leave(region, normal);
    builder.handler(region);
    builder.emit(Opcode::PopTop);
    builder.end_handler(region);
    load(builder, integer(1));
    builder.emit(Opcode::Return);
    builder.bind(normal);
    load(builder, integer(2));
    builder.emit(Opcode::Return);
    const auto fn = function(builder);
    ReturnSlot out(context);
    for (bool fail : {false, true}) {
        std::array args{context.builtin(fail ? CoreObject::True : CoreObject::False)};
        call(context, fn, {args, {}}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.value()), fail ? "1" : "2");
    }
    CodeBuilder invalid_target(context);
    const auto protected_label = invalid_target.label();
    const auto caught = invalid_target.region(RegionKind::Except);
    invalid_target.enter(caught);
    invalid_target.bind(protected_label);
    invalid_target.leave(caught, protected_label);
    invalid_target.handler(caught);
    invalid_target.emit(Opcode::PopTop);
    invalid_target.end_handler(caught);
    load(invalid_target, builtin(CoreObject::None));
    invalid_target.emit(Opcode::Return);
    invalid(invalid_target, CodeErrorKind::RegionJump);
}

TEST_F(CoreUnwind, VerifierRevisitsCleanupEndForALateNormalContinuation)
{
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::Finally);
    auto longer = builder.label();
    builder.enter(region);
    load(builder, builtin(CoreObject::False));
    builder.jump(Opcode::JumpIfFalse, longer);
    load(builder, integer(1));
    builder.emit(Opcode::Return);
    builder.bind(longer);
    for (int i = 0; i < 50; ++i) {
        load(builder, integer(1));
        builder.emit(Opcode::PopTop);
    }
    builder.leave(region);
    builder.handler(region);
    builder.end_handler(region);
    invalid(builder, CodeErrorKind::Fallthrough);
    load(builder, builtin(CoreObject::None));
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
}

TEST_F(CoreUnwind, AncestorHandlerCatchesScheduledErrorsAndKeepsBothActualFrames)
{
    auto error = exception();
    CodeBuilder child(context);
    throw_instance(child, error);
    auto target = function(child);
    CodeSpec spec;
    spec.locals = {U"error"};
    CodeBuilder parent(context, spec);
    auto region = parent.region(RegionKind::Except);
    auto recorded = log();
    parent.enter(region);
    invoke(parent, target);
    parent.leave(region);
    parent.handler(region);
    parent.emit(Opcode::StoreFast);
    invoke(parent, probe(recorded, 1, error));
    parent.emit(Opcode::LoadException);
    parent.emit(Opcode::Return);
    parent.end_handler(region);
    load(parent, builtin(CoreObject::None));
    parent.emit(Opcode::Return);
    auto fn = function(parent);
    ReturnSlot out(context);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), error.get(context));
    auto trace = read_edge(context, error, &ExceptionObject::traceback);
    ASSERT_TRUE(trace);
    EXPECT_EQ(context.payload<FrameObject>(context.payload<TracebackObject>(*trace).frame).function,
              fn.get(context));
    trace = read_edge(context, *trace, &TracebackObject::next);
    ASSERT_TRUE(trace);
    EXPECT_EQ(context.payload<FrameObject>(context.payload<TracebackObject>(*trace).frame).function,
              target.get(context));
    EXPECT_FALSE(read_edge(context, *trace, &TracebackObject::next));
    EXPECT_FALSE(context.current_frame());
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreUnwind, NestedCleanupKeepsPendingReturnAcrossGcAndInternallyCaughtErrors)
{
    ReturnSlot out(context);
    make_native_function(
        context, "fresh return", {},
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &out) {
            make_str(c, U"transient return", out);
        },
        {}, out);
    auto fresh = roots.root(out.result().value());
    auto recorded = log();
    auto error = exception();
    CodeBuilder builder(context);
    auto outer = builder.region(RegionKind::Finally);
    auto inner = builder.region(RegionKind::Finally);
    auto caught = builder.region(RegionKind::Except);
    builder.enter(outer);
    builder.enter(inner);
    load(builder, fresh);
    builder.call(0);
    builder.emit(Opcode::Return);
    builder.leave(inner);
    builder.handler(inner);
    invoke(builder, probe(recorded, 1));
    builder.enter(caught);
    throw_instance(builder, error);
    builder.leave(caught);
    builder.handler(caught);
    builder.emit(Opcode::PopTop);
    invoke(builder, probe(recorded, 2, error));
    builder.end_handler(caught);
    invoke(builder, probe(recorded, 3));
    builder.end_handler(inner);
    builder.leave(outer);
    builder.handler(outer);
    invoke(builder, probe(recorded, 4));
    builder.end_handler(outer);
    auto fn = function(builder);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"transient return");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2", "3", "4"}));
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreUnwind, ReturnInCleanupReplacesOnlyItsOwnContinuationAndRunsOuterCleanup)
{
    auto recorded = log();
    CodeBuilder builder(context);
    auto outer = builder.region(RegionKind::Finally);
    auto inner = builder.region(RegionKind::Finally);
    builder.enter(outer);
    builder.enter(inner);
    load(builder, integer(1));
    builder.emit(Opcode::Return);
    builder.leave(inner);
    builder.handler(inner);
    invoke(builder, probe(recorded, 1));
    load(builder, integer(2));
    builder.emit(Opcode::Return);
    builder.end_handler(inner);
    builder.leave(outer);
    builder.handler(outer);
    invoke(builder, probe(recorded, 2));
    builder.end_handler(outer);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "2");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
}

TEST_F(CoreUnwind, CleanupRestoresHandledExceptionAfterAnInnerHandlerAndBareReraise)
{
    auto first = exception();
    auto second = exception(CoreObject::TypeErrorType);
    auto recorded = log();
    CodeBuilder builder(context);
    auto caught = builder.region(RegionKind::Except);
    auto cleanup = builder.region(RegionKind::Finally);
    auto nested = builder.region(RegionKind::Except);
    builder.enter(caught);
    builder.enter(cleanup);
    throw_instance(builder, first);
    builder.leave(cleanup);
    builder.handler(cleanup);
    invoke(builder, probe(recorded, 1, first));
    builder.enter(nested);
    throw_instance(builder, second);
    builder.leave(nested);
    builder.handler(nested);
    builder.emit(Opcode::PopTop);
    invoke(builder, probe(recorded, 2, second));
    builder.end_handler(nested);
    invoke(builder, probe(recorded, 3, first));
    builder.end_handler(cleanup);
    builder.leave(caught);
    builder.handler(caught);
    builder.emit(Opcode::PopTop);
    invoke(builder, probe(recorded, 4, first));
    builder.emit(Opcode::Reraise);
    builder.end_handler(caught);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), first.get(context));
    EXPECT_EQ(read_edge(context, second, &ExceptionObject::context), first.get(context));
    const auto trace = read_edge(context, first, &ExceptionObject::traceback);
    ASSERT_TRUE(trace);
    EXPECT_FALSE(read_edge(context, *trace, &TracebackObject::next));
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2", "3", "4"}));
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreUnwind, NewRaiseInCleanupReplacesThePendingErrorAndChainsItsContext)
{
    auto first = exception();
    auto second = exception(CoreObject::RuntimeErrorType);
    CodeBuilder builder(context);
    auto cleanup = builder.region(RegionKind::Finally);
    builder.enter(cleanup);
    throw_instance(builder, first);
    builder.leave(cleanup);
    builder.handler(cleanup);
    throw_instance(builder, second);
    builder.end_handler(cleanup);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), second.get(context));
    EXPECT_EQ(read_edge(context, second, &ExceptionObject::context), first.get(context));
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreUnwind, BreakAndContinueRunOnlyCrossedCleanupsAndPreserveOuterOperands)
{
    auto recorded = log();
    CodeSpec spec;
    spec.locals = {U"i"};
    CodeBuilder builder(context, spec);
    auto outer = builder.region(RegionKind::Finally);
    auto inner = builder.region(RegionKind::Finally);
    auto start = builder.label();
    auto done = builder.label();
    auto stop = builder.label();
    auto loop = builder.loop(start, done);
    load(builder, integer(42));
    builder.enter(outer);
    load(builder, integer(0));
    builder.emit(Opcode::StoreFast);
    builder.enter(loop);
    builder.bind(start);
    builder.enter(inner);
    builder.emit(Opcode::LoadFast);
    load(builder, integer(1));
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::StoreFast);
    builder.emit(Opcode::LoadFast);
    load(builder, integer(3));
    builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Lt));
    builder.jump(Opcode::JumpIfFalse, stop);
    builder.control(Opcode::ContinueLoop, loop);
    builder.bind(stop);
    builder.control(Opcode::BreakLoop, loop);
    builder.leave(inner);
    builder.handler(inner);
    invoke(builder, probe(recorded, 1));
    builder.end_handler(inner);
    builder.leave(loop);
    builder.bind(done);
    builder.leave(outer);
    builder.handler(outer);
    invoke(builder, probe(recorded, 2));
    builder.end_handler(outer);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "42");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "1", "1", "2"}));
}

TEST_F(CoreUnwind, CalleesInheritHandledStateAndBareRaiseDoesNotAddTheirFrame)
{
    auto error = exception();
    CodeBuilder child(context);
    child.emit(Opcode::Reraise);
    auto target = function(child);
    for (bool native_bridge : {false, true}) {
        context.store(error.get(context), context.payload<ExceptionObject>(error).traceback, {});
        auto callable = target;
        ReturnSlot out(context);
        if (native_bridge) {
            std::array captures{target.get(context)};
            make_native_function(
                context, "bare raise bridge", {},
                [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                    RootScope roots(c);
                    call(c, roots.root(c.payload<NativeFunctionObject>(fn).captures[0]), {}, out);
                },
                captures, out);
            callable = roots.root(out.result().value());
        }
        CodeBuilder parent(context);
        auto outer = parent.region(RegionKind::Except);
        auto inner = parent.region(RegionKind::Except);
        parent.enter(outer);
        throw_instance(parent, error);
        parent.leave(outer);
        parent.handler(outer);
        parent.emit(Opcode::PopTop);
        parent.enter(inner);
        invoke(parent, callable);
        parent.leave(inner);
        parent.handler(inner);
        parent.emit(Opcode::PopTop);
        parent.end_handler(inner);
        parent.end_handler(outer);
        parent.emit(Opcode::LoadException);
        parent.emit(Opcode::Return);
        call(context, function(parent), {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
        std::size_t count = 0;
        for (auto node = read_edge(context, error, &ExceptionObject::traceback); node;
             node = read_edge(context, *node, &TracebackObject::next))
            ++count;
        EXPECT_EQ(count, 2U);
        EXPECT_FALSE(context.handled_exception());
    }
    ReturnSlot out(context);
    call(context, target, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::RuntimeErrorType));
}

TEST_F(CoreUnwind, ChildAllocationFailureCanBeCaughtWithoutAllocatingDuringUnwind)
{
    CodeBuilder child(context);
    load(child, builtin(CoreObject::None));
    child.emit(Opcode::Return);
    auto target = function(child);
    CodeBuilder parent(context);
    auto region = parent.region(RegionKind::Except);
    parent.enter(region);
    invoke(parent, target);
    parent.leave(region);
    parent.handler(region);
    parent.emit(Opcode::Return);
    parent.end_handler(region);
    load(parent, builtin(CoreObject::None));
    parent.emit(Opcode::Return);
    auto fn = function(parent);
    ReturnSlot out(context);
    context.set_allocation_limit(1);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::MemoryError));
    EXPECT_FALSE(context.current_frame());
    EXPECT_FALSE(context.handled_exception());
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    call(context, fn, {}, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
}

TEST_F(CoreUnwind, ImplicitContextAvoidsNewCyclesAndTerminatesOnExistingCycles)
{
    auto first = exception();
    auto second = exception();
    auto third = exception();
    context.store(first.get(context), context.payload<ExceptionObject>(first).context,
                  second.get(context));
    context.store(second.get(context), context.payload<ExceptionObject>(second).context,
                  first.get(context));
    CodeBuilder builder(context);
    auto outer = builder.region(RegionKind::Except);
    auto inner = builder.region(RegionKind::Except);
    builder.enter(outer);
    throw_instance(builder, first);
    builder.leave(outer);
    builder.handler(outer);
    builder.emit(Opcode::PopTop);
    builder.enter(inner);
    throw_instance(builder, third);
    builder.leave(inner);
    builder.handler(inner);
    builder.emit(Opcode::PopTop);
    throw_instance(builder, first);
    builder.end_handler(inner);
    builder.end_handler(outer);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), first.get(context));
    EXPECT_EQ(read_edge(context, first, &ExceptionObject::context), third.get(context));
    EXPECT_FALSE(read_edge(context, third, &ExceptionObject::context));
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreUnwind, ExceptionMatchChecksEveryClassAndRequiresAnExceptionInstance)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"error"}, {U"matcher"}};
    spec.locals = {U"error", U"matcher"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::ExceptionMatch);
    builder.emit(Opcode::Return);
    auto fn = function(builder);
    auto error = exception();
    ReturnSlot out(context);
    std::array args{error.get(context), context.builtin(CoreObject::ExceptionType)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    args[1] = context.builtin(CoreObject::TypeErrorType);
    call(context, fn, {args, {}}, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::False));
    std::array invalid{context.builtin(CoreObject::ValueErrorType),
                       context.builtin(CoreObject::IntOne)};
    cxxpython::core::make_tuple(context, invalid, out);
    auto tuple = roots.root(out.result().value());
    args[1] = tuple.get(context);
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::TypeErrorType));
    args[0] = context.builtin(CoreObject::None);
    args[1] = context.builtin(CoreObject::ExceptionType);
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.result().exception()),
              context.builtin(CoreObject::TypeErrorType));
}

TEST_F(CoreUnwind, BreakFromCleanupOverridesReturnWithoutSkippingTheOuterFinally)
{
    auto recorded = log();
    CodeBuilder builder(context);
    auto outer = builder.region(RegionKind::Finally);
    auto inner = builder.region(RegionKind::Finally);
    auto start = builder.label();
    auto done = builder.label();
    auto loop = builder.loop(start, done);
    load(builder, integer(42));
    builder.enter(outer);
    builder.enter(loop);
    builder.bind(start);
    builder.enter(inner);
    load(builder, integer(99));
    builder.emit(Opcode::Return);
    builder.leave(inner);
    builder.handler(inner);
    invoke(builder, probe(recorded, 1));
    builder.control(Opcode::BreakLoop, loop);
    builder.end_handler(inner);
    builder.leave(loop);
    builder.bind(done);
    builder.leave(outer);
    builder.handler(outer);
    invoke(builder, probe(recorded, 2));
    builder.end_handler(outer);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "42");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
}
} // namespace
