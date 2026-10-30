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
