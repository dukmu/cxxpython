#include "cxxpython/compiler.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;
class CoreList : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
};
TEST_F(CoreList, CallbacksAndSelfExtensionKeepValuesRooted)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(a = [1, 2]
a.extend(a)
alias = a
a *= 2
class Index:
    def __index__(self):
        a.clear()
        a.extend((7, 8))
        return -1
value = a[Index()]
class Item:
    def __repr__(self):
        a.clear()
        return 'item'
a.append(Item())
text = repr(a)
class Sequence:
    def __getitem__(self, i):
        if i == 2:
            raise IndexError(i)
        return i + 1
converted = list(Sequence())
class L(list):
    def __iter__(self): return iter((9,))
b = L([1])
b.extend(b)
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    SourceUnit query(SourceBuffer("value, a, alias is a, text, converted, b", "list_eval.py"),
                     InputMode::Eval);
    run(context, {&query, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto result = roots.root(out.value());
    context.collect();
    repr(context, result, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(source_utf8(context.payload<StrObject>(out.value()).value),
              "(8, [], True, '[7, 8, item]', [1, 2], [1, 9])");
}
TEST_F(CoreList, UnreachableSelfCycleIsCollected)
{
    context.collect();
    const auto before = context.stats().objects;
    {
        RootScope local(context);
        ReturnSlot out(context);
        make_list(context, {}, out);
        const auto list = local.root(out.value());
        make_str(context, U"append", out);
        get_attribute(context, list, local.root(out.value()), out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto append = local.root(out.value());
        const std::array args{list.get(context)};
        call(context, append, {args}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        context.collect();
        detail::list_item(context, list, 0, out);
        EXPECT_EQ(out.value(), list.get(context));
    }
    context.collect();
    EXPECT_EQ(context.stats().objects, before);
}
TEST_F(CoreList, ConcurrentAppendAndCollectionPreserveEveryItem)
{
    ReturnSlot out(context);
    make_list(context, {}, out);
    const auto shared = roots.root(out.value());
    std::array transfers{context.make_transfer(shared), context.make_transfer(shared)};
    std::latch ready(2), start(1);
    std::array<std::future<void>, 2> workers;
    {
        NativeWait parked(context);
        for (std::size_t worker = 0; worker < workers.size(); ++worker)
            workers[worker] = std::async(
                std::launch::async, [&, worker, token = std::move(transfers[worker])]() mutable {
                    bool announced = false;
                    try {
                        Attachment actor(interpreter);
                        RootScope local(actor);
                        ReturnSlot result(actor);
                        actor.receive(token, result);
                        const auto list = local.root(result.value());
                        make_str(actor, U"append", result);
                        get_attribute(actor, list, local.root(result.value()), result);
                        if (result.tag() != ResultTag::Ok)
                            throw std::runtime_error("append lookup failed");
                        const auto append = local.root(result.value());
                        {
                            NativeWait wait(actor);
                            ready.count_down();
                            announced = true;
                            start.wait();
                        }
                        for (std::size_t i = 0; i < 200; ++i) {
                            RootScope iteration(actor);
                            make_int(actor, worker * 200 + i, result);
                            const std::array args{iteration.root(result.value()).get(actor)};
                            call(actor, append, {args}, result);
                            if (result.tag() != ResultTag::Ok)
                                throw std::runtime_error("append failed");
                            if (i % 17 == 0)
                                actor.collect();
                        }
                    } catch (...) {
                        if (!announced)
                            ready.count_down();
                        throw;
                    }
                });
        ready.wait();
        start.count_down();
        for (auto &worker : workers)
            EXPECT_NO_THROW(worker.get());
    }
    ASSERT_EQ(detail::list_size(context, shared), 400u);
    std::array<bool, 400> seen{};
    for (std::size_t i = 0; i < seen.size(); ++i) {
        detail::list_item(context, shared, i, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        std::int64_t value;
        ASSERT_TRUE(int_fits_signed(context, out.value(), value));
        ASSERT_GE(value, 0);
        ASSERT_LT(value, static_cast<std::int64_t>(seen.size()));
        EXPECT_FALSE(seen[value]);
        seen[value] = true;
    }
}
} // namespace
