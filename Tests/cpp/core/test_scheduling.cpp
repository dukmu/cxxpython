#include "cxxpython/core/execution.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    class CoreScheduling : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle builtin(CoreObject id) { return roots.root(context.builtin(id)); }
        Handle integer(std::int64_t value)
        { ReturnSlot out(context); make_int(context, value, out); return roots.root(out.result().value()); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle function(CodeBuilder &builder)
        {
            ReturnSlot out(context); builder.freeze(out); auto code = roots.root(out.result().value());
            context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out); auto globals = roots.root(out.result().value());
            make_function(context, {code, globals}, out); return roots.root(out.result().value());
        }
        Handle identity()
        {
            CodeSpec spec; spec.signature.parameters = {{U"value"}}; spec.locals = {U"value"};
            CodeBuilder builder(context, spec); builder.emit(Opcode::LoadFast); builder.emit(Opcode::Return); return function(builder);
        }
        Handle caller(Handle target, std::span<const Handle> args, std::span<const std::u32string> keywords = {})
        {
            CodeBuilder builder(context); builder.emit(Opcode::LoadConst, builder.constant(target));
            for (auto value : args) builder.emit(Opcode::LoadConst, builder.constant(value));
            builder.call(static_cast<std::uint32_t>(args.size() - keywords.size()), keywords); builder.emit(Opcode::Return);
            return function(builder);
        }
        void error(ReturnSlot &out, CoreObject type)
        { ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type)); }
        void invalid(CodeBuilder &builder, CodeErrorKind expected)
        {
            ReturnSlot out(context);
            try { builder.freeze(out); FAIL() << "invalid CALL accepted"; }
            catch (const CodeValidationError &error) { EXPECT_EQ(error.kind, expected); }
            EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        }
    };

    TEST_F(CoreScheduling, CallMetadataValidatesSiteBoundsArityAndStackEffect)
    {
        CodeBuilder unknown(context); unknown.emit(Opcode::Call, 0); invalid(unknown, CodeErrorKind::CallBounds);
        EXPECT_THROW(instruction_effect({Opcode::Call, 0}), CodeValidationError);
        CodeBuilder inputs(context); auto fn = identity(); inputs.emit(Opcode::LoadConst, inputs.constant(fn));
        inputs.call(1); inputs.emit(Opcode::Return); invalid(inputs, CodeErrorKind::StackUnderflow);
        CodeBuilder oversized(context); oversized.call(std::numeric_limits<std::uint32_t>::max()); invalid(oversized, CodeErrorKind::SizeLimit);
        CodeBuilder valid(context); valid.emit(Opcode::LoadConst, valid.constant(fn));
        valid.emit(Opcode::LoadConst, valid.constant(integer(42))); valid.call(0, std::array<std::u32string, 1>{U"value"}); valid.emit(Opcode::Return);
        ReturnSlot out(context); valid.freeze(out); auto code = roots.root(out.result().value());
        const auto &program = context.payload<CodeObject>(code).program;
        EXPECT_EQ(program.max_stack(), 2U); EXPECT_EQ(instruction_effect(program.instructions()[2], program.calls()).pops, 2U);
        EXPECT_EQ(disassemble(context, code), "0 LOAD_CONST 0\n1 LOAD_CONST 1\n2 CALL 0\n3 RETURN\n");
    }

    TEST_F(CoreScheduling, PythonAndNativeCallsUseOneDispatchAndKeywordBinder)
    {
        auto expected = integer(42); auto fn = identity(); std::array args{expected}; ReturnSlot out(context);
        auto program = caller(fn, args); call(context, program, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), expected.get(context));
        program = caller(fn, args, std::array<std::u32string, 1>{U"value"}); call(context, program, {}, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
        SignatureLayout signature; signature.parameters = {{U"value"}};
        make_native_function(context, "native identity", signature,
            [](Attachment &c, Handle, std::span<const Value> values, ReturnSlot &out) { c.collect(); out.ok(values[0]); }, {}, out);
        program = caller(roots.root(out.result().value()), args, std::array<std::u32string, 1>{U"value"});
        call(context, program, {}, out); EXPECT_EQ(out.result().value(), expected.get(context));
    }

    TEST_F(CoreScheduling, BoundMethodsAndPythonCallMethodsAreDeferredThroughSharedSemantics)
    {
        CodeSpec spec; spec.signature.parameters = {{U"self"}, {U"value"}}; spec.locals = {U"self", U"value"};
        CodeBuilder body(context, spec); body.emit(Opcode::LoadFast, 1); body.emit(Opcode::Return); auto fn = function(body);
        ReturnSlot out(context); create_type(context, {"Callable"}, out); auto cls = roots.root(out.result().value());
        set_attribute(context, cls, text(U"method"), fn, out); set_attribute(context, cls, text(U"__call__"), fn, out);
        context.allocate_instance<InstanceObject>(cls, out); auto obj = roots.root(out.result().value());
        get_attribute(context, obj, text(U"method"), out); auto bound = roots.root(out.result().value());
        auto expected = integer(7); std::array args{expected};
        for (auto target : {bound, obj})
        {
            auto program = caller(target, args); call(context, program, {}, out);
            ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), expected.get(context));
        }
    }

    TEST_F(CoreScheduling, CallFailuresDoNotMasqueradeAsScheduledTargets)
    {
        ReturnSlot out(context); std::array args{integer(1), integer(2)};
        auto fn = identity(); auto wrong = caller(fn, args); call(context, wrong, {}, out); error(out, CoreObject::TypeErrorType);
        auto duplicate = caller(fn, args, std::array<std::u32string, 1>{U"value"}); call(context, duplicate, {}, out); error(out, CoreObject::TypeErrorType);
        auto noncallable = caller(builtin(CoreObject::None), {}); call(context, noncallable, {}, out); error(out, CoreObject::TypeErrorType);
        auto missing = caller(fn, {}); call(context, missing, {}, out); error(out, CoreObject::TypeErrorType);
        EXPECT_FALSE(context.current_frame());
        std::array one{args[0]}; call(context, caller(fn, one), {}, out); EXPECT_EQ(out.result().value(), args[0].get(context));
    }

    TEST_F(CoreScheduling, PreparedTargetOwnsItsSnapshotsInTheCallerRootScope)
    {
        auto fn = identity(); auto expected = text(U"rooted keyword"); ReturnSlot out(context); RootScope prepared_roots(context);
        std::array named{KeywordArg{text(U"value").get(context), expected.get(context)}};
        auto prepared = detail::prepare_call(context, fn, {{}, named}, prepared_roots, out);
        ASSERT_TRUE(prepared); EXPECT_EQ(out.result().tag(), ResultTag::Empty); context.collect();
        EXPECT_EQ(prepared->function.get(context), fn.get(context)); EXPECT_FALSE(prepared->receiver);
        ASSERT_EQ(prepared->keywords.size(), 1U); EXPECT_EQ(prepared->keywords[0].value, expected.get(context));
        {
            RootScope temporary(context); ReturnSlot local(context);
            make_str(context, U"only prepared receiver", local); auto receiver = temporary.root(local.result().value());
            context.allocate_instance<BoundMethodObject>(builtin(CoreObject::BoundMethodType), local, fn.get(context), receiver.get(context));
            prepared = detail::prepare_call(context, temporary.root(local.result().value()), {}, prepared_roots, out);
        }
        context.collect(); ASSERT_TRUE(prepared); ASSERT_TRUE(prepared->receiver);
        EXPECT_EQ(context.payload<StrObject>(prepared->receiver->get(context)).value, U"only prepared receiver");
        auto none = builtin(CoreObject::None);
        EXPECT_FALSE(detail::prepare_call(context, none, {}, prepared_roots, out)); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreScheduling, ChildFrameBackEdgesKeepParkedOperandsAndContinuationsRooted)
    {
        ReturnSlot out(context); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out); auto observed = roots.root(out.result().value());
        std::array captures{observed.get(context)};
        make_native_function(context, "observe child", {},
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
                const auto child = *c.current_frame(); const auto &data = c.payload<FrameObject>(child);
                ASSERT_TRUE(data.back); const auto &parent = c.payload<FrameObject>(*data.back);
                EXPECT_TRUE(parent.pending_call); EXPECT_EQ(parent.sp, 1U);
                EXPECT_EQ(c.type_of(parent.operands[0]), c.builtin(CoreObject::FunctionType));
                c.payload<CellObject>(c.payload<NativeFunctionObject>(fn).captures[0]).contents = child;
                c.collect(); out.ok(c.builtin(CoreObject::None));
            }, captures, out);
        auto probe = roots.root(out.result().value()); CodeBuilder child(context);
        child.emit(Opcode::LoadConst, child.constant(probe)); child.call(0); child.emit(Opcode::PopTop);
        child.emit(Opcode::LoadConst, child.constant(integer(42))); child.emit(Opcode::Return);
        auto parent = caller(function(child), {}); call(context, parent, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto saved = roots.root(*context.payload<CellObject>(observed).contents); const auto &inner = context.payload<FrameObject>(saved);
        const auto &outer = context.payload<FrameObject>(*inner.back);
        for (const auto *frame : {&inner, &outer})
        { EXPECT_EQ(frame->state, FrameState::Completed); EXPECT_FALSE(frame->pending_call); EXPECT_EQ(frame->sp, 0U); }
        EXPECT_FALSE(context.current_frame()); EXPECT_EQ(int_decimal(context, out.result().value()), "42");
    }

    TEST_F(CoreScheduling, ChildAllocationFailurePropagatesMemoryErrorAndRestoresDepth)
    {
        auto target = identity(); std::array args{integer(9)}; auto fn = caller(target, args); ReturnSlot out(context);
        context.set_allocation_limit(1); call(context, fn, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
        EXPECT_FALSE(context.current_frame()); context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), args[0].get(context));
    }

    TEST_F(CoreScheduling, RaisedAndHostFaultsCompleteEveryParkedFrame)
    {
        ReturnSlot out(context); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out); auto saved = roots.root(out.result().value());
        std::array captures{saved.get(context)};
        make_native_function(context, "host fault", {},
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &) {
                c.payload<CellObject>(c.payload<NativeFunctionObject>(fn).captures[0]).contents = *c.current_frame();
                throw std::runtime_error("scheduled host fault");
            }, captures, out);
        auto native = roots.root(out.result().value()); auto child = caller(native, {}); auto parent = caller(child, {});
        EXPECT_THROW(call(context, parent, {}, out), std::runtime_error); EXPECT_FALSE(context.current_frame());
        auto frame = *context.payload<CellObject>(saved).contents;
        for (;;)
        {
            const auto &data = context.payload<FrameObject>(frame); EXPECT_EQ(data.state, FrameState::Completed);
            EXPECT_EQ(data.sp, 0U); EXPECT_FALSE(data.pending_call);
            if (!data.back) break;
            frame = *data.back;
        }
        CodeBuilder raises(context); raise_error(context, CoreObject::ValueErrorType, U"child error", out);
        auto exception = roots.root(out.result().exception()); raises.emit(Opcode::LoadConst, raises.constant(exception)); raises.emit(Opcode::RaiseInstance);
        auto failed = caller(function(raises), {}); call(context, failed, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(out.result().exception(), exception.get(context));
        EXPECT_FALSE(context.current_frame());
    }

    TEST_F(CoreScheduling, ScheduledReturnCannotHideAnUnreadyValueBeforeTheOuterReturn)
    {
        ReturnSlot out(context);
        context.allocate_instance<TypeObject>(roots.root(context.type_type()), out, U"Unready", LayoutDescriptor::of<InstanceObject>());
        auto unready = roots.root(out.result().value()); CodeBuilder child(context);
        child.emit(Opcode::LoadConst, child.constant(unready)); child.emit(Opcode::Return); auto target = function(child);
        CodeBuilder parent(context); parent.emit(Opcode::LoadConst, parent.constant(target)); parent.call(0); parent.emit(Opcode::PopTop);
        parent.emit(Opcode::LoadConst, parent.constant(builtin(CoreObject::None))); parent.emit(Opcode::Return);
        auto fn = function(parent); EXPECT_THROW(call(context, fn, {}, out), std::logic_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty); EXPECT_FALSE(context.current_frame());
    }

    Handle recursive_function(Attachment &context, RootScope &roots)
    {
        CodeSpec spec; spec.signature.parameters = {{U"function"}, {U"n"}}; spec.locals = {U"function", U"n"};
        CodeBuilder builder(context, spec); ReturnSlot out(context); make_int(context, 0, out); auto zero = builder.constant(roots.root(out.result().value()));
        make_int(context, 1, out); auto one = builder.constant(roots.root(out.result().value())); auto recursive = builder.label();
        builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::LoadConst, zero); builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Eq));
        builder.jump(Opcode::JumpIfFalse, recursive); builder.emit(Opcode::LoadConst, zero); builder.emit(Opcode::Return);
        builder.bind(recursive); builder.emit(Opcode::LoadFast, 0); builder.emit(Opcode::LoadFast, 0); builder.emit(Opcode::LoadFast, 1);
        builder.emit(Opcode::LoadConst, one); builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Subtract)); builder.call(2); builder.emit(Opcode::Return);
        builder.freeze(out); auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out); auto globals = roots.root(out.result().value());
        make_function(context, {code, globals}, out); return roots.root(out.result().value());
    }

    TEST(CoreSchedulingDepth, ThousandsOfPythonCallsDoNotConsumeNativeRecursionDepth)
    {
        Interpreter interpreter({.minimum_gc_budget = 1024 * 1024, .max_call_depth = 8192, .max_native_depth = 16});
        Attachment context(interpreter); context.bootstrap_core(); RootScope roots(context); auto fn = recursive_function(context, roots);
        ReturnSlot out(context); make_int(context, 6000, out); auto n = roots.root(out.result().value());
        context.collect(); const auto before = context.stats().objects; std::array args{fn.get(context), n.get(context)};
        call(context, fn, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.result().value()), "0"); EXPECT_FALSE(context.current_frame());
        out.reset(); context.collect(); EXPECT_EQ(context.stats().objects, before);
    }

    TEST(CoreSchedulingDepth, LogicalRecursionLimitUnwindsAndNativeReentryHasItsOwnLimit)
    {
        Interpreter interpreter({.collect_every_allocation = true, .max_call_depth = 24, .max_native_depth = 8});
        Attachment context(interpreter); context.bootstrap_core(); RootScope roots(context); auto fn = recursive_function(context, roots);
        ReturnSlot out(context); make_int(context, 100, out); auto n = roots.root(out.result().value()); std::array args{fn.get(context), n.get(context)};
        call(context, fn, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::RecursionErrorType)); EXPECT_FALSE(context.current_frame());
        std::array base{fn.get(context), context.builtin(CoreObject::IntZero)};
        call(context, fn, {base, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        context.allocate_instance<CellObject>(roots.root(context.builtin(CoreObject::CellType)), out); auto shared = roots.root(out.result().value());
        std::array captures{shared.get(context)};
        make_native_function(context, "native recursion", {},
            [](Attachment &c, Handle function, std::span<const Value>, ReturnSlot &out) {
                RootScope roots(c); const auto cell = c.payload<NativeFunctionObject>(function).captures[0];
                call(c, roots.root(*c.payload<CellObject>(cell).contents), {}, out);
            }, captures, out);
        auto native = roots.root(out.result().value()); context.payload<CellObject>(shared).contents = native.get(context);
        call(context, native, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::RecursionErrorType));
        call(context, fn, {base, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_FALSE(context.current_frame());
    }
}
