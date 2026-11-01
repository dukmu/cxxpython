#include "cxxpython/execution.hpp"
#include "cxxpython/typealiasobject.hpp"
#include "cxxpython/typevarobject.hpp"
#include "internal/abstract.hpp"
#include "internal/exceptiongroup.hpp"
#include "internal/patternmatching.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class CoreExecution : public testing::Test {
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
    Handle dictionary()
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(builtin(CoreObject::DictType), out);
        return roots.root(out.result().value());
    }
    Handle code(CodeBuilder &builder)
    {
        ReturnSlot out(context);
        builder.freeze(out);
        return roots.root(out.result().value());
    }
    Handle function(Handle code, std::optional<Handle> defaults = {},
                    std::optional<Handle> keywords = {})
    {
        ReturnSlot out(context);
        make_function(context, {code, dictionary(), defaults, keywords}, out);
        return roots.root(out.result().value());
    }
    void error(ReturnSlot &out, CoreObject type)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
    }
    void invalid(CodeBuilder &builder, CodeErrorKind kind)
    {
        ReturnSlot out(context);
        try {
            builder.freeze(out);
            FAIL() << "invalid code accepted";
        } catch (const CodeValidationError &error) {
            EXPECT_EQ(error.kind, kind) << error.what();
        }
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    }
};

TEST_F(CoreExecution, ScalarOperandsAreNotGcPointersAndTrimRestoresBoxedSlots)
{
    CodeBuilder builder(context);
    const auto one = builder.constant(integer(1));
    for (int i = 0; i < 3; ++i)
        builder.emit(Opcode::LoadConst, one);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::Return);
    const auto program = code(builder), callable = function(program);
    ReturnSlot out(context);
    context.allocate_instance<FrameObject>(builtin(CoreObject::FrameType), out,
                                           callable.get(context), program.get(context), 0, 3,
                                           context.builtin(CoreObject::None), 0);
    const auto frame = roots.root(out.value());
    auto &data = context.payload<FrameObject>(frame);
    data.operands[0] = OperandSlot::integer(std::numeric_limits<std::int64_t>::min());
    data.operands[1] = OperandSlot::floating(std::numeric_limits<double>::quiet_NaN());
    make_str(context, U"only retained by the operand stack", out);
    data.operands[2] = out.value();
    data.sp = 3;
    data.scalar_operands = 2;
    out.reset();
    context.collect();
    EXPECT_EQ(data.operands[0].integer(), std::numeric_limits<std::int64_t>::min());
    EXPECT_TRUE(std::isnan(data.operands[1].floating()));
    EXPECT_EQ(context.payload<StrObject>(data.operands[2].boxed()).value,
              U"only retained by the operand stack");
    EXPECT_THROW(data.operands[0].boxed(), std::logic_error);
    data.trim_stack(context, 0);
    EXPECT_EQ(data.scalar_operands, 0U);
    for (const auto &slot : data.operands)
        EXPECT_EQ(slot.boxed(), context.builtin(CoreObject::None));
}

TEST_F(CoreExecution, NumericLeaseUsesActualDefinitionAndExistingRootSlot)
{
    context.set_gc_enabled(false);
    RootScope cold(context);
    detail::resolve_builtin_slot(context, CoreObject::FloatType, Protocol::Add, cold);
    detail::resolve_builtin_slot(context, CoreObject::FloatType, Protocol::IAdd, cold);
    ReturnSlot lease(context), out(context);
    const auto count = context.stats().roots;
    auto kind =
        context.lease_builtin_numeric(CoreObject::FloatType, Protocol::Add, Protocol::IAdd, lease);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, NativeNumericKind::FloatAdd);
    EXPECT_EQ(context.stats().roots, count);
    const auto descriptor = roots.root(lease.value());
    const auto &method = context.payload<MethodDescriptorObject>(descriptor);
    const auto &native = context.payload<NativeFunctionObject>(method.function);
    EXPECT_EQ(native.numeric_kind, NativeNumericKind::FloatAdd);
    context.invalidate_types();
    EXPECT_FALSE(context.lease_builtin_numeric(CoreObject::FloatType, Protocol::Add, {}, lease));
    EXPECT_EQ(lease.tag(), ResultTag::Empty);

    const auto cls = builtin(CoreObject::FloatType);
    const auto space = roots.root(*TypeView(context, cls).dictionary());
    const auto key = text(U"__add__");
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly},
                            {U"other", ParameterKind::KeywordOnly}};
    detail::allocate_function(context, builtin(CoreObject::NativeFunctionType), "tagged add",
                              signature, native.entry, {}, out, {},
                              NativeFunctionBinding::Descriptor, {}, NativeNumericKind::FloatAdd);
    make_method_descriptor(context, cls, roots.root(out.value()), out);
    detail::name_store(context, space.get(context), key.get(context), out.value());
    detail::resolve_builtin_slot(context, CoreObject::FloatType, Protocol::Add, cold);
    kind = context.lease_builtin_numeric(CoreObject::FloatType, Protocol::Add, {}, lease);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, NativeNumericKind::None);
    EXPECT_EQ(lease.tag(), ResultTag::Empty);
    detail::name_store(context, space.get(context), key.get(context), descriptor.get(context));
    const auto inplace = text(U"__iadd__");
    detail::name_store(context, space.get(context), inplace.get(context), descriptor.get(context));
    detail::resolve_builtin_slot(context, CoreObject::FloatType, Protocol::Add, cold);
    detail::resolve_builtin_slot(context, CoreObject::FloatType, Protocol::IAdd, cold);
    kind =
        context.lease_builtin_numeric(CoreObject::FloatType, Protocol::Add, Protocol::IAdd, lease);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, NativeNumericKind::None);
    EXPECT_EQ(lease.tag(), ResultTag::Empty);
    ASSERT_TRUE(detail::name_erase(context, space.get(context), U"__iadd__"));
}

TEST_F(CoreExecution, VerificationRejectsEmptyUnknownAndUnexpectedOperands)
{
    CodeBuilder empty(context);
    invalid(empty, CodeErrorKind::EmptyCode);
    CodeBuilder unknown(context);
    unknown.emit(static_cast<Opcode>(255));
    invalid(unknown, CodeErrorKind::UnknownOpcode);
    CodeBuilder operand(context);
    operand.emit(Opcode::Return, 1);
    invalid(operand, CodeErrorKind::Operand);
    CodeBuilder domain(context);
    domain.emit(Opcode::Binary, 255);
    invalid(domain, CodeErrorKind::Operand);
    CodeBuilder flags(context);
    flags.emit(Opcode::MakeFunction, 16);
    invalid(flags, CodeErrorKind::Operand);
    CodeBuilder function(context);
    function.emit(Opcode::MakeFunction, 3);
    invalid(function, CodeErrorKind::StackUnderflow);
}

TEST_F(CoreExecution, ImportInstructionsVerifyNamesStackAndModuleOnlyStar)
{
    for (auto op : {Opcode::ImportName, Opcode::ImportFrom}) {
        CodeBuilder bounds(context);
        bounds.emit(op);
        invalid(bounds, CodeErrorKind::NameBounds);
        CodeBuilder underflow(context);
        underflow.emit(op, underflow.name(U"sys"));
        invalid(underflow, CodeErrorKind::StackUnderflow);
    }
    CodeBuilder nonmodule(context);
    nonmodule.emit(Opcode::ImportStar);
    invalid(nonmodule, CodeErrorKind::Operand);
    CodeSpec spec;
    spec.kind = CodeKind::Module;
    CodeBuilder star(context, spec);
    star.emit(Opcode::ImportStar);
    invalid(star, CodeErrorKind::StackUnderflow);
    CodeBuilder valid(context, spec);
    valid.emit(Opcode::LoadConst, valid.constant(integer(0)));
    valid.emit(Opcode::LoadConst, valid.constant(roots.root(context.builtin(CoreObject::None))));
    valid.emit(Opcode::ImportName, valid.name(U"sys"));
    valid.emit(Opcode::ImportFrom, valid.name(U"modules"));
    valid.emit(Opcode::PopTop);
    valid.emit(Opcode::Return);
    auto program = code(valid);
    EXPECT_NE(disassemble(context, program).find("IMPORT_NAME"), std::string::npos);
    EXPECT_NE(disassemble(context, program).find("IMPORT_FROM"), std::string::npos);
}

TEST_F(CoreExecution, SetupAnnotationsVerificationAndModuleRoots)
{
    const auto &definition = opcode_definition(Opcode::SetupAnnotations);
    EXPECT_EQ(definition.operand, OperandDomain::Name);
    EXPECT_TRUE(definition.may_raise);
    EXPECT_TRUE(definition.safepoint);
    const auto effect = instruction_effect({Opcode::SetupAnnotations, 0});
    EXPECT_EQ(effect.inputs, 0U);
    EXPECT_EQ(effect.pops, 0U);
    EXPECT_EQ(effect.pushes, 0U);

    CodeSpec spec;
    spec.kind = CodeKind::Module;
    CodeBuilder bounds(context, spec);
    bounds.emit(Opcode::SetupAnnotations);
    invalid(bounds, CodeErrorKind::NameBounds);
    CodeBuilder function_code(context);
    function_code.emit(Opcode::SetupAnnotations, function_code.name(U"__annotations__"));
    invalid(function_code, CodeErrorKind::Operand);
    for (auto kind : {CodeKind::Module, CodeKind::ClassBody}) {
        spec.kind = kind;
        CodeBuilder builder(context, spec);
        builder.emit(Opcode::SetupAnnotations, builder.name(U"__annotations__"));
        builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None)));
        builder.emit(Opcode::Return);
        EXPECT_EQ(context.payload<CodeObject>(code(builder)).program.max_stack(), 1U);
    }

    spec.kind = CodeKind::Module;
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::SetupAnnotations, builder.name(U"__annotations__"));
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None)));
    builder.emit(Opcode::Return);
    const auto program = code(builder);
    const auto globals = dictionary();
    ReturnSlot out(context);
    make_function(context, {program, globals}, out);
    const auto fn = roots.root(out.value());
    call(context, fn, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    const auto key = text(U"__annotations__");
    dict_lookup(context, globals, key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto annotations = roots.root(out.value());
    EXPECT_TRUE(DictView(context, annotations.get(context)).entries().empty());
    call(context, fn, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    dict_lookup(context, globals, key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), annotations.get(context));

    const auto empty_globals = dictionary();
    make_function(context, {program, empty_globals}, out);
    const auto limited = roots.root(out.value());
    context.set_allocation_limit(0);
    bool exhausted = false;
    try {
        call(context, limited, {}, out);
    } catch (const std::bad_alloc &) {
        exhausted = true;
    }
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    if (!exhausted)
        error(out, CoreObject::MemoryErrorType);
    context.collect();
    call(context, limited, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    dict_lookup(context, empty_globals, key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_TRUE(DictView(context, out.value()).entries().empty());
}

TEST_F(CoreExecution, FutureFlagsAreValidatedAndCapturedByVerifiedCode)
{
    CodeSpec spec;
    spec.future_flags = supported_future_flags | 1U;
    CodeBuilder invalid_flags(context, spec);
    invalid_flags.emit(Opcode::Jump);
    invalid(invalid_flags, CodeErrorKind::Operand);
    spec.future_flags = future_annotations;
    spec.nested = true;
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None)));
    builder.emit(Opcode::Return);
    const auto program = code(builder);
    spec.future_flags = 0;
    spec.nested = false;
    const auto &verified = context.payload<CodeObject>(program).program;
    EXPECT_EQ(verified.future_flags(), future_annotations);
    EXPECT_EQ(verified.flags(), future_annotations | 3U | 16U);
    context.collect();
    EXPECT_EQ(context.payload<CodeObject>(program).program.flags(), future_annotations | 19U);
}

TEST_F(CoreExecution, FunctionCreationRetainsCodeIdentityAndInheritsTheExecutionEnvironment)
{
    CodeBuilder child(context);
    child.emit(Opcode::LoadGlobal, child.name(U"answer"));
    child.emit(Opcode::Return);
    const auto child_code = code(child);
    CodeBuilder parent(context);
    parent.emit(Opcode::LoadConst, parent.constant(child_code));
    parent.emit(Opcode::MakeFunction);
    parent.emit(Opcode::Return);
    const auto globals = dictionary(), builtins = dictionary();
    const auto answer = integer(42);
    detail::name_store(context, builtins.get(context), text(U"answer").get(context),
                       answer.get(context));
    FunctionSpec spec{code(parent), globals};
    spec.builtins = builtins;
    ReturnSlot out(context);
    make_function(context, spec, out);
    const auto factory = roots.root(out.result().value());
    call(context, factory, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto first = roots.root(out.result().value());
    call(context, factory, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto second = roots.root(out.result().value());
    EXPECT_NE(first.get(context), second.get(context));
    for (auto fn : {first, second}) {
        const auto &function = context.payload<FunctionObject>(fn);
        EXPECT_EQ(function.code, child_code.get(context));
        EXPECT_EQ(function.globals, globals.get(context));
        EXPECT_EQ(function.builtins, builtins.get(context));
        call(context, fn, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), answer.get(context));
    }
}

TEST_F(CoreExecution, FunctionCreationConsumesDefaultsInDeclaredStackOrder)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"a"}, {U"b", ParameterKind::KeywordOnly}};
    spec.locals = {U"a", U"b"};
    CodeBuilder child(context, spec);
    child.emit(Opcode::LoadFast);
    child.emit(Opcode::LoadFast, 1);
    child.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    child.emit(Opcode::Return);
    const auto child_code = code(child);
    ReturnSlot out(context);
    std::array values{integer(40).get(context)};
    cxxpython::core::make_tuple(context, values, out);
    const auto defaults = roots.root(out.result().value());
    const auto keywords = dictionary();
    detail::name_store(context, keywords.get(context), text(U"b").get(context),
                       integer(2).get(context));
    CodeBuilder parent(context);
    parent.emit(Opcode::LoadConst, parent.constant(defaults));
    parent.emit(Opcode::LoadConst, parent.constant(keywords));
    parent.emit(Opcode::LoadConst, parent.constant(child_code));
    parent.emit(Opcode::MakeFunction, 3);
    parent.emit(Opcode::Return);
    call(context, function(code(parent)), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto fn = roots.root(out.result().value());
    context.load(fn.get(context), context.payload<FunctionObject>(fn).defaults, out);
    EXPECT_EQ(out.value(), defaults.get(context));
    context.load(fn.get(context), context.payload<FunctionObject>(fn).kwdefaults, out);
    EXPECT_EQ(out.value(), keywords.get(context));
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "42");
    CodeBuilder invalid_function(context);
    invalid_function.emit(Opcode::LoadConst, invalid_function.constant(integer(1)));
    invalid_function.emit(Opcode::MakeFunction);
    invalid_function.emit(Opcode::Return);
    call(context, function(code(invalid_function)), {}, out);
    error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreExecution, VerificationChecksOperandBoundsEvenInUnreachableInstructions)
{
    CodeBuilder constants(context);
    constants.emit(Opcode::LoadConst, 0);
    invalid(constants, CodeErrorKind::ConstantBounds);
    CodeBuilder local(context);
    local.emit(Opcode::LoadFast, 0);
    invalid(local, CodeErrorKind::LocalBounds);
    CodeBuilder jump(context);
    jump.emit(Opcode::Jump, 1);
    invalid(jump, CodeErrorKind::JumpBounds);
    CodeBuilder dead(context);
    dead.emit(Opcode::Jump, 0);
    dead.emit(Opcode::LoadConst, 0);
    invalid(dead, CodeErrorKind::ConstantBounds);
    for (auto opcode : {Opcode::LoadGlobal, Opcode::StoreGlobal, Opcode::DeleteGlobal}) {
        CodeBuilder global(context);
        global.emit(opcode);
        invalid(global, CodeErrorKind::NameBounds);
    }
}

TEST_F(CoreExecution, ClosureOperandsAndLayoutsAreVerifiedBeforePublication)
{
    for (auto opcode :
         {Opcode::LoadDeref, Opcode::StoreDeref, Opcode::DeleteDeref, Opcode::LoadClosure}) {
        CodeBuilder builder(context);
        builder.emit(Opcode::Jump);
        builder.emit(opcode);
        invalid(builder, CodeErrorKind::CellBounds);
    }
    CodeSpec spec;
    spec.locals = {U"x"};
    spec.cellvars = {U"missing"};
    CodeBuilder missing(context, spec);
    missing.emit(Opcode::Jump);
    invalid(missing, CodeErrorKind::CellLayout);
    spec.cellvars = {U"x", U"x"};
    CodeBuilder duplicate(context, spec);
    duplicate.emit(Opcode::Jump);
    invalid(duplicate, CodeErrorKind::CellLayout);
    spec.cellvars = {U"x"};
    spec.freevars = {U"x"};
    CodeBuilder overlap(context, spec);
    overlap.emit(Opcode::Jump);
    invalid(overlap, CodeErrorKind::CellLayout);
    spec.freevars.clear();
    for (auto opcode : {Opcode::LoadFast, Opcode::StoreFast, Opcode::DeleteFast}) {
        CodeBuilder fast(context, spec);
        fast.emit(opcode);
        invalid(fast, CodeErrorKind::CellLayout);
    }
    CodeBuilder flags(context);
    flags.emit(Opcode::MakeFunction, 7);
    invalid(flags, CodeErrorKind::StackUnderflow);
    const auto effect = instruction_effect({Opcode::MakeFunction, 7});
    EXPECT_EQ(effect.inputs, 4U);
    EXPECT_EQ(effect.pops, 4U);
    EXPECT_EQ(effect.pushes, 1U);
    const auto annotated = instruction_effect({Opcode::MakeFunction, 15});
    EXPECT_EQ(annotated.inputs, 5U);
    EXPECT_EQ(annotated.pops, 5U);
    EXPECT_EQ(annotated.pushes, 1U);
}

TEST_F(CoreExecution, FunctionClosureShapeAndCellAllocationFailuresUseNormalResultPaths)
{
    CodeSpec spec;
    spec.freevars = {U"value"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadDeref);
    builder.emit(Opcode::Return);
    const auto program = code(builder);
    FunctionSpec function_spec{program, dictionary()};
    ReturnSlot out(context);
    make_function(context, function_spec, out);
    error(out, CoreObject::TypeErrorType);
    function_spec.closure = integer(1);
    make_function(context, function_spec, out);
    error(out, CoreObject::TypeErrorType);
    function_spec.closure = builtin(CoreObject::EmptyTuple);
    make_function(context, function_spec, out);
    error(out, CoreObject::ValueErrorType);
    std::array wrong{context.builtin(CoreObject::IntOne)};
    cxxpython::core::make_tuple(context, wrong, out);
    function_spec.closure = roots.root(out.result().value());
    make_function(context, function_spec, out);
    error(out, CoreObject::TypeErrorType);
    context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out,
                                          context.builtin(CoreObject::IntOne));
    const auto cell = roots.root(out.result().value());
    std::array values{cell.get(context)};
    cxxpython::core::make_tuple(context, values, out);
    const auto closure = roots.root(out.result().value());
    function_spec.closure = closure;
    make_function(context, function_spec, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto fn = roots.root(out.result().value());
    call(context, fn, {}, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::IntOne));
    EXPECT_EQ(disassemble(context, program), "0 LOAD_DEREF 0\n1 RETURN\n");
    spec.freevars.clear();
    spec.signature.parameters = {{U"value"}};
    spec.locals = {U"value"};
    spec.cellvars = {U"value"};
    CodeBuilder captured(context, spec);
    captured.emit(Opcode::LoadDeref);
    captured.emit(Opcode::Return);
    const auto target = function(code(captured));
    std::array args{context.builtin(CoreObject::IntOne)};
    for (auto limit : {0U, 1U}) {
        context.set_allocation_limit(limit);
        call(context, target, {args, {}}, out);
        error(out, CoreObject::MemoryErrorType);
        EXPECT_FALSE(context.current_frame());
    }
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    call(context, target, {args, {}}, out);
    EXPECT_EQ(out.result().value(), args[0]);
}

TEST_F(CoreExecution, SourceLoweringOpcodesHaveVerifiedOperandsAndStackInputs)
{
    CodeBuilder unary(context);
    unary.emit(Opcode::Unary, 255);
    invalid(unary, CodeErrorKind::Operand);
    CodeBuilder swap(context);
    swap.emit(Opcode::Swap, 1);
    invalid(swap, CodeErrorKind::Operand);
    CodeBuilder deep(context);
    deep.emit(Opcode::Swap, 2);
    invalid(deep, CodeErrorKind::StackUnderflow);
    CodeBuilder tuple(context);
    tuple.emit(Opcode::BuildTuple, 1);
    invalid(tuple, CodeErrorKind::StackUnderflow);
    CodeBuilder list(context);
    list.emit(Opcode::BuildList, 1);
    invalid(list, CodeErrorKind::StackUnderflow);
    const auto list_effect = instruction_effect({Opcode::BuildList, 3});
    EXPECT_EQ(list_effect.inputs, 3U);
    EXPECT_EQ(list_effect.pops, 3U);
    EXPECT_EQ(list_effect.pushes, 1U);
    EXPECT_EQ(opcode_definition(Opcode::BuildList).name, "BUILD_LIST");
    CodeBuilder set(context);
    set.emit(Opcode::BuildSet, 1);
    invalid(set, CodeErrorKind::StackUnderflow);
    const auto set_effect = instruction_effect({Opcode::BuildSet, 3});
    EXPECT_EQ(set_effect.inputs, 3U);
    EXPECT_EQ(set_effect.pops, 3U);
    EXPECT_EQ(set_effect.pushes, 1U);
    EXPECT_EQ(opcode_definition(Opcode::BuildSet).name, "BUILD_SET");
    for (auto opcode : {Opcode::SetAdd, Opcode::SetUpdate}) {
        CodeBuilder set_update(context);
        set_update.emit(opcode);
        invalid(set_update, CodeErrorKind::StackUnderflow);
        const auto effect = instruction_effect({opcode, 0});
        EXPECT_EQ(effect.inputs, 2U);
        EXPECT_EQ(effect.pops, 1U);
        EXPECT_EQ(effect.pushes, 0U);
        EXPECT_TRUE(opcode_definition(opcode).may_raise);
        EXPECT_TRUE(opcode_definition(opcode).safepoint);
    }
    CodeBuilder display(context);
    display.emit(Opcode::PrintExpr);
    invalid(display, CodeErrorKind::StackUnderflow);
    const auto display_effect = instruction_effect({Opcode::PrintExpr, 0});
    EXPECT_EQ(display_effect.inputs, 1U);
    EXPECT_EQ(display_effect.pops, 1U);
    EXPECT_EQ(display_effect.pushes, 0U);
    EXPECT_EQ(opcode_definition(Opcode::PrintExpr).name, "PRINT_EXPR");
    for (auto opcode : {Opcode::LoadAttr, Opcode::StoreAttr, Opcode::DeleteAttr}) {
        CodeBuilder attribute(context);
        attribute.emit(opcode);
        invalid(attribute, CodeErrorKind::NameBounds);
    }
    CodeBuilder dictionary(context);
    std::array<std::u32string, 1> names{U"x"};
    dictionary.name_dict(names);
    invalid(dictionary, CodeErrorKind::StackUnderflow);
}

TEST_F(CoreExecution, YieldOpcodesRequireGeneratorCodeAndVerifiedResumptionSlots)
{
    for (auto opcode : {Opcode::YieldValue, Opcode::YieldFrom, Opcode::AsyncYieldValue}) {
        CodeBuilder ordinary(context);
        ordinary.emit(opcode);
        invalid(ordinary, CodeErrorKind::Operand);
        CodeSpec spec;
        spec.generator = true;
        spec.coroutine = opcode == Opcode::AsyncYieldValue;
        CodeBuilder underflow(context, spec);
        underflow.emit(opcode);
        invalid(underflow, CodeErrorKind::StackUnderflow);
        CodeBuilder operand(context, spec);
        operand.emit(opcode, 1);
        invalid(operand, CodeErrorKind::Operand);
        CodeBuilder valid(context, spec);
        valid.emit(Opcode::LoadConst, valid.constant(builtin(CoreObject::None)));
        valid.emit(opcode);
        valid.emit(Opcode::Return);
        const auto program = code(valid);
        EXPECT_TRUE(context.payload<CodeObject>(program).program.is_generator());
        EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 1U);
        EXPECT_NE(disassemble(context, program).find(opcode_definition(opcode).name),
                  std::string::npos);
        const auto effect = instruction_effect({opcode, 0});
        EXPECT_EQ(effect.inputs, 1U);
        EXPECT_EQ(effect.pops, 1U);
        EXPECT_EQ(effect.pushes, 1U);
        EXPECT_TRUE(opcode_definition(opcode).may_raise);
        EXPECT_TRUE(opcode_definition(opcode).safepoint);
    }
    CodeSpec mapping;
    mapping.generator = true;
    mapping.kind = CodeKind::Module;
    CodeBuilder module(context, mapping);
    module.emit(Opcode::LoadConst, module.constant(builtin(CoreObject::None)));
    module.emit(Opcode::Return);
    invalid(module, CodeErrorKind::Signature);
}

TEST_F(CoreExecution, AwaitOpcodeRequiresCoroutineCodeAndOneResumptionSlot)
{
    CodeBuilder ordinary(context);
    ordinary.emit(Opcode::GetAwaitable);
    invalid(ordinary, CodeErrorKind::Operand);
    CodeSpec spec;
    spec.coroutine = true;
    CodeBuilder underflow(context, spec);
    underflow.emit(Opcode::GetAwaitable);
    invalid(underflow, CodeErrorKind::StackUnderflow);
    CodeBuilder valid(context, spec);
    valid.emit(Opcode::LoadConst, valid.constant(builtin(CoreObject::None)));
    valid.emit(Opcode::GetAwaitable);
    valid.emit(Opcode::YieldFrom);
    valid.emit(Opcode::Return);
    const auto program = code(valid);
    EXPECT_TRUE(context.payload<CodeObject>(program).program.is_coroutine());
    EXPECT_FALSE(context.payload<CodeObject>(program).program.is_generator());
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 1U);
    EXPECT_NE(disassemble(context, program).find("GET_AWAITABLE"), std::string::npos);
    const auto effect = instruction_effect({Opcode::GetAwaitable, 0});
    EXPECT_EQ(effect.inputs, 1U);
    EXPECT_EQ(effect.pops, 1U);
    EXPECT_EQ(effect.pushes, 1U);
}

TEST_F(CoreExecution, AsyncWithVerifiesAwaitedCleanupStackAndOperandDomain)
{
    const auto effect = instruction_effect({Opcode::PrepareAsyncWith, 0});
    EXPECT_EQ(effect.inputs, 1U);
    EXPECT_EQ(effect.pops, 1U);
    EXPECT_EQ(effect.pushes, 2U);
    EXPECT_TRUE(opcode_definition(Opcode::PrepareAsyncWith).may_raise);
    EXPECT_TRUE(opcode_definition(Opcode::PrepareAsyncWith).safepoint);
    CodeBuilder underflow(context);
    underflow.emit(Opcode::PrepareAsyncWith);
    invalid(underflow, CodeErrorKind::StackUnderflow);
    CodeBuilder operand(context);
    operand.emit(Opcode::PrepareAsyncWith, 1);
    invalid(operand, CodeErrorKind::Operand);
    CodeSpec spec;
    spec.coroutine = true;
    CodeBuilder valid(context, spec);
    const auto region = valid.region(RegionKind::With, true);
    valid.emit(Opcode::LoadConst, valid.constant(builtin(CoreObject::None)));
    valid.enter(region);
    valid.emit(Opcode::PopTop);
    valid.leave(region);
    valid.emit(Opcode::LoadConst, valid.constant(builtin(CoreObject::None)));
    valid.emit(Opcode::Return);
    const auto program = code(valid);
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 5U);
    const auto listing = disassemble(context, program);
    EXPECT_NE(listing.find("PREPARE_ASYNC_WITH"), std::string::npos);
    const auto entering = listing.find("GET_AWAITABLE");
    ASSERT_NE(entering, std::string::npos);
    EXPECT_NE(listing.find("GET_AWAITABLE", entering + 1), std::string::npos);
    CodeBuilder ordinary(context);
    const auto invalid_region = ordinary.region(RegionKind::With, true);
    ordinary.emit(Opcode::LoadConst, ordinary.constant(builtin(CoreObject::None)));
    ordinary.enter(invalid_region);
    ordinary.emit(Opcode::PopTop);
    ordinary.leave(invalid_region);
    ordinary.emit(Opcode::LoadConst, ordinary.constant(builtin(CoreObject::None)));
    ordinary.emit(Opcode::Return);
    invalid(ordinary, CodeErrorKind::Operand);
    EXPECT_THROW(ordinary.region(RegionKind::Finally, true), CodeValidationError);
}

TEST_F(CoreExecution, AsyncIterationReusesDelegationAndVerifiesBothBranchStacks)
{
    const auto item = instruction_effect({Opcode::ForAwait, 0});
    const auto exhausted = instruction_effect({Opcode::ForAwait, 0}, {}, true);
    EXPECT_EQ(item.inputs, 1U);
    EXPECT_EQ(item.pops, 0U);
    EXPECT_EQ(item.pushes, 1U);
    EXPECT_EQ(exhausted.pops, 0U);
    EXPECT_EQ(exhausted.pushes, 0U);
    EXPECT_TRUE(opcode_definition(Opcode::ForAwait).may_raise);
    EXPECT_TRUE(opcode_definition(Opcode::ForAwait).safepoint);
    CodeBuilder ordinary(context);
    ordinary.emit(Opcode::ForAwait);
    invalid(ordinary, CodeErrorKind::Operand);
    CodeSpec spec;
    spec.coroutine = true;
    CodeBuilder underflow(context, spec);
    underflow.emit(Opcode::ForAwait);
    invalid(underflow, CodeErrorKind::StackUnderflow);
    CodeBuilder bad_operand(context, spec);
    bad_operand.emit(Opcode::GetAiter, 1);
    invalid(bad_operand, CodeErrorKind::Operand);
    CodeBuilder valid(context, spec);
    const auto loop = valid.label(), done = valid.label();
    valid.emit(Opcode::LoadConst, valid.constant(builtin(CoreObject::None)));
    valid.emit(Opcode::GetAiter);
    valid.bind(loop);
    valid.jump(Opcode::ForAwait, done);
    valid.emit(Opcode::PopTop);
    valid.jump(Opcode::Jump, loop);
    valid.bind(done);
    valid.emit(Opcode::Return);
    const auto program = code(valid);
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 2U);
    EXPECT_NE(disassemble(context, program).find("FOR_AWAIT"), std::string::npos);
}

TEST_F(CoreExecution, ExceptionGroupOpcodesUseVerifiedHandlerStacksAndReraiseArity)
{
    for (auto op : {Opcode::CheckEgMatch, Opcode::PrepReraiseStar}) {
        const auto effect = instruction_effect({op, 0});
        EXPECT_EQ(effect.inputs, 2U);
        EXPECT_EQ(effect.pops, 2U);
        EXPECT_EQ(effect.pushes, op == Opcode::CheckEgMatch ? 2U : 1U);
        EXPECT_TRUE(opcode_definition(op).may_raise);
        EXPECT_TRUE(opcode_definition(op).safepoint);
        CodeBuilder outside(context);
        outside.emit(op);
        invalid(outside, CodeErrorKind::RegionStructure);
    }
    EXPECT_EQ(instruction_effect({Opcode::Reraise, 0}).inputs, 0U);
    EXPECT_EQ(instruction_effect({Opcode::Reraise, 1}).inputs, 1U);
    EXPECT_EQ(instruction_effect({Opcode::Reraise, 1}).pops, 1U);
    CodeBuilder arity(context);
    arity.emit(Opcode::Reraise, 2);
    invalid(arity, CodeErrorKind::Operand);
    CodeBuilder missing(context);
    missing.emit(Opcode::Reraise, 1);
    invalid(missing, CodeErrorKind::StackUnderflow);
    CodeBuilder builder(context);
    const auto region = builder.region(RegionKind::Except);
    const auto value = builder.constant(builtin(CoreObject::None));
    builder.enter(region);
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::RaiseInstance);
    builder.leave(region);
    builder.handler(region);
    builder.emit(Opcode::BuildList);
    builder.emit(Opcode::Copy, 2);
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::CheckEgMatch);
    builder.emit(Opcode::PopTop);
    builder.emit(Opcode::ListAppend);
    builder.emit(Opcode::PrepReraiseStar);
    builder.emit(Opcode::Reraise, 1);
    builder.end_handler(region);
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::Return);
    const auto program = code(builder);
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 4U);
    const auto listing = disassemble(context, program);
    EXPECT_NE(listing.find("CHECK_EG_MATCH"), listing.npos);
    EXPECT_NE(listing.find("PREP_RERAISE_STAR"), listing.npos);
}

TEST_F(CoreExecution, TypeAliasOpcodesVerifyOperandsAndStackEffects)
{
    struct Contract {
        Opcode op;
        const char *name;
        OperandDomain domain;
        std::size_t inputs;
    };
    for (const auto &contract :
         {Contract{Opcode::MakeTypeAlias, "MAKE_TYPE_ALIAS", OperandDomain::None, 3},
          Contract{Opcode::MakeTypeVar, "MAKE_TYPE_VAR", OperandDomain::None, 3},
          Contract{Opcode::MakeParamSpec, "MAKE_PARAM_SPEC", OperandDomain::None, 1},
          Contract{Opcode::MakeTypeVarTuple, "MAKE_TYPE_VAR_TUPLE", OperandDomain::None, 1},
          Contract{Opcode::SetFunctionTypeParams, "SET_FUNCTION_TYPE_PARAMS", OperandDomain::None,
                   2},
          Contract{Opcode::MakeGenericBase, "MAKE_GENERIC_BASE", OperandDomain::None, 1},
          Contract{Opcode::LoadFromDictOrGlobals, "LOAD_FROM_DICT_OR_GLOBALS", OperandDomain::Name,
                   1},
          Contract{Opcode::LoadFromDictOrDeref, "LOAD_FROM_DICT_OR_DEREF", OperandDomain::Cell,
                   1}}) {
        SCOPED_TRACE(contract.name);
        const auto &definition = opcode_definition(contract.op);
        EXPECT_EQ(definition.name, contract.name);
        EXPECT_EQ(definition.operand, contract.domain);
        EXPECT_TRUE(definition.may_raise);
        EXPECT_TRUE(definition.safepoint);
        const auto effect = instruction_effect({contract.op, 0});
        EXPECT_EQ(effect.inputs, contract.inputs);
        EXPECT_EQ(effect.pops, contract.inputs);
        EXPECT_EQ(effect.pushes, 1U);
        CodeSpec spec;
        spec.freevars = {U"FreeVar"};
        CodeBuilder underflow(context, spec);
        const auto constant = underflow.constant(builtin(CoreObject::None));
        underflow.name(U"ClassLocal");
        for (std::size_t i = 1; i < contract.inputs; ++i)
            underflow.emit(Opcode::LoadConst, constant);
        underflow.emit(contract.op);
        invalid(underflow, CodeErrorKind::StackUnderflow);
        CodeBuilder bounds(context, spec);
        bounds.emit(contract.op, contract.domain == OperandDomain::Cell ? 1 : 0);
        invalid(bounds, contract.domain == OperandDomain::None   ? CodeErrorKind::StackUnderflow
                        : contract.domain == OperandDomain::Name ? CodeErrorKind::NameBounds
                                                                 : CodeErrorKind::CellBounds);
        if (contract.domain == OperandDomain::None) {
            CodeBuilder operand(context);
            operand.emit(contract.op, 1);
            invalid(operand, CodeErrorKind::Operand);
        }
        CodeBuilder valid(context, spec);
        const auto value = valid.constant(builtin(CoreObject::None));
        valid.name(U"ClassLocal");
        for (std::size_t i = 0; i < contract.inputs; ++i)
            valid.emit(Opcode::LoadConst, value);
        valid.emit(contract.op);
        valid.emit(Opcode::Return);
        const auto program = code(valid);
        EXPECT_NE(disassemble(context, program).find(contract.name), std::string::npos);
        EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), contract.inputs);
    }
}

TEST_F(CoreExecution, TypeVarHelperKeepsLazyEvaluatorsAcrossAllocationFailures)
{
    const auto name = text(U"T");
    const auto bound = integer(42);
    ReturnSlot out(context);
    const Value items[] = {bound.get(context)};
    make_tuple(context, items, out);
    const auto constraints = roots.root(out.value());
    CodeBuilder bound_code(context), constraints_code(context);
    bound_code.emit(Opcode::LoadConst, bound_code.constant(bound));
    bound_code.emit(Opcode::Return);
    constraints_code.emit(Opcode::LoadConst, constraints_code.constant(constraints));
    constraints_code.emit(Opcode::Return);
    const auto bound_function = function(code(bound_code));
    const auto constraints_function = function(code(constraints_code));
    const auto bound_name = text(U"__bound__");
    const auto constraints_name = text(U"__constraints__");
    bool succeeded = false;
    for (std::size_t quota = 0; quota < 256; ++quota) {
        SCOPED_TRACE(quota);
        out.reset();
        context.set_allocation_limit(quota);
        bool exhausted = false;
        try {
            detail::make_typevar(context, name, bound_function, constraints_function, out);
        } catch (const std::bad_alloc &) {
            exhausted = true;
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        context.collect();
        if (!exhausted && out.tag() == ResultTag::Ok) {
            const auto variable = roots.root(out.value());
            for (const auto &entry :
                 {std::pair{bound_name, bound}, std::pair{constraints_name, constraints}}) {
                get_attribute(context, variable, entry.first, out);
                ASSERT_EQ(out.tag(), ResultTag::Ok);
                EXPECT_EQ(out.value(), entry.second.get(context));
                context.collect();
                get_attribute(context, variable, entry.first, out);
                ASSERT_EQ(out.tag(), ResultTag::Ok);
                EXPECT_EQ(out.value(), entry.second.get(context));
            }
            succeeded = true;
            break;
        }
        if (out.tag() == ResultTag::Raised) {
            EXPECT_EQ(context.type_of(out.exception()),
                      context.builtin(CoreObject::MemoryErrorType));
        }
        EXPECT_EQ(interpreter.gc_state().phase, GcPhase::Running);
        call(context, bound_function, {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), bound.get(context));
        call(context, constraints_function, {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), constraints.get(context));
    }
    EXPECT_TRUE(succeeded);
}

TEST_F(CoreExecution, TypeAliasHelperKeepsRootsAcrossAllocationFailures)
{
    const auto name = text(U"Alias");
    const auto parameters = builtin(CoreObject::EmptyTuple);
    const auto value = integer(42);
    CodeBuilder builder(context);
    builder.emit(Opcode::LoadConst, builder.constant(value));
    builder.emit(Opcode::Return);
    const auto compute = function(code(builder));
    const auto value_name = text(U"__value__");
    ReturnSlot out(context);
    bool succeeded = false;
    for (std::size_t quota = 0; quota < 256; ++quota) {
        SCOPED_TRACE(quota);
        out.reset();
        context.set_allocation_limit(quota);
        bool exhausted = false;
        try {
            detail::make_type_alias(context, name, parameters, compute, out);
        } catch (const std::bad_alloc &) {
            exhausted = true;
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        if (!exhausted && out.tag() == ResultTag::Ok) {
            const auto alias = roots.root(out.value());
            context.collect();
            get_attribute(context, alias, value_name, out);
            ASSERT_EQ(out.tag(), ResultTag::Ok);
            EXPECT_EQ(out.value(), value.get(context));
            succeeded = true;
            break;
        }
        if (out.tag() == ResultTag::Raised) {
            EXPECT_EQ(context.type_of(out.exception()),
                      context.builtin(CoreObject::MemoryErrorType));
        }
        context.collect();
        EXPECT_EQ(interpreter.gc_state().phase, GcPhase::Running);
        call(context, compute, {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), value.get(context));
    }
    EXPECT_TRUE(succeeded);
}

TEST_F(CoreExecution, MatchOpcodesVerifyOperandsAndStackEffects)
{
    struct Contract {
        Opcode op;
        const char *name;
        std::size_t inputs, pops, pushes;
        bool may_raise;
    };
    for (const auto &contract :
         {Contract{Opcode::MatchSequence, "MATCH_SEQUENCE", 1, 0, 1, false},
          Contract{Opcode::MatchMapping, "MATCH_MAPPING", 1, 0, 1, false},
          Contract{Opcode::GetLen, "GET_LEN", 1, 0, 1, true},
          Contract{Opcode::MatchKeys, "MATCH_KEYS", 2, 0, 1, true},
          Contract{Opcode::MatchClass, "MATCH_CLASS", 3, 3, 1, true},
          Contract{Opcode::CopyDictWithoutKeys, "COPY_DICT_WITHOUT_KEYS", 2, 2, 1, true}}) {
        const auto &definition = opcode_definition(contract.op);
        EXPECT_EQ(definition.name, contract.name);
        EXPECT_EQ(definition.may_raise, contract.may_raise);
        EXPECT_EQ(definition.safepoint, contract.may_raise);
        const auto effect = instruction_effect({contract.op, 0});
        EXPECT_EQ(effect.inputs, contract.inputs);
        EXPECT_EQ(effect.pops, contract.pops);
        EXPECT_EQ(effect.pushes, contract.pushes);
        CodeBuilder underflow(context);
        const auto value = underflow.constant(builtin(CoreObject::None));
        for (std::size_t i = 1; i < contract.inputs; ++i)
            underflow.emit(Opcode::LoadConst, value);
        underflow.emit(contract.op);
        invalid(underflow, CodeErrorKind::StackUnderflow);
        if (contract.op != Opcode::MatchClass) {
            CodeBuilder bad_operand(context);
            bad_operand.emit(contract.op, 1);
            invalid(bad_operand, CodeErrorKind::Operand);
        }
        CodeBuilder valid(context);
        const auto constant = valid.constant(builtin(CoreObject::None));
        for (std::size_t i = 0; i < contract.inputs; ++i)
            valid.emit(Opcode::LoadConst, constant);
        valid.emit(contract.op, contract.op == Opcode::MatchClass ? 2 : 0);
        for (std::size_t i = 1; i < contract.inputs - contract.pops + contract.pushes; ++i)
            valid.emit(Opcode::PopTop);
        valid.emit(Opcode::Return);
        const auto program = code(valid);
        EXPECT_NE(disassemble(context, program).find(contract.name), std::string::npos);
    }
    for (auto count : {0U, 1U, 42U}) {
        const auto effect = instruction_effect({Opcode::MatchClass, count});
        EXPECT_EQ(effect.inputs, 3U);
        EXPECT_EQ(effect.pops, 3U);
        EXPECT_EQ(effect.pushes, 1U);
    }
}

TEST_F(CoreExecution, PatternHelpersKeepRootsAcrossEveryAllocationFailure)
{
    auto subject = dictionary();
    auto key = text(U"value");
    auto value = integer(42);
    ReturnSlot out(context);
    dict_store(context, subject, key, value, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const Value names[] = {key.get(context)};
    cxxpython::core::make_tuple(context, names, out);
    auto keys = roots.root(out.value());
    auto keywords = builtin(CoreObject::EmptyTuple);
    for (int operation = 0; operation < 3; ++operation) {
        SCOPED_TRACE(operation);
        bool succeeded = false;
        for (std::size_t quota = 0; quota < 256; ++quota) {
            SCOPED_TRACE(quota);
            out.reset();
            context.set_allocation_limit(quota);
            bool exhausted = false;
            try {
                if (operation == 0)
                    detail::match_keys(context, subject, keys, out);
                else if (operation == 1)
                    detail::match_mapping_rest(context, subject, keys, out);
                else
                    detail::match_class(context, value, builtin(CoreObject::IntType), keywords, 1,
                                        out);
            } catch (const std::bad_alloc &) {
                exhausted = true;
            }
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            context.collect();
            if (!exhausted && out.tag() == ResultTag::Ok) {
                succeeded = true;
                if (operation == 1) {
                    EXPECT_TRUE(DictView(context, out.value()).entries().empty());
                } else {
                    const auto &values = context.payload<TupleObject>(out.value()).items;
                    ASSERT_EQ(values.size(), 1U);
                    EXPECT_EQ(values[0], value.get(context));
                }
                break;
            }
            if (out.tag() == ResultTag::Raised) {
                EXPECT_EQ(context.type_of(out.exception()),
                          context.builtin(CoreObject::MemoryErrorType));
            }
            EXPECT_EQ(interpreter.gc_state().phase, GcPhase::Running);
            dict_lookup(context, subject, key, out);
            ASSERT_EQ(out.tag(), ResultTag::Ok);
            EXPECT_EQ(out.value(), value.get(context));
        }
        EXPECT_TRUE(succeeded);
    }
}

TEST_F(CoreExecution, GroupSplitAndProjectionKeepRootsAcrossEveryAllocationFailure)
{
    ReturnSlot out(context), match(context), rest(context), source(context);
    call(context, builtin(CoreObject::ValueErrorType), {}, out);
    auto left = roots.root(out.value());
    call(context, builtin(CoreObject::TypeErrorType), {}, out);
    auto right = roots.root(out.value());
    const Value leaves[] = {left.get(context), right.get(context)};
    cxxpython::core::make_tuple(context, leaves, source);
    const Value arguments[] = {text(U"quota").get(context), source.value()};
    call(context, builtin(CoreObject::BaseExceptionGroupType), {arguments}, out);
    auto original = roots.root(out.value());
    detail::exception_group_match(context, original, builtin(CoreObject::ValueErrorType), match,
                                  rest);
    ASSERT_EQ(match.tag(), ResultTag::Ok);
    ASSERT_EQ(rest.tag(), ResultTag::Ok);
    const Value parts[] = {match.value(), rest.value()};
    make_list(context, parts, source);
    auto results = roots.root(source.value());
    for (bool projection : {false, true}) {
        SCOPED_TRACE(projection);
        bool succeeded = false;
        for (std::size_t quota = 0; quota < 256; ++quota) {
            SCOPED_TRACE(quota);
            out.reset();
            rest.reset();
            context.set_allocation_limit(quota);
            bool exhausted = false;
            try {
                if (projection)
                    detail::prepare_reraise_star(context, original, results, out);
                else
                    detail::exception_group_match(context, original,
                                                  builtin(CoreObject::ValueErrorType), out, rest);
            } catch (const std::bad_alloc &) {
                exhausted = true;
            }
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            context.collect();
            if (!exhausted && out.tag() == ResultTag::Ok) {
                succeeded = true;
                EXPECT_TRUE(context.has_layout<ExceptionObject>(out.value()));
                break;
            }
            if (out.tag() == ResultTag::Raised) {
                EXPECT_EQ(context.type_of(out.exception()),
                          context.builtin(CoreObject::MemoryErrorType));
            }
            EXPECT_EQ(interpreter.gc_state().phase, GcPhase::Running);
            EXPECT_TRUE(context.has_layout<ExceptionObject>(original.get(context)));
        }
        EXPECT_TRUE(succeeded);
    }
    detail::prepare_reraise_star(context, original, results, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto combined = roots.root(out.value());
    context.load(
        combined.get(context),
        context.instance_slot(combined.get(context), detail::exception_group_exceptions_slot),
        source);
    const auto &values = context.payload<TupleObject>(source.value()).items;
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0], left.get(context));
    EXPECT_EQ(values[1], right.get(context));
}

TEST_F(CoreExecution, IterationAndUnpackOpcodesVerifyBothBranchStackEffects)
{
    CodeBuilder empty(context);
    empty.emit(Opcode::ForIter);
    invalid(empty, CodeErrorKind::StackUnderflow);
    CodeBuilder copy(context);
    copy.emit(Opcode::Copy, 0);
    invalid(copy, CodeErrorKind::Operand);
    CodeBuilder unpack(context);
    unpack.emit(Opcode::UnpackSequence, 2);
    invalid(unpack, CodeErrorKind::StackUnderflow);
    const auto item = instruction_effect({Opcode::ForIter, 0});
    const auto done = instruction_effect({Opcode::ForIter, 0}, {}, true);
    EXPECT_EQ(item.inputs, 1U);
    EXPECT_EQ(item.pops, 0U);
    EXPECT_EQ(item.pushes, 1U);
    EXPECT_EQ(done.inputs, 1U);
    EXPECT_EQ(done.pops, 0U);
    EXPECT_EQ(done.pushes, 0U);
    const auto effect = instruction_effect({Opcode::UnpackSequence, 3});
    EXPECT_EQ(effect.inputs, 1U);
    EXPECT_EQ(effect.pops, 1U);
    EXPECT_EQ(effect.pushes, 3U);
    CodeBuilder loop(context);
    const auto again = loop.label(), exhausted = loop.label();
    loop.emit(Opcode::LoadConst, loop.constant(builtin(CoreObject::EmptyTuple)));
    loop.emit(Opcode::GetIter);
    loop.bind(again);
    loop.jump(Opcode::ForIter, exhausted);
    loop.emit(Opcode::PopTop);
    loop.jump(Opcode::Jump, again);
    loop.bind(exhausted);
    loop.emit(Opcode::PopTop);
    loop.emit(Opcode::LoadConst, loop.constant(builtin(CoreObject::None)));
    loop.emit(Opcode::Return);
    const auto program = code(loop);
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 2U);
    EXPECT_NE(disassemble(context, program).find("FOR_ITER"), std::string::npos);
    ReturnSlot out(context);
    call(context, function(program), {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::None));
}

TEST_F(CoreExecution, RaiseOpcodeValidatesArityAndNormalizesExceptionTypesAndCauses)
{
    CodeBuilder missing(context);
    missing.emit(Opcode::Raise, 0);
    invalid(missing, CodeErrorKind::Operand);
    CodeBuilder underflow(context);
    underflow.emit(Opcode::Raise, 2);
    invalid(underflow, CodeErrorKind::StackUnderflow);
    const auto effect = instruction_effect({Opcode::Raise, 2});
    EXPECT_EQ(effect.inputs, 2U);
    EXPECT_EQ(effect.pops, 2U);
    EXPECT_EQ(effect.pushes, 0U);
    CodeBuilder builder(context);
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::ValueErrorType)));
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::TypeErrorType)));
    builder.emit(Opcode::Raise, 2);
    const auto program = code(builder);
    EXPECT_NE(disassemble(context, program).find("RAISE 2"), std::string::npos);
    ReturnSlot out(context);
    call(context, function(program), {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    const auto exception = roots.root(out.exception());
    get_attribute(context, exception, text(U"__cause__"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.type_of(out.value()), context.builtin(CoreObject::TypeErrorType));
}

TEST_F(CoreExecution, MappingCodeKindsAndClassCellLayoutsHaveExplicitVerification)
{
    CodeBuilder fast(context);
    fast.emit(Opcode::LoadName, fast.name(U"value"));
    fast.emit(Opcode::Return);
    invalid(fast, CodeErrorKind::Operand);
    CodeSpec spec;
    spec.kind = CodeKind::ClassBody;
    spec.cellvars = {U"__class__"};
    spec.freevars = {U"__class__"};
    CodeBuilder body(context, spec);
    body.emit(Opcode::LoadClassDeref, 1);
    body.emit(Opcode::Return);
    const auto program = code(body);
    EXPECT_EQ(context.payload<CodeObject>(program).program.kind(), CodeKind::ClassBody);
    EXPECT_NE(disassemble(context, program).find("LOAD_CLASS_DEREF"), std::string::npos);
    CodeBuilder local_cell(context, spec);
    local_cell.emit(Opcode::LoadClassDeref, 0);
    invalid(local_cell, CodeErrorKind::CellBounds);
    spec.locals = {U"hidden"};
    CodeBuilder hidden(context, spec);
    hidden.emit(Opcode::LoadConst, hidden.constant(builtin(CoreObject::None)));
    hidden.emit(Opcode::Return);
    invalid(hidden, CodeErrorKind::Signature);
}

TEST_F(CoreExecution, GlobalLookupUsesLiveGlobalsThenTheFunctionsBuiltinsMapping)
{
    auto globals = dictionary(), builtins = dictionary();
    auto key = text(U"answer");
    auto first = integer(41), second = integer(42);
    ReturnSlot out(context);
    detail::name_store(context, builtins.get(context), key.get(context), first.get(context));
    CodeBuilder builder(context);
    const auto index = builder.name(U"answer");
    EXPECT_EQ(builder.name(U"answer"), index);
    builder.emit(Opcode::LoadGlobal, index);
    builder.emit(Opcode::Return);
    auto program = code(builder);
    FunctionSpec spec{program, globals};
    spec.builtins = builtins;
    make_function(context, spec, out);
    const auto fn = roots.root(out.result().value());
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), first.get(context));
    detail::name_store(context, globals.get(context), key.get(context), second.get(context));
    call(context, fn, {}, out);
    EXPECT_EQ(out.result().value(), second.get(context));
    detail::name_erase(context, globals.get(context), U"answer");
    detail::name_store(context, builtins.get(context), key.get(context), second.get(context));
    call(context, fn, {}, out);
    EXPECT_EQ(out.result().value(), second.get(context));
    detail::name_erase(context, builtins.get(context), U"answer");
    call(context, fn, {}, out);
    error(out, CoreObject::NameErrorType);
    get_attribute(context, fn, text(U"__builtins__"), out);
    EXPECT_EQ(out.result().value(), builtins.get(context));
    get_attribute(context, program, text(U"co_names"), out);
    const auto &names = context.payload<TupleObject>(out.result().value()).items;
    ASSERT_EQ(names.size(), 1U);
    EXPECT_EQ(context.payload<StrObject>(names[0]).value, U"answer");
    EXPECT_NE(disassemble(context, program).find("LOAD_GLOBAL"), std::string::npos);
}

TEST_F(CoreExecution, GlobalStoresAndDeletesDoNotMutateBuiltinsAndTranslateMissingNames)
{
    auto globals = dictionary(), builtins = dictionary();
    auto key = text(U"answer");
    auto value = integer(42);
    ReturnSlot out(context);
    detail::name_store(context, builtins.get(context), key.get(context),
                       context.builtin(CoreObject::IntOne));
    CodeBuilder store(context);
    const auto name = store.name(U"answer");
    store.emit(Opcode::LoadConst, store.constant(value));
    store.emit(Opcode::StoreGlobal, name);
    store.emit(Opcode::LoadGlobal, name);
    store.emit(Opcode::Return);
    auto program = code(store);
    FunctionSpec spec{program, globals};
    spec.builtins = builtins;
    make_function(context, spec, out);
    auto writer = roots.root(out.result().value());
    call(context, writer, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), value.get(context));
    EXPECT_EQ(detail::name_lookup(context, globals.get(context), U"answer"), value.get(context));
    EXPECT_EQ(detail::name_lookup(context, builtins.get(context), U"answer"),
              context.builtin(CoreObject::IntOne));
    CodeBuilder erase(context);
    const auto erased = erase.name(U"answer");
    erase.emit(Opcode::DeleteGlobal, erased);
    erase.emit(Opcode::LoadGlobal, erased);
    erase.emit(Opcode::Return);
    spec.code = code(erase);
    make_function(context, spec, out);
    auto deleter = roots.root(out.result().value());
    call(context, deleter, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::IntOne));
    call(context, deleter, {}, out);
    error(out, CoreObject::NameErrorType);
    context.set_allocation_limit(0);
    call(context, writer, {}, out);
    error(out, CoreObject::MemoryErrorType);
    EXPECT_FALSE(detail::name_lookup(context, globals.get(context), U"answer"));
}

TEST_F(CoreExecution, ImageBuiltinsUseOrdinaryProtocolsAndCalls)
{
    CodeBuilder builder(context);
    builder.emit(Opcode::LoadGlobal, builder.name(U"len"));
    builder.emit(Opcode::LoadConst, builder.constant(text(U"abc")));
    builder.call(1);
    builder.emit(Opcode::Return);
    auto fn = function(code(builder));
    ReturnSlot out(context);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "3");
    EXPECT_EQ(detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"int"),
              context.builtin(CoreObject::IntType));
    EXPECT_EQ(detail::name_lookup(context, context.builtin(CoreObject::Builtins), U"object"),
              context.object_type());
}

TEST_F(CoreExecution, GlobalStoreUsesNoManagedAllocationAndRhsFailurePreservesBinding)
{
    ReturnSlot out(context);
    auto globals = dictionary(), name = text(U"answer"), old = integer(7);
    detail::name_store(context, globals.get(context), name.get(context), old.get(context));
    make_native_function(
        context, "exhaust allocation quota", {},
        [](Attachment &context, Value, std::span<const Value>, ReturnSlot &out) {
            context.set_allocation_limit(0);
            out.ok(context.builtin(CoreObject::IntOne));
        },
        {}, out);
    auto exhaust = roots.root(out.result().value());
    CodeBuilder builder(context);
    builder.emit(Opcode::LoadConst, builder.constant(exhaust));
    builder.call(0);
    builder.emit(Opcode::StoreGlobal, builder.name(U"answer"));
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None)));
    builder.emit(Opcode::Return);
    make_function(context, {code(builder), globals}, out);
    const auto fn = roots.root(out.result().value());
    call(context, fn, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(detail::name_lookup(context, globals.get(context), U"answer"),
              context.builtin(CoreObject::IntOne));
    detail::name_store(context, globals.get(context), name.get(context), old.get(context));
    make_native_function(
        context, "fail before store", {},
        [](Attachment &actor, Value, std::span<const Value>, ReturnSlot &result) {
            actor.set_allocation_limit(0);
            make_str(actor, U"new value", result);
        },
        {}, out);
    const auto failing = roots.root(out.value());
    CodeBuilder failure(context);
    failure.emit(Opcode::LoadConst, failure.constant(failing));
    failure.call(0);
    failure.emit(Opcode::StoreGlobal, failure.name(U"answer"));
    failure.emit(Opcode::LoadConst, failure.constant(builtin(CoreObject::None)));
    failure.emit(Opcode::Return);
    make_function(context, {code(failure), globals}, out);
    call(context, roots.root(out.value()), {}, out);
    error(out, CoreObject::MemoryErrorType);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(detail::name_lookup(context, globals.get(context), U"answer"), old.get(context));
}

TEST_F(CoreExecution, VerificationRejectsUnderflowFallthroughAndGrowingBackEdges)
{
    CodeBuilder underflow(context);
    underflow.emit(Opcode::DupTop);
    invalid(underflow, CodeErrorKind::StackUnderflow);
    CodeBuilder fallthrough(context);
    auto value = fallthrough.constant(integer(1));
    fallthrough.emit(Opcode::LoadConst, value);
    invalid(fallthrough, CodeErrorKind::Fallthrough);
    CodeBuilder grows(context);
    value = grows.constant(integer(1));
    auto start = grows.label();
    grows.bind(start);
    grows.emit(Opcode::LoadConst, value);
    grows.jump(Opcode::Jump, start);
    invalid(grows, CodeErrorKind::StackJoin);
    CodeBuilder infinite(context);
    start = infinite.label();
    infinite.bind(start);
    infinite.jump(Opcode::Jump, start);
    auto valid = code(infinite);
    EXPECT_EQ(context.payload<CodeObject>(valid).program.max_stack(), 0U);
}

TEST_F(CoreExecution, StackMergesMustHaveOneConsistentHeight)
{
    CodeBuilder builder(context);
    auto one = builder.constant(integer(1));
    auto alternative = builder.label();
    auto merge = builder.label();
    builder.emit(Opcode::LoadConst, one);
    builder.jump(Opcode::JumpIfFalse, alternative);
    builder.emit(Opcode::LoadConst, one);
    builder.jump(Opcode::Jump, merge);
    builder.bind(alternative);
    builder.emit(Opcode::LoadConst, one);
    builder.emit(Opcode::LoadConst, one);
    builder.bind(merge);
    builder.emit(Opcode::Return);
    invalid(builder, CodeErrorKind::StackJoin);
}

TEST_F(CoreExecution, LabelsSignaturesAndPositionsAreValidatedBeforePublication)
{
    CodeBuilder first(context), second(context);
    auto label = first.label();
    EXPECT_THROW(second.jump(Opcode::Jump, label), CodeValidationError);
    first.bind(label);
    EXPECT_THROW(first.bind(label), CodeValidationError);
    auto missing = second.label();
    second.jump(Opcode::Jump, missing);
    invalid(second, CodeErrorKind::UnboundLabel);
    CodeSpec spec;
    spec.signature.parameters = {{U"arg"}};
    spec.locals = {U"different"};
    CodeBuilder signature(context, spec);
    signature.emit(Opcode::Jump, 0);
    invalid(signature, CodeErrorKind::Signature);
    spec.signature.parameters = {
        {U"arg", ParameterKind::PositionalOnly, context.builtin(CoreObject::None)}};
    spec.locals = {U"arg"};
    CodeBuilder defaults(context, spec);
    defaults.emit(Opcode::Jump, 0);
    invalid(defaults, CodeErrorKind::Signature);
    CodeBuilder source(context);
    source.emit(Opcode::Jump, 0, {2, 4, 1, 8});
    invalid(source, CodeErrorKind::SourcePosition);
}

TEST_F(CoreExecution, OpcodeMetadataDrivesDisassemblyAndMaximumStack)
{
    CodeBuilder builder(context);
    auto value = builder.constant(integer(8));
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::DupTop);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::Return);
    auto program = code(builder);
    EXPECT_EQ(context.payload<CodeObject>(program).program.max_stack(), 2U);
    EXPECT_EQ(disassemble(context, program), "0 LOAD_CONST 0\n1 DUP_TOP\n2 BINARY 0\n3 RETURN\n");
    EXPECT_TRUE(opcode_definition(Opcode::Binary).safepoint);
    ReturnSlot out(context);
    call(context, function(program), {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "16");
}

TEST_F(CoreExecution, BuilderRootsConstantsAndFunctionsRetainOneActualCodeObject)
{
    CodeBuilder builder(context);
    ReturnSlot out(context);
    make_str(context, U"only builder root", out);
    {
        RootScope temporary(context);
        builder.emit(Opcode::LoadConst, builder.constant(temporary.root(out.result().value())));
    }
    out.reset();
    context.collect();
    builder.emit(Opcode::Return);
    auto program = code(builder);
    auto first = function(program);
    auto second = function(program);
    auto name = text(U"__code__");
    get_attribute(context, first, name, out);
    EXPECT_EQ(out.result().value(), program.get(context));
    get_attribute(context, second, name, out);
    EXPECT_EQ(out.result().value(), program.get(context));
    context.collect();
    call(context, first, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"only builder root");
    EXPECT_EQ(context.type_of(program), context.builtin(CoreObject::CodeType));
}

TEST_F(CoreExecution, PythonFunctionsUseTheExistingBinderDefaultsAndVariadics)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"x", ParameterKind::PositionalOnly},
                                 {U"y", ParameterKind::PositionalOrKeyword},
                                 {U"z", ParameterKind::KeywordOnly}};
    spec.signature.varargs = U"args";
    spec.signature.varkwargs = U"kwargs";
    spec.locals = {U"x", U"y", U"z", U"args", U"kwargs"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    std::array default_items{integer(7).get(context)};
    cxxpython::core::make_tuple(context, default_items, out);
    auto defaults = roots.root(out.result().value());
    auto keywords = dictionary();
    detail::name_store(context, keywords.get(context), text(U"z").get(context),
                       integer(9).get(context));
    auto fn = function(code(builder), defaults, keywords);
    std::array args{integer(1).get(context)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "7");
    call(context, fn, {}, out);
    error(out, CoreObject::TypeErrorType);
    std::array named{KeywordArg{text(U"y").get(context), integer(11).get(context)}};
    call(context, fn, {args, named}, out);
    EXPECT_EQ(int_decimal(context, out.result().value()), "11");
    spec.signature = {{{U"self", ParameterKind::PositionalOnly}}, {}, {}};
    spec.locals = {U"self"};
    CodeBuilder method(context, spec);
    method.emit(Opcode::LoadFast);
    method.emit(Opcode::Return);
    create_type(context, {"Owner"}, out);
    auto cls = roots.root(out.result().value());
    auto name = text(U"method");
    set_attribute(context, cls, name, function(code(method)), out);
    context.allocate_instance<InstanceObject>(cls, out);
    auto self = roots.root(out.result().value());
    get_attribute(context, self, name, out);
    auto bound = roots.root(out.result().value());
    call(context, bound, {}, out);
    EXPECT_EQ(out.result().value(), self.get(context));
}

TEST_F(CoreExecution, BranchLoopAndFastLocalsExecuteThroughObjectOps)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"n"}};
    spec.locals = {U"n", U"i", U"total"};
    CodeBuilder builder(context, spec);
    auto zero = builder.constant(integer(0));
    auto one = builder.constant(integer(1));
    builder.emit(Opcode::LoadConst, zero);
    builder.emit(Opcode::StoreFast, 1);
    builder.emit(Opcode::LoadConst, zero);
    builder.emit(Opcode::StoreFast, 2);
    auto loop = builder.label();
    auto done = builder.label();
    builder.bind(loop);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::LoadFast, 0);
    builder.emit(Opcode::Compare, static_cast<std::uint32_t>(CompareOp::Lt));
    builder.jump(Opcode::JumpIfFalse, done);
    builder.emit(Opcode::LoadFast, 2);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::StoreFast, 2);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::LoadConst, one);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::StoreFast, 1);
    builder.jump(Opcode::Jump, loop);
    builder.bind(done);
    builder.emit(Opcode::LoadFast, 2);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    auto fn = function(code(builder));
    std::array args{integer(100).get(context)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "4950");
    EXPECT_FALSE(context.current_frame());
}

TEST_F(CoreExecution, UnboundIsNotNoneAndManagedFailuresCrossTheCallerSlot)
{
    CodeSpec spec;
    spec.locals = {U"x"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadFast, 0);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    call(context, function(code(builder)), {}, out);
    error(out, CoreObject::UnboundLocalErrorType);
    CodeBuilder deleted(context, spec);
    auto none = deleted.constant(builtin(CoreObject::None));
    deleted.emit(Opcode::LoadConst, none);
    deleted.emit(Opcode::StoreFast);
    deleted.emit(Opcode::DeleteFast);
    deleted.emit(Opcode::LoadFast);
    deleted.emit(Opcode::Return);
    call(context, function(code(deleted)), {}, out);
    error(out, CoreObject::UnboundLocalErrorType);
    CodeBuilder division(context);
    auto one = division.constant(integer(1));
    auto zero = division.constant(integer(0));
    division.emit(Opcode::LoadConst, one);
    division.emit(Opcode::LoadConst, zero);
    division.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::FloorDivide));
    division.emit(Opcode::Return);
    call(context, function(code(division)), {}, out);
    error(out, CoreObject::ZeroDivisionErrorType);
    EXPECT_FALSE(context.current_frame());
}

TEST_F(CoreExecution, RunningFrameSlotsSurviveCallbacksGcAndCompletedFrameObservation)
{
    ReturnSlot out(context);
    context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
    auto observed = roots.root(out.result().value());
    create_type(context, {"Operand"}, out);
    auto cls = roots.root(out.result().value());
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"other"}};
    std::array captures{observed.get(context)};
    make_native_function(
        context, "observe", signature,
        [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
            auto frame = *c.current_frame();
            auto &data = c.payload<FrameObject>(frame);
            EXPECT_EQ(data.state.load(), FrameState::Running);
            EXPECT_EQ(data.sp, 2U);
            EXPECT_EQ(data.operands[0].boxed(), args[0]);
            auto *address = data.operands.data();
            write_cell(c, c.payload<NativeFunctionObject>(fn).captures[0], frame);
            c.collect();
            EXPECT_EQ(data.operands.data(), address);
            EXPECT_EQ(data.locals[0], args[0]);
            RootScope roots(c);
            auto self = roots.root(frame);
            make_str(c, U"f_lineno", out);
            get_attribute(c, self, roots.root(out.result().value()), out);
            EXPECT_EQ(int_decimal(c, out.result().value()), "41");
            out.ok(args[1]);
        },
        captures, out);
    auto native = roots.root(out.result().value());
    set_attribute(context, cls, text(U"__add__"), native, out);
    context.allocate_instance<InstanceObject>(cls, out);
    auto self = roots.root(out.result().value());
    CodeSpec spec;
    spec.signature.parameters = {{U"x"}};
    spec.locals = {U"x"};
    CodeBuilder builder(context, spec);
    auto one = builder.constant(integer(1));
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::LoadConst, one);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add), {41, 2, 41, 7});
    builder.emit(Opcode::Return);
    auto program = code(builder);
    std::array args{self.get(context)};
    call(context, function(program), {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    ReturnSlot snapshot(context);
    ASSERT_TRUE(read_cell(context, observed.get(context), snapshot));
    auto frame = roots.root(snapshot.result().value());
    auto &data = context.payload<FrameObject>(frame);
    EXPECT_EQ(data.state.load(), FrameState::Completed);
    EXPECT_EQ(data.sp, 0U);
    EXPECT_EQ(data.locals[0], self.get(context));
    for (auto value : data.operands)
        EXPECT_EQ(value.boxed(), context.builtin(CoreObject::None));
    get_attribute(context, frame, text(U"f_code"), out);
    EXPECT_EQ(out.result().value(), program.get(context));
    get_attribute(context, frame, text(U"f_globals"), out);
    EXPECT_EQ(out.result().value(), context.payload<FunctionObject>(data.function).globals);
    get_attribute(context, frame, text(U"f_builtins"), out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::Builtins));
    EXPECT_FALSE(context.current_frame());
}

TEST_F(CoreExecution, CreatedOrdinaryFrameRetainsExecutionOwnershipBeforeFirstInstruction)
{
    CodeSpec spec;
    spec.locals = {U"kept"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadConst, builder.constant(builtin(CoreObject::None)));
    builder.emit(Opcode::Return);
    const auto program = code(builder), callable = function(program);
    ReturnSlot out(context);
    context.allocate_instance<FrameObject>(builtin(CoreObject::FrameType), out,
                                           callable.get(context), program.get(context), 1, 1,
                                           context.builtin(CoreObject::None));
    const auto frame = roots.root(out.value());
    auto &data = context.payload<FrameObject>(frame);
    data.locals[0] = context.builtin(CoreObject::True);
    get_attribute(context, frame, text(U"clear"), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    call(context, roots.root(out.value()), {}, out);
    error(out, CoreObject::RuntimeErrorType);
    EXPECT_EQ(data.state.load(), FrameState::Created);
    EXPECT_EQ(data.locals[0], context.builtin(CoreObject::True));
}

TEST_F(CoreExecution, FrameAllocationFailureLeavesNoCurrentFrameAndCanRetry)
{
    CodeBuilder builder(context);
    auto value = builder.constant(integer(3));
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::Return);
    auto fn = function(code(builder));
    ReturnSlot out(context);
    context.set_allocation_limit(0);
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Raised);
    EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
    EXPECT_FALSE(context.current_frame());
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    call(context, fn, {}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.result().value()), "3");
}

TEST_F(CoreExecution, EmptyAndAbsentDefaultsAreDistinctAndVariadicResultsAreTransferred)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"x", ParameterKind::PositionalOnly}};
    spec.signature.varargs = U"args";
    spec.signature.varkwargs = U"kwargs";
    spec.locals = {U"x", U"args", U"kwargs"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::Return);
    auto program = code(builder);
    auto empty = builtin(CoreObject::EmptyTuple);
    auto fn = function(program, empty);
    auto absent = function(program);
    ReturnSlot out(context);
    auto name = text(U"__defaults__");
    get_attribute(context, fn, name, out);
    EXPECT_EQ(out.result().value(), empty.get(context));
    get_attribute(context, absent, name, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
    std::array args{integer(1).get(context), integer(2).get(context)};
    call(context, fn, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    context.collect();
    auto &extra = context.payload<TupleObject>(out.result().value()).items;
    ASSERT_EQ(extra.size(), 1U);
    EXPECT_EQ(extra[0], args[1]);
    CodeBuilder keywords(context, spec);
    keywords.emit(Opcode::LoadFast, 2);
    keywords.emit(Opcode::Return);
    fn = function(code(keywords));
    std::array named{KeywordArg{text(U"x").get(context), integer(3).get(context)}};
    call(context, fn, {args, named}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    context.collect();
    EXPECT_EQ(detail::name_lookup(context, out.result().value(), U"x"), named[0].value);
    set_attribute(context, fn, text(U"custom"), integer(4), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    get_attribute(context, fn, text(U"custom"), out);
    EXPECT_EQ(int_decimal(context, out.result().value()), "4");
}

TEST_F(CoreExecution, BytecodeFunctionsParticipateInImplicitSpecialMethods)
{
    CodeSpec spec;
    spec.signature.parameters = {{U"self"}, {U"other"}};
    spec.locals = {U"self", U"other"};
    CodeBuilder builder(context, spec);
    builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::Return);
    ReturnSlot out(context);
    create_type(context, {"PythonAdder"}, out);
    auto cls = roots.root(out.result().value());
    set_attribute(context, cls, text(U"__add__"), function(code(builder)), out);
    context.allocate_instance<InstanceObject>(cls, out);
    auto obj = roots.root(out.result().value());
    auto expected = integer(9);
    binary(context, obj, expected, BinaryOp::Add, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(out.result().value(), expected.get(context));
    EXPECT_FALSE(context.current_frame());
}

TEST_F(CoreExecution, FreezeAllocationFailuresDoNotConsumeTheBuilderOrPublishPartialCode)
{
    CodeBuilder builder(context);
    auto value = builder.constant(integer(10));
    builder.emit(Opcode::LoadConst, value);
    builder.emit(Opcode::Return);
    context.collect();
    const auto before = context.stats().objects;
    ReturnSlot out(context);
    builder.freeze(out);
    auto reference = roots.root(out.result().value());
    (void)reference;
    context.collect();
    const auto allocations = context.stats().objects - before;
    out.reset();
    const auto baseline = context.stats().objects;
    for (std::size_t limit = 0; limit < allocations; ++limit) {
        context.set_allocation_limit(limit);
        EXPECT_THROW(builder.freeze(out), std::bad_alloc) << limit;
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        context.collect();
        EXPECT_EQ(context.stats().objects, baseline);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_NO_THROW(builder.freeze(out));
        EXPECT_EQ(out.result().tag(), ResultTag::Ok);
        out.reset();
        context.collect();
        EXPECT_EQ(context.stats().objects, baseline);
    }
}

TEST_F(CoreExecution, NativeReentryRestoresFramesAndHostFaultsUnwindThem)
{
    ReturnSlot out(context);
    context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
    auto outer_cell = roots.root(out.result().value());
    context.allocate_instance<CellObject>(builtin(CoreObject::CellType), out);
    auto inner_cell = roots.root(out.result().value());
    CodeSpec spec;
    spec.signature.parameters = {{U"x"}};
    spec.locals = {U"x"};
    CodeBuilder builder(context, spec);
    auto one = builder.constant(integer(1));
    builder.emit(Opcode::LoadFast);
    builder.emit(Opcode::LoadConst, one);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
    builder.emit(Opcode::Return);
    auto program = code(builder);
    auto inner = function(program);
    auto outer = function(program);
    create_type(context, {"Reentrant"}, out);
    auto cls = roots.root(out.result().value());
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"other"}};
    std::array captures{outer_cell.get(context), inner_cell.get(context), inner.get(context)};
    make_native_function(
        context, "reenter", signature,
        [](Attachment &c, Value fn, std::span<const Value> args, ReturnSlot &out) {
            const auto &captures = c.payload<NativeFunctionObject>(fn).captures;
            const auto frame = *c.current_frame();
            ReturnSlot back(c);
            c.load(frame, c.payload<FrameObject>(frame).back, back);
            if (back.tag() == ResultTag::Ok) {
                write_cell(c, captures[1], frame);
                c.collect();
                out.ok(args[1]);
                return;
            }
            write_cell(c, captures[0], frame);
            RootScope roots(c);
            std::array nested{args[0]};
            call(c, roots.root(captures[2]), {nested, {}}, out);
            EXPECT_EQ(c.current_frame(), frame);
            c.collect();
        },
        captures, out);
    auto observer = roots.root(out.result().value());
    auto name = text(U"__add__");
    set_attribute(context, cls, name, observer, out);
    context.allocate_instance<InstanceObject>(cls, out);
    auto self = roots.root(out.result().value());
    std::array args{self.get(context)};
    call(context, outer, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    ReturnSlot outer_snapshot(context), inner_snapshot(context);
    ASSERT_TRUE(read_cell(context, outer_cell.get(context), outer_snapshot));
    ASSERT_TRUE(read_cell(context, inner_cell.get(context), inner_snapshot));
    auto outer_frame = outer_snapshot.result().value();
    auto inner_frame = inner_snapshot.result().value();
    ReturnSlot back(context);
    context.load(inner_frame, context.payload<FrameObject>(inner_frame).back, back);
    ASSERT_EQ(back.tag(), ResultTag::Ok);
    EXPECT_EQ(back.value(), outer_frame);
    EXPECT_FALSE(context.current_frame());
    make_native_function(
        context, "host fault", signature,
        [](Attachment &c, Value fn, std::span<const Value>, ReturnSlot &) {
            write_cell(c, c.payload<NativeFunctionObject>(fn).captures[0], *c.current_frame());
            throw std::runtime_error("native host fault");
        },
        captures, out);
    set_attribute(context, cls, name, roots.root(out.result().value()), out);
    EXPECT_THROW(call(context, outer, {args, {}}, out), std::runtime_error);
    EXPECT_EQ(out.result().tag(), ResultTag::Empty);
    EXPECT_FALSE(context.current_frame());
    ASSERT_TRUE(read_cell(context, outer_cell.get(context), outer_snapshot));
    const auto &failed = context.payload<FrameObject>(outer_snapshot.result().value());
    EXPECT_EQ(failed.state.load(), FrameState::Completed);
    EXPECT_EQ(failed.sp, 0U);
    set_attribute(context, cls, name, observer, out);
    call(context, outer, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_FALSE(context.current_frame());
}

TEST(CoreExecutionLifecycle, FunctionGlobalsCodeCyclesAreCollected)
{
    Interpreter interpreter({.collect_every_allocation = true});
    Attachment context(interpreter);
    context.bootstrap_core();
    context.collect();
    const auto before = context.stats().objects;
    {
        RootScope roots(context);
        ReturnSlot out(context);
        CodeBuilder builder(context);
        auto none = roots.root(context.builtin(CoreObject::None));
        builder.emit(Opcode::LoadConst, builder.constant(none));
        builder.emit(Opcode::Return);
        builder.freeze(out);
        auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        auto globals = roots.root(out.result().value());
        make_function(context, {code, globals}, out);
        auto fn = roots.root(out.result().value());
        make_str(context, U"function", out);
        detail::name_store(context, globals.get(context), out.result().value(), fn.get(context));
        call(context, fn, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    }
    context.collect();
    EXPECT_EQ(context.stats().objects, before);
}
} // namespace
