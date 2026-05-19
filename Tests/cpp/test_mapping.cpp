#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"
#include "test_support.hpp"

TEST(CoreObjects, DictMergesEqualBoolAndIntKeys)
{
    cxxpython::Runtime runtime;

    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::StrObject *bool_value = runtime.new_str("bool");
    cxxpython::StrObject *int_value = runtime.new_str("int");
    dict->set(runtime.true_value(), bool_value);
    dict->set(runtime.new_int(1), int_value);

    EXPECT_EQ(dict->size(), 1U);
    EXPECT_EQ(dict->get(runtime.true_value()), int_value);
    EXPECT_EQ(dict->get(runtime.new_int(1)), int_value);
    ASSERT_EQ(dict->entries().size(), 1U);
    EXPECT_EQ(dict->entries()[0].key, runtime.true_value());
    EXPECT_EQ(dict->entries()[0].value, int_value);
}

TEST(CoreObjects, DictSupportsTupleKeysAndInsertionOrder)
{
    cxxpython::Runtime runtime;

    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::StrObject *int_value = runtime.new_str("int");
    dict->set(runtime.true_value(), int_value);

    cxxpython::TupleObject *tuple_key = runtime.new_tuple({runtime.new_int(4), runtime.new_str("x")});
    cxxpython::StrObject *tuple_value = runtime.new_str("tuple");
    dict->set(tuple_key, tuple_value);

    EXPECT_EQ(dict->size(), 2U);
    EXPECT_EQ(dict->get(runtime.new_tuple({runtime.new_int(4), runtime.new_str("x")})), tuple_value);
    ASSERT_EQ(dict->entries().size(), 2U);
    EXPECT_EQ(dict->entries()[0].key, runtime.true_value());
    EXPECT_EQ(dict->entries()[0].value, int_value);
    EXPECT_EQ(dict->entries()[1].key, tuple_key);
    EXPECT_EQ(dict->entries()[1].value, tuple_value);
}

TEST(CoreObjects, MappingSlotsReadWriteAndContainDictItems)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::Object *key = runtime.new_str("key");
    cxxpython::Object *value = runtime.new_int(1);
    cxxpython::Object *replacement = runtime.new_int(2);

    EXPECT_EQ(cxxpython::object_length(dict), 0);
    EXPECT_FALSE(cxxpython::object_contains(dict, key));

    cxxpython::object_set_item(dict, key, value);
    EXPECT_EQ(cxxpython::object_length(dict), 1);
    EXPECT_EQ(cxxpython::object_get_item(dict, key), value);
    EXPECT_TRUE(cxxpython::object_contains(dict, key));

    cxxpython::object_set_item(dict, key, replacement);
    EXPECT_EQ(cxxpython::object_length(dict), 1);
    EXPECT_EQ(cxxpython::object_get_item(dict, key), replacement);
    cxxpython::object_delete_item(dict, key);
    EXPECT_EQ(cxxpython::object_length(dict), 0);
    EXPECT_FALSE(cxxpython::object_contains(dict, key));

    try
    {
        (void)cxxpython::object_get_item(dict, runtime.new_str("missing"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.key_error_type());
        EXPECT_STREQ(error.what(), "'missing'");
    }

    try
    {
        cxxpython::object_delete_item(dict, runtime.new_str("missing"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.key_error_type());
        EXPECT_STREQ(error.what(), "'missing'");
    }
}

TEST(CoreObjects, MappingProxySlotsReadAndContainWrappedDictItems)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *dict = runtime.new_dict();
    cxxpython::Object *key = runtime.new_str("key");
    cxxpython::Object *value = runtime.new_str("value");
    cxxpython::MappingProxyObject *proxy = runtime.new_mappingproxy(dict);

    EXPECT_EQ(cxxpython::object_length(proxy), 0);
    EXPECT_FALSE(cxxpython::object_contains(proxy, key));

    dict->set(key, value);
    EXPECT_EQ(cxxpython::object_length(proxy), 1);
    EXPECT_EQ(cxxpython::object_get_item(proxy, key), value);
    EXPECT_TRUE(cxxpython::object_contains(proxy, key));

    try
    {
        cxxpython::object_set_item(proxy, key, runtime.new_str("no"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'mappingproxy' object does not support item assignment");
    }

    try
    {
        cxxpython::object_delete_item(proxy, key);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'mappingproxy' object does not support item deletion");
    }
}

TEST(CoreObjects, DictGrowsAndHandlesHashCollisions)
{
    cxxpython::Runtime runtime;

    cxxpython::DictObject *dict = runtime.new_dict();
    std::vector<cxxpython::StrObject *> keys;
    for (int i = 0; i < 96; ++i)
    {
        cxxpython::StrObject *key = runtime.new_str("key-" + std::to_string(i));
        keys.push_back(key);
        dict->set(key, runtime.new_int(i));
    }

    ASSERT_EQ(dict->size(), keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        auto *value = dynamic_cast<cxxpython::IntObject *>(dict->get(keys[i]));
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value->value, static_cast<std::int64_t>(i));
        EXPECT_EQ(dict->entries()[i].key, keys[i]);
    }

    dict->set(keys[42], runtime.new_int(4200));
    EXPECT_EQ(dict->size(), keys.size());
    EXPECT_EQ(static_cast<cxxpython::IntObject *>(dict->get(keys[42]))->value, 4200);

    cxxpython::TypeSlots collision_slots;
    collision_slots.hash = test_support::custom_hash;
    cxxpython::TypeObject *collision_type =
        runtime.new_heap_type("CollisionKey", runtime.object_type(), collision_slots);
    cxxpython::Object *collision_a = runtime.allocate<cxxpython::Object>(collision_type);
    cxxpython::Object *collision_b = runtime.allocate<cxxpython::Object>(collision_type);

    dict->set(collision_a, runtime.new_str("a"));
    dict->set(collision_b, runtime.new_str("b"));
    EXPECT_EQ(dict->size(), keys.size() + 2);
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(dict->get(collision_a))->value, "a");
    EXPECT_EQ(static_cast<cxxpython::StrObject *>(dict->get(collision_b))->value, "b");
}

TEST(CoreObjects, DictRaisesPythonErrors)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *dict = runtime.new_dict();

    try
    {
        dict->set(nullptr, runtime.new_int(1));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "dict key cannot be null");
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.value()->type(), runtime.type_error_type());
        EXPECT_EQ(error.value()->message(), "dict key cannot be null");
    }

    try
    {
        (void)cxxpython::object_hash(dict);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unhashable type: 'dict'");
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.value()->type(), runtime.type_error_type());
        EXPECT_EQ(error.value()->message(), "unhashable type: 'dict'");
    }
}
