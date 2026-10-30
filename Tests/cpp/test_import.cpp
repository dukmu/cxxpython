#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;
class Imports : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
        ReturnSlot out(context);
        initialize_imports(context, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
    void source(std::u32string name, std::string text)
    {
        register_source_module(context, std::move(name),
                               std::make_shared<const SourceUnit>(SourceBuffer(std::move(text))));
    }
    Value attribute(Handle object, std::u32string name)
    {
        ReturnSlot out(context), key(context);
        make_str(context, std::move(name), key);
        get_attribute(context, object, roots.root(key.value()), out);
        if (out.tag() != ResultTag::Ok)
            throw std::runtime_error("module attribute failed");
        return roots.root(out.value()).get(context);
    }
};
TEST_F(Imports, MemorySpecsUseCanonicalPolicyAndExposeInitializingAndCode)
{
    source(U"memory_one", "during = __spec__._initializing\nvalue = 42\ndef function(): pass\n");
    source(U"memory_two", "value = 7\n");
    ReturnSlot out(context);
    import_module(context, U"memory_one", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto first = roots.root(out.value());
    import_module(context, U"memory_two", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto second = roots.root(out.value());
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto globals = roots.root(module_dict(context, out.value()));
    make_str(context, U"first", out);
    detail::name_store(context, globals.get(context), out.value(), first.get(context));
    make_str(context, U"second", out);
    detail::name_store(context, globals.get(context), out.value(), second.get(context));
    SourceUnit check(SourceBuffer(R"(from importlib import machinery
assert type(first.__spec__) is machinery.ModuleSpec
assert type(second.__spec__) is machinery.ModuleSpec
assert type(first.__loader__) is machinery.MemorySourceLoader
assert type(second.__loader__) is machinery.MemorySourceLoader
assert first.__loader__ is first.__spec__.loader
assert first.during is True
assert first.__spec__._initializing is False
code = first.__loader__.get_code('memory_one')
assert type(code) is type(second.__loader__.get_code('memory_two'))
assert type(code) is type(first.function.__code__)
)"));
    run(context, {&check, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}

TEST_F(Imports, NativeLoaderDoesNotExposeSourceOrCode)
{
    source(U"native_policy_check", R"(import _imp
from importlib import machinery
assert type(_imp.__loader__) is machinery.NativeLoader
assert _imp.__loader__.get_source('_imp') is None
assert _imp.__loader__.get_code('_imp') is None
)");
    ReturnSlot out(context);
    import_module(context, U"native_policy_check", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}
TEST_F(Imports, PythonSpecPolicyAndModuleConstructionAreRealObjects)
{
    source(U"through_loader", "value = 41\n");
    source(U"spec_policy_check", R"(import sys
import importlib
from importlib.machinery import ModuleSpec
from importlib.util import module_from_spec
assert importlib._bootstrap.ModuleSpec is ModuleSpec
a = ModuleSpec('pkg.child', None, origin='memory')
b = ModuleSpec('pkg.child', None, origin='memory')
assert a == b
assert a.parent == 'pkg'
assert a.has_location is False
a.has_location = True
a.cached = 'custom'
assert a != b
assert a.has_location is True
assert a.cached == 'custom'
module = module_from_spec(a)
assert module.__name__ == 'pkg.child'
assert module.__package__ == 'pkg'
assert module.__spec__ is a
assert module.__file__ == 'memory'
assert module.__cached__ == 'custom'
assert 'pkg.child' not in sys.modules
p = ModuleSpec('parent', None, is_package=True)
package = module_from_spec(p)
assert package.__path__ is p.submodule_search_locations
assert p.parent == 'parent'
assert importlib.import_module('importlib.util') is sys.modules['importlib.util']
assert importlib.import_module('.util', 'importlib') is sys.modules['importlib.util']
loader_type = importlib._bootstrap.MemorySourceLoader
execute = loader_type.exec_module
def observed(self, module):
    module.via_loader = True
    return execute(self, module)
loader_type.exec_module = observed
try:
    loaded = importlib.import_module('through_loader')
    assert loaded.via_loader is True
    assert loaded.value == 41
finally:
    loader_type.exec_module = execute
)");
    ReturnSlot out(context);
    import_module(context, U"spec_policy_check", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
}
TEST_F(Imports, FailedBodyClearsThePublishedSpecFlag)
{
    source(U"failed_flag", "import sys\nsys.failed_spec = __spec__\nraise ValueError('body')\n");
    ReturnSlot out(context);
    import_module(context, U"failed_flag", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    EXPECT_FALSE(
        detail::name_lookup(context, context.builtin(CoreObject::Modules), U"failed_flag"));
    import_module(context, U"sys", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto spec = roots.root(attribute(roots.root(out.value()), U"failed_spec"));
    EXPECT_EQ(attribute(spec, U"_initializing"), context.builtin(CoreObject::False));
}
TEST_F(Imports, RecursiveReloadRejectsANoncanonicalModule)
{
    source(U"recursive_reload", R"(import sys
import importlib
fake = type(sys)(__name__)
try:
    importlib.reload(fake)
except ImportError:
    rejected = True
else:
    raise AssertionError('reload accepted a noncanonical module')
assert importlib.reload(sys.modules[__name__]) is sys.modules[__name__]
)");
    ReturnSlot out(context);
    import_module(context, U"recursive_reload", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(attribute(roots.root(out.value()), U"rejected"), context.builtin(CoreObject::True));
}
TEST_F(Imports, SourceNamespaceAndCoreModuleIdentitiesAreAuthoritative)
{
    source(U"example", "value = 40\ndef answer(): return value + 2\n");
    ReturnSlot out(context);
    import_module(context, U"example", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto module = roots.root(out.value());
    EXPECT_EQ(*detail::name_lookup(context, context.builtin(CoreObject::Modules), U"example"),
              module.get(context));
    auto function = roots.root(attribute(module, U"answer"));
    call(context, function, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "42");
    import_module(context, U"example", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), module.get(context));
    import_module(context, U"sys", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(attribute(roots.root(out.value()), U"modules"), context.builtin(CoreObject::Modules));
    import_module(context, U"builtins", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(module_dict(context, out.value()), context.builtin(CoreObject::Builtins));
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    EXPECT_TRUE(is_module(context, out.value()));
}
TEST_F(Imports, RecursiveSourceImportsSeeThePublishedPartialNamespace)
{
    source(U"a", "value = 40\nb = __import__('b')\nanswer = b.answer\n");
    source(U"b", "a = __import__('a')\nanswer = a.value + 2\n");
    ReturnSlot out(context);
    import_module(context, U"a", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"answer")), "42");
}
TEST_F(Imports, RegisteredPackagesHonorFromlistRelativeNamesAndParentBinding)
{
    register_source_module(
        context, U"pkg",
        std::make_shared<const SourceUnit>(SourceBuffer("value = 40\n__all__ = ('part',)\n")),
        true);
    source(U"pkg.part", "answer = __import__('pkg').value + 2\n");
    ReturnSlot out(context), text(context), from(context);
    make_str(context, U"pkg", text);
    const auto package_name = roots.root(text.value());
    make_str(context, U"*", text);
    cxxpython::core::make_tuple(context, std::array{text.value()}, from);
    const auto all = roots.root(from.value());
    const auto zero = roots.root(context.builtin(CoreObject::IntZero));
    const auto globals = roots.root(context.builtin(CoreObject::Builtins));
    import_name(context, package_name, globals, all, zero, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto package = roots.root(out.value());
    const auto child = roots.root(attribute(package, U"part"));
    make_str(context, U"pkg.part", text);
    import_name(context, roots.root(text.value()), globals,
                roots.root(context.builtin(CoreObject::EmptyTuple)), zero, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), package.get(context));
    make_str(context, U"part", text);
    const auto part = roots.root(text.value());
    make_str(context, U"answer", from);
    const auto answer = roots.root(from.value());
    cxxpython::core::make_tuple(context, std::array{answer.get(context)}, from);
    import_name(context, part, roots.root(module_dict(context, child.get(context))),
                roots.root(from.value()), roots.root(context.builtin(CoreObject::IntOne)), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), child.get(context));
    import_from(context, child, answer, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, out.value()), "42");
}
TEST_F(Imports, InitializationAllocationFailuresRollbackWithoutManagedAllocation)
{
    source(U"fragile", "value = (7, 8)\ndef answer(): return value[1]\n");
    ReturnSlot out(context);
    bool succeeded = false;
    for (std::size_t quota = 0; quota < 300; ++quota) {
        SCOPED_TRACE(quota);
        context.set_allocation_limit(quota);
        import_module(context, U"fragile", out);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        if (out.tag() == ResultTag::Ok) {
            succeeded = true;
            break;
        }
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::MemoryErrorType));
        EXPECT_FALSE(
            detail::name_lookup(context, context.builtin(CoreObject::Modules), U"fragile"));
        context.collect();
    }
    EXPECT_TRUE(succeeded);
}
TEST_F(Imports, FailureDoesNotEraseAUserReplacement)
{
    register_native_module(
        context, U"replaced", +[](Attachment &actor, Handle module, ReturnSlot &out) {
            const auto key =
                *detail::name_lookup(actor, module_dict(actor, module.get(actor)), U"__name__");
            detail::name_store(actor, actor.builtin(CoreObject::Modules), key,
                               actor.builtin(CoreObject::True));
            raise_error(actor, CoreObject::ValueErrorType, U"failed after replacement", out);
        });
    ReturnSlot out(context);
    import_module(context, U"replaced", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    import_module(context, U"replaced", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
}
TEST_F(Imports, PublicModulesLookupUsesCustomKeyEqualityAndPropagatesErrors)
{
    source(U"cached_target", "value = 42\n");
    source(U"error_target", "value = 42\n");
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto globals = roots.root(module_dict(context, out.value()));
    SourceUnit setup(SourceBuffer(R"(import sys
class CacheKey:
    def __init__(self, name, fail):
        self.name = name
        self.fail = fail
    def __hash__(self):
        return hash(self.name)
    def __eq__(self, other):
        if self.fail:
            raise ValueError('cache equality')
        return self.name == other
sys.modules[CacheKey('cached_target', False)] = True
sys.modules[CacheKey('error_target', True)] = False
)"));
    run(context, {&setup, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    import_module(context, U"cached_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    import_module(context, U"error_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
}
TEST_F(Imports, NoneInPublicModulesStopsInitialization)
{
    source(U"blocked_target", "raise ValueError('initialized')\n");
    ReturnSlot out(context), key(context);
    make_str(context, U"blocked_target", key);
    auto name = roots.root(key.value());
    auto modules = roots.root(context.builtin(CoreObject::Modules));
    dict_store(context, modules, name, roots.root(context.builtin(CoreObject::None)), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    import_module(context, U"blocked_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()),
              context.builtin(CoreObject::ModuleNotFoundErrorType));
    dict_lookup(context, modules, name, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::None));
}
TEST_F(Imports, EqualKeyPublicationRollsBackAtZeroQuotaWithoutAnotherCallback)
{
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto main = roots.root(out.value());
    const auto globals = roots.root(module_dict(context, main.get(context)));
    SourceUnit setup(SourceBuffer(R"(import sys
class CacheKey:
    calls = 0
    match = False
    def __hash__(self):
        return hash('unstable_cache')
    def __eq__(self, other):
        self.calls = self.calls + 1
        return self.match
key = CacheKey()
sys.modules[key] = False
from importlib._bootstrap import NativeLoader
create = NativeLoader.create_module
execute = NativeLoader.exec_module
def created(self, spec):
    module = create(self, spec)
    if spec.name == 'unstable_cache':
        key.match = True
    return module
def executed(self, module):
    if module.__name__ == 'unstable_cache':
        key.before_cleanup = key.calls
    return execute(self, module)
NativeLoader.create_module = created
NativeLoader.exec_module = executed
)"));
    run(context, {&setup, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto key = roots.root(attribute(main, U"key"));
    register_native_module(
        context, U"unstable_cache", +[](Attachment &actor, Handle, ReturnSlot &result) {
            actor.set_allocation_limit(0);
            result.raised(actor.builtin(CoreObject::MemoryError));
        });
    import_module(context, U"unstable_cache", out);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(out.exception(), context.builtin(CoreObject::MemoryError));
    context.collect();
    EXPECT_EQ(int_decimal(context, attribute(key, U"calls")),
              int_decimal(context, attribute(key, U"before_cleanup")));
    dict_lookup(context, roots.root(context.builtin(CoreObject::Modules)), key, out);
    EXPECT_EQ(out.tag(), ResultTag::Empty);
}
TEST_F(Imports, EmptyNativeInitializerRaisesAndRemovesItsPublication)
{
    register_native_module(
        context, U"empty_initializer", +[](Attachment &, Handle, ReturnSlot &) {});
    ReturnSlot out(context);
    import_module(context, U"empty_initializer", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::RuntimeErrorType));
    EXPECT_FALSE(
        detail::name_lookup(context, context.builtin(CoreObject::Modules), U"empty_initializer"));
}
TEST_F(Imports, DeletingPublicModulesEntryReexecutesSource)
{
    source(U"repeat_target", "import __main__\n__main__.runs = __main__.runs + 1\n");
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto main = roots.root(out.value());
    auto globals = roots.root(module_dict(context, main.get(context)));
    SourceUnit setup(SourceBuffer("runs = 0\n"));
    run(context, {&setup, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"repeat_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto first = roots.root(out.value());
    SourceUnit remove(SourceBuffer("import sys\ndel sys.modules['repeat_target']\n"));
    run(context, {&remove, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    import_module(context, U"repeat_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_NE(out.value(), first.get(context));
    EXPECT_EQ(int_decimal(context, attribute(main, U"runs")), "2");
}
TEST_F(Imports, FailedSourcePreservesPythonDeleteAndReinsertReplacement)
{
    source(U"replacement_target", "import sys\ndel sys.modules['replacement_target']\n"
                                  "sys.modules['replacement_target'] = True\n"
                                  "raise ValueError('failed')\n");
    ReturnSlot out(context), key(context);
    import_module(context, U"replacement_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    context.collect();
    make_str(context, U"replacement_target", key);
    dict_lookup(context, roots.root(context.builtin(CoreObject::Modules)), roots.root(key.value()),
                out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    import_module(context, U"replacement_target", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
}
TEST_F(Imports, RollbackAtZeroQuotaPreservesModulesLoadedDuringInitialization)
{
    source(U"survivor", "value = 42\n");
    register_native_module(
        context, U"lowmem", +[](Attachment &actor, Handle, ReturnSlot &out) {
            import_module(actor, U"survivor", out);
            if (out.tag() == ResultTag::Raised)
                return;
            actor.set_allocation_limit(0);
            raise_error(actor, CoreObject::ValueErrorType, U"allocation fails here", out);
        });
    ReturnSlot out(context);
    import_module(context, U"lowmem", out);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::MemoryErrorType));
    EXPECT_FALSE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"lowmem"));
    EXPECT_TRUE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"survivor"));
}

struct ImportGate {
    explicit ImportGate(int count) : entered(count) {}
    std::latch entered, release{1};
    std::atomic<int> calls{0};
};
struct GateObject {
    ObjectHeader header;
    ImportGate *const gate;
    GateObject(InitialType head, ImportGate *value) : header(head), gate(value) {}
    void trace(Tracer &) const noexcept {}
};
TEST(ParallelImports, ColdBootstrapConcurrentAndAllocationFailureRetry)
{
    for (std::size_t quota : {0u, 1u, 10u, 100u, 1000u, 5000u}) {
        SCOPED_TRACE(quota);
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment owner(interpreter);
        owner.bootstrap_core();
        ReturnSlot out(owner);
        owner.set_allocation_limit(quota);
        initialize_imports(owner, out);
        owner.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        if (out.tag() == ResultTag::Raised)
            ASSERT_EQ(owner.type_of(out.exception()), owner.builtin(CoreObject::MemoryErrorType));
        else
            ASSERT_EQ(out.tag(), ResultTag::Ok);
        owner.collect();
        initialize_imports(owner, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment owner(interpreter);
    owner.bootstrap_core();
    std::latch start{1};
    std::array<std::future<bool>, 2> workers;
    {
        NativeWait parked(owner);
        for (auto &worker : workers)
            worker = std::async(std::launch::async, [&] {
                Attachment actor(interpreter);
                ReturnSlot out(actor);
                {
                    NativeWait waiting(actor);
                    start.wait();
                }
                initialize_imports(actor, out);
                if (out.tag() != ResultTag::Ok)
                    return false;
                actor.collect();
                return true;
            });
        start.count_down();
        for (auto &worker : workers)
            EXPECT_TRUE(worker.get());
    }
}
TEST(ParallelImports, PerModuleLeasesAndCrossThreadCyclesParkForGc)
{
    for (int variant : {0, 1, 2}) {
        SCOPED_TRACE(variant);
        Interpreter interpreter;
        Attachment owner(interpreter);
        owner.bootstrap_core();
        RootScope roots(owner);
        ReturnSlot out(owner);
        // This host control outlives every future/callback. The managed capture
        // owns no Python references outside the normal traced captures vector.
        ImportGate gate(variant == 0 ? 1 : 2);
        owner.allocate<GateObject>(out, &gate);
        auto control = roots.root(out.value());
        make_native_function(
            owner, "gate", {},
            +[](Attachment &actor, Handle function, std::span<const Value>, ReturnSlot &result) {
                const auto &captures = actor.payload<NativeFunctionObject>(function).captures;
                auto &gate = *actor.payload<GateObject>(captures[0]).gate;
                gate.calls.fetch_add(1);
                gate.entered.count_down();
                {
                    NativeWait parked(actor);
                    gate.release.wait();
                }
                actor.collect();
                result.ok(actor.builtin(CoreObject::None));
            },
            std::array{control.get(owner)}, out);
        auto callback = roots.root(out.value());
        make_str(owner, U"gate", out);
        detail::name_store(owner, owner.builtin(CoreObject::Builtins), out.value(),
                           callback.get(owner));
        register_source_module(owner, U"left",
                               std::make_shared<const SourceUnit>(SourceBuffer(
                                   variant == 2 ? "gate()\nimport right as peer\nvalue = 42\n"
                                                : "gate()\nvalue = 42\n")));
        register_source_module(owner, U"right",
                               std::make_shared<const SourceUnit>(SourceBuffer(
                                   variant == 2 ? "gate()\nimport left as peer\nvalue = 42\n"
                                                : "gate()\nvalue = 42\n")));
        std::array<std::future<Transfer>, 2> workers;
        {
            NativeWait parked(owner);
            for (std::size_t i = 0; i < workers.size(); ++i)
                workers[i] = std::async(std::launch::async, [&, i] {
                    Attachment actor(interpreter);
                    RootScope local(actor);
                    ReturnSlot result(actor);
                    import_module(actor, i == 0 || variant == 0 ? U"left" : U"right", result);
                    if (result.tag() != ResultTag::Ok)
                        throw std::runtime_error("parallel import raised");
                    return actor.make_transfer(local.root(result.value()));
                });
            gate.entered.wait();
        }
        owner.collect();
        gate.release.count_down();
        std::optional<Handle> first;
        for (auto &worker : workers) {
            Transfer token;
            {
                NativeWait parked(owner);
                token = worker.get();
            }
            owner.receive(token, out);
            EXPECT_TRUE(is_module(owner, out.value()));
            if (first && variant == 0) {
                EXPECT_EQ(first->get(owner), out.value());
            }
            first = roots.root(out.value());
        }
        EXPECT_EQ(gate.calls.load(), variant == 0 ? 1 : 2);
    }
}
TEST_F(Imports, NamespaceViewsRetainOrderAndValuesAcrossDeletionAndCollection)
{
    ReturnSlot out(context);
    make_module(context, U"view", out);
    auto module = roots.root(out.value());
    auto dictionary = roots.root(module_dict(context, module.get(context)));
    ReturnSlot key(context);
    make_str(context, U"last", key);
    make_str(context, U"retained", out);
    detail::name_store(context, dictionary.get(context), key.value(), out.value());
    DictView view(context, dictionary.get(context));
    EXPECT_EQ(context.payload<StrObject>(view.entries().back().key).value, U"last");
    context.set_allocation_limit(0);
    EXPECT_TRUE(detail::name_erase(context, dictionary.get(context), U"last"));
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    out.reset();
    context.collect();
    ASSERT_TRUE(view.find(U"last"));
    EXPECT_EQ(context.payload<StrObject>(*view.find(U"last")).value, U"retained");
    EXPECT_FALSE(detail::name_lookup(context, dictionary.get(context), U"last"));
}
} // namespace
