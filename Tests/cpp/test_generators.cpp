#include "cxxpython/compiler.hpp"
#include "cxxpython/genobject.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class GeneratorProtocol : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};

    void SetUp() override
    {
        context.bootstrap_core();
    }
};

TEST_F(GeneratorProtocol, HostFaultsAndManagedAllocationFailureCompleteTheExecutionClaim)
{
    for (bool host_fault : {false, true}) {
        SCOPED_TRACE(host_fault);
        ReturnSlot out(context);
        NativeEntry entry =
            host_fault ? +[](Attachment &, Handle, std::span<const Value>,
                             ReturnSlot &) { throw std::runtime_error("generator host fault"); }
                       : +[](Attachment &context, Handle, std::span<const Value>, ReturnSlot &out) {
                             context.set_allocation_limit(0);
                             out.ok(context.builtin(CoreObject::None));
                         };
        make_native_function(context, "fail in generator", {}, entry, {}, out);
        const auto fault = roots.root(out.value());
        CodeSpec spec;
        spec.generator = true;
        CodeBuilder builder(context, spec);
        builder.emit(Opcode::LoadConst, builder.constant(fault));
        builder.call(0);
        builder.emit(Opcode::PopTop);
        builder.emit(Opcode::LoadConst,
                     builder.constant(roots.root(context.builtin(CoreObject::EmptyTuple))));
        builder.emit(Opcode::GetIter);
        builder.emit(Opcode::YieldFrom);
        builder.emit(Opcode::Return);
        builder.freeze(out);
        const auto code = roots.root(out.value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        const auto globals = roots.root(out.value());
        make_function(context, {code, globals}, out);
        call(context, roots.root(out.value()), {}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto generator = roots.root(out.value());
        context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame,
                     out);
        const auto frame = roots.root(out.value());
        if (host_fault) {
            EXPECT_THROW(iter_next(context, generator, out), std::runtime_error);
        } else {
            EXPECT_EQ(iter_next(context, generator, out), NextStatus::Raised);
            ASSERT_EQ(out.tag(), ResultTag::Raised);
            EXPECT_EQ(out.exception(), context.builtin(CoreObject::MemoryError));
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        EXPECT_EQ(context.payload<FrameObject>(frame).state.load(), FrameState::Completed);
        EXPECT_FALSE(context.current_frame());
        EXPECT_FALSE(context.handled_exception());
        context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame,
                     out);
        EXPECT_EQ(out.tag(), ResultTag::Empty);
        context.collect();
    }
}

TEST_F(GeneratorProtocol, FailedDelegationDropsItsIteratorBeforeYieldingFromTheHandler)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source{SourceBuffer("class Delegate:\n"
                                   "    def __iter__(self): return self\n"
                                   "    def __next__(self): return 1\n"
                                   "    def send(self, value): return value\n"
                                   "def f():\n"
                                   "    try:\n"
                                   "        yield from Delegate()\n"
                                   "    except MemoryError:\n"
                                   "        yield 'caught'\n"
                                   "g = f()\n")};
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    SourceUnit expression{SourceBuffer("g"), InputMode::Eval};
    run(context, {&expression, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto generator = roots.root(out.value());
    ASSERT_EQ(iter_next(context, generator, out), NextStatus::Item);
    make_str(context, U"send", out);
    get_attribute(context, generator, roots.root(out.value()), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto send = roots.root(out.value());
    context.load(generator.get(context), context.payload<GeneratorObject>(generator).frame, out);
    const auto frame = roots.root(out.value());
    const Value args[] = {context.builtin(CoreObject::IntOne)};
    context.set_allocation_limit(0);
    call(context, send, {args}, out);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"caught");
    context.load(frame.get(context), context.payload<FrameObject>(frame).delegate, out);
    EXPECT_EQ(out.tag(), ResultTag::Empty);
}

} // namespace
