#include "cxxpython/compiler.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class SetOperations : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx{interpreter};
    RootScope roots{ctx};
    void SetUp() override
    {
        ctx.bootstrap_core();
    }
    void check(const char *source, const char *expression, const char *expected)
    {
        ReturnSlot out(ctx);
        ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), out);
        auto globals = roots.root(out.value());
        SourceUnit program(SourceBuffer(source, "set_operations.py"));
        run(ctx, {&program, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit query(SourceBuffer(expression, "set_operations_result.py"), InputMode::Eval);
        run(ctx, {&query, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto result = roots.root(out.value());
        ctx.collect();
        repr(ctx, result, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(ctx.payload<StrObject>(out.value()).value), expected);
    }
};

TEST_F(SetOperations, MethodsOperatorsAndResultTypes)
{
    check(R"(class S(set):
    pass
class F(frozenset):
    pass
a = S((1, 2))
b = F((2, 3))
u = a.union((3,), (4,))
i = a.intersection((2, 3))
d = a.difference((2,))
x = a.symmetric_difference((2, 3, 3))
)",
          "u == set((1, 2, 3, 4)), i == set((2,)), d == set((1,)), "
          "x == set((1, 3)), a | b == set((1, 2, 3)), a & b == set((2,)), "
          "a - b == set((1,)), a ^ b == set((1, 3)), type(u) is set, "
          "type(b | a) is frozenset, a.issubset((1, 2, 3)), "
          "a.issuperset((1,)), a.isdisjoint((3,))",
          "(True, True, True, True, True, True, True, True, True, True, True, True, True)");
}

TEST_F(SetOperations, UpdatesAndIdentity)
{
    check(R"(s = set((1, 2))
original = s
s.update((3,), (4,))
s.intersection_update((2, 3, 4))
s.difference_update((4,))
s.symmetric_difference_update((3, 5, 5))
s |= set((6,))
s &= set((2, 5, 6))
s -= set((6,))
s ^= set((2, 7))
)",
          "s is original, s == set((5, 7))", "(True, True)");
}

TEST_F(SetOperations, CachedHashesAndErrors)
{
    check(R"(calls = []
class Key:
    def __hash__(self):
        calls.append(1)
        return 42
k = Key()
a = set((k,))
b = set((k,))
before = len(calls)
u = a | b
i = a & b
d = a - b
x = a ^ b
a.update(b)
a.difference_update(b)
after = len(calls)
wrong = False
try:
    set((1,)) | [1]
except TypeError:
    wrong = True
bad = False
try:
    set((1,)).update(([],))
except TypeError:
    bad = True
)",
          "before == after, len(u), len(i), len(d), len(x), wrong, bad",
          "(True, 1, 1, 0, 0, True, True)");
}

TEST_F(SetOperations, ReflectionArgumentsAndCallbackErrors)
{
    check(R"(a = set((1, 2))
b = frozenset((2, 3))
reverse = a.__rsub__(b)
wrong = a.__or__([1]) is NotImplemented
arity = False
try:
    a.symmetric_difference()
except TypeError:
    arity = True
keyword = False
try:
    a.union(other=(1,))
except TypeError:
    keyword = True
class Bad:
    def __hash__(self):
        raise ValueError('hash')
raised = False
try:
    a.intersection_update((2,), (Bad(),))
except ValueError:
    raised = True
)",
          "reverse == frozenset((3,)), type(reverse) is frozenset, wrong, arity, "
          "keyword, raised, a == set((1, 2))",
          "(True, True, True, True, True, True, True)");
}

TEST_F(SetOperations, ReentrantEqualityException)
{
    check(R"(s = set()
class Key:
    def __hash__(self):
        return 91
    def __eq__(self, other):
        s.add(10)
        raise ValueError('comparison')
a = Key()
b = Key()
s.add(a)
other = set((b,))
raised = False
try:
    s.update(other)
except ValueError:
    raised = True
)",
          "raised, 10 in s, a in s", "(True, True, True)");
}
TEST_F(SetOperations, OperationSpecificDictAndSubclassPaths)
{
    check(R"(calls = []
class Key:
    def __hash__(self):
        calls.append(1)
        return 123
k = Key()
s = set((k,))
d = {k: 1}
before = len(calls)
u = s.union(d)
after_union = len(calls)
i = s.intersection(d)
after_intersection = len(calls)
disjoint = s.isdisjoint(d)
after_disjoint = len(calls)
s.difference_update(d)
after_difference = len(calls)
class S(set):
    def __iter__(self):
        return iter((99,))
sub = S((k,))
uses_override = set((99,)).isdisjoint(sub)
)",
          "after_union - before, after_intersection - after_union, "
          "after_disjoint - after_intersection, after_difference - after_disjoint, "
          "disjoint, uses_override, len(u), len(i)",
          "(0, 1, 1, 1, False, False, 1, 1)");
}

TEST_F(SetOperations, IntersectionStopsAfterAllMatches)
{
    check(R"(class Items:
    def __init__(self):
        self.consumed = 0
    def __iter__(self):
        return self
    def __next__(self):
        self.consumed += 1
        if self.consumed == 1:
            return 1
        raise ValueError('must not consume')
s = set((1,))
intersection_items = Items()
subset_items = Items()
i = s.intersection(intersection_items)
subset = s.issubset(subset_items)
)",
          "i == s, subset, intersection_items.consumed, subset_items.consumed",
          "(True, True, 1, 1)");
}

TEST_F(SetOperations, IntersectionPreservesSmallerSetElement)
{
    check(R"(class Key:
    def __hash__(self):
        return 7
    def __eq__(self, other):
        return True
small_key = Key()
large_key = Key()
small = set((small_key,))
large = set((large_key, 1, 2, 3))
result = large.intersection(small)
)",
          "len(result), next(iter(result)) is small_key", "(1, True)");
}
} // namespace
