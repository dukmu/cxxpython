#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"

namespace
{
    std::string custom_repr(const cxxpython::Object &object)
    {
        (void)object;
        return "<custom>";
    }

    std::int64_t custom_hash(const cxxpython::Object &object)
    {
        (void)object;
        return 42;
    }

    bool custom_equal(const cxxpython::Object &left, const cxxpython::Object &right)
    {
        return left.type() == right.type();
    }

    bool custom_false(const cxxpython::Object &object)
    {
        (void)object;
        return false;
    }
}

TEST(CoreObjects, RuntimeBootstrapsBuiltinTypes)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(runtime.object_type()->type(), runtime.type_type());
    EXPECT_EQ(runtime.type_type()->type(), runtime.type_type());
    EXPECT_EQ(runtime.none()->type(), runtime.none_type());
    EXPECT_EQ(runtime.true_value()->type(), runtime.bool_type());
    EXPECT_EQ(runtime.false_value()->type(), runtime.bool_type());

    EXPECT_EQ(runtime.list_type()->base, runtime.object_type());
    EXPECT_TRUE(cxxpython::is_subclass(runtime.bool_type(), runtime.int_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.bool_type(), runtime.object_type()));
    EXPECT_TRUE(cxxpython::is_instance(runtime.true_value(), runtime.int_type()));

    ASSERT_EQ(runtime.bool_type()->mro.size(), 3U);
    EXPECT_EQ(runtime.bool_type()->mro[0], runtime.bool_type());
    EXPECT_EQ(runtime.bool_type()->mro[1], runtime.int_type());
    EXPECT_EQ(runtime.bool_type()->mro[2], runtime.object_type());
    EXPECT_EQ(runtime.bool_type()->base, runtime.int_type());
    ASSERT_EQ(runtime.bool_type()->bases.size(), 1U);
    EXPECT_EQ(runtime.bool_type()->bases[0], runtime.int_type());
}

TEST(CoreObjects, RuntimeBootstrapsExceptionHierarchy)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(runtime.base_exception_type()->base, runtime.object_type());
    EXPECT_EQ(runtime.exception_type()->base, runtime.base_exception_type());
    EXPECT_EQ(runtime.type_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.index_error_type()->base, runtime.exception_type());
    EXPECT_TRUE(cxxpython::is_subclass(runtime.type_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.index_error_type(), runtime.base_exception_type()));
}

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
    collision_slots.hash = custom_hash;
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

TEST(CoreObjects, ReprAndExceptionObjects)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(cxxpython::object_repr(runtime.none()), "None");
    EXPECT_EQ(cxxpython::object_repr(runtime.true_value()), "True");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_int(1)), "1");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str("hello")), "'hello'");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str("it's\\ok\n\t")),
              "'it\\'s\\\\ok\\n\\t'");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str(std::string("a") + std::string(1, '\x01') + "z")),
              "'a\\x01z'");
    EXPECT_EQ(cxxpython::object_repr(runtime.type_type()), "<class 'type'>");

    cxxpython::ExceptionObject *manual_error =
        runtime.new_exception(runtime.type_error_type(), "bad\\operand\n");
    EXPECT_EQ(manual_error->type(), runtime.type_error_type());
    EXPECT_EQ(manual_error->message(), "bad\\operand\n");
    EXPECT_TRUE(cxxpython::is_instance(manual_error, runtime.base_exception_type()));
    EXPECT_EQ(cxxpython::object_repr(manual_error), "TypeError('bad\\\\operand\\n')");
}

TEST(CoreObjects, HeapTypesUseAndInheritSlots)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots custom_slots;
    custom_slots.repr = custom_repr;
    custom_slots.hash = custom_hash;
    custom_slots.equal = custom_equal;
    custom_slots.truth = custom_false;
    cxxpython::TypeObject *custom_type =
        runtime.new_heap_type("Custom", runtime.object_type(), custom_slots);

    cxxpython::Object *custom_a = runtime.allocate<cxxpython::Object>(custom_type);
    cxxpython::Object *custom_b = runtime.allocate<cxxpython::Object>(custom_type);
    EXPECT_EQ(cxxpython::object_repr(custom_a), "<custom>");
    EXPECT_EQ(cxxpython::object_hash(custom_a), 42);
    EXPECT_TRUE(cxxpython::object_equal(custom_a, custom_b));
    EXPECT_FALSE(cxxpython::is_true(custom_a));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, custom_type));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, runtime.object_type()));

    cxxpython::TypeObject *repr_only_type =
        runtime.new_heap_type("ReprOnly", custom_type, cxxpython::TypeSlots{.repr = custom_repr});
    cxxpython::Object *repr_only = runtime.allocate<cxxpython::Object>(repr_only_type);
    EXPECT_EQ(cxxpython::object_repr(repr_only), "<custom>");
    EXPECT_EQ(cxxpython::object_hash(repr_only), 42);
    EXPECT_FALSE(cxxpython::is_true(repr_only));
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
