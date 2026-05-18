#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"
#include "test_support.hpp"

TEST(CoreObjects, BoolAndIntSharePythonSemantics)
{
    cxxpython::Runtime runtime;

    cxxpython::IntObject *one = runtime.new_int(1);
    cxxpython::IntObject *zero = runtime.new_int(0);

    EXPECT_TRUE(cxxpython::object_equal(runtime.true_value(), one));
    EXPECT_TRUE(cxxpython::object_equal(runtime.false_value(), zero));
    EXPECT_FALSE(cxxpython::object_equal(runtime.true_value(), runtime.new_int(2)));
    EXPECT_EQ(cxxpython::object_hash(runtime.true_value()), cxxpython::object_hash(one));
    EXPECT_EQ(cxxpython::object_hash(runtime.false_value()), cxxpython::object_hash(zero));
    EXPECT_TRUE(cxxpython::is_true(runtime.true_value()));
    EXPECT_FALSE(cxxpython::is_true(runtime.false_value()));
    EXPECT_FALSE(cxxpython::is_true(runtime.none()));
}

TEST(CoreObjects, ListSemantics)
{
    cxxpython::Runtime runtime;

    cxxpython::IntObject *one = runtime.new_int(1);
    cxxpython::ListObject *empty_list = runtime.new_list({});
    EXPECT_EQ(cxxpython::object_repr(empty_list), "[]");
    EXPECT_FALSE(cxxpython::is_true(empty_list));

    cxxpython::ListObject *list = runtime.new_list({one, runtime.new_str("two")});
    ASSERT_EQ(list->size(), 2U);
    EXPECT_EQ(list->get(0), one);
    EXPECT_EQ(cxxpython::object_repr(list), "[1, 'two']");
    EXPECT_TRUE(cxxpython::is_true(list));

    list->append(runtime.new_int(3));
    ASSERT_EQ(list->size(), 3U);
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(list->get(-1))->value, 3);
    EXPECT_TRUE(cxxpython::object_equal(
        list,
        runtime.new_list({one, runtime.new_str("two"), runtime.new_int(3)})));

    std::size_t visited_children = 0;
    list->visit_children([&](cxxpython::Object *object)
                         {
                             if (object)
                                 ++visited_children;
                         });
    EXPECT_EQ(visited_children, list->size());
}

TEST(CoreObjects, ListRaisesPythonErrors)
{
    cxxpython::Runtime runtime;
    cxxpython::ListObject *list = runtime.new_list({runtime.new_int(1)});

    try
    {
        (void)list->get(99);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.index_error_type());
        EXPECT_STREQ(error.what(), "list index out of range");
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.value()->message(), "list index out of range");
    }

    try
    {
        (void)cxxpython::object_hash(list);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unhashable type: 'list'");
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.value()->message(), "unhashable type: 'list'");
    }
}

TEST(CoreObjects, ListAppendIsExposedAsMethodDescriptor)
{
    cxxpython::Runtime runtime;
    cxxpython::ListObject *list = runtime.new_list({});
    cxxpython::Object *item = runtime.new_int(7);

    cxxpython::Object *descriptor = cxxpython::object_get_attr(runtime.list_type(), "append");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->type(), runtime.method_descriptor_type());
    EXPECT_EQ(cxxpython::object_repr(descriptor), "<method 'append' of 'list' objects>");

    cxxpython::Object *unbound_result = cxxpython::object_call(descriptor, {list, item});
    EXPECT_EQ(unbound_result, runtime.none());
    ASSERT_EQ(list->size(), 1U);
    EXPECT_EQ(list->get(0), item);

    cxxpython::Object *bound = cxxpython::object_get_attr(list, "append");
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->type(), runtime.native_function_type());
    EXPECT_TRUE(cxxpython::object_repr(bound).starts_with("<built-in method append of list object at "));
    std::vector<cxxpython::Object *> bound_children;
    bound->visit_children([&bound_children](cxxpython::Object *object)
                          { bound_children.push_back(object); });
    ASSERT_EQ(bound_children.size(), 2U);
    EXPECT_EQ(bound_children[0], descriptor);
    EXPECT_EQ(bound_children[1], list);

    cxxpython::Object *second = runtime.new_str("second");
    cxxpython::Object *bound_result = cxxpython::object_call(bound, {second});
    EXPECT_EQ(bound_result, runtime.none());
    ASSERT_EQ(list->size(), 2U);
    EXPECT_EQ(list->get(1), second);
}

TEST(CoreObjects, ListAppendDescriptorValidatesReceiverAndArguments)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *descriptor = cxxpython::object_get_attr(runtime.list_type(), "append");

    try
    {
        (void)cxxpython::object_call(descriptor, {runtime.new_int(1)});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "descriptor 'append' for 'list' objects doesn't apply to a 'int' object");
    }

    try
    {
        (void)cxxpython::object_call(descriptor, {runtime.new_list({})});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "list.append() takes exactly one argument (0 given)");
    }
}
