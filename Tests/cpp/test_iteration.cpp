#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

#include <future>
#include <latch>

namespace {
using namespace cxxpython::core;
class CoreIteration : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    void eval(std::string source, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(std::move(source)), InputMode::Eval);
        run(context, {&unit}, out);
    }
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.value());
    }
};

TEST_F(CoreIteration, NativeSequencesAreLazyAndExhaustionRemainsDistinctFromFailure)
{
    ReturnSlot out(context);
    for (auto source : {"iter(range(3))", "iter((0, 1, 2))"}) {
        eval(source, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto iterator = roots.root(out.value());
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Item);
            EXPECT_EQ(int_decimal(context, out.value()), std::to_string(i));
            context.collect();
        }
        EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Done);
        EXPECT_TRUE(is_subtype(context, context.type_of(out.exception()),
                               context.builtin(CoreObject::StopIterationType)));
        EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Done);
    }
    eval("iter('a\\U0001f600')", out);
    const auto iterator = roots.root(out.value());
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Item);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"a");
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Item);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"\U0001f600");
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Done);
    eval("iter(range(3))", out);
    const auto retry = roots.root(out.value());
    context.set_allocation_limit(0);
    EXPECT_EQ(iter_next(context, retry, out), NextStatus::Item);
    EXPECT_EQ(int_decimal(context, out.value()), "0");
    EXPECT_EQ(iter_next(context, retry, out), NextStatus::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::MemoryErrorType));
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(iter_next(context, retry, out), NextStatus::Item);
    EXPECT_EQ(int_decimal(context, out.value()), "1");
}

TEST_F(CoreIteration, UserProtocolsPreserveStopIterationValuesAndOtherExceptions)
{
    ReturnSlot out(context);
    create_type(context, {U"Iterator"}, out);
    const auto type = roots.root(out.value());
    context.allocate_instance<InstanceObject>(type, out);
    const auto iterator = roots.root(out.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    make_native_function(
        context, "self", unary,
        [](Attachment &, Handle, std::span<const Value> args, ReturnSlot &out) { out.ok(args[0]); },
        {}, out);
    set_attribute(context, type, text(U"__iter__"), roots.root(out.value()), out);
    eval("StopIteration(73)", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto stop = roots.root(out.value());
    std::array captures{stop.get(context)};
    make_native_function(
        context, "stop", unary,
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out) {
            c.collect();
            out.raised(c.payload<NativeFunctionObject>(fn).captures[0]);
        },
        captures, out);
    set_attribute(context, type, text(U"__next__"), roots.root(out.value()), out);
    get_iter(context, iterator, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), iterator.get(context));
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Done);
    EXPECT_EQ(out.exception(), stop.get(context));
    get_attribute(context, stop, text(U"value"), out);
    EXPECT_EQ(int_decimal(context, out.value()), "73");
    make_native_function(
        context, "error", unary,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &out) {
            raise_error(c, CoreObject::ValueErrorType, U"not exhaustion", out);
        },
        {}, out);
    set_attribute(context, type, text(U"__next__"), roots.root(out.value()), out);
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    set_attribute(context, type, text(U"__next__"), roots.root(context.builtin(CoreObject::None)),
                  out);
    get_iter(context, iterator, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(iter_next(context, iterator, out), NextStatus::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
}

TEST_F(CoreIteration, ValueIteratorSurvivesAliasedOutputAndSeesNextReplacement)
{
    ReturnSlot out(context);
    create_type(context, {U"Iterator"}, out);
    const auto type = roots.root(out.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    make_native_function(
        context, "self", unary,
        [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &result) {
            c.collect();
            result.ok(args[0]);
        },
        {}, out);
    auto self = roots.root(out.value());
    set_attribute(context, type, text(U"__iter__"), self, out);
    set_attribute(context, type, text(U"__next__"), self, out);
    context.allocate_instance<InstanceObject>(type, out);
    get_iter(context, out.value(), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.type_of(out.value()), type.get(context));
    EXPECT_EQ(iter_next(context, out.value(), out), NextStatus::Item);
    EXPECT_EQ(context.type_of(out.value()), type.get(context));

    // Keep the sole iterator root in out while changing the class and its cached slot.
    ReturnSlot update(context);
    make_native_function(
        context, "stop", unary,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
            c.collect();
            raise_error(c, CoreObject::StopIterationType, U"finished", result);
        },
        {}, update);
    set_attribute(context, type, text(U"__next__"), roots.root(update.value()), update);
    EXPECT_EQ(iter_next(context, out.value(), out), NextStatus::Done);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::StopIterationType));

    make_native_function(
        context, "error", unary,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
            c.collect();
            raise_error(c, CoreObject::ValueErrorType, U"failure", result);
        },
        {}, update);
    set_attribute(context, type, text(U"__next__"), roots.root(update.value()), update);
    context.allocate_instance<InstanceObject>(type, out);
    EXPECT_EQ(iter_next(context, out.value(), out), NextStatus::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
}

TEST(ParallelIteration, SharedBuiltinIteratorClaimsEachItemOnceDuringGc)
{
    constexpr std::size_t count = 700;
    for (int kind : {0, 1, 2, 3, 4, 5, 6}) {
        const auto expected = kind == 4 ? std::size_t{256} : count;
        Interpreter interpreter;
        Attachment owner(interpreter);
        owner.bootstrap_core();
        RootScope roots(owner);
        ReturnSlot out(owner);
        if (kind == 0) {
            SourceUnit unit(SourceBuffer("iter(range(700))"), InputMode::Eval);
            run(owner, {&unit}, out);
        } else if (kind == 5 || kind == 6) {
            SourceUnit unit(
                SourceBuffer(kind == 5 ? "iter(set(range(700)))" : "iter(frozenset(range(700)))"),
                InputMode::Eval);
            run(owner, {&unit}, out);
        } else if (kind == 4) {
            std::string values;
            for (std::size_t i = 0; i < expected; ++i)
                values += static_cast<char>(i);
            make_bytes(owner, std::move(values), out);
            get_iter(owner, roots.root(out.value()), out);
        } else {
            if (kind == 1 || kind == 3) {
                std::vector<Value> values;
                for (std::size_t i = 0; i < count; ++i) {
                    make_int(owner, i, out);
                    values.push_back(roots.root(out.value()).get(owner));
                }
                if (kind == 1)
                    cxxpython::core::make_tuple(owner, values, out);
                else
                    make_list(owner, values, out);
            } else {
                std::u32string values;
                for (std::size_t i = 0; i < count; ++i)
                    values += static_cast<char32_t>(0x1000 + i);
                make_str(owner, std::move(values), out);
            }
            get_iter(owner, roots.root(out.value()), out);
        }
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto iterator = roots.root(out.value());
        std::array<std::atomic<unsigned>, count> seen{};
        std::array tokens{owner.make_transfer(iterator), owner.make_transfer(iterator)};
        std::latch ready(2), start(1);
        std::array<std::future<void>, 2> workers;
        {
            NativeWait wait(owner);
            for (std::size_t i = 0; i < workers.size(); ++i)
                workers[i] =
                    std::async(std::launch::async, [&, token = std::move(tokens[i])]() mutable {
                        bool announced = false;
                        try {
                            Attachment context(interpreter);
                            RootScope roots(context);
                            ReturnSlot input(context), result(context);
                            context.receive(token, input);
                            const auto iterator = roots.root(input.value());
                            announced = true;
                            ready.count_down();
                            start.wait();
                            for (;;) {
                                const auto status = iter_next(context, iterator, result);
                                if (status == NextStatus::Done)
                                    break;
                                if (status == NextStatus::Raised)
                                    throw std::runtime_error("parallel iteration failed");
                                std::int64_t index;
                                if (kind == 2)
                                    index = context.payload<StrObject>(result.value()).value[0] -
                                            0x1000;
                                else if (!int_fits_signed(context, result.value(), index))
                                    throw std::runtime_error("invalid item");
                                if (index < 0 || index >= static_cast<std::int64_t>(expected))
                                    throw std::runtime_error("item bounds");
                                ++seen[index];
                                if (index % 37 == 0)
                                    context.collect();
                            }
                        } catch (...) {
                            if (!announced)
                                ready.count_down();
                            throw;
                        }
                    });
            ready.wait();
            EXPECT_EQ(interpreter.gc_state().active, 2U);
            start.count_down();
            for (auto &worker : workers)
                EXPECT_NO_THROW(worker.get());
        }
        for (std::size_t i = 0; i < seen.size(); ++i)
            EXPECT_EQ(seen[i].load(), i < expected ? 1U : 0U);
        owner.collect();
    }
}
} // namespace
