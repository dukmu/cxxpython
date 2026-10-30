#include "cxxpython/compiler.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"

#include <array>
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class SetScripts : public testing::Test {
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
        SourceUnit program(SourceBuffer(source, "set_fixture.py"));
        run(ctx, {&program, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit query(SourceBuffer(expression, "set_expression.py"), InputMode::Eval);
        run(ctx, {&query, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto result = roots.root(out.value());
        ctx.collect();
        repr(ctx, result, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(ctx.payload<StrObject>(out.value()).value), expected);
    }
};

TEST_F(SetScripts, ComparisonsCopiesHashAndSubclassConstruction)
{
    check(R"(class S(set):
    pass
class F(frozenset):
    pass
a = S((1, 2))
a.label = 9
b = F((2, 1))
b.label = 10
c = set((1,))
acopy = a.copy()
bcopy = b.copy()
f = frozenset((1, 2))
)",
          "a.label, b.label, a == b, a != b, c < a, c <= a, a > c, a >= c, "
          "a < b, a <= b, hash(b) == hash(f), hash(f) == hash(f), "
          "type(acopy) is set, type(bcopy) is frozenset, f.copy() is f, "
          "repr(set()), repr(frozenset()), repr(S()), repr(F())",
          "(9, 10, True, False, True, True, True, True, False, True, True, True, True, True, True, "
          "'set()', 'frozenset()', 'S()', 'F()')");
}

TEST_F(SetScripts, MutableSetLookupRemovalAndErrors)
{
    check(R"(f = frozenset((1, 2))
s = set((f, 4))
k = set((2, 1))
present = k in s
s.discard(k)
absent = k not in s
s.discard(k)
missing = False
try:
    s.remove(k)
except KeyError:
    missing = True
s.add(f)
s.remove(k)
popped = s.pop()
empty_error = False
try:
    s.pop()
except KeyError:
    empty_error = True
)",
          "present, absent, missing, popped, empty_error", "(True, True, True, 4, True)");
}

TEST_F(SetScripts, CallbackErrorsAndReentrancy)
{
    check(R"(s = set()
class Key:
    def __hash__(self):
        return 7
    def __eq__(self, other):
        s.add(9)
        raise ValueError('compare')
a = Key()
b = Key()
s.add(a)
raised = False
try:
    b in s
except ValueError:
    raised = True
class Bad:
    def __hash__(self):
        raise ValueError('hash')
hash_raised = False
try:
    s.discard(Bad())
except ValueError:
    hash_raised = True
)",
          "raised, hash_raised, 9 in s, a in s", "(True, True, True, True)");
}

TEST_F(SetScripts, ReprRecursionAndComparisonErrors)
{
    check(R"(s = set()
class ReprKey:
    def __hash__(self):
        return 13
    def __repr__(self):
        return repr(s)
s.add(ReprKey())
recursive = repr(s)
class Bad:
    def __hash__(self):
        return 42
    def __eq__(self, other):
        raise ValueError('comparison')
a = set((Bad(),))
b = set((Bad(),))
raised = False
try:
    a == b
except ValueError:
    raised = True
)",
          "recursive, raised", "('{set(...)}', True)");
}

TEST_F(SetScripts, CachedHashesAndSubclassIteration)
{
    check(R"(calls = []
class Key:
    def __hash__(self):
        calls.append('hash')
        return 137
    def __eq__(self, other):
        return True
class S(set):
    def __iter__(self):
        return iter((99,))
k = Key()
s = S((k,))
before = len(calls)
f = frozenset(s)
copy = s.copy()
equal = s == f
h = hash(f)
after = len(calls)
shown = repr(s)
)",
          "before, after, equal, len(f), len(copy), shown", "(1, 1, True, 1, 1, 'S({99})')");
}

TEST_F(SetScripts, FrozenHashMatchesReferenceAndIsOrderIndependent)
{
    check("",
          "hash(frozenset()), hash(frozenset((1, 2))), "
          "hash(frozenset((2, 1))) == hash(frozenset((1, 2)))",
          "(133146708735736, -1826646154956904602, True)");
}

TEST_F(SetScripts, MutableSubclassConversionAndReprErrors)
{
    check(R"(class K(set):
    def __hash__(self):
        raise TypeError('unhashable')
    def __iter__(self):
        return iter((99,))
k = K((1, 2))
s = set((frozenset((1, 2)),))
found = k in s
s.remove(k)
class BadRepr:
    def __hash__(self):
        return 5
    def __repr__(self):
        raise ValueError('repr')
r = set((BadRepr(),))
raised = False
try:
    repr(r)
except ValueError:
    raised = True
)",
          "found, len(s), raised", "(True, 0, True)");
}

TEST_F(SetScripts, RemovalExceptionsAndUnhashableKeys)
{
    check(R"(s = set((1,))
key_error = False
try:
    s.remove(2)
except KeyError as e:
    key_error = e.args == (2,)
bad_contains = False
try:
    [] in s
except TypeError:
    bad_contains = True
bad_discard = False
try:
    s.discard([])
except TypeError:
    bad_discard = True
bad_add = False
try:
    s.add([])
except TypeError:
    bad_add = True
)",
          "key_error, bad_contains, bad_discard, bad_add, len(s)", "(True, True, True, True, 1)");
}

TEST_F(SetScripts, NonSetComparisonAndConstructionErrors)
{
    check(R"(equality = set((1,)) == [1]
ordering = False
try:
    set((1,)) < [1]
except TypeError:
    ordering = True
set_args = False
try:
    set((1,), (2,))
except TypeError:
    set_args = True
frozen_args = False
try:
    frozenset((1,), (2,))
except TypeError:
    frozen_args = True
set_keyword = False
try:
    set(value=1)
except TypeError:
    set_keyword = True
frozen_keyword = False
try:
    frozenset(value=1)
except TypeError:
    frozen_keyword = True
)",
          "equality, ordering, set_args, frozen_args, set_keyword, frozen_keyword",
          "(False, True, True, True, True, True)");
}

TEST_F(SetScripts, DescriptorsRejectWrongReceiver)
{
    check(R"(wrong_repr = False
try:
    set.__repr__(frozenset())
except TypeError:
    wrong_repr = True
wrong_compare = False
try:
    frozenset.__eq__(set(), set())
except TypeError:
    wrong_compare = True
wrong_new = False
try:
    set.__new__(frozenset)
except TypeError:
    wrong_new = True
wrong_frozen_new = False
try:
    frozenset.__new__(set)
except TypeError:
    wrong_frozen_new = True
)",
          "wrong_repr, wrong_compare, wrong_new, wrong_frozen_new", "(True, True, True, True)");
}

TEST_F(SetScripts, IteratorFailureAndReinitialization)
{
    check(R"(class BadIterable:
    def __iter__(self):
        raise ValueError('iterator')
s = set((1,))
failed = False
try:
    s.__init__(BadIterable())
except ValueError:
    failed = True
frozen_failed = False
try:
    frozenset(BadIterable())
except ValueError:
    frozen_failed = True
)",
          "failed, len(s), frozen_failed", "(True, 0, True)");
}

TEST_F(SetScripts, ReinitializingFromSelfClearsSet)
{
    check(R"(s = set((1, 2))
s.__init__(s)
)",
          "len(s), repr(s)", "(0, 'set()')");
}

TEST_F(SetScripts, FrozenSubclassKeywordsAndIteratorMutation)
{
    check(R"(class Frozen(frozenset):
    pass
keyword_error = False
try:
    Frozen(value=1)
except TypeError:
    keyword_error = True
s = set((1, 2))
it = iter(s)
s.add(3)
iteration_error = False
try:
    next(it)
except RuntimeError:
    iteration_error = True
)",
          "keyword_error, iteration_error", "(True, True)");
}

TEST(CoreSet, BackingTableKeepsElementsAliveAcrossCollection)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment ctx{interpreter};
    ctx.bootstrap_core();
    RootScope roots(ctx);
    ReturnSlot value(ctx), result(ctx), found(ctx);
    make_str(ctx, U"element", value);
    auto key = roots.root(value.value());
    const std::array values{key.get(ctx), key.get(ctx)};
    make_set(ctx, values, result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    auto set = roots.root(result.value());
    ASSERT_TRUE(is_set(ctx, set.get(ctx)));
    ctx.collect();
    auto table = roots.root(ctx.payload<SetObject>(set).table);
    EXPECT_EQ(detail::dictionary_size(ctx, table.get(ctx)), 1);
    dict_lookup(ctx, table, key, found);
    EXPECT_EQ(found.tag(), ResultTag::Ok);
    make_str(ctx, U"another", value);
    set_add(ctx, set, roots.root(value.value()), result);
    ASSERT_EQ(result.tag(), ResultTag::Ok);
    ctx.collect();
    EXPECT_EQ(detail::dictionary_size(ctx, table.get(ctx)), 2);
}
} // namespace
