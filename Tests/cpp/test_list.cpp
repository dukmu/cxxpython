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
    void check_script(std::string source, std::string expression, std::string expected)
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        const auto globals = roots.root(out.value());
        SourceUnit unit{SourceBuffer(std::move(source), "slice_contract.py")};
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit query{SourceBuffer(std::move(expression), "slice_eval.py"), InputMode::Eval};
        run(context, {&query, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto result = roots.root(out.value());
        context.collect();
        repr(context, result, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(context.payload<StrObject>(out.value()).value), expected);
    }
};
TEST_F(CoreList, SliceConstructionFieldsComparisonAndReadonlyContract)
{
    check_script(R"(s = slice(1, 9, 2)
errors = []
try:
    s.start = 4
except AttributeError:
    errors.append('readonly')
try:
    del s.stop
except AttributeError:
    errors.append('delete')
try:
    hash(slice([], None))
except TypeError:
    errors.append('unhashable')
try:
    slice()
except TypeError:
    errors.append('arity')
try:
    slice(stop=5)
except TypeError:
    errors.append('keyword')
try:
    class Child(slice):
        pass
except TypeError:
    errors.append('subclass')
field = []
held = slice(field, 'stop', None)
field.append(held)
)",
                 "s, (s.start, s.stop, s.step), slice(5), slice(1, 5), "
                 "s == slice(1, 9, 2), s < slice(2, 0, 0), s <= s, s != 2, "
                 "held.start[0] is held, errors",
                 "(slice(1, 9, 2), (1, 9, 2), slice(None, 5, None), slice(1, 5, None), "
                 "True, True, True, True, True, "
                 "['readonly', 'delete', 'unhashable', 'arity', 'keyword', 'subclass'])");
}
TEST_F(CoreList, SliceIndicesPreserveBigIntegersAndCallbackOrder)
{
    check_script(R"(calls = []
class Index:
    def __init__(self, name, value):
        self.name = name
        self.value = value
    def __index__(self):
        calls.append(self.name)
        return self.value
normalized = slice(Index('start', -8), Index('stop', 2), Index('step', -2)).indices(Index('length', 10))
big = 1000000000000000000000000000000
errors = []
try:
    slice(None, None, 0).indices(10)
except ValueError:
    errors.append('zero')
try:
    slice(None).indices(-1)
except ValueError:
    errors.append('length')
try:
    slice('bad').indices(10)
except TypeError:
    errors.append('index')
)",
                 "normalized, calls, slice(None, None, -big).indices(big), "
                 "slice(-big, big, 1).indices(10), slice(None, None, -1).indices(0), errors",
                 "((2, 2, -2), ['length', 'step', 'start', 'stop'], "
                 "(999999999999999999999999999999, -1, -1000000000000000000000000000000), "
                 "(0, 10, 1), (-1, -1, -1), ['zero', 'length', 'index'])");
}
TEST_F(CoreList, ImmutableSequenceSlicesUseNativeBaseTypesAndCodepoints)
{
    check_script(
        R"(class Text(str):
    pass
class Tuple(tuple):
    pass
s = Text('a\U0001f600bc')
t = Tuple((1, 2, 3, 4))
whole = (1, 2, 3)
big = 1000000000000000000000000000000
r = range(big)
cut = r[slice(None, None, -big)]
)",
        "s[slice(None, None, -1)], type(s[slice(None)]).__name__, "
        "t[slice(None, None, -2)], type(t[slice(None)]).__name__, "
        "whole[slice(None)] is whole, s[slice(-big, big, big)], "
        "cut, cut[0], bool(cut), r[slice(big, big)], range(1, 10, 2)[slice(None, None, -1)]",
        "('cb\xF0\x9F\x98\x80"
        "a', 'str', (4, 2), 'tuple', True, 'a', "
        "range(999999999999999999999999999999, -1, -1000000000000000000000000000000), "
        "999999999999999999999999999999, True, "
        "range(1000000000000000000000000000000, 1000000000000000000000000000000), "
        "range(9, -1, -2))");
}
TEST_F(CoreList, ListSlicesSupportGrowthShrinkExtendedDeleteAndSelfReplacement)
{
    check_script(R"(a = [0, 1, 2, 3, 4, 5]
read = a[slice(None, None, -2)]
a[slice(2, 4)] = [7, 8, 9]
grown = a.copy()
a[slice(1, 5)] = []
shrunk = a.copy()
a[slice(2, 1)] = [10, 11]
inserted = a.copy()
del a[slice(None, None, -2)]
deleted = a.copy()
b = [0, 1, 2, 3]
b[slice(None, None, -1)] = b
self_reverse = b.copy()
b[slice(1, 3)] = b
self_growth = b.copy()
c = [0, 1, 2, 3, 4]
c[slice(None, None, 2)] = (7, 8, 9)
extended = c.copy()
try:
    c[slice(None, None, 2)] = (1,)
except ValueError:
    mismatch = True
del c[slice(None, None, -2)]
)",
                 "read, grown, shrunk, inserted, deleted, self_reverse, self_growth, "
                 "extended, mismatch, c",
                 "([5, 3, 1], [0, 1, 7, 8, 9, 4, 5], [0, 4, 5], [0, 4, 10, 11, 5], "
                 "[4, 11], [3, 2, 1, 0], [3, 3, 2, 1, 0, 0], [7, 1, 8, 3, 9], True, [1, 3])");
}
TEST_F(CoreList, SliceCallbacksMayMutateBeforeListLengthIsObserved)
{
    check_script(
        R"(a = [0, 1, 2, 3]
calls = []
class Index:
    def __init__(self, name, value):
        self.name = name
        self.value = value
    def __index__(self):
        calls.append(self.name)
        a.clear()
        a.extend((7, 8, 9))
        return self.value
read = a[slice(Index('start', 0), Index('stop', 3), Index('step', 1))]
class Source:
    def __iter__(self):
        a.clear()
        a.extend((10, 11, 12, 13))
        return iter((20, 21))
a[slice(1, 3)] = Source()
assigned = a.copy()
class Broken:
    def __iter__(self):
        return self
    def __next__(self):
        raise ValueError('iterator failed')
try:
    a[slice(None)] = Broken()
except ValueError:
    preserved = a.copy()
try:
    a[slice(Index('not called', 0), None, 0)]
except ValueError:
    zero = True
)",
        "read, calls, assigned, preserved, zero",
        "([7, 8, 9], ['step', 'start', 'stop'], [10, 20, 21, 13], [10, 20, 21, 13], True)");
}
TEST_F(CoreList, SliceOwnsAndTracesAllThreeFields)
{
    ReturnSlot saved(context), out(context);
    {
        RootScope local(context);
        std::vector<Handle> fields;
        for (std::int64_t i = 1; i <= 3; ++i) {
            make_int(context, i, out);
            const auto value = local.root(out.value());
            const std::array values{value.get(context)};
            make_list(context, values, out);
            fields.push_back(local.root(out.value()));
        }
        make_slice(context, fields[0], fields[1], fields[2], saved);
    }
    context.collect();
    ASSERT_EQ(context.type_of(saved.value()), context.builtin(CoreObject::SliceType));
    repr(context, roots.root(saved.value()), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(context.payload<StrObject>(out.value()).value, U"slice([1], [2], [3])");
}
TEST_F(CoreList, SliceReplacementHintsFailuresAndSelfSubclassFollowProtocolOrder)
{
    check_script(R"(a = [0]
class Input:
    def __init__(self):
        self.position = 0
    def __iter__(self):
        return self
    def __length_hint__(self):
        a.clear()
        a.extend((10, 11, 12, 13))
        return 2
    def __next__(self):
        if self.position == 2:
            raise StopIteration
        self.position += 1
        return self.position + 19
a[slice(None, None, 2)] = Input()
class Failure:
    def __init__(self):
        self.position = 0
    def __iter__(self):
        return self
    def __next__(self):
        self.position += 1
        if self.position == 1:
            return [99]
        raise RuntimeError('after first item')
try:
    a[slice(None)] = Failure()
except RuntimeError:
    failed = True
class Child(list):
    def __iter__(self):
        raise ValueError('must not iterate exact self replacement')
b = Child([0, 1, 2])
b[slice(None, None, -1)] = b
)",
                 "a, failed, b", "([20, 11, 21, 13], True, [2, 1, 0])");
}
TEST_F(CoreList, ConcurrentSliceSnapshotsRemainRootedAcrossReplacementAndCollection)
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
                        const auto none = local.root(actor.builtin(CoreObject::None));
                        make_slice(actor, none, none, none, result);
                        const auto key = local.root(result.value());
                        {
                            NativeWait wait(actor);
                            ready.count_down();
                            announced = true;
                            start.wait();
                        }
                        for (std::size_t i = 0; i < 60; ++i) {
                            RootScope iteration(actor);
                            if (worker == 0) {
                                make_str(actor, U"retained", result);
                                const auto value = iteration.root(result.value());
                                std::vector<Value> values(i % 7 + 1, value.get(actor));
                                cxxpython::core::make_tuple(actor, values, result);
                                set_item(actor, list, key, iteration.root(result.value()), result);
                            } else {
                                get_item(actor, list, key, result);
                                if (result.tag() != ResultTag::Ok)
                                    throw std::runtime_error("slice read failed");
                                const auto snapshot = iteration.root(result.value());
                                actor.collect();
                                const auto count = detail::list_size(actor, snapshot);
                                if (count > 7)
                                    throw std::runtime_error("inconsistent slice length");
                                for (std::size_t j = 0; j < count; ++j) {
                                    detail::list_item(actor, snapshot, j, result);
                                    if (result.tag() != ResultTag::Ok ||
                                        !actor.has_layout<StrObject>(result.value()) ||
                                        actor.payload<StrObject>(result.value()).value !=
                                            U"retained")
                                        throw std::runtime_error("slice value was not retained");
                                }
                                result.ok(actor.builtin(CoreObject::None));
                            }
                            if (result.tag() != ResultTag::Ok)
                                throw std::runtime_error("slice replacement failed");
                            actor.poll();
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
}
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
