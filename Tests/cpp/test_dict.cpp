#include "cxxpython/compiler.hpp"
#include "internal/abstract.hpp"
#include "internal/dict.hpp"

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <latch>
#include <limits>
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

TEST_F(CoreDict, OwnedOperationsKeepCallerRootsThroughCollectingCallbacks)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    auto dictionary = roots.root(out.value());
    create_type(context, {"CollectingKey", {}}, out);
    auto cls = roots.root(out.value());
    context.allocate_instance<InstanceObject>(cls, out);
    auto key = roots.root(out.value());
    context.allocate_instance<InstanceObject>(cls, out);
    auto equal_key = roots.root(out.value());
    make_int(context, 17, out);
    auto hash = roots.root(out.value());
    make_int(context, 42, out);
    auto value = roots.root(out.value());

    auto install = [&](std::u32string name, Handle result, bool binary) {
        SignatureLayout signature;
        signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
        if (binary)
            signature.parameters.push_back({U"other", ParameterKind::PositionalOnly});
        std::array captures{result.get(context)};
        make_native_function(
            context, "collecting dict protocol", signature,
            [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &result) {
                c.collect();
                result.ok(c.payload<NativeFunctionObject>(fn).captures[0]);
            },
            captures, out);
        auto function = roots.root(out.value());
        make_str(context, std::move(name), out);
        auto attribute = roots.root(out.value());
        set_attribute(context, cls, attribute, function, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    };
    install(U"__hash__", hash, false);
    install(U"__eq__", roots.root(context.builtin(CoreObject::True)), true);

    detail::dict_store_owned(context, dictionary, key, value, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    detail::dict_lookup_owned(context, dictionary, equal_key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));
    detail::dict_setdefault_owned(context, dictionary, equal_key, hash, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));
    detail::dict_erase_owned(context, dictionary, equal_key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    detail::dict_setdefault_owned(context, dictionary, key, value, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));

    out.ok(equal_key.get(context));
    auto reused = roots.root(out.value());
    dict_lookup(context, dictionary, reused, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));
    context.collect();
    EXPECT_EQ(context.type_of(key.get(context)), cls.get(context));
    EXPECT_EQ(context.type_of(reused.get(context)), cls.get(context));
    EXPECT_EQ(int_decimal(context, value.get(context)), "42");
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
    ASSERT_NE(indexed.name, nullptr);
    EXPECT_EQ(indexed.name->data(), context.payload<StrObject>(indexed.entry.key).value.data());

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

TEST_F(CoreDict, StorageIndexesShareOrderedNodesAcrossRemovalAndCollection)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto dictionary = roots.root(out.value());
    make_str(context, U"indexed name", out);
    const auto name = roots.root(out.value());
    make_int(context, 42, out);
    const auto number = roots.root(out.value());
    const auto &storage = *context.payload<DictObject>(dictionary.get(context)).contents;

    auto check_membership = [&] {
        EXPECT_EQ(storage.hashes.size(), storage.names.size());
        std::size_t named = 0;
        for (const auto &node : storage.names) {
            EXPECT_EQ(&*node.order, &node);
            std::size_t hash_matches = 0;
            for (const auto &indexed : storage.hashes)
                hash_matches += &indexed == &node;
            EXPECT_EQ(hash_matches, 1U);
            std::size_t name_matches = 0;
            for (const auto &indexed : storage.index)
                name_matches += &indexed == &node;
            EXPECT_EQ(name_matches, node.name ? 1U : 0U);
            named += node.name != nullptr;
        }
        EXPECT_EQ(storage.index.size(), named);
    };

    dict_store(context, dictionary, name, number, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    dict_store(context, dictionary, number, name, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    ASSERT_EQ(storage.names.size(), 2U);
    check_membership();
    dict_erase(context, dictionary, name, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    ASSERT_EQ(storage.names.size(), 1U);
    check_membership();
    dict_store(context, dictionary, name, number, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    check_membership();
    dict_clear(context, dictionary, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    EXPECT_TRUE(storage.names.empty());
    check_membership();
    dict_store(context, dictionary, name, number, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    ASSERT_EQ(storage.names.size(), 1U);
    check_membership();
}

TEST_F(CoreDict, BindingRollbackIsNonallocatingAndPreservesReplacement)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto dictionary = roots.root(out.value());
    make_str(context, U"binding", out);
    const auto key = roots.root(out.value());
    make_int(context, 42, out);
    const auto value = roots.root(out.value());
    detail::DictBinding original;
    detail::dict_store_binding(context, dictionary, key, value, original, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.set_allocation_limit(0);
    const bool erased = detail::dict_erase_binding(context, dictionary, original, value);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_TRUE(erased);

    detail::DictBinding replacement;
    detail::dict_store_binding(context, dictionary, key, value, replacement, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_NE(original.identity, replacement.identity);
    context.set_allocation_limit(0);
    const bool erased_replacement =
        detail::dict_erase_binding(context, dictionary, original, value);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_FALSE(erased_replacement);
    context.collect();
    dict_lookup(context, dictionary, key, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), value.get(context));
}
} // namespace
