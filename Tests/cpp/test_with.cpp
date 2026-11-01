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
void record(Attachment &c, Value cell, Value event)
{
    ReturnSlot previous(c);
    ASSERT_TRUE(read_cell(c, cell, previous));
    auto items = c.payload<TupleObject>(previous.result().value()).items;
    items.push_back(event);
    ReturnSlot out(c);
    cxxpython::core::make_tuple(c, items, out);
    write_cell(c, cell, out.result().value());
}
class CoreWith : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    struct Manager {
        Handle instance, type, arguments;
    };
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
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.result().value());
    }
    Handle cell(Value value)
    {
        ReturnSlot out(context);
        context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out, value);
        return roots.root(out.result().value());
    }
    Handle log()
    {
        return cell(context.builtin(CoreObject::EmptyTuple));
    }
    Handle exception()
    {
        ReturnSlot out(context);
        raise_error(context, CoreObject::ValueErrorType, U"body error", out);
        return roots.root(out.result().exception());
    }
    void set(Handle self, std::u32string name, Handle value)
    {
        ReturnSlot out(context);
        set_attribute(context, self, text(std::move(name)), value, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
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
    void load(CodeBuilder &builder, Handle value)
    {
        builder.emit(Opcode::LoadConst, builder.constant(value));
    }
    void returned_none(CodeBuilder &builder)
    {
        load(builder, builtin(CoreObject::None));
        builder.emit(Opcode::Return);
    }
    void throw_instance(CodeBuilder &builder, Handle error)
    {
        load(builder, error);
        builder.emit(Opcode::RaiseInstance);
    }
    void error(ReturnSlot &out, CoreObject kind)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(kind));
    }
    std::vector<std::string> events(Handle log)
    {
        std::vector<std::string> result;
        ReturnSlot snapshot(context);
        read_cell(context, log.get(context), snapshot);
        for (auto value : context.payload<TupleObject>(snapshot.result().value()).items)
            result.push_back(int_decimal(context, value));
        return result;
    }
    std::span<const Value> arguments(const Manager &manager, ReturnSlot &snapshot)
    {
        read_cell(context, manager.arguments.get(context), snapshot);
        return context.payload<TupleObject>(snapshot.result().value()).items;
    }
    Manager manager(Handle log, Handle value, Handle exit_result, bool enter_error = false,
                    bool exit_error = false, std::int64_t enter_event = 1,
                    std::int64_t exit_event = 2)
    {
        ReturnSlot out(context);
        create_type(context, {"Manager"}, out);
        auto cls = roots.root(out.result().value());
        auto saved = cell(context.builtin(CoreObject::None));
        SignatureLayout enter;
        enter.parameters = {{U"self"}};
        std::array enter_captures{
            log.get(context), integer(enter_event).get(context), value.get(context),
            context.builtin(enter_error ? CoreObject::True : CoreObject::False)};
        make_native_function(
            context, "enter", enter,
            [](Attachment &c, Value fn, std::span<const Value>, ReturnSlot &out) {
                const auto &v = c.payload<NativeFunctionObject>(fn).captures;
                record(c, v[0], v[1]);
                c.collect();
                if (v[3] == c.builtin(CoreObject::True))
                    raise_error(c, CoreObject::ValueErrorType, U"enter error", out);
                else
                    out.ok(v[2]);
            },
            enter_captures, out);
        set(cls, U"__enter__", roots.root(out.result().value()));
        SignatureLayout exit;
        exit.parameters = {{U"self"}, {U"kind"}, {U"error"}, {U"traceback"}};
        std::array exit_captures{
            log.get(context), integer(exit_event).get(context), exit_result.get(context),
            saved.get(context), context.builtin(exit_error ? CoreObject::True : CoreObject::False)};
        make_native_function(
            context, "exit", exit,
            [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
                const auto &v = c.payload<NativeFunctionObject>(fn).captures;
                cxxpython::core::make_tuple(c, args.subspan(1), out);
                write_cell(c, v[3], out.result().value());
                record(c, v[0], v[1]);
                c.collect();
                if (args[2] != c.builtin(CoreObject::None)) {
                    EXPECT_EQ(c.handled_exception(), args[2]);
                }
                if (v[4] == c.builtin(CoreObject::True))
                    raise_error(c, CoreObject::RuntimeErrorType, U"exit error", out);
                else
                    out.ok(v[2]);
            },
            exit_captures, out);
        set(cls, U"__exit__", roots.root(out.result().value()));
        context.allocate_instance<InstanceObject>(cls, out);
        return {roots.root(out.result().value()), cls, saved};
    }
    Handle descriptor(Handle callable, Handle log, int event,
                      std::optional<Handle> replacement_class = {})
    {
        ReturnSlot out(context);
        create_type(context, {"WithDescriptor"}, out);
        auto cls = roots.root(out.result().value());
        SignatureLayout get;
        get.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
        std::array captures{callable.get(context), log.get(context), integer(event).get(context),
                            replacement_class ? replacement_class->get(context)
                                              : context.builtin(CoreObject::None)};
        make_native_function(
            context, "with descriptor", get,
            [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
                const auto &v = c.payload<NativeFunctionObject>(fn).captures;
                record(c, v[1], v[2]);
                RootScope roots(c);
                if (v[3] != c.builtin(CoreObject::None)) {
                    make_str(c, U"__class__", out);
                    auto name = roots.root(out.result().value());
                    set_attribute(c, roots.root(args[1]), name, roots.root(v[3]), out);
                    if (out.result().tag() == ResultTag::Raised)
                        return;
                }
                c.collect();
                out.ok(v[0]);
            },
            captures, out);
        set(cls, U"__get__", roots.root(out.result().value()));
        context.allocate_instance<InstanceObject>(cls, out);
        return roots.root(out.result().value());
    }
};

TEST_F(CoreWith, NormalExitBindsTheEnterValueAndCallsExitWithThreeNoneArguments)
{
    auto recorded = log();
    auto expected = text(U"entered");
    auto owner = manager(recorded, expected, builtin(CoreObject::False));
    CodeSpec spec;
    spec.locals = {U"value"};
    CodeBuilder builder(context, spec);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region, {10, 0, 10, 8});
    builder.emit(Opcode::StoreFast);
    builder.leave(region, {10, 0, 10, 8});
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::Return);
    auto fn = function(builder);
    ReturnSlot out(context);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), expected.get(context));
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
    ASSERT_EQ(arguments(owner, out).size(), 3U);
    for (auto arg : arguments(owner, out))
        EXPECT_EQ(arg, context.builtin(CoreObject::None));
    const auto code = roots.root(context.payload<FunctionObject>(fn).code);
    const auto &program = context.payload<CodeObject>(code).program;
    EXPECT_EQ(program.max_stack(), 5U);
    EXPECT_NE(disassemble(context, code).find("WITH_EXIT"), std::string::npos);
    EXPECT_FALSE(context.current_frame());
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreWith, ExceptionExitGetsRealTypeValueAndTracebackAndCanSuppress)
{
    for (bool suppress : {false, true}) {
        auto recorded = log();
        auto raised = exception();
        auto owner = manager(recorded, builtin(CoreObject::None),
                             builtin(suppress ? CoreObject::True : CoreObject::False));
        CodeBuilder builder(context);
        auto region = builder.region(RegionKind::With);
        load(builder, owner.instance);
        builder.enter(region);
        builder.emit(Opcode::PopTop);
        throw_instance(builder, raised);
        builder.leave(region);
        returned_none(builder);
        ReturnSlot out(context);
        call(context, function(builder), {}, out);
        if (suppress) {
            ASSERT_EQ(out.result().tag(), ResultTag::Ok);
            EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
        } else {
            ASSERT_EQ(out.result().tag(), ResultTag::Raised);
            EXPECT_EQ(out.result().exception(), raised.get(context));
        }
        const auto args = arguments(owner, out);
        ASSERT_EQ(args.size(), 3U);
        EXPECT_EQ(args[0], context.builtin(CoreObject::ValueErrorType));
        EXPECT_EQ(args[1], raised.get(context));
        EXPECT_EQ(context.type_of(args[2]), context.builtin(CoreObject::TracebackType));
        EXPECT_EQ(read_edge(context, raised, &ExceptionObject::traceback), args[2]);
        EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
        EXPECT_FALSE(context.handled_exception());
    }
}

TEST_F(CoreWith, EnterFailureDoesNotRegisterOrCallExit)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::True), true);
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    error(out, CoreObject::ValueErrorType);
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1"}));
    ASSERT_TRUE(read_cell(context, owner.arguments.get(context), out));
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
}

TEST_F(CoreWith, MissingExitIsDiagnosedBeforeCallingEnter)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::True));
    ReturnSlot out(context);
    del_attribute(context, owner.type, text(U"__exit__"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    call(context, function(builder), {}, out);
    error(out, CoreObject::TypeErrorType);
    EXPECT_TRUE(events(recorded).empty());
}

TEST_F(CoreWith, ExitErrorReplacesTheBodyErrorAndLinksItsImplicitContext)
{
    auto recorded = log();
    auto raised = exception();
    auto owner =
        manager(recorded, builtin(CoreObject::None), builtin(CoreObject::True), false, true);
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    throw_instance(builder, raised);
    builder.leave(region);
    returned_none(builder);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    error(out, CoreObject::RuntimeErrorType);
    EXPECT_EQ(read_edge(context, out.result().exception(), &ExceptionObject::context),
              raised.get(context));
    EXPECT_FALSE(context.handled_exception());
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
}

TEST_F(CoreWith, ReturnIgnoresExitTruthConversionAndReleasesItsCachedOperand)
{
    ReturnSlot out(context);
    create_type(context, {"BadTruth"}, out);
    auto cls = roots.root(out.result().value());
    SignatureLayout unary;
    unary.parameters = {{U"self"}};
    make_native_function(
        context, "must not test truth", unary,
        [](Attachment &c, Value, std::span<const Value>, ReturnSlot &out) {
            raise_error(c, CoreObject::ValueErrorType, U"unexpected truth", out);
        },
        {}, out);
    set(cls, U"__bool__", roots.root(out.result().value()));
    context.allocate_instance<InstanceObject>(cls, out);
    auto bad_truth = roots.root(out.result().value());
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), bad_truth);
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, integer(42));
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    load(builder, integer(99));
    builder.emit(Opcode::Return);
    builder.leave(region);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "99");
    for (auto arg : arguments(owner, out)) {
        EXPECT_EQ(arg, context.builtin(CoreObject::None));
    }
    EXPECT_FALSE(context.current_frame());
    CodeBuilder normal(context);
    region = normal.region(RegionKind::With);
    load(normal, owner.instance);
    normal.enter(region);
    normal.emit(Opcode::PopTop);
    normal.leave(region);
    returned_none(normal);
    call(context, function(normal), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    auto original = exception();
    CodeBuilder exceptional(context);
    region = exceptional.region(RegionKind::With);
    load(exceptional, owner.instance);
    exceptional.enter(region);
    exceptional.emit(Opcode::PopTop);
    throw_instance(exceptional, original);
    exceptional.leave(region);
    returned_none(exceptional);
    call(context, function(exceptional), {}, out);
    error(out, CoreObject::ValueErrorType);
    EXPECT_EQ(read_edge(context, out.result().exception(), &ExceptionObject::context),
              original.get(context));
}

TEST_F(CoreWith, BindingFailureInsideTheRegisteredBodyStillCallsExit)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False));
    ReturnSlot out(context);
    SignatureLayout setter;
    setter.parameters = {{U"value"}};
    std::array captures{owner.instance.get(context)};
    make_native_function(
        context, "bind target", setter,
        [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
            RootScope roots(c);
            make_str(c, U"__class__", out);
            auto name = roots.root(out.result().value());
            set_attribute(c, roots.root(c.payload<NativeFunctionObject>(fn).captures[0]), name,
                          roots.root(args[0]), out);
        },
        captures, out);
    auto assign = roots.root(out.result().value());
    CodeSpec spec;
    spec.locals = {U"entered"};
    CodeBuilder builder(context, spec);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::StoreFast);
    load(builder, assign);
    builder.emit(Opcode::LoadFast);
    builder.call(1);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    call(context, function(builder), {}, out);
    error(out, CoreObject::TypeErrorType);
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2"}));
    ASSERT_EQ(arguments(owner, out).size(), 3U);
    EXPECT_EQ(arguments(owner, out)[0], context.builtin(CoreObject::TypeErrorType));
}

TEST_F(CoreWith, NestedManagersExitInReverseOrderAndOuterExitSeesSuppression)
{
    auto recorded = log();
    auto raised = exception();
    auto outer = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False), false,
                         false, 1, 4);
    auto inner =
        manager(recorded, builtin(CoreObject::None), builtin(CoreObject::True), false, false, 2, 3);
    CodeBuilder builder(context);
    auto a = builder.region(RegionKind::With);
    auto b = builder.region(RegionKind::With);
    load(builder, outer.instance);
    builder.enter(a);
    builder.emit(Opcode::PopTop);
    load(builder, inner.instance);
    builder.enter(b);
    builder.emit(Opcode::PopTop);
    throw_instance(builder, raised);
    builder.leave(b);
    builder.leave(a);
    returned_none(builder);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2", "3", "4"}));
    EXPECT_EQ(arguments(inner, out)[1], raised.get(context));
    for (auto arg : arguments(outer, out))
        EXPECT_EQ(arg, context.builtin(CoreObject::None));
}

TEST_F(CoreWith, ImplicitDescriptorLookupPrecedesEnterAndIgnoresInstanceAttributeHooks)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False));
    ReturnSlot out(context);
    get_attribute(context, owner.instance, text(U"__enter__"), out);
    auto enter = roots.root(out.result().value());
    get_attribute(context, owner.instance, text(U"__exit__"), out);
    auto exit = roots.root(out.result().value());
    set(owner.type, U"__enter__", descriptor(enter, recorded, 10));
    set(owner.type, U"__exit__", descriptor(exit, recorded, 20));
    set(owner.instance, U"__enter__", builtin(CoreObject::None));
    set(owner.instance, U"__exit__", builtin(CoreObject::None));
    SignatureLayout hook;
    hook.parameters = {{U"self"}, {U"name"}};
    make_native_function(
        context, "attribute hook", hook,
        [](Attachment &c, Value, std::span<const Value>, ReturnSlot &out) {
            raise_error(c, CoreObject::RuntimeErrorType, U"must not use instance hook", out);
        },
        {}, out);
    set(owner.type, U"__getattribute__", roots.root(out.result().value()));
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"10", "20", "1", "2"}));
}

TEST_F(CoreWith, ExitLookupUsesTheTypeAfterEnterDescriptorSideEffects)
{
    auto recorded = log();
    auto original = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False), false,
                            false, 1, 2);
    auto replacement = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False),
                               false, false, 3, 4);
    ReturnSlot out(context);
    get_attribute(context, original.instance, text(U"__enter__"), out);
    auto enter = roots.root(out.result().value());
    set(original.type, U"__enter__", descriptor(enter, recorded, 10, replacement.type));
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, original.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.type_of(original.instance), replacement.type.get(context));
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"10", "1", "4"}));
}

TEST_F(CoreWith, VerifierRejectsForgedWithOpcodesAndProtectsTheCachedExit)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False));
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    ReturnSlot out(context);
    try {
        builder.freeze(out);
        FAIL() << "cached exit consumed";
    } catch (const CodeValidationError &error) {
        EXPECT_EQ(error.kind, CodeErrorKind::StackBase);
    }
    CodeBuilder forged(context);
    region = forged.region(RegionKind::With);
    load(forged, owner.instance);
    forged.enter(region);
    forged.emit(Opcode::PopTop);
    forged.emit(Opcode::WithExit, region.index);
    forged.leave(region);
    returned_none(forged);
    try {
        forged.freeze(out);
        FAIL() << "forged cleanup accepted";
    } catch (const CodeValidationError &error) {
        EXPECT_EQ(error.kind, CodeErrorKind::RegionStructure);
    }
}

TEST_F(CoreWith, ExitBoundBeforeEnterRemainsRootedAfterEnterReplacesItsTypeAttribute)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False));
    auto replacement = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::False),
                               false, false, 8, 9);
    ReturnSlot out(context);
    get_attribute(context, replacement.instance, text(U"__exit__"), out);
    auto changed = roots.root(out.result().value());
    SignatureLayout enter;
    enter.parameters = {{U"self"}};
    std::array captures{owner.type.get(context), changed.get(context)};
    make_native_function(
        context, "replace exit during enter", enter,
        [](Attachment &c, Value fn, std::span<const Value>, ReturnSlot &out) {
            const auto &v = c.payload<NativeFunctionObject>(fn).captures;
            RootScope roots(c);
            make_str(c, U"__exit__", out);
            auto name = roots.root(out.result().value());
            set_attribute(c, roots.root(v[0]), name, roots.root(v[1]), out);
            if (out.result().tag() == ResultTag::Raised)
                return;
            c.collect();
            out.ok(c.builtin(CoreObject::None));
        },
        captures, out);
    set(owner.type, U"__enter__", roots.root(out.result().value()));
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"2"}));
}

TEST_F(CoreWith, LoopBreakAndContinueRunExitWithoutSuppressingControlTransfers)
{
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), builtin(CoreObject::True));
    CodeSpec spec;
    spec.locals = {U"i"};
    CodeBuilder builder(context, spec);
    load(builder, integer(0));
    builder.emit(Opcode::StoreFast);
    auto start = builder.label();
    auto done = builder.label();
    auto stop = builder.label();
    auto loop = builder.loop(start, done);
    auto region = builder.region(RegionKind::With);
    builder.enter(loop);
    builder.bind(start);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    builder.emit(Opcode::LoadFast);
    load(builder, integer(1));
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::StoreFast);
    builder.emit(Opcode::LoadFast);
    load(builder, integer(2));
    builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Lt));
    builder.jump(Opcode::JumpIfFalse, stop);
    builder.control(Opcode::ContinueLoop, loop);
    builder.bind(stop);
    builder.control(Opcode::BreakLoop, loop);
    builder.leave(region);
    builder.leave(loop);
    builder.bind(done);
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "2");
    EXPECT_EQ(events(recorded), (std::vector<std::string>{"1", "2", "1", "2"}));
    for (auto arg : arguments(owner, out)) {
        EXPECT_EQ(arg, context.builtin(CoreObject::None));
    }
}

TEST_F(CoreWith, SuppressionTruthCallbackSeesTheOriginalErrorAndRestoresOuterHandling)
{
    auto first = exception();
    auto second = exception();
    ReturnSlot out(context);
    create_type(context, {"Truthy"}, out);
    auto cls = roots.root(out.result().value());
    SignatureLayout unary;
    unary.parameters = {{U"self"}};
    std::array captures{second.get(context)};
    make_native_function(
        context, "truth during exit", unary,
        [](Attachment &c, Value fn, std::span<const Value>, ReturnSlot &out) {
            EXPECT_EQ(c.handled_exception(), c.payload<NativeFunctionObject>(fn).captures[0]);
            c.collect();
            out.ok(c.builtin(CoreObject::True));
        },
        captures, out);
    set(cls, U"__bool__", roots.root(out.result().value()));
    context.allocate_instance<InstanceObject>(cls, out);
    auto truthy = roots.root(out.result().value());
    auto recorded = log();
    auto owner = manager(recorded, builtin(CoreObject::None), truthy);
    CodeBuilder builder(context);
    auto caught = builder.region(RegionKind::Except);
    auto region = builder.region(RegionKind::With);
    builder.enter(caught);
    throw_instance(builder, first);
    builder.leave(caught);
    builder.handler(caught);
    builder.emit(Opcode::PopTop);
    load(builder, owner.instance);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    throw_instance(builder, second);
    builder.leave(region);
    builder.emit(Opcode::LoadException);
    builder.emit(Opcode::Return);
    builder.end_handler(caught);
    returned_none(builder);
    call(context, function(builder), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), first.get(context));
    EXPECT_EQ(read_edge(context, second, &ExceptionObject::context), first.get(context));
    EXPECT_FALSE(context.handled_exception());
}

TEST_F(CoreWith, AllocationFailureAfterEnterCanBeSuppressedByAnAllocationFreeExit)
{
    ReturnSlot out(context);
    create_type(context, {"LowMemoryManager"}, out);
    auto cls = roots.root(out.result().value());
    auto exited = cell(context.builtin(CoreObject::False));
    SignatureLayout enter;
    enter.parameters = {{U"self"}};
    make_native_function(
        context, "arm OOM", enter,
        [](Attachment &c, Value, std::span<const Value>, ReturnSlot &out) {
            c.set_allocation_limit(0);
            out.ok(c.builtin(CoreObject::None));
        },
        {}, out);
    set(cls, U"__enter__", roots.root(out.result().value()));
    SignatureLayout exit;
    exit.parameters = {{U"self"}, {U"kind"}, {U"error"}, {U"traceback"}};
    std::array captures{exited.get(context)};
    make_native_function(
        context, "exit without allocation", exit,
        [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
            EXPECT_EQ(args[2], c.builtin(CoreObject::MemoryError));
            EXPECT_EQ(c.handled_exception(), args[2]);
            write_cell(c, c.payload<NativeFunctionObject>(fn).captures[0],
                       c.builtin(CoreObject::True));
            c.collect();
            out.ok(c.builtin(CoreObject::True));
        },
        captures, out);
    set(cls, U"__exit__", roots.root(out.result().value()));
    context.allocate_instance<InstanceObject>(cls, out);
    auto self = roots.root(out.result().value());
    make_native_function(
        context, "allocate body value", {},
        [](Attachment &c, Value, std::span<const Value>, ReturnSlot &out) {
            make_str(c, U"allocation fails", out);
        },
        {}, out);
    auto body = roots.root(out.result().value());
    CodeBuilder builder(context);
    auto region = builder.region(RegionKind::With);
    load(builder, self);
    builder.enter(region);
    builder.emit(Opcode::PopTop);
    load(builder, body);
    builder.call(0);
    builder.emit(Opcode::PopTop);
    builder.leave(region);
    returned_none(builder);
    auto fn = function(builder);
    call(context, fn, {}, out);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
    ASSERT_TRUE(read_cell(context, exited.get(context), out));
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::True));
    EXPECT_FALSE(context.handled_exception());
    EXPECT_FALSE(
        read_edge(context, context.builtin(CoreObject::MemoryError), &ExceptionObject::traceback));
}

TEST(CoreWithLifecycle, PythonEnterAndExitUseTheFrameSchedulerAtDeepWithNesting)
{
    Interpreter interpreter(
        {.minimum_gc_budget = 64 * 1024, .max_call_depth = 2048, .max_native_depth = 16});
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    auto builtin = [&](CoreObject id) { return roots.root(context.builtin(id)); };
    auto function = [&](CodeBuilder &builder) {
        builder.freeze(out);
        auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out);
        auto globals = roots.root(out.result().value());
        make_function(context, {code, globals}, out);
        return roots.root(out.result().value());
    };
    CodeSpec enter;
    enter.signature.parameters = {{U"self"}};
    enter.locals = {U"self"};
    CodeBuilder entry(context, enter);
    entry.emit(Opcode::LoadFast);
    entry.emit(Opcode::Return);
    auto enter_function = function(entry);
    CodeSpec exit;
    exit.signature.parameters = {{U"self"}, {U"kind"}, {U"error"}, {U"traceback"}};
    exit.locals = {U"self", U"kind", U"error", U"traceback"};
    CodeBuilder leaving(context, exit);
    leaving.emit(Opcode::LoadConst, leaving.constant(builtin(CoreObject::None)));
    leaving.emit(Opcode::Return);
    auto exit_function = function(leaving);
    create_type(context, {"PythonManager"}, out);
    auto cls = roots.root(out.result().value());
    for (const auto &[name, value] : std::array{std::pair{U"__enter__", enter_function},
                                                std::pair{U"__exit__", exit_function}}) {
        make_str(context, name, out);
        auto key = roots.root(out.result().value());
        set_attribute(context, cls, key, value, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    }
    context.allocate_instance<InstanceObject>(cls, out);
    auto self = roots.root(out.result().value());
    CodeSpec spec;
    spec.signature.parameters = {{U"function"}, {U"manager"}, {U"n"}};
    spec.locals = {U"function", U"manager", U"n"};
    CodeBuilder recursive(context, spec);
    auto region = recursive.region(RegionKind::With);
    auto done = recursive.label();
    recursive.emit(Opcode::LoadFast, 1);
    recursive.enter(region);
    recursive.emit(Opcode::PopTop);
    recursive.emit(Opcode::LoadFast, 2);
    recursive.emit(Opcode::LoadConst, recursive.constant(builtin(CoreObject::IntZero)));
    recursive.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Gt));
    recursive.jump(Opcode::JumpIfFalse, done);
    recursive.emit(Opcode::LoadFast);
    recursive.emit(Opcode::LoadFast);
    recursive.emit(Opcode::LoadFast, 1);
    recursive.emit(Opcode::LoadFast, 2);
    recursive.emit(Opcode::LoadConst, recursive.constant(builtin(CoreObject::IntOne)));
    recursive.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Subtract));
    recursive.call(3);
    recursive.emit(Opcode::PopTop);
    recursive.bind(done);
    recursive.leave(region);
    recursive.emit(Opcode::LoadConst, recursive.constant(builtin(CoreObject::None)));
    recursive.emit(Opcode::Return);
    auto fn = function(recursive);
    make_int(context, 600, out);
    auto n = roots.root(out.result().value());
    out.reset();
    context.collect();
    const auto baseline = context.stats().objects;
    std::array args{fn.get(context), self.get(context), n.get(context)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
    EXPECT_FALSE(context.current_frame());
    EXPECT_FALSE(context.handled_exception());
    out.reset();
    context.collect();
    EXPECT_EQ(context.stats().objects, baseline);
}
} // namespace
