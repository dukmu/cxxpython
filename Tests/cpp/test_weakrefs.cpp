#include "cxxpython/compiler.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "cxxpython/weakrefobject.hpp"

#include <array>
#include <future>
#include <gtest/gtest.h>
#include <latch>
#include <stdexcept>

namespace {
using namespace cxxpython::core;

class WeakRefs : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    std::optional<Handle> globals;

    void SetUp() override
    {
        context.bootstrap_core();
        ReturnSlot module(context), result(context), key(context), dictionary(context);
        make_module(context, U"_weakref", module);
        auto weakref = roots.root(module.value());
        detail::initialize_weakref(context, weakref, result);
        ASSERT_EQ(result.tag(), ResultTag::Ok);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              dictionary);
        globals = roots.root(dictionary.value());
        make_str(context, U"_weakref", key);
        dict_store(context, *globals, roots.root(key.value()), weakref, result);
        ASSERT_EQ(result.tag(), ResultTag::Ok);
    }

    void exec(const char *source, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(source, "weakref_test.py"));
        run(context, {&unit, *globals}, out);
    }

    void eval(const char *source, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(source, "weakref_test.py"), InputMode::Eval);
        run(context, {&unit, *globals}, out);
    }
};

TEST_F(WeakRefs, SharedRefIsReusedAcrossAttachments)
{
    ReturnSlot out(context);
    exec("class Target: pass\ntarget = Target()\nref = _weakref.ref(target)\n", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("(_weakref.ref, target, ref)", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto bundle = roots.root(out.value());
    std::array transfers{context.make_transfer(bundle), context.make_transfer(bundle)};
    std::array<std::future<void>, 2> workers;
    std::latch ready(2), start(1);
    {
        NativeWait parked(context);
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i] = std::async(std::launch::async, [&, token =
                                                                std::move(transfers[i])]() mutable {
                bool announced = false;
                try {
                    Attachment worker(interpreter);
                    RootScope local(worker);
                    ReturnSlot received(worker), result(worker);
                    worker.receive(token, received);
                    auto tuple = local.root(received.value());
                    const auto &items = worker.payload<TupleObject>(tuple).items;
                    auto type = local.root(items[0]);
                    auto target = local.root(items[1]);
                    auto expected = local.root(items[2]);
                    announced = true;
                    ready.count_down();
                    start.wait();
                    for (int n = 0; n < 50; ++n) {
                        const std::array arguments{target.get(worker)};
                        call(worker, type, {arguments, {}}, result);
                        if (result.tag() != ResultTag::Ok || result.value() != expected.get(worker))
                            throw std::runtime_error("shared weakref was not reused");
                        if (n % 10 == 0)
                            worker.collect();
                    }
                } catch (...) {
                    if (!announced)
                        ready.count_down();
                    throw;
                }
            });
        }
        ready.wait();
        start.count_down();
        for (auto &worker : workers)
            EXPECT_NO_THROW(worker.get());
    }
}

TEST_F(WeakRefs, ConcurrentFirstConstructionCommitsOneBasicRef)
{
    ReturnSlot out(context);
    exec("class Target: pass\ntarget = Target()\n", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("(_weakref.ref, target)", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto bundle = roots.root(out.value());
    std::array transfers{context.make_transfer(bundle), context.make_transfer(bundle)};
    std::array<std::future<ObjectHeader *>, 2> workers;
    std::latch ready(2), start(1), constructed(2);
    {
        NativeWait parked(context);
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i] =
                std::async(std::launch::async, [&, token = std::move(transfers[i])]() mutable {
                    bool announced = false, completed = false;
                    try {
                        Attachment worker(interpreter);
                        RootScope local(worker);
                        ReturnSlot received(worker), result(worker);
                        worker.receive(token, received);
                        auto tuple = local.root(received.value());
                        const auto &items = worker.payload<TupleObject>(tuple).items;
                        auto type = local.root(items[0]);
                        auto target = local.root(items[1]);
                        announced = true;
                        ready.count_down();
                        start.wait();
                        const std::array arguments{target.get(worker)};
                        call(worker, type, {arguments, {}}, result);
                        if (result.tag() != ResultTag::Ok)
                            throw std::runtime_error("weakref construction failed");
                        auto *identity = result.value().object();
                        completed = true;
                        constructed.count_down();
                        {
                            NativeWait wait(worker);
                            constructed.wait();
                        }
                        return identity;
                    } catch (...) {
                        if (!announced)
                            ready.count_down();
                        if (!completed)
                            constructed.count_down();
                        throw;
                    }
                });
        }
        ready.wait();
        start.count_down();
        auto *first = workers[0].get();
        EXPECT_EQ(first, workers[1].get());
    }
}
} // namespace
