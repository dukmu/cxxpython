#include "cxxpython/compiler.hpp"
#include "cxxpython/setobject.hpp"

#include <array>
#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

TEST(ParallelSets, SharedAddsAndPopRetainEachKeyDuringGc)
{
    constexpr std::size_t count = 300;
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot result(owner);
    make_set(owner, {}, result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    const auto set = roots.root(result.value());
    std::array<std::future<void>, 2> workers;
    std::array tokens{owner.make_transfer(set), owner.make_transfer(set)};
    std::latch ready(2), start(1);
    {
        NativeWait parked(owner);
        for (std::size_t i = 0; i < workers.size(); ++i)
            workers[i] =
                std::async(std::launch::async, [&, token = std::move(tokens[i])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope roots(context);
                        ReturnSlot target(context), key(context), out(context);
                        context.receive(token, target);
                        const auto set = roots.root(target.value());
                        announced = true;
                        ready.count_down();
                        start.wait();
                        for (std::size_t n = 0; n < count; ++n) {
                            make_int(context, n, key);
                            RootScope iteration(context);
                            set_add(context, set, iteration.root(key.value()), out);
                            if (out.tag() != ResultTag::Ok)
                                throw std::runtime_error("shared set add failed");
                            if (n % 29 == 0)
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
    EXPECT_EQ(detail::dictionary_size(owner, owner.payload<SetObject>(set).table), count);
    make_str(owner, U"pop", result);
    get_attribute(owner, set, roots.root(result.value()), result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    const auto pop = roots.root(result.value());
    std::array<std::atomic<unsigned>, count> seen{};
    std::array pop_tokens{owner.make_transfer(pop), owner.make_transfer(pop)};
    std::latch pop_ready(2), pop_start(1);
    {
        NativeWait parked(owner);
        for (std::size_t i = 0; i < workers.size(); ++i)
            workers[i] =
                std::async(std::launch::async, [&, token = std::move(pop_tokens[i])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope roots(context);
                        ReturnSlot method(context), out(context);
                        context.receive(token, method);
                        const auto pop = roots.root(method.value());
                        announced = true;
                        pop_ready.count_down();
                        pop_start.wait();
                        for (;;) {
                            call(context, pop, {}, out);
                            if (out.tag() == ResultTag::Raised) {
                                if (context.type_of(out.exception()) !=
                                    context.builtin(CoreObject::KeyErrorType))
                                    throw std::runtime_error("shared set pop failed");
                                break;
                            }
                            std::int64_t n;
                            if (!int_fits_signed(context, out.value(), n) || n < 0 ||
                                n >= static_cast<std::int64_t>(count))
                                throw std::runtime_error("invalid popped key");
                            ++seen[n];
                            if (n % 31 == 0)
                                context.collect();
                        }
                    } catch (...) {
                        if (!announced)
                            pop_ready.count_down();
                        throw;
                    }
                });
        pop_ready.wait();
        EXPECT_EQ(interpreter.gc_state().active, 2U);
        pop_start.count_down();
        for (auto &worker : workers)
            EXPECT_NO_THROW(worker.get());
    }
    for (const auto &visits : seen)
        EXPECT_EQ(visits.load(), 1U);
    EXPECT_EQ(detail::dictionary_size(owner, owner.payload<SetObject>(set).table), 0U);
    owner.collect();
}
} // namespace
