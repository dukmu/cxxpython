#include "cxxpython/objects.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class CoreCells : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.result().value());
    }
    Handle cell(std::span<const Value> values = {})
    {
        ReturnSlot out(context);
        call(context, roots.root(context.builtin(CoreObject::CellType)), {values, {}}, out);
        return roots.root(out.result().value());
    }
    Handle dictionary()
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        return roots.root(out.result().value());
    }
    void put(Handle dictionary, std::u32string key, Handle value)
    {
        detail::name_store(context, dictionary.get(context), text(std::move(key)).get(context),
                           value.get(context));
    }
    void define(Handle dictionary, ReturnSlot &out, Handle bases)
    {
        auto name = text(U"Created");
        std::array args{name.get(context), bases.get(context), dictionary.get(context)};
        call(context, roots.root(context.type_type()), {args, {}}, out);
    }
    Handle empty_bases()
    {
        ReturnSlot out(context);
        cxxpython::core::make_tuple(context, {}, out);
        return roots.root(out.result().value());
    }
    void expect_error(ReturnSlot &out, CoreObject type)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
        context.collect();
    }
};

TEST_F(CoreCells, EmptyContentsAreDistinctFromNoneAndDeletionIsIdempotent)
{
    auto value = cell();
    auto name = text(U"cell_contents");
    ReturnSlot out(context);
    get_attribute(context, value, name, out);
    expect_error(out, CoreObject::ValueErrorType);
    auto none = roots.root(context.builtin(CoreObject::None));
    set_attribute(context, value, name, none, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    get_attribute(context, value, name, out);
    EXPECT_EQ(out.result().value(), none.get(context));
    del_attribute(context, value, name, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    del_attribute(context, value, name, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    get_attribute(context, value, name, out);
    expect_error(out, CoreObject::ValueErrorType);
}

TEST_F(CoreCells, ConstructorChecksArityKeywordsAndSubclassability)
{
    auto type = roots.root(context.builtin(CoreObject::CellType));
    auto initial = text(U"initial");
    std::array args{initial.get(context)};
    auto value = cell(args);
    auto name = text(U"cell_contents");
    ReturnSlot out(context);
    get_attribute(context, value, name, out);
    EXPECT_EQ(out.result().value(), initial.get(context));
    std::array excess{initial.get(context), initial.get(context)};
    call(context, type, {excess, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
    std::array keywords{KeywordArg{name.get(context), initial.get(context)}};
    call(context, type, {{}, keywords}, out);
    expect_error(out, CoreObject::TypeErrorType);
    EXPECT_THROW(create_type(context, {"NotACellSubclass", {&type, 1}}, out), TypeDefinitionError);
}

TEST_F(CoreCells, SharedNativeClosuresObserveOneCellAcrossMutationAndCollection)
{
    auto shared = cell();
    ReturnSlot out(context);
    std::array captures{shared.get(context)};
    make_native_function(
        context, "closure", {},
        [](Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &result) {
            c.collect();
            if (!read_cell(c, c.payload<NativeFunctionObject>(fn).captures[0], result))
                raise_error(c, CoreObject::ValueErrorType, U"empty closure", result);
        },
        captures, out);
    auto closure = roots.root(out.result().value());
    auto name = text(U"cell_contents");
    auto first = text(U"first");
    auto second = text(U"second");
    set_attribute(context, shared, name, first, out);
    call(context, closure, {}, out);
    EXPECT_EQ(out.result().value(), first.get(context));
    set_attribute(context, shared, name, second, out);
    roots.release(shared);
    call(context, closure, {}, out);
    EXPECT_EQ(out.result().value(), second.get(context));
}

TEST_F(CoreCells, SelfReferentialCellCyclesAreCollectedWithoutRefcounting)
{
    context.collect();
    const auto before = context.stats().objects;
    auto value = cell();
    auto name = text(U"cell_contents");
    ReturnSlot out(context);
    set_attribute(context, value, name, value, out);
    roots.release(value);
    out.reset();
    context.collect();
    EXPECT_EQ(context.stats().objects, before + 1); // only the rooted attribute name remains
}

TEST_F(CoreCells, ClassCellIsFilledAndConsumedWithoutChangingTheInputNamespace)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    put(dictionary, U"__classcell__", shared);
    ReturnSlot out(context);
    define(dictionary, out, empty_bases());
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    auto cls = roots.root(out.result().value());
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), cls.get(context));
    EXPECT_FALSE(
        detail::name_lookup(context, *TypeView(context, cls).dictionary(), U"__classcell__"));
    EXPECT_EQ(detail::name_lookup(context, dictionary.get(context), U"__classcell__"),
              shared.get(context));
    context.collect();
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), cls.get(context));
}

TEST_F(CoreCells, SetNameSeesTheFilledCellAndCanMutateIt)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    ReturnSlot out(context);
    create_type(context, {"Descriptor"}, out);
    auto type = roots.root(out.result().value());
    SignatureLayout signature;
    signature.parameters = {{U"self"}, {U"owner"}, {U"name"}};
    std::array captures{shared.get(context)};
    make_native_function(
        context, "__set_name__", signature,
        [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &result) {
            auto shared = c.payload<NativeFunctionObject>(fn).captures[0];
            ReturnSlot contents(c);
            ASSERT_TRUE(read_cell(c, shared, contents));
            EXPECT_EQ(contents.result().value(), args[1]);
            EXPECT_FALSE(
                detail::name_lookup(c, *TypeView(c, args[1]).dictionary(), U"__classcell__"));
            c.collect();
            write_cell(c, shared, c.builtin(CoreObject::None));
            result.ok(c.builtin(CoreObject::None));
        },
        captures, out);
    auto function = roots.root(out.result().value());
    auto name = text(U"__set_name__");
    set_attribute(context, type, name, function, out);
    context.allocate_instance<InstanceObject>(type, out);
    auto descriptor = roots.root(out.result().value());
    put(dictionary, U"field", descriptor);
    put(dictionary, U"__classcell__", shared);
    define(dictionary, out, empty_bases());
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::None));
}

TEST_F(CoreCells, FailedCallbackLeavesThePublishedCellSideEffectIntact)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    ReturnSlot out(context);
    create_type(context, {"Base"}, out);
    auto base = roots.root(out.result().value());
    SignatureLayout signature;
    signature.parameters = {{U"cls"}};
    make_native_function(
        context, "__init_subclass__", signature,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
            raise_error(c, CoreObject::RuntimeErrorType, U"hook failed", result);
        },
        {}, out);
    auto function = roots.root(out.result().value());
    set_attribute(context, base, text(U"__init_subclass__"), function, out);
    put(dictionary, U"__classcell__", shared);
    std::array items{base.get(context)};
    cxxpython::core::make_tuple(context, items, out);
    auto bases = roots.root(out.result().value());
    define(dictionary, out, bases);
    expect_error(out, CoreObject::RuntimeErrorType);
    ReturnSlot contents(context);
    ASSERT_TRUE(read_cell(context, shared.get(context), contents));
    const auto value = contents.result().value();
    EXPECT_EQ(context.type_of(value), context.type_type());
    EXPECT_TRUE(is_subtype(context, value, base.get(context)));
}

TEST_F(CoreCells, InvalidClassCellDoesNotAcceptAnUnrelatedCppCellLayout)
{
    ReturnSlot out(context);
    create_native_type(context, {{"NotCell"}, LayoutDescriptor::of<CellObject>()}, out);
    auto fake_type = roots.root(out.result().value());
    context.allocate_instance<CellObject>(fake_type, out);
    auto fake = roots.root(out.result().value());
    auto dictionary = this->dictionary();
    put(dictionary, U"__classcell__", fake);
    define(dictionary, out, empty_bases());
    expect_error(out, CoreObject::TypeErrorType);
    EXPECT_FALSE(read_cell(context, fake.get(context), out));
}

TEST_F(CoreCells, AllocationFailuresNeverPublishAnUnpreparedDefinitionThroughTheCell)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    ReturnSlot out(context);
    put(dictionary, U"__classcell__", shared);
    SignatureLayout signature;
    signature.parameters = {{U"cls"}};
    make_native_function(
        context, "__new__", signature,
        [](Attachment &c, Handle, std::span<const Value>, ReturnSlot &result) {
            result.ok(c.builtin(CoreObject::None));
        },
        {}, out);
    put(dictionary, U"__new__", roots.root(out.result().value()));
    auto name = text(U"Created");
    auto bases = empty_bases();
    auto metaclass = roots.root(context.type_type());
    std::array args{name.get(context), bases.get(context), dictionary.get(context)};
    bool completed = false;
    for (std::size_t quota = 0; quota < 100; ++quota) {
        write_cell(context, shared.get(context), {});
        context.set_allocation_limit(quota);
        call(context, metaclass, {args, {}}, out);
        completed = out.result().tag() == ResultTag::Ok;
        if (!completed) {
            EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
        }
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        ReturnSlot contents(context);
        if (read_cell(context, shared.get(context), contents)) {
            auto &type = context.payload<TypeObject>(contents.result().value());
            EXPECT_EQ(type.state, TypeState::Ready);
            TypeView metadata(context, contents.result().value());
            EXPECT_FALSE(detail::name_lookup(context, *metadata.dictionary(), U"__classcell__"));
            auto method = detail::name_lookup(context, *metadata.dictionary(), U"__new__");
            ASSERT_TRUE(method.has_value());
            EXPECT_EQ(context.type_of(*method), context.builtin(CoreObject::StaticMethodType));
        }
        if (completed)
            break;
    }
    EXPECT_TRUE(completed);
}

TEST_F(CoreCells, ClassDictionaryCellRefersToTheActualCopiedNamespace)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    put(dictionary, U"__classdictcell__", shared);
    ReturnSlot out(context);
    define(dictionary, out, empty_bases());
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto target = *TypeView(context, out.result().value()).dictionary();
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), target);
    EXPECT_NE(target, dictionary.get(context));
    EXPECT_FALSE(detail::name_lookup(context, target, U"__classdictcell__"));
    EXPECT_EQ(detail::name_lookup(context, dictionary.get(context), U"__classdictcell__"),
              shared.get(context));
    context.collect();
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), target);
}

TEST_F(CoreCells, AliasedClassAndDictionaryCellsFollowTheDefinitionWriteOrder)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    put(dictionary, U"__classcell__", shared);
    put(dictionary, U"__classdictcell__", shared);
    ReturnSlot out(context);
    define(dictionary, out, empty_bases());
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    const auto target = *TypeView(context, out.result().value()).dictionary();
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    EXPECT_EQ(out.result().value(), target);
    EXPECT_FALSE(detail::name_lookup(context, target, U"__classcell__"));
    EXPECT_FALSE(detail::name_lookup(context, target, U"__classdictcell__"));
}

TEST_F(CoreCells, InvalidDictionaryCellKeepsTheEarlierClassCellPublication)
{
    auto shared = cell();
    auto dictionary = this->dictionary();
    put(dictionary, U"__classcell__", shared);
    put(dictionary, U"__classdictcell__", roots.root(context.builtin(CoreObject::None)));
    ReturnSlot out(context);
    define(dictionary, out, empty_bases());
    expect_error(out, CoreObject::TypeErrorType);
    ASSERT_TRUE(read_cell(context, shared.get(context), out));
    const auto value = out.result().value();
    EXPECT_EQ(context.payload<TypeObject>(value).state, TypeState::Ready);
    EXPECT_EQ(context.type_of(value), context.type_type());
}
} // namespace
