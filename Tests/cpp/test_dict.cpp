#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <latch>
#include <stdexcept>

namespace {
using namespace cxxpython::core;

class CoreDict : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};

    void SetUp() override
    {
        context.bootstrap_core();
    }

    void check(const char *source, const char *expression, const char *expected)
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        auto globals = roots.root(out.result().value());
        SourceUnit program(SourceBuffer(source, "dict_fixture.py"));
        run(context, {&program, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        SourceUnit query(SourceBuffer(expression, "dict_expression.py"), InputMode::Eval);
        run(context, {&query, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        auto value = roots.root(out.result().value());
        context.collect();
        repr(context, value, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(source_utf8(context.payload<StrObject>(out.result().value()).value), expected);
    }
};

TEST_F(CoreDict, NumericIdentityNanAndInsertionOrder)
{
    check(R"(d = dict()
d[1] = 'first'
d[True] = 'second'
d[1.0] = 'third'
original = next(iter(d))
n = float('nan')
d[n] = 'nan'
d['tail'] = 4
del d[1.0]
d[True] = 'again'
it = iter(d)
first = next(it)
second = next(it)
third = next(it)
)",
          "original is 1, first is n, second, third is True, d[n], len(d)",
          "(True, True, 'tail', True, 'nan', 3)");
}

TEST_F(CoreDict, CollisionsAndCallbacksCanMutateOrRaise)
{
    check(R"(d = dict()
class Key:
    def __init__(self, value):
        self.value = value
    def __hash__(self):
        return 17
    def __eq__(self, other):
        d['side'] = 9
        return self.value == other.value
a = Key(1)
b = Key(2)
d[a] = 10
d[b] = 20
found = d[Key(1)]
class BadHash:
    def __hash__(self):
        raise ValueError('hash')
class BadEq:
    def __hash__(self):
        return 23
    def __eq__(self, other):
        raise ValueError('eq')
d[BadEq()] = 1
hash_error = False
eq_error = False
try:
    d[BadHash()] = 1
except ValueError:
    hash_error = True
try:
    d[BadEq()]
except ValueError:
    eq_error = True
)",
          "found, d[b], d['side'], hash_error, eq_error", "(10, 20, 9, True, True)");
}

TEST_F(CoreDict, ConstructorsMethodsAndMissing)
{
    check(R"(class Default(dict):
    def __missing__(self, key):
        return 42
d = dict((('a', 1), ('b', 2)), c=3)
copy = d.copy()
copy.update(dict((('a', 5), ('d', 6))))
old = copy.pop('b')
default = copy.setdefault('e', 7)
missing = copy.get('absent', 8)
copy.clear()
unhashable = False
try:
    hash(d)
except TypeError:
    unhashable = True
)",
          "d['a'], d['c'], old, default, missing, len(copy), Default()['x'], unhashable",
          "(1, 3, 2, 7, 8, 0, 42, True)");
}

TEST_F(CoreDict, ModuleNamespacesAndDynamicReadOnlyClassMapping)
{
    check(R"(import sys
namespace = sys.__dict__
namespace['_dict_test_value'] = 37
seen = sys._dict_test_value
del namespace['_dict_test_value']
sys.modules['_dict_test_alias'] = sys
same = sys.modules['_dict_test_alias'] is sys
del sys.modules['_dict_test_alias']
class C:
    value = 1
view = C.__dict__
C.value = 2
C.added = 3
readonly = False
try:
    view['value'] = 4
except TypeError:
    readonly = True
)",
          "seen, same, view['value'], view['added'], readonly", "(37, True, 2, 3, True)");
}

TEST_F(CoreDict, AttributesUseKeyEqualityAndPreserveOriginalClassKeys)
{
    check(R"(class Key:
    def __hash__(self):
        return hash('alias')
    def __eq__(self, other):
        return other == 'alias'
key = Key()
class Instance: pass
obj = Instance()
obj.__dict__[key] = 10
before = obj.alias
obj.alias = 11
after = obj.__dict__[key]
del obj.alias
missing = obj.__dict__.get(key)
C = type('C', (), dict(((key, 20),)))
class_before = C.alias
C.alias = 21
class_after = C.__dict__[key]
copy = C.__dict__.copy()
del C.alias
class_missing = C.__dict__.get(key)
)",
          "before, after, missing, class_before, class_after, copy[key], class_missing",
          "(10, 11, None, 20, 21, 21, None)");
}

TEST_F(CoreDict, DynamicViewsAndIteratorMutations)
{
    check(R"(d = dict((('a', 1), ('b', 2)))
keys = d.keys()
values = d.values()
items = d.items()
d['a'] = 4
d['c'] = 3
del d['b']
ki = iter(keys)
vi = iter(values)
ii = iter(items)
k1 = next(ki)
k2 = next(ki)
v1 = next(vi)
v2 = next(vi)
i1 = next(ii)
i2 = next(ii)
iterator = iter(d)
first = next(iterator)
d['c'] = 5
second = next(iterator)
exhausted = next(iterator, 'end')
d['extra'] = 6
still_exhausted = next(iterator, 'end')
stopped = False
try:
    next(iterator)
except StopIteration:
    stopped = True
changing = iter(d)
next(changing)
d['new'] = 7
size_error = False
try:
    next(changing)
except RuntimeError:
    size_error = True
)",
          "len(keys), len(values), len(items), k1, k2, v1, v2, i1, i2, "
          "first, second, exhausted, still_exhausted, stopped, size_error",
          "(4, 4, 4, 'a', 'c', 4, 3, ('a', 4), ('c', 3), 'a', 'c', 'end', 'end', True, True)");
}

TEST_F(CoreDict, SameSizeReplacementDoesNotYieldNewKeyAfterOldKeysConsumed)
{
    check(R"(d = dict((('a', 1), ('b', 2)))
it = iter(d)
first = next(it)
del d['a']
d['c'] = 3
second = next(it)
changed = False
try:
    next(it)
except RuntimeError:
    changed = True
)",
          "first, second, changed", "('a', 'b', True)");
}

TEST_F(CoreDict, RepresentationsEqualityAndNonStringClassKeys)
{
    check(R"(d = dict((('a', 1), ('b', 2)))
equal = dict((('b', 2), ('a', 1)))
unequal = dict((('a', 1), ('b', 3)))
C = type('C', (), dict(((1, 2), ('x', 3))))
proxy = C.__dict__
copy = dict(proxy)
)",
          "repr(d), repr(d.keys()), repr(d.values()), repr(d.items()), "
          "repr(proxy) == 'mappingproxy(' + repr(copy) + ')', "
          "d == equal, d == unequal, d != unequal, proxy[1], copy[1], copy['x']",
          "(\"{'a': 1, 'b': 2}\", \"dict_keys(['a', 'b'])\", "
          "'dict_values([1, 2])', \"dict_items([('a', 1), ('b', 2)])\", "
          "True, True, False, True, 2, 2, 3)");
}

TEST_F(CoreDict, SharedDictionaryStoresAcrossAttachmentsDuringCollection)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto dictionary = roots.root(out.result().value());
    std::array transfers{context.make_transfer(dictionary), context.make_transfer(dictionary)};
    std::latch ready(2), start(1);
    std::array<std::future<void>, 2> workers;
    std::array<std::int64_t, 2> winners{};
    {
        NativeWait wait(context);
        for (int i = 0; i < 2; ++i) {
            workers[i] = std::async(
                std::launch::async, [&, i, transfer = std::move(transfers[i])]() mutable {
                    Attachment worker(interpreter);
                    RootScope local(worker);
                    ReturnSlot received(worker), result(worker);
                    worker.receive(transfer, received);
                    auto shared = local.root(received.result().value());
                    {
                        NativeWait wait(worker);
                        ready.count_down();
                        start.wait();
                    }
                    make_int(worker, -1, result);
                    const auto common = local.root(result.value());
                    make_int(worker, i, result);
                    dict_setdefault(worker, shared, common, local.root(result.value()), result);
                    if (result.tag() != ResultTag::Ok ||
                        !int_fits_signed(worker, result.value(), winners[i]))
                        throw std::runtime_error("concurrent setdefault failed");
                    for (int n = 0; n < 100; ++n) {
                        RootScope iteration(worker);
                        make_int(worker, i * 100 + n, result);
                        auto key = iteration.root(result.result().value());
                        dict_store(worker, shared, key, key, result);
                        if (result.tag() != ResultTag::Ok)
                            throw std::runtime_error("shared dict store raised");
                    }
                });
        }
        ready.wait();
        start.count_down();
    }
    context.collect();
    for (auto &worker : workers) {
        NativeWait wait(context);
        EXPECT_NO_THROW(worker.get());
    }
    EXPECT_EQ(winners[0], winners[1]);
    for (int n = 0; n < 200; ++n) {
        RootScope iteration(context);
        make_int(context, n, out);
        auto key = iteration.root(out.result().value());
        dict_lookup(context, dictionary, key, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(int_decimal(context, out.result().value()), std::to_string(n));
    }
}
TEST_F(CoreDict, CachedHashesAndCopyDoNotReplayUserHashCallbacks)
{
    check(R"(class K:
    def __hash__(self):
        return 7
key = K()
d = dict(((key, 42),))
def bad_hash(self):
    raise ValueError('hash must not be replayed')
K.__hash__ = bad_hash
copied = d.copy()
constructed = dict(d)
equal = copied == constructed
)",
          "len(copied), len(constructed), equal", "(1, 1, True)");
}

TEST_F(CoreDict, LongNameIndexesAliasTracedKeysAndCopiesSurviveCollection)
{
    const std::u32string name = U"long_dictionary_key_\u03bb_" + std::u32string(128, U'x');
    const std::u32string text = U"long_dictionary_value_" + std::u32string(128, U'y');
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto dictionary = roots.root(out.value());
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto copy = roots.root(out.value());
    {
        RootScope inputs(context);
        make_str(context, name, out);
        const auto key = inputs.root(out.value());
        make_str(context, text, out);
        const auto value = inputs.root(out.value());
        dict_store(context, dictionary, key, value, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }

    const auto &ordinary = *context.payload<DictObject>(dictionary.get(context)).contents;
    ASSERT_EQ(ordinary.index.size(), 1U);
    const auto &indexed = *ordinary.index.begin();
    EXPECT_EQ(indexed.first.data(), context.payload<StrObject>(indexed.second->key).value.data());

    ReturnSlot snapshot_root(context);
    {
        DictView view(context, dictionary.get(context));
        ASSERT_EQ(view.entries().size(), 1U);
        detail::copy_dictionary_entries(context, copy.get(context), view.entries());
        const auto snapshot = view.storage();
        ASSERT_TRUE(snapshot.has_value());
        snapshot_root.ok(*snapshot);
        const auto &storage = context.payload<DictStorage>(snapshot_root.value());
        ASSERT_EQ(storage.index.size(), 1U);
        const auto &immutable_indexed = *storage.index.begin();
        EXPECT_EQ(
            immutable_indexed.first.data(),
            context.payload<StrObject>(storage.entries[immutable_indexed.second].key).value.data());

        dict_clear(context, dictionary, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        context.collect();
        ASSERT_TRUE(view.find(name).has_value());
        EXPECT_EQ(context.payload<StrObject>(*view.find(name)).value, text);
    }
    context.collect();
    {
        DictView copy_view(context, copy.get(context));
        ASSERT_EQ(copy_view.entries().size(), 1U);
        EXPECT_EQ(context.payload<StrObject>(copy_view.entries()[0].key).value, name);
        ASSERT_TRUE(copy_view.find(name).has_value());
        EXPECT_EQ(context.payload<StrObject>(*copy_view.find(name)).value, text);
    }
    dict_clear(context, copy, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    DictView snapshot_view(context, snapshot_root.value());
    ASSERT_EQ(snapshot_view.entries().size(), 1U);
    ASSERT_TRUE(snapshot_view.find(name).has_value());
    EXPECT_EQ(context.payload<StrObject>(*snapshot_view.find(name)).value, text);
    EXPECT_EQ(context.payload<StrObject>(snapshot_view.entries()[0].key).value, name);
}
} // namespace
