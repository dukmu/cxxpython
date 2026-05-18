#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"
#include "test_support.hpp"

namespace
{
    void init_sets_value(cxxpython::Object &object, const std::vector<cxxpython::Object *> &args)
    {
        if (args.size() != 1)
            cxxpython::raise_type_error(object.runtime(), "InitCarrier() takes exactly one argument");
        cxxpython::object_set_attr(&object, "value", args[0]);
    }
}

TEST(CoreObjects, HeapTypesUseAndInheritSlots)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots custom_slots;
    custom_slots.repr = test_support::custom_repr;
    custom_slots.hash = test_support::custom_hash;
    custom_slots.equal = test_support::custom_equal;
    custom_slots.truth = test_support::custom_false;
    custom_slots.binary_op = test_support::custom_binary_op;
    custom_slots.unary_op = test_support::custom_unary_op;
    custom_slots.length = test_support::custom_length;
    custom_slots.getitem = test_support::custom_getitem;
    custom_slots.setitem = test_support::custom_setitem;
    custom_slots.contains = test_support::custom_contains;
    cxxpython::TypeObject *custom_type =
        runtime.new_heap_type("Custom", runtime.object_type(), custom_slots);

    cxxpython::Object *custom_a = runtime.allocate<cxxpython::Object>(custom_type);
    cxxpython::Object *custom_b = runtime.allocate<cxxpython::Object>(custom_type);
    ASSERT_NE(custom_type->dict, nullptr);
    auto *custom_name = dynamic_cast<cxxpython::StrObject *>(custom_type->dict->get(runtime.new_str("__name__")));
    ASSERT_NE(custom_name, nullptr);
    EXPECT_EQ(custom_name->value, "Custom");
    EXPECT_EQ(cxxpython::object_repr(custom_a), "<custom>");
    EXPECT_EQ(cxxpython::object_hash(custom_a), 42);
    EXPECT_TRUE(cxxpython::object_equal(custom_a, custom_b));
    EXPECT_EQ(cxxpython::object_rich_compare(custom_a, custom_b, cxxpython::RichCompareOp::Equal),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(custom_a, custom_b, cxxpython::RichCompareOp::NotEqual),
              runtime.false_value());
    EXPECT_FALSE(cxxpython::is_true(custom_a));
    auto *custom_add = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_binary_op(custom_a, custom_b, cxxpython::BinaryOp::Add));
    ASSERT_NE(custom_add, nullptr);
    EXPECT_EQ(custom_add->value, "custom-add");
    auto *custom_negative = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_unary_op(custom_a, cxxpython::UnaryOp::Negative));
    ASSERT_NE(custom_negative, nullptr);
    EXPECT_EQ(custom_negative->value, "custom-negative");
    EXPECT_EQ(cxxpython::object_length(custom_a), 7U);
    EXPECT_EQ(cxxpython::object_get_item(custom_a, custom_b), custom_b);
    EXPECT_TRUE(cxxpython::object_contains(custom_a, custom_b));
    cxxpython::object_set_item(custom_a, custom_b, runtime.new_str("ok"));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, custom_type));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, runtime.object_type()));

    cxxpython::TypeObject *repr_only_type =
        runtime.new_heap_type("ReprOnly", custom_type, cxxpython::TypeSlots{.repr = test_support::custom_repr});
    cxxpython::Object *repr_only = runtime.allocate<cxxpython::Object>(repr_only_type);
    cxxpython::Object *repr_only_peer = runtime.allocate<cxxpython::Object>(repr_only_type);
    EXPECT_EQ(cxxpython::object_repr(repr_only), "<custom>");
    EXPECT_EQ(cxxpython::object_hash(repr_only), 42);
    EXPECT_EQ(cxxpython::object_rich_compare(repr_only, repr_only_peer, cxxpython::RichCompareOp::Equal),
              runtime.true_value());
    auto *inherited_subtract = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_binary_op(repr_only, repr_only_peer, cxxpython::BinaryOp::Subtract));
    ASSERT_NE(inherited_subtract, nullptr);
    EXPECT_EQ(inherited_subtract->value, "custom-subtract");
    auto *inherited_invert = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_unary_op(repr_only, cxxpython::UnaryOp::Invert));
    ASSERT_NE(inherited_invert, nullptr);
    EXPECT_EQ(inherited_invert->value, "custom-invert");
    EXPECT_FALSE(cxxpython::is_true(repr_only));
    EXPECT_EQ(cxxpython::object_length(repr_only), 7U);
    EXPECT_EQ(cxxpython::object_get_item(repr_only, custom_a), custom_a);
    EXPECT_TRUE(cxxpython::object_contains(repr_only, custom_a));

    cxxpython::TypeSlots rich_slots;
    rich_slots.rich_compare = test_support::custom_rich_compare;
    cxxpython::TypeObject *rich_type = runtime.new_heap_type("Rich", runtime.object_type(), rich_slots);
    cxxpython::Object *rich_a = runtime.allocate<cxxpython::Object>(rich_type);
    cxxpython::Object *rich_b = runtime.allocate<cxxpython::Object>(rich_type);
    EXPECT_FALSE(cxxpython::object_equal(rich_a, rich_b));
    EXPECT_EQ(cxxpython::object_rich_compare(rich_a, rich_b, cxxpython::RichCompareOp::Equal),
              runtime.true_value());

    cxxpython::TypeObject *rich_child_type = runtime.new_heap_type("RichChild", rich_type);
    cxxpython::Object *rich_child_a = runtime.allocate<cxxpython::Object>(rich_child_type);
    cxxpython::Object *rich_child_b = runtime.allocate<cxxpython::Object>(rich_child_type);
    EXPECT_EQ(cxxpython::object_rich_compare(rich_child_a, rich_child_b, cxxpython::RichCompareOp::Equal),
              runtime.true_value());
}

TEST(CoreObjects, TypeFlagsSeparateCompactBuiltinsFromHeapInstances)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *heap_type = runtime.new_heap_type("HeapObject", runtime.object_type());
    cxxpython::TypeObject *int_child = runtime.new_heap_type("IntChild", runtime.int_type());

    EXPECT_EQ(runtime.object_type()->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
    EXPECT_EQ(runtime.list_type()->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
    EXPECT_NE(heap_type->flags & cxxpython::TypeFlagHeapType, 0U);
    EXPECT_NE(heap_type->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
    EXPECT_EQ(runtime.object_type()->solid_base, runtime.object_type());
    EXPECT_EQ(runtime.list_type()->solid_base, runtime.list_type());
    EXPECT_EQ(runtime.dict_type()->solid_base, runtime.dict_type());
    EXPECT_EQ(heap_type->solid_base, runtime.object_type());
    EXPECT_EQ(int_child->solid_base, runtime.int_type());
}

TEST(CoreObjects, ClassCreationRejectsIncompatibleNativeLayouts)
{
    cxxpython::Runtime runtime;

    auto *list_child = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("ListChild"), runtime.new_tuple({runtime.list_type()}), runtime.new_dict()}));
    ASSERT_NE(list_child, nullptr);
    EXPECT_EQ(list_child->base, runtime.list_type());
    EXPECT_EQ(list_child->solid_base, runtime.list_type());
    auto *list_child_instance = dynamic_cast<cxxpython::ListObject *>(cxxpython::object_call(list_child, {}));
    ASSERT_NE(list_child_instance, nullptr);
    EXPECT_EQ(list_child_instance->type(), list_child);

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("Bad"),
             runtime.new_tuple({runtime.list_type(), runtime.dict_type()}),
             runtime.new_dict()});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "multiple bases have instance lay-out conflict");
    }

    auto *dict_child = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("DictChild"), runtime.new_tuple({runtime.dict_type()}), runtime.new_dict()}));
    ASSERT_NE(dict_child, nullptr);
    EXPECT_EQ(dict_child->solid_base, runtime.dict_type());

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("BadDerived"),
             runtime.new_tuple({list_child, dict_child}),
             runtime.new_dict()});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "multiple bases have instance lay-out conflict");
    }
}

TEST(CoreObjects, TypeCallConstructsObjectsThroughNewInitProtocol)
{
    cxxpython::Runtime runtime;

    cxxpython::Object *plain_object = cxxpython::object_call(runtime.object_type(), {});
    ASSERT_NE(plain_object, nullptr);
    EXPECT_EQ(plain_object->type(), runtime.object_type());
    EXPECT_EQ(cxxpython::object_call(runtime.type_type(), {plain_object}), runtime.object_type());

    cxxpython::TypeObject *heap_type = runtime.new_heap_type("HeapObject", runtime.object_type());
    cxxpython::Object *heap_object = cxxpython::object_call(heap_type, {});
    ASSERT_NE(heap_object, nullptr);
    EXPECT_EQ(heap_object->type(), heap_type);
    EXPECT_TRUE(cxxpython::is_instance(heap_object, runtime.object_type()));

    cxxpython::Object *stored = runtime.new_str("stored");
    cxxpython::object_set_attr(heap_object, "field", stored);
    EXPECT_EQ(cxxpython::object_get_attr(heap_object, "field"), stored);

    cxxpython::TypeObject *init_type = runtime.new_heap_type(
        "InitCarrier",
        runtime.object_type(),
        cxxpython::TypeSlots{.init = init_sets_value});
    cxxpython::Object *argument = runtime.new_str("initialized");
    cxxpython::Object *initialized = cxxpython::object_call(init_type, {argument});
    ASSERT_NE(initialized, nullptr);
    EXPECT_EQ(initialized->type(), init_type);
    EXPECT_EQ(cxxpython::object_get_attr(initialized, "value"), argument);

    auto *list = dynamic_cast<cxxpython::ListObject *>(cxxpython::object_call(runtime.list_type(), {}));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->type(), runtime.list_type());
    EXPECT_EQ(list->size(), 0U);

    try
    {
        (void)cxxpython::object_call(runtime.list_type(), {runtime.new_int(1)});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "list() takes no arguments");
    }

    try
    {
        (void)cxxpython::object_call(runtime.object_type(), {runtime.new_int(1)});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "object() takes no arguments");
    }

    try
    {
        (void)cxxpython::object_call(runtime.type_type(), {});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "type() takes 1 or 3 arguments");
    }
}

TEST(CoreObjects, TypeCallCreatesHeapTypesFromNameBasesNamespace)
{
    cxxpython::Runtime runtime;

    cxxpython::DictObject *base_namespace = runtime.new_dict();
    cxxpython::Object *base_value = runtime.new_str("base-value");
    base_namespace->set(runtime.new_str("base_attr"), base_value);
    auto *base_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("Base"), runtime.new_tuple({runtime.object_type()}), base_namespace}));
    ASSERT_NE(base_type, nullptr);
    EXPECT_EQ(base_type->name, "Base");
    EXPECT_EQ(base_type->base, runtime.object_type());
    EXPECT_EQ(base_type->bases.size(), 1U);
    EXPECT_EQ(base_type->bases[0], runtime.object_type());
    EXPECT_EQ(cxxpython::object_get_attr(base_type, "base_attr"), base_value);

    cxxpython::DictObject *namespace_dict = runtime.new_dict();
    cxxpython::Object *class_value = runtime.new_str("class-value");
    namespace_dict->set(runtime.new_str("class_attr"), class_value);
    namespace_dict->set(runtime.new_str("__module__"), runtime.new_str("custom_module"));
    auto *type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("Made"), runtime.new_tuple({base_type}), namespace_dict}));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name, "Made");
    EXPECT_EQ(type->module, "custom_module");
    EXPECT_EQ(type->base, base_type);
    ASSERT_EQ(type->bases.size(), 1U);
    EXPECT_EQ(type->bases[0], base_type);
    EXPECT_TRUE(cxxpython::is_subclass(type, base_type));
    EXPECT_TRUE(cxxpython::is_subclass(type, runtime.object_type()));
    EXPECT_EQ(cxxpython::object_get_attr(type, "class_attr"), class_value);
    EXPECT_EQ(cxxpython::object_get_attr(type, "base_attr"), base_value);

    auto *bases_attr = dynamic_cast<cxxpython::TupleObject *>(cxxpython::object_get_attr(type, "__bases__"));
    ASSERT_NE(bases_attr, nullptr);
    ASSERT_EQ(bases_attr->size(), 1U);
    EXPECT_EQ(bases_attr->items()[0], base_type);

    cxxpython::Object *instance = cxxpython::object_call(type, {});
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->type(), type);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "class_attr"), class_value);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "base_attr"), base_value);

    auto *default_base_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("DefaultBase"), runtime.new_tuple({}), runtime.new_dict()}));
    ASSERT_NE(default_base_type, nullptr);
    EXPECT_EQ(default_base_type->base, runtime.object_type());
    ASSERT_EQ(default_base_type->bases.size(), 1U);
    EXPECT_EQ(default_base_type->bases[0], runtime.object_type());

    cxxpython::DictObject *right_namespace = runtime.new_dict();
    cxxpython::Object *right_value = runtime.new_str("right-value");
    right_namespace->set(runtime.new_str("right_attr"), right_value);
    auto *right_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("Right"), runtime.new_tuple({runtime.object_type()}), right_namespace}));
    ASSERT_NE(right_type, nullptr);

    cxxpython::DictObject *multi_namespace = runtime.new_dict();
    auto *multi_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("Multi"), runtime.new_tuple({base_type, right_type}), multi_namespace}));
    ASSERT_NE(multi_type, nullptr);
    EXPECT_EQ(multi_type->base, base_type);
    ASSERT_EQ(multi_type->bases.size(), 2U);
    EXPECT_EQ(multi_type->bases[0], base_type);
    EXPECT_EQ(multi_type->bases[1], right_type);
    ASSERT_GE(multi_type->mro.size(), 4U);
    EXPECT_EQ(multi_type->mro[0], multi_type);
    EXPECT_EQ(multi_type->mro[1], base_type);
    EXPECT_EQ(multi_type->mro[2], right_type);
    EXPECT_EQ(multi_type->mro.back(), runtime.object_type());
    EXPECT_EQ(cxxpython::object_get_attr(multi_type, "base_attr"), base_value);
    EXPECT_EQ(cxxpython::object_get_attr(multi_type, "right_attr"), right_value);
}

TEST(CoreObjects, TypeCallValidatesThreeArgumentClassCreation)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *namespace_dict = runtime.new_dict();

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_int(1), runtime.new_tuple({runtime.object_type()}), namespace_dict});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "type() name must be a str");
    }

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("BadBases"), runtime.new_list({runtime.object_type()}), namespace_dict});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "type() bases must be a tuple");
    }

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("BadBase"), runtime.new_tuple({runtime.new_str("not-type")}), namespace_dict});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "type() bases must contain only types");
    }

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("BadNamespace"), runtime.new_tuple({runtime.object_type()}), runtime.new_list({})});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "type() namespace must be a dict");
    }

    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("DuplicateBase"),
             runtime.new_tuple({runtime.object_type(), runtime.object_type()}),
             namespace_dict});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "duplicate base class object");
    }

    auto *x_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("X"), runtime.new_tuple({runtime.object_type()}), runtime.new_dict()}));
    auto *y_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("Y"), runtime.new_tuple({runtime.object_type()}), runtime.new_dict()}));
    auto *a_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("A"), runtime.new_tuple({x_type, y_type}), runtime.new_dict()}));
    auto *b_type = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_call(
        runtime.type_type(),
        {runtime.new_str("B"), runtime.new_tuple({y_type, x_type}), runtime.new_dict()}));
    ASSERT_NE(a_type, nullptr);
    ASSERT_NE(b_type, nullptr);
    try
    {
        (void)cxxpython::object_call(
            runtime.type_type(),
            {runtime.new_str("Inconsistent"), runtime.new_tuple({a_type, b_type}), runtime.new_dict()});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "cannot create a consistent method resolution order (MRO) for bases A B");
    }
}

TEST(CoreObjects, RuntimeVisitsRootsAndContainerChildren)
{
    cxxpython::Runtime runtime;

    EXPECT_GE(runtime.heap_size(), 16U);
    EXPECT_GE(runtime.root_count(), 16U);

    std::size_t visited_roots = 0;
    runtime.visit_roots([&](cxxpython::Object *object)
                        {
                            if (object)
                                ++visited_roots;
                        });
    EXPECT_EQ(visited_roots, runtime.root_count());

    cxxpython::TupleObject *tuple = runtime.new_tuple({runtime.new_int(4), runtime.new_str("x")});
    std::size_t tuple_children = 0;
    tuple->visit_children([&](cxxpython::Object *object)
                          {
                              if (object)
                                  ++tuple_children;
                          });
    EXPECT_EQ(tuple_children, 2U);

    cxxpython::ListObject *list = runtime.new_list({runtime.new_int(1), runtime.new_int(2)});
    std::size_t list_children = 0;
    list->visit_children([&](cxxpython::Object *object)
                         {
                             if (object)
                                 ++list_children;
                         });
    EXPECT_EQ(list_children, list->size());

    cxxpython::DictObject *dict = runtime.new_dict();
    dict->set(runtime.new_str("key"), runtime.new_str("value"));
    std::size_t dict_children = 0;
    dict->visit_children([&](cxxpython::Object *object)
                         {
                             if (object)
                                 ++dict_children;
                         });
    EXPECT_EQ(dict_children, dict->size() * 2);
}
