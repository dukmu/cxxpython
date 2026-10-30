#include "cxxpython/objects.hpp"
#include "cxxpython/type.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

TEST(AbcParallel, DirectSubclassWeakSnapshotsSurviveConcurrentCreationAndCollection)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot value(owner);
    create_type(owner, TypeSpec{"SharedBase"}, value);
    ASSERT_EQ(value.tag(), ResultTag::Ok);
    auto base = roots.root(value.value());
    std::array transfers{owner.make_transfer(base), owner.make_transfer(base)};
    std::array<std::future<bool>, 2> workers;
    std::latch ready(2), start(1);
    {
        NativeWait parked(owner);
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i] =
                std::async(std::launch::async, [&, input = std::move(transfers[i])]() mutable {
                    Attachment context(interpreter);
                    RootScope local(context);
                    ReturnSlot received(context), created(context), subclasses(context);
                    context.receive(input, received);
                    auto parent = local.root(received.value());
                    {
                        NativeWait wait(context);
                        ready.count_down();
                        start.wait();
                    }
                    for (int n = 0; n < 20; ++n) {
                        RootScope iteration(context);
                        const std::array bases{parent};
                        TypeSpec spec{"ConcurrentChild", bases};
                        create_type(context, spec, created);
                        if (created.tag() != ResultTag::Ok)
                            return false;
                        auto child = iteration.root(created.value());
                        detail::direct_subclasses(context, parent, subclasses);
                        std::vector<Value> live;
                        detail::list_snapshot(context, iteration.root(subclasses.value()),
                                              iteration, live);
                        if (std::find(live.begin(), live.end(), child.get(context)) == live.end())
                            return false;
                        if (n % 5 == 0)
                            context.collect();
                        created.reset();
                        subclasses.reset();
                    }
                    return true;
                });
        }
        ready.wait();
        start.count_down();
        for (auto &worker : workers)
            EXPECT_TRUE(worker.get());
    }
    owner.collect();
    detail::direct_subclasses(owner, base, value);
    std::vector<Value> remaining;
    detail::list_snapshot(owner, roots.root(value.value()), roots, remaining);
    EXPECT_TRUE(remaining.empty());
}
} // namespace
