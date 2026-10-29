#include "cxxpython/core/execution.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    class CoreExecution : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle builtin(CoreObject id) { return roots.root(context.builtin(id)); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle integer(std::int64_t value)
        { ReturnSlot out(context); make_int(context, value, out); return roots.root(out.result().value()); }
        Handle dictionary()
        { ReturnSlot out(context); context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out); return roots.root(out.result().value()); }
        Handle code(CodeBuilder &builder)
        { ReturnSlot out(context); builder.freeze(out); return roots.root(out.result().value()); }
        Handle function(Handle code, std::optional<Handle> defaults = {}, std::optional<Handle> keywords = {})
        {
            ReturnSlot out(context); make_function(context, {code, dictionary(), defaults, keywords}, out);
            return roots.root(out.result().value());
        }
        void error(ReturnSlot &out, CoreObject type)
        { ASSERT_EQ(out.result().tag(), ResultTag::Raised); EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type)); }
        void invalid(CodeBuilder &builder, CodeErrorKind kind)
        {
            ReturnSlot out(context);
            try { builder.freeze(out); FAIL() << "invalid code accepted"; }
            catch (const CodeValidationError &error) { EXPECT_EQ(error.kind, kind) << error.what(); }
            EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        }
    };

    TEST_F(CoreExecution, VerificationRejectsEmptyUnknownAndUnexpectedOperands)
    {
        CodeBuilder empty(context); invalid(empty, CodeErrorKind::EmptyCode);
        CodeBuilder unknown(context); unknown.emit(static_cast<Opcode>(255)); invalid(unknown, CodeErrorKind::UnknownOpcode);
        CodeBuilder operand(context); operand.emit(Opcode::Return, 1); invalid(operand, CodeErrorKind::Operand);
        CodeBuilder domain(context); domain.emit(Opcode::Binary, 255); invalid(domain, CodeErrorKind::Operand);
        CodeBuilder flags(context); flags.emit(Opcode::MakeFunction, 8); invalid(flags, CodeErrorKind::Operand);
        CodeBuilder function(context); function.emit(Opcode::MakeFunction, 3); invalid(function, CodeErrorKind::StackUnderflow);
    }

    TEST_F(CoreExecution, FunctionCreationRetainsCodeIdentityAndInheritsTheExecutionEnvironment)
    {
        CodeBuilder child(context); child.emit(Opcode::LoadGlobal, child.name(U"answer")); child.emit(Opcode::Return);
        const auto child_code = code(child); CodeBuilder parent(context);
        parent.emit(Opcode::LoadConst, parent.constant(child_code)); parent.emit(Opcode::MakeFunction); parent.emit(Opcode::Return);
        const auto globals = dictionary(), builtins = dictionary(); const auto answer = integer(42);
        detail::name_store(context, builtins.get(context), text(U"answer").get(context), answer.get(context));
        FunctionSpec spec{code(parent), globals}; spec.builtins = builtins; ReturnSlot out(context); make_function(context, spec, out);
        const auto factory = roots.root(out.result().value()); call(context, factory, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        const auto first = roots.root(out.result().value()); call(context, factory, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        const auto second = roots.root(out.result().value()); EXPECT_NE(first.get(context), second.get(context));
        for (auto fn : {first, second})
        {
            const auto &function = context.payload<FunctionObject>(fn);
            EXPECT_EQ(function.code, child_code.get(context)); EXPECT_EQ(function.globals, globals.get(context)); EXPECT_EQ(function.builtins, builtins.get(context));
            call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), answer.get(context));
        }
    }

    TEST_F(CoreExecution, FunctionCreationConsumesDefaultsInDeclaredStackOrder)
    {
        CodeSpec spec; spec.signature.parameters = {{U"a"}, {U"b", ParameterKind::KeywordOnly}}; spec.locals = {U"a", U"b"};
        CodeBuilder child(context, spec); child.emit(Opcode::LoadFast); child.emit(Opcode::LoadFast, 1);
        child.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); child.emit(Opcode::Return); const auto child_code = code(child);
        ReturnSlot out(context); std::array values{integer(40).get(context)}; cxxpython::core::make_tuple(context, values, out); const auto defaults = roots.root(out.result().value());
        const auto keywords = dictionary(); detail::name_store(context, keywords.get(context), text(U"b").get(context), integer(2).get(context));
        CodeBuilder parent(context); parent.emit(Opcode::LoadConst, parent.constant(defaults)); parent.emit(Opcode::LoadConst, parent.constant(keywords));
        parent.emit(Opcode::LoadConst, parent.constant(child_code)); parent.emit(Opcode::MakeFunction, 3); parent.emit(Opcode::Return);
        call(context, function(code(parent)), {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); const auto fn = roots.root(out.result().value());
        EXPECT_EQ(context.payload<FunctionObject>(fn).defaults, defaults.get(context)); EXPECT_EQ(context.payload<FunctionObject>(fn).kwdefaults, keywords.get(context));
        call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(context, out.result().value()), "42");
        CodeBuilder invalid_function(context); invalid_function.emit(Opcode::LoadConst, invalid_function.constant(integer(1)));
        invalid_function.emit(Opcode::MakeFunction); invalid_function.emit(Opcode::Return);
        call(context, function(code(invalid_function)), {}, out); error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreExecution, VerificationChecksOperandBoundsEvenInUnreachableInstructions)
    {
        CodeBuilder constants(context); constants.emit(Opcode::LoadConst, 0); invalid(constants, CodeErrorKind::ConstantBounds);
        CodeBuilder local(context); local.emit(Opcode::LoadFast, 0); invalid(local, CodeErrorKind::LocalBounds);
        CodeBuilder jump(context); jump.emit(Opcode::Jump, 1); invalid(jump, CodeErrorKind::JumpBounds);
        CodeBuilder dead(context); dead.emit(Opcode::Jump, 0); dead.emit(Opcode::LoadConst, 0); invalid(dead, CodeErrorKind::ConstantBounds);
        for (auto opcode : {Opcode::LoadGlobal, Opcode::StoreGlobal, Opcode::DeleteGlobal})
        { CodeBuilder global(context); global.emit(opcode); invalid(global, CodeErrorKind::NameBounds); }
    }

    TEST_F(CoreExecution, ClosureOperandsAndLayoutsAreVerifiedBeforePublication)
    {
        for (auto opcode : {Opcode::LoadDeref, Opcode::StoreDeref, Opcode::DeleteDeref, Opcode::LoadClosure})
        {
            CodeBuilder builder(context); builder.emit(Opcode::Jump); builder.emit(opcode);
            invalid(builder, CodeErrorKind::CellBounds);
        }
        CodeSpec spec; spec.locals = {U"x"}; spec.cellvars = {U"missing"};
        CodeBuilder missing(context, spec); missing.emit(Opcode::Jump); invalid(missing, CodeErrorKind::CellLayout);
        spec.cellvars = {U"x", U"x"}; CodeBuilder duplicate(context, spec); duplicate.emit(Opcode::Jump); invalid(duplicate, CodeErrorKind::CellLayout);
        spec.cellvars = {U"x"}; spec.freevars = {U"x"}; CodeBuilder overlap(context, spec); overlap.emit(Opcode::Jump); invalid(overlap, CodeErrorKind::CellLayout);
        spec.freevars.clear();
        for (auto opcode : {Opcode::LoadFast, Opcode::StoreFast, Opcode::DeleteFast})
        {
            CodeBuilder fast(context, spec); fast.emit(opcode); invalid(fast, CodeErrorKind::CellLayout);
        }
        CodeBuilder flags(context); flags.emit(Opcode::MakeFunction, 7); invalid(flags, CodeErrorKind::StackUnderflow);
        const auto effect = instruction_effect({Opcode::MakeFunction, 7});
        EXPECT_EQ(effect.inputs, 4U); EXPECT_EQ(effect.pops, 4U); EXPECT_EQ(effect.pushes, 1U);
    }

    TEST_F(CoreExecution, FunctionClosureShapeAndCellAllocationFailuresUseNormalResultPaths)
    {
        CodeSpec spec; spec.freevars = {U"value"}; CodeBuilder builder(context, spec);
        builder.emit(Opcode::LoadDeref); builder.emit(Opcode::Return); const auto program = code(builder);
        FunctionSpec function_spec{program, dictionary()}; ReturnSlot out(context);
        make_function(context, function_spec, out); error(out, CoreObject::TypeErrorType);
        function_spec.closure = integer(1); make_function(context, function_spec, out); error(out, CoreObject::TypeErrorType);
        function_spec.closure = builtin(CoreObject::EmptyTuple); make_function(context, function_spec, out); error(out, CoreObject::ValueErrorType);
        std::array wrong{context.builtin(CoreObject::IntOne)}; cxxpython::core::make_tuple(context, wrong, out);
        function_spec.closure = roots.root(out.result().value()); make_function(context, function_spec, out); error(out, CoreObject::TypeErrorType);
        context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out, context.builtin(CoreObject::IntOne));
        const auto cell = roots.root(out.result().value()); std::array values{cell.get(context)};
        cxxpython::core::make_tuple(context, values, out); const auto closure = roots.root(out.result().value());
        function_spec.closure = closure; make_function(context, function_spec, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        const auto fn = roots.root(out.result().value()); call(context, fn, {}, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::IntOne));
        EXPECT_EQ(disassemble(context, program), "0 LOAD_DEREF 0\n1 RETURN\n");
        spec.freevars.clear(); spec.signature.parameters = {{U"value"}}; spec.locals = {U"value"}; spec.cellvars = {U"value"};
        CodeBuilder captured(context, spec); captured.emit(Opcode::LoadDeref); captured.emit(Opcode::Return);
        const auto target = function(code(captured)); std::array args{context.builtin(CoreObject::IntOne)};
        for (auto limit : {0U, 1U})
        {
            context.set_allocation_limit(limit); call(context, target, {args, {}}, out); error(out, CoreObject::MemoryErrorType);
            EXPECT_FALSE(context.current_frame());
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        call(context, target, {args, {}}, out); EXPECT_EQ(out.result().value(), args[0]);
    }

    TEST_F(CoreExecution, SourceLoweringOpcodesHaveVerifiedOperandsAndStackInputs)
    {
        CodeBuilder unary(context); unary.emit(Opcode::Unary, 255); invalid(unary, CodeErrorKind::Operand);
        CodeBuilder swap(context); swap.emit(Opcode::Swap, 1); invalid(swap, CodeErrorKind::Operand);
        CodeBuilder deep(context); deep.emit(Opcode::Swap, 2); invalid(deep, CodeErrorKind::StackUnderflow);
        CodeBuilder tuple(context); tuple.emit(Opcode::BuildTuple, 1); invalid(tuple, CodeErrorKind::StackUnderflow);
        for (auto opcode : {Opcode::LoadAttr, Opcode::StoreAttr, Opcode::DeleteAttr})
        {
            CodeBuilder attribute(context); attribute.emit(opcode); invalid(attribute, CodeErrorKind::NameBounds);
        }
        CodeBuilder dictionary(context); std::array<std::u32string, 1> names{U"x"}; dictionary.name_dict(names);
        invalid(dictionary, CodeErrorKind::StackUnderflow);
    }

    TEST_F(CoreExecution, GlobalLookupUsesLiveGlobalsThenTheFunctionsBuiltinsMapping)
    {
        auto globals = dictionary(), builtins = dictionary(); auto key = text(U"answer");
        auto first = integer(41), second = integer(42); ReturnSlot out(context);
        detail::name_store(context, builtins.get(context), key.get(context), first.get(context));
        CodeBuilder builder(context); const auto index = builder.name(U"answer"); EXPECT_EQ(builder.name(U"answer"), index);
        builder.emit(Opcode::LoadGlobal, index); builder.emit(Opcode::Return); auto program = code(builder);
        FunctionSpec spec{program, globals}; spec.builtins = builtins; make_function(context, spec, out); const auto fn = roots.root(out.result().value());
        call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), first.get(context));
        detail::name_store(context, globals.get(context), key.get(context), second.get(context));
        call(context, fn, {}, out); EXPECT_EQ(out.result().value(), second.get(context));
        detail::name_erase(context, globals.get(context), U"answer");
        detail::name_store(context, builtins.get(context), key.get(context), second.get(context));
        call(context, fn, {}, out); EXPECT_EQ(out.result().value(), second.get(context));
        detail::name_erase(context, builtins.get(context), U"answer"); call(context, fn, {}, out); error(out, CoreObject::NameErrorType);
        get_attribute(context, fn, text(U"__builtins__"), out); EXPECT_EQ(out.result().value(), builtins.get(context));
        get_attribute(context, program, text(U"co_names"), out); const auto &names = context.payload<TupleObject>(out.result().value()).items;
        ASSERT_EQ(names.size(), 1U); EXPECT_EQ(context.payload<StrObject>(names[0]).value, U"answer");
        EXPECT_NE(disassemble(context, program).find("LOAD_GLOBAL"), std::string::npos);
    }

    TEST_F(CoreExecution, GlobalStoresAndDeletesDoNotMutateBuiltinsAndTranslateMissingNames)
    {
        auto globals = dictionary(), builtins = dictionary(); auto key = text(U"answer"); auto value = integer(42); ReturnSlot out(context);
        detail::name_store(context, builtins.get(context), key.get(context), context.builtin(CoreObject::IntOne));
        CodeBuilder store(context); const auto name = store.name(U"answer");
        store.emit(Opcode::LoadConst, store.constant(value)); store.emit(Opcode::StoreGlobal, name);
        store.emit(Opcode::LoadGlobal, name); store.emit(Opcode::Return); auto program = code(store);
        FunctionSpec spec{program, globals}; spec.builtins = builtins; make_function(context, spec, out); auto writer = roots.root(out.result().value());
        call(context, writer, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), value.get(context));
        EXPECT_EQ(detail::name_lookup(context, globals.get(context), U"answer"), value.get(context));
        EXPECT_EQ(detail::name_lookup(context, builtins.get(context), U"answer"), context.builtin(CoreObject::IntOne));
        CodeBuilder erase(context); const auto erased = erase.name(U"answer"); erase.emit(Opcode::DeleteGlobal, erased);
        erase.emit(Opcode::LoadGlobal, erased); erase.emit(Opcode::Return); spec.code = code(erase); make_function(context, spec, out); auto deleter = roots.root(out.result().value());
        call(context, deleter, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::IntOne));
        call(context, deleter, {}, out); error(out, CoreObject::NameErrorType);
        context.set_allocation_limit(0); call(context, writer, {}, out); error(out, CoreObject::MemoryErrorType);
        EXPECT_FALSE(detail::name_lookup(context, globals.get(context), U"answer"));
    }

    TEST_F(CoreExecution, ImageBuiltinsUseOrdinaryProtocolsAndCalls)
    {
        CodeBuilder builder(context); builder.emit(Opcode::LoadGlobal, builder.name(U"len"));
        builder.emit(Opcode::LoadConst, builder.constant(text(U"abc"))); builder.call(1); builder.emit(Opcode::Return);
        auto fn = function(code(builder)); ReturnSlot out(context); call(context, fn, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(context, out.result().value()), "3");
        EXPECT_EQ(detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"int"), context.builtin(CoreObject::IntType));
        EXPECT_EQ(detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"object"), context.object_type());
    }

    TEST_F(CoreExecution, GlobalStoreAllocationFailureLeavesTheOldBindingIntact)
    {
        ReturnSlot out(context); auto globals = dictionary(), name = text(U"answer"), old = integer(7);
        detail::name_store(context, globals.get(context), name.get(context), old.get(context));
        make_native_function(context, "exhaust allocation quota", {},
            [](Attachment &context, Handle, std::span<const Value>, ReturnSlot &out) {
                context.set_allocation_limit(0); out.ok(context.builtin(CoreObject::IntOne));
            }, {}, out);
        auto exhaust = roots.root(out.result().value()); CodeBuilder builder(context);
        builder.emit(Opcode::LoadConst, builder.constant(exhaust)); builder.call(0);
        builder.emit(Opcode::StoreGlobal, builder.name(U"answer")); builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None))); builder.emit(Opcode::Return);
        make_function(context, {code(builder), globals}, out); const auto fn = roots.root(out.result().value());
        call(context, fn, {}, out); error(out, CoreObject::MemoryErrorType);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_EQ(detail::name_lookup(context, globals.get(context), U"answer"), old.get(context));
    }

    TEST_F(CoreExecution, VerificationRejectsUnderflowFallthroughAndGrowingBackEdges)
    {
        CodeBuilder underflow(context); underflow.emit(Opcode::DupTop); invalid(underflow, CodeErrorKind::StackUnderflow);
        CodeBuilder fallthrough(context); auto value = fallthrough.constant(integer(1));
        fallthrough.emit(Opcode::LoadConst, value); invalid(fallthrough, CodeErrorKind::Fallthrough);
        CodeBuilder grows(context); value = grows.constant(integer(1)); auto start = grows.label(); grows.bind(start);
        grows.emit(Opcode::LoadConst, value); grows.jump(Opcode::Jump, start); invalid(grows, CodeErrorKind::StackJoin);
        CodeBuilder infinite(context); start = infinite.label(); infinite.bind(start); infinite.jump(Opcode::Jump, start);
        auto valid = code(infinite); EXPECT_EQ(context.payload<CodeObject>(valid).program.max_stack(), 0U);
    }

    TEST_F(CoreExecution, StackMergesMustHaveOneConsistentHeight)
    {
        CodeBuilder builder(context); auto one = builder.constant(integer(1));
        auto alternative = builder.label(); auto merge = builder.label();
        builder.emit(Opcode::LoadConst, one); builder.jump(Opcode::JumpIfFalse, alternative);
        builder.emit(Opcode::LoadConst, one); builder.jump(Opcode::Jump, merge);
        builder.bind(alternative); builder.emit(Opcode::LoadConst, one); builder.emit(Opcode::LoadConst, one);
        builder.bind(merge); builder.emit(Opcode::Return); invalid(builder, CodeErrorKind::StackJoin);
    }

    TEST_F(CoreExecution, LabelsSignaturesAndPositionsAreValidatedBeforePublication)
    {
        CodeBuilder first(context), second(context); auto label = first.label();
        EXPECT_THROW(second.jump(Opcode::Jump, label), CodeValidationError);
        first.bind(label); EXPECT_THROW(first.bind(label), CodeValidationError);
        auto missing = second.label(); second.jump(Opcode::Jump, missing); invalid(second, CodeErrorKind::UnboundLabel);
        CodeSpec spec; spec.signature.parameters = {{U"arg"}}; spec.locals = {U"different"};
        CodeBuilder signature(context, spec); signature.emit(Opcode::Jump, 0); invalid(signature, CodeErrorKind::Signature);
        spec.signature.parameters = {{U"arg", ParameterKind::PositionalOnly, context.builtin(CoreObject::None)}};
        spec.locals = {U"arg"}; CodeBuilder defaults(context, spec); defaults.emit(Opcode::Jump, 0); invalid(defaults, CodeErrorKind::Signature);
        CodeBuilder source(context); source.emit(Opcode::Jump, 0, {2, 4, 1, 8}); invalid(source, CodeErrorKind::SourcePosition);
    }

    TEST_F(CoreExecution, OpcodeMetadataDrivesDisassemblyAndMaximumStack)
    {
        CodeBuilder builder(context); auto value = builder.constant(integer(8));
        builder.emit(Opcode::LoadConst, value); builder.emit(Opcode::DupTop);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); builder.emit(Opcode::Return);
        auto program = code(builder); EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 2U);
        EXPECT_EQ(disassemble(context, program), "0 LOAD_CONST 0\n1 DUP_TOP\n2 BINARY 0\n3 RETURN\n");
        EXPECT_TRUE(opcode_definition(Opcode::Binary).safepoint);
        ReturnSlot out(context); call(context, function(program), {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(context, out.result().value()), "16");
    }

    TEST_F(CoreExecution, BuilderRootsConstantsAndFunctionsRetainOneActualCodeObject)
    {
        CodeBuilder builder(context); ReturnSlot out(context); make_str(context, U"only builder root", out);
        { RootScope temporary(context); builder.emit(Opcode::LoadConst, builder.constant(temporary.root(out.result().value()))); }
        out.reset(); context.collect(); builder.emit(Opcode::Return); auto program = code(builder);
        auto first = function(program); auto second = function(program); auto name = text(U"__code__");
        get_attribute(context, first, name, out); EXPECT_EQ(out.result().value(), program.get(context));
        get_attribute(context, second, name, out); EXPECT_EQ(out.result().value(), program.get(context));
        context.collect(); call(context, first, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"only builder root");
        EXPECT_EQ(context.type_of(program), context.builtin(CoreObject::CodeType));
    }

    TEST_F(CoreExecution, PythonFunctionsUseTheExistingBinderDefaultsAndVariadics)
    {
        CodeSpec spec; spec.signature.parameters = {{U"x", ParameterKind::PositionalOnly},
            {U"y", ParameterKind::PositionalOrKeyword}, {U"z", ParameterKind::KeywordOnly}};
        spec.signature.varargs = U"args"; spec.signature.varkwargs = U"kwargs";
        spec.locals = {U"x", U"y", U"z", U"args", U"kwargs"};
        CodeBuilder builder(context, spec); builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::Return);
        ReturnSlot out(context); std::array default_items{integer(7).get(context)};
        cxxpython::core::make_tuple(context, default_items, out); auto defaults = roots.root(out.result().value());
        auto keywords = dictionary(); detail::name_store(context, keywords.get(context), text(U"z").get(context), integer(9).get(context));
        auto fn = function(code(builder), defaults, keywords); std::array args{integer(1).get(context)};
        call(context, fn, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.result().value()), "7");
        call(context, fn, {}, out); error(out, CoreObject::TypeErrorType);
        std::array named{KeywordArg{text(U"y").get(context), integer(11).get(context)}};
        call(context, fn, {args, named}, out); EXPECT_EQ(int_decimal(context, out.result().value()), "11");
        spec.signature = {{{U"self", ParameterKind::PositionalOnly}}, {}, {}}; spec.locals = {U"self"};
        CodeBuilder method(context, spec); method.emit(Opcode::LoadFast); method.emit(Opcode::Return);
        create_type(context, {"Owner"}, out); auto cls = roots.root(out.result().value()); auto name = text(U"method");
        set_attribute(context, cls, name, function(code(method)), out); context.allocate_instance<InstanceObject>(cls, out);
        auto self = roots.root(out.result().value()); get_attribute(context, self, name, out); auto bound = roots.root(out.result().value());
        call(context, bound, {}, out); EXPECT_EQ(out.result().value(), self.get(context));
    }

    TEST_F(CoreExecution, BranchLoopAndFastLocalsExecuteThroughObjectOps)
    {
        CodeSpec spec; spec.signature.parameters = {{U"n"}}; spec.locals = {U"n", U"i", U"total"};
        CodeBuilder builder(context, spec); auto zero = builder.constant(integer(0)); auto one = builder.constant(integer(1));
        builder.emit(Opcode::LoadConst, zero); builder.emit(Opcode::StoreFast, 1);
        builder.emit(Opcode::LoadConst, zero); builder.emit(Opcode::StoreFast, 2);
        auto loop = builder.label(); auto done = builder.label(); builder.bind(loop);
        builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::LoadFast, 0);
        builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Lt)); builder.jump(Opcode::JumpIfFalse, done);
        builder.emit(Opcode::LoadFast, 2); builder.emit(Opcode::LoadFast, 1);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); builder.emit(Opcode::StoreFast, 2);
        builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::LoadConst, one);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); builder.emit(Opcode::StoreFast, 1);
        builder.jump(Opcode::Jump, loop); builder.bind(done); builder.emit(Opcode::LoadFast, 2); builder.emit(Opcode::Return);
        ReturnSlot out(context); auto fn = function(code(builder)); std::array args{integer(100).get(context)};
        call(context, fn, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.result().value()), "4950"); EXPECT_FALSE(context.current_frame());
    }

    TEST_F(CoreExecution, UnboundIsNotNoneAndManagedFailuresCrossTheCallerSlot)
    {
        CodeSpec spec; spec.locals = {U"x"}; CodeBuilder builder(context, spec);
        builder.emit(Opcode::LoadFast, 0); builder.emit(Opcode::Return); ReturnSlot out(context);
        call(context, function(code(builder)), {}, out); error(out, CoreObject::UnboundLocalErrorType);
        CodeBuilder deleted(context, spec); auto none = deleted.constant(builtin(CoreObject::None));
        deleted.emit(Opcode::LoadConst, none); deleted.emit(Opcode::StoreFast);
        deleted.emit(Opcode::DeleteFast); deleted.emit(Opcode::LoadFast); deleted.emit(Opcode::Return);
        call(context, function(code(deleted)), {}, out); error(out, CoreObject::UnboundLocalErrorType);
        CodeBuilder division(context); auto one = division.constant(integer(1)); auto zero = division.constant(integer(0));
        division.emit(Opcode::LoadConst, one); division.emit(Opcode::LoadConst, zero);
        division.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::FloorDivide)); division.emit(Opcode::Return);
        call(context, function(code(division)), {}, out); error(out, CoreObject::ZeroDivisionErrorType);
        EXPECT_FALSE(context.current_frame());
    }

    TEST_F(CoreExecution, RunningFrameSlotsSurviveCallbacksGcAndCompletedFrameObservation)
    {
        ReturnSlot out(context); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
        auto observed = roots.root(out.result().value()); create_type(context, {"Operand"}, out); auto cls = roots.root(out.result().value());
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"other"}};
        std::array captures{observed.get(context)};
        make_native_function(context, "observe", signature,
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
                auto frame = *c.current_frame(); auto &data = c.payload<FrameObject>(frame);
                EXPECT_EQ(data.state, FrameState::Running); EXPECT_EQ(data.sp, 2U);
                EXPECT_EQ(data.operands[0], args[0]); auto *address = data.operands.data();
                write_cell(c, c.payload<NativeFunctionObject>(fn).captures[0], frame);
                c.collect(); EXPECT_EQ(data.operands.data(), address); EXPECT_EQ(data.locals[0], args[0]);
                RootScope roots(c); auto self = roots.root(frame); make_str(c, U"f_lineno", out);
                get_attribute(c, self, roots.root(out.result().value()), out);
                EXPECT_EQ(int_decimal(c, out.result().value()), "41");
                out.ok(args[1]);
            }, captures, out);
        auto native = roots.root(out.result().value()); set_attribute(context, cls, text(U"__add__"), native, out);
        context.allocate_instance<InstanceObject>(cls, out); auto self = roots.root(out.result().value());
        CodeSpec spec; spec.signature.parameters = {{U"x"}}; spec.locals = {U"x"}; CodeBuilder builder(context, spec);
        auto one = builder.constant(integer(1)); builder.emit(Opcode::LoadFast); builder.emit(Opcode::LoadConst, one);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add), {41, 2, 41, 7}); builder.emit(Opcode::Return);
        auto program = code(builder); std::array args{self.get(context)};
        call(context, function(program), {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        ReturnSlot snapshot(context); ASSERT_TRUE(read_cell(context, observed.get(context), snapshot));
        auto frame = roots.root(snapshot.result().value()); auto &data = context.payload<FrameObject>(frame);
        EXPECT_EQ(data.state, FrameState::Completed); EXPECT_EQ(data.sp, 0U); EXPECT_EQ(data.locals[0], self.get(context));
        for (auto value : data.operands) EXPECT_EQ(value, context.builtin(CoreObject::None));
        get_attribute(context, frame, text(U"f_code"), out); EXPECT_EQ(out.result().value(), program.get(context));
        get_attribute(context, frame, text(U"f_globals"), out); EXPECT_EQ(out.result().value(), context.payload<FunctionObject>(data.function).globals);
        get_attribute(context, frame, text(U"f_builtins"), out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::Builtins));
        EXPECT_FALSE(context.current_frame());
    }

    TEST_F(CoreExecution, FrameAllocationFailureLeavesNoCurrentFrameAndCanRetry)
    {
        CodeBuilder builder(context); auto value = builder.constant(integer(3)); builder.emit(Opcode::LoadConst, value); builder.emit(Opcode::Return);
        auto fn = function(code(builder)); ReturnSlot out(context); context.set_allocation_limit(0);
        call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError)); EXPECT_FALSE(context.current_frame());
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max()); call(context, fn, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(context, out.result().value()), "3");
    }

    TEST_F(CoreExecution, EmptyAndAbsentDefaultsAreDistinctAndVariadicResultsAreTransferred)
    {
        CodeSpec spec; spec.signature.parameters = {{U"x", ParameterKind::PositionalOnly}};
        spec.signature.varargs = U"args"; spec.signature.varkwargs = U"kwargs"; spec.locals = {U"x", U"args", U"kwargs"};
        CodeBuilder builder(context, spec); builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::Return);
        auto program = code(builder); auto empty = builtin(CoreObject::EmptyTuple);
        auto fn = function(program, empty); auto absent = function(program); ReturnSlot out(context); auto name = text(U"__defaults__");
        get_attribute(context, fn, name, out); EXPECT_EQ(out.result().value(), empty.get(context));
        get_attribute(context, absent, name, out); EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
        std::array args{integer(1).get(context), integer(2).get(context)};
        call(context, fn, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); context.collect();
        auto &extra = context.payload<TupleObject>(out.result().value()).items; ASSERT_EQ(extra.size(), 1U); EXPECT_EQ(extra[0], args[1]);
        CodeBuilder keywords(context, spec); keywords.emit(Opcode::LoadFast, 2); keywords.emit(Opcode::Return);
        fn = function(code(keywords)); std::array named{KeywordArg{text(U"x").get(context), integer(3).get(context)}};
        call(context, fn, {args, named}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok); context.collect();
        EXPECT_EQ(detail::name_lookup(context, out.result().value(), U"x"), named[0].value);
        set_attribute(context, fn, text(U"custom"), integer(4), out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        get_attribute(context, fn, text(U"custom"), out); EXPECT_EQ(int_decimal(context, out.result().value()), "4");
    }

    TEST_F(CoreExecution, BytecodeFunctionsParticipateInImplicitSpecialMethods)
    {
        CodeSpec spec; spec.signature.parameters = {{U"self"}, {U"other"}}; spec.locals = {U"self", U"other"};
        CodeBuilder builder(context, spec); builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::Return);
        ReturnSlot out(context); create_type(context, {"PythonAdder"}, out); auto cls = roots.root(out.result().value());
        set_attribute(context, cls, text(U"__add__"), function(code(builder)), out);
        context.allocate_instance<InstanceObject>(cls, out); auto obj = roots.root(out.result().value()); auto expected = integer(9);
        binary(context, obj, expected, BinaryOp::Add, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), expected.get(context)); EXPECT_FALSE(context.current_frame());
    }

    TEST_F(CoreExecution, FreezeAllocationFailuresDoNotConsumeTheBuilderOrPublishPartialCode)
    {
        CodeBuilder builder(context); auto value = builder.constant(integer(10)); builder.emit(Opcode::LoadConst, value); builder.emit(Opcode::Return);
        context.collect(); const auto before = context.stats().objects;
        ReturnSlot out(context); builder.freeze(out); auto reference = roots.root(out.result().value());
        (void)reference; context.collect(); const auto allocations = context.stats().objects - before;
        out.reset(); const auto baseline = context.stats().objects;
        for (std::size_t limit = 0; limit < allocations; ++limit)
        {
            context.set_allocation_limit(limit); EXPECT_THROW(builder.freeze(out), std::bad_alloc) << limit;
            EXPECT_EQ(out.result().tag(), ResultTag::Empty); context.collect(); EXPECT_EQ(context.stats().objects, baseline);
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max()); EXPECT_NO_THROW(builder.freeze(out));
            EXPECT_EQ(out.result().tag(), ResultTag::Ok); out.reset(); context.collect(); EXPECT_EQ(context.stats().objects, baseline);
        }
    }

    TEST_F(CoreExecution, NativeReentryRestoresFramesAndHostFaultsUnwindThem)
    {
        ReturnSlot out(context); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
        auto outer_cell = roots.root(out.result().value()); context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
        auto inner_cell = roots.root(out.result().value()); CodeSpec spec; spec.signature.parameters = {{U"x"}}; spec.locals = {U"x"};
        CodeBuilder builder(context, spec); auto one = builder.constant(integer(1));
        builder.emit(Opcode::LoadFast); builder.emit(Opcode::LoadConst, one);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); builder.emit(Opcode::Return);
        auto program = code(builder); auto inner = function(program); auto outer = function(program);
        create_type(context, {"Reentrant"}, out); auto cls = roots.root(out.result().value());
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"other"}};
        std::array captures{outer_cell.get(context), inner_cell.get(context), inner.get(context)};
        make_native_function(context, "reenter", signature,
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
                const auto &captures = c.payload<NativeFunctionObject>(fn).captures; const auto frame = *c.current_frame();
                if (c.payload<FrameObject>(frame).back)
                { write_cell(c, captures[1], frame); c.collect(); out.ok(args[1]); return; }
                write_cell(c, captures[0], frame);
                RootScope roots(c); std::array nested{args[0]}; call(c, roots.root(captures[2]), {nested, {}}, out);
                EXPECT_EQ(c.current_frame(), frame); c.collect();
            }, captures, out);
        auto observer = roots.root(out.result().value()); auto name = text(U"__add__");
        set_attribute(context, cls, name, observer, out); context.allocate_instance<InstanceObject>(cls, out);
        auto self = roots.root(out.result().value()); std::array args{self.get(context)};
        call(context, outer, {args, {}}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        ReturnSlot outer_snapshot(context), inner_snapshot(context);
        ASSERT_TRUE(read_cell(context, outer_cell.get(context), outer_snapshot));
        ASSERT_TRUE(read_cell(context, inner_cell.get(context), inner_snapshot));
        auto outer_frame = outer_snapshot.result().value(); auto inner_frame = inner_snapshot.result().value();
        EXPECT_EQ(context.payload<FrameObject>(inner_frame).back, outer_frame); EXPECT_FALSE(context.current_frame());
        make_native_function(context, "host fault", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &) {
                write_cell(c, c.payload<NativeFunctionObject>(fn).captures[0], *c.current_frame());
                throw std::runtime_error("native host fault");
            }, captures, out);
        set_attribute(context, cls, name, roots.root(out.result().value()), out);
        EXPECT_THROW(call(context, outer, {args, {}}, out), std::runtime_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty); EXPECT_FALSE(context.current_frame());
        ASSERT_TRUE(read_cell(context, outer_cell.get(context), outer_snapshot));
        const auto &failed = context.payload<FrameObject>(outer_snapshot.result().value());
        EXPECT_EQ(failed.state, FrameState::Completed); EXPECT_EQ(failed.sp, 0U);
        set_attribute(context, cls, name, observer, out); call(context, outer, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_FALSE(context.current_frame());
    }

    TEST(CoreExecutionLifecycle, FunctionGlobalsCodeCyclesAreCollected)
    {
        Interpreter interpreter({.collect_every_allocation = true}); Attachment context(interpreter); context.bootstrap_core();
        context.collect(); const auto before = context.stats().objects;
        {
            RootScope roots(context); ReturnSlot out(context); CodeBuilder builder(context);
            auto none = roots.root(context.builtin(CoreObject::None)); builder.emit(Opcode::LoadConst, builder.constant(none)); builder.emit(Opcode::Return);
            builder.freeze(out); auto code = roots.root(out.result().value());
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out); auto globals = roots.root(out.result().value());
            make_function(context, {code, globals}, out); auto fn = roots.root(out.result().value());
            make_str(context, U"function", out); detail::name_store(context, globals.get(context), out.result().value(), fn.get(context));
            call(context, fn, {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        }
        context.collect(); EXPECT_EQ(context.stats().objects, before);
    }
}
