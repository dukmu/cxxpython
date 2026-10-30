#include "internal/errors.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <new>

namespace {
using namespace cxxpython::core;
using namespace cxxpython::core::detail;

void allocation_failure(Attachment &, Handle, std::span<const Value>, ReturnSlot &)
{
    throw std::bad_alloc();
}

class ErrorHooks : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx{interpreter};
    RootScope roots{ctx};
    Handle sys;
    void SetUp() override
    {
        ctx.bootstrap_core();
        ReturnSlot out(ctx);
        import_module(ctx, U"sys", out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        sys = roots.root(out.value());
    }
    Handle text(std::u32string s)
    {
        ReturnSlot out(ctx);
        make_str(ctx, std::move(s), out);
        return roots.root(out.value());
    }
    Handle get(Handle object, std::u32string name)
    {
        ReturnSlot out(ctx);
        get_attribute(ctx, object, text(std::move(name)), out);
        EXPECT_EQ(out.tag(), ResultTag::Ok);
        return roots.root(out.value());
    }
    void set(Handle object, std::u32string name, Handle value)
    {
        ReturnSlot out(ctx);
        set_attribute(ctx, object, text(std::move(name)), value, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
    Handle native(const char *name, NativeEntry entry)
    {
        SignatureLayout sig;
        sig.parameters.push_back({U"first", ParameterKind::PositionalOnly});
        ReturnSlot out(ctx);
        make_native_function(ctx, name, sig, entry, {}, out);
        return roots.root(out.value());
    }
    Handle exception()
    {
        CodeSpec spec;
        spec.name = U"leaf";
        spec.first_line = 42;
        CodeBuilder builder(ctx, spec);
        ReturnSlot out(ctx);
        raise_error(ctx, CoreObject::ValueErrorType, U"original", out);
        auto error = roots.root(out.exception());
        builder.emit(Opcode::LoadConst, builder.constant(error));
        builder.emit(Opcode::RaiseInstance);
        builder.freeze(out);
        auto code = roots.root(out.value());
        ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), out);
        auto globals = roots.root(out.value());
        make_function(ctx, {code, globals}, out);
        auto function = roots.root(out.value());
        call(ctx, function, {}, out);
        EXPECT_EQ(out.tag(), ResultTag::Raised);
        return roots.root(out.exception());
    }
};

TEST_F(ErrorHooks, UnraisableDoesNotEscapeWhenManagedAllocationIsExhausted)
{
    auto error = exception();
    ctx.set_allocation_limit(0);
    EXPECT_NO_THROW(write_unraisable(ctx, error, {}));
    ctx.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(get(error, U"__traceback__").get(ctx) == ctx.builtin(CoreObject::None), false);
}

TEST_F(ErrorHooks, UnraisableDoesNotEscapeHostAllocationFailure)
{
    auto error = exception();
    set(sys, U"unraisablehook", native("allocation_failure", allocation_failure));
    EXPECT_NO_THROW(write_unraisable(ctx, error, {}));
}
} // namespace
