#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"
#include "test_support.hpp"

TEST(CoreObjects, HeapTypesUseAndInheritSlots)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots custom_slots;
    custom_slots.repr = test_support::custom_repr;
    custom_slots.hash = test_support::custom_hash;
    custom_slots.equal = test_support::custom_equal;
    custom_slots.truth = test_support::custom_false;
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
    EXPECT_FALSE(cxxpython::is_true(custom_a));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, custom_type));
    EXPECT_TRUE(cxxpython::is_instance(custom_a, runtime.object_type()));

    cxxpython::TypeObject *repr_only_type =
        runtime.new_heap_type("ReprOnly", custom_type, cxxpython::TypeSlots{.repr = test_support::custom_repr});
    cxxpython::Object *repr_only = runtime.allocate<cxxpython::Object>(repr_only_type);
    EXPECT_EQ(cxxpython::object_repr(repr_only), "<custom>");
    EXPECT_EQ(cxxpython::object_hash(repr_only), 42);
    EXPECT_FALSE(cxxpython::is_true(repr_only));
}

TEST(CoreObjects, TypeFlagsSeparateCompactBuiltinsFromHeapInstances)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *heap_type = runtime.new_heap_type("HeapObject", runtime.object_type());

    EXPECT_EQ(runtime.object_type()->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
    EXPECT_EQ(runtime.list_type()->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
    EXPECT_NE(heap_type->flags & cxxpython::TypeFlagHeapType, 0U);
    EXPECT_NE(heap_type->flags & cxxpython::TypeFlagHasInstanceDict, 0U);
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
