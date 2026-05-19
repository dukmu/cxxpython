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

TEST(CoreObjects, BinaryOperatorProtocolDispatchesBuiltinTypes)
{
    cxxpython::Runtime runtime;

    auto *sum = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_binary_op(runtime.new_int(4), runtime.new_int(3), cxxpython::BinaryOp::Add));
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->value, 7);

    auto *difference = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_binary_op(runtime.new_int(4), runtime.new_int(3), cxxpython::BinaryOp::Subtract));
    ASSERT_NE(difference, nullptr);
    EXPECT_EQ(difference->value, 1);

    auto *product = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_binary_op(runtime.new_int(4), runtime.new_int(3), cxxpython::BinaryOp::Multiply));
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(product->value, 12);

    auto *text = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_binary_op(runtime.new_str("py"), runtime.new_str("thon"), cxxpython::BinaryOp::Add));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->value, "python");
}

TEST(CoreObjects, RichCompareProtocolDispatchesBuiltinOrdering)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(cxxpython::object_rich_compare(runtime.new_int(1), runtime.new_int(2),
                                             cxxpython::RichCompareOp::Less),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(runtime.new_int(2), runtime.new_int(2),
                                             cxxpython::RichCompareOp::LessEqual),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(runtime.new_int(3), runtime.new_int(2),
                                             cxxpython::RichCompareOp::Greater),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(runtime.new_int(3), runtime.new_int(3),
                                             cxxpython::RichCompareOp::GreaterEqual),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(runtime.new_str("ant"), runtime.new_str("bee"),
                                             cxxpython::RichCompareOp::Less),
              runtime.true_value());

    try
    {
        (void)cxxpython::object_rich_compare(runtime.new_int(1), runtime.new_str("x"),
                                             cxxpython::RichCompareOp::Less);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "comparison not supported between instances of 'int' and 'str'");
    }
}

TEST(CoreObjects, BinaryOperatorProtocolUsesNotImplementedFallbacks)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots left_slots;
    left_slots.binary_op = [](const cxxpython::Object &left,
                              const cxxpython::Object &right,
                              cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)right;
        (void)op;
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *left_type = runtime.new_heap_type("LeftOperand", runtime.object_type(), left_slots);

    cxxpython::TypeSlots right_slots;
    right_slots.reflected_binary_op = [](const cxxpython::Object &right,
                                         const cxxpython::Object &left,
                                         cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)left;
        if (op == cxxpython::BinaryOp::Add)
            return right.runtime()->new_str("reflected-add");
        return right.runtime()->not_implemented();
    };
    cxxpython::TypeObject *right_type = runtime.new_heap_type("RightOperand", runtime.object_type(), right_slots);

    cxxpython::Object *left = runtime.allocate<cxxpython::Object>(left_type);
    cxxpython::Object *right = runtime.allocate<cxxpython::Object>(right_type);
    auto *result = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_binary_op(left, right, cxxpython::BinaryOp::Add));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "reflected-add");

    try
    {
        (void)cxxpython::object_binary_op(left, right, cxxpython::BinaryOp::Subtract);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unsupported operand type(s) for -: 'LeftOperand' and 'RightOperand'");
    }
}

TEST(CoreObjects, BinaryOperatorProtocolPrefersSubclassReflectedSlot)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots base_slots;
    base_slots.binary_op = [](const cxxpython::Object &left,
                              const cxxpython::Object &right,
                              cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)right;
        if (op == cxxpython::BinaryOp::Add)
            return left.runtime()->new_str("base-add");
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *base_type = runtime.new_heap_type("BaseOperand", runtime.object_type(), base_slots);

    cxxpython::TypeSlots child_slots;
    child_slots.reflected_binary_op = [](const cxxpython::Object &right,
                                         const cxxpython::Object &left,
                                         cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)left;
        if (op == cxxpython::BinaryOp::Add)
            return right.runtime()->new_str("child-reflected-add");
        return right.runtime()->not_implemented();
    };
    cxxpython::TypeObject *child_type = runtime.new_heap_type("ChildOperand", base_type, child_slots);

    cxxpython::Object *base = runtime.allocate<cxxpython::Object>(base_type);
    cxxpython::Object *child = runtime.allocate<cxxpython::Object>(child_type);
    auto *result = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_binary_op(base, child, cxxpython::BinaryOp::Add));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "child-reflected-add");
}

TEST(CoreObjects, InplaceBinaryOperatorFallsBackToBinaryProtocol)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots inplace_slots;
    inplace_slots.inplace_binary_op = [](const cxxpython::Object &left,
                                         const cxxpython::Object &right,
                                         cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)right;
        if (op == cxxpython::BinaryOp::Add)
            return left.runtime()->new_str("inplace-add");
        return left.runtime()->not_implemented();
    };
    inplace_slots.binary_op = [](const cxxpython::Object &left,
                                 const cxxpython::Object &right,
                                 cxxpython::BinaryOp op) -> cxxpython::Object *
    {
        (void)right;
        if (op == cxxpython::BinaryOp::Subtract)
            return left.runtime()->new_str("binary-subtract");
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *type = runtime.new_heap_type("InplaceOperand", runtime.object_type(), inplace_slots);
    cxxpython::Object *left = runtime.allocate<cxxpython::Object>(type);
    cxxpython::Object *right = runtime.allocate<cxxpython::Object>(type);

    auto *inplace = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_inplace_binary_op(left, right, cxxpython::BinaryOp::Add));
    ASSERT_NE(inplace, nullptr);
    EXPECT_EQ(inplace->value, "inplace-add");

    auto *fallback = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_inplace_binary_op(left, right, cxxpython::BinaryOp::Subtract));
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->value, "binary-subtract");
}

TEST(CoreObjects, RichCompareProtocolUsesNotImplementedFallbacks)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots left_slots;
    left_slots.rich_compare = [](const cxxpython::Object &left,
                                 const cxxpython::Object &right,
                                 cxxpython::RichCompareOp op) -> cxxpython::Object *
    {
        (void)right;
        (void)op;
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *left_type = runtime.new_heap_type("LeftComparable", runtime.object_type(), left_slots);

    cxxpython::TypeSlots right_slots;
    right_slots.rich_compare = [](const cxxpython::Object &right,
                                  const cxxpython::Object &left,
                                  cxxpython::RichCompareOp op) -> cxxpython::Object *
    {
        (void)left;
        if (op == cxxpython::RichCompareOp::Equal)
            return right.runtime()->new_str("right-equal");
        if (op == cxxpython::RichCompareOp::Greater)
            return right.runtime()->new_str("right-greater");
        return right.runtime()->not_implemented();
    };
    cxxpython::TypeObject *right_type = runtime.new_heap_type("RightComparable", runtime.object_type(), right_slots);

    cxxpython::Object *left = runtime.allocate<cxxpython::Object>(left_type);
    cxxpython::Object *right = runtime.allocate<cxxpython::Object>(right_type);

    auto *equal = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_rich_compare(left, right, cxxpython::RichCompareOp::Equal));
    ASSERT_NE(equal, nullptr);
    EXPECT_EQ(equal->value, "right-equal");

    auto *less = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_rich_compare(left, right, cxxpython::RichCompareOp::Less));
    ASSERT_NE(less, nullptr);
    EXPECT_EQ(less->value, "right-greater");
}

TEST(CoreObjects, RichCompareProtocolPrefersSubclassSlot)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots base_slots;
    base_slots.rich_compare = [](const cxxpython::Object &left,
                                 const cxxpython::Object &right,
                                 cxxpython::RichCompareOp op) -> cxxpython::Object *
    {
        (void)right;
        if (op == cxxpython::RichCompareOp::Equal)
            return left.runtime()->new_str("base-equal");
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *base_type = runtime.new_heap_type("BaseComparable", runtime.object_type(), base_slots);

    cxxpython::TypeSlots child_slots;
    child_slots.rich_compare = [](const cxxpython::Object &left,
                                  const cxxpython::Object &right,
                                  cxxpython::RichCompareOp op) -> cxxpython::Object *
    {
        (void)right;
        if (op == cxxpython::RichCompareOp::Equal)
            return left.runtime()->new_str("child-equal");
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *child_type = runtime.new_heap_type("ChildComparable", base_type, child_slots);

    cxxpython::Object *base = runtime.allocate<cxxpython::Object>(base_type);
    cxxpython::Object *child = runtime.allocate<cxxpython::Object>(child_type);
    auto *result = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_rich_compare(base, child, cxxpython::RichCompareOp::Equal));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "child-equal");
}

TEST(CoreObjects, RichCompareNotImplementedFallsBackToDefaultResults)
{
    cxxpython::Runtime runtime;

    cxxpython::TypeSlots slots;
    slots.rich_compare = [](const cxxpython::Object &left,
                            const cxxpython::Object &right,
                            cxxpython::RichCompareOp op) -> cxxpython::Object *
    {
        (void)right;
        (void)op;
        return left.runtime()->not_implemented();
    };
    cxxpython::TypeObject *type = runtime.new_heap_type("DefaultComparable", runtime.object_type(), slots);
    cxxpython::Object *first = runtime.allocate<cxxpython::Object>(type);
    cxxpython::Object *second = runtime.allocate<cxxpython::Object>(type);

    EXPECT_EQ(cxxpython::object_rich_compare(first, first, cxxpython::RichCompareOp::Equal),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_rich_compare(first, second, cxxpython::RichCompareOp::Equal),
              runtime.false_value());
    EXPECT_EQ(cxxpython::object_rich_compare(first, second, cxxpython::RichCompareOp::NotEqual),
              runtime.true_value());

    try
    {
        (void)cxxpython::object_rich_compare(first, second, cxxpython::RichCompareOp::Less);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "comparison not supported between instances of 'DefaultComparable' and 'DefaultComparable'");
    }
}

TEST(CoreObjects, UnaryOperatorProtocolDispatchesBuiltinTypes)
{
    cxxpython::Runtime runtime;

    auto *positive = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_unary_op(runtime.new_int(4), cxxpython::UnaryOp::Positive));
    ASSERT_NE(positive, nullptr);
    EXPECT_EQ(positive->value, 4);

    auto *negative = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_unary_op(runtime.new_int(4), cxxpython::UnaryOp::Negative));
    ASSERT_NE(negative, nullptr);
    EXPECT_EQ(negative->value, -4);

    auto *inverted = dynamic_cast<cxxpython::IntObject *>(
        cxxpython::object_unary_op(runtime.new_int(1), cxxpython::UnaryOp::Invert));
    ASSERT_NE(inverted, nullptr);
    EXPECT_EQ(inverted->value, -2);

    try
    {
        (void)cxxpython::object_unary_op(runtime.new_str("x"), cxxpython::UnaryOp::Negative);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "bad operand type for unary -: 'str'");
    }
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

TEST(CoreObjects, SequenceSlotsReadAndWriteListAndTupleItems)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *replacement = runtime.new_str("replacement");
    cxxpython::ListObject *list = runtime.new_list({first, runtime.new_str("second")});
    cxxpython::TupleObject *tuple = runtime.new_tuple({first, runtime.new_int(2)});

    EXPECT_EQ(cxxpython::object_length(list), 2);
    EXPECT_EQ(cxxpython::object_get_item(list, runtime.new_int(0)), first);
    EXPECT_EQ(cxxpython::object_get_item(list, runtime.new_int(-1)), list->get(1));
    EXPECT_TRUE(cxxpython::object_contains(list, first));
    EXPECT_FALSE(cxxpython::object_contains(list, runtime.new_str("missing")));

    cxxpython::object_set_item(list, runtime.new_int(1), replacement);
    EXPECT_EQ(cxxpython::object_get_item(list, runtime.new_int(1)), replacement);
    cxxpython::object_delete_item(list, runtime.new_int(0));
    EXPECT_EQ(cxxpython::object_length(list), 1);
    EXPECT_EQ(cxxpython::object_get_item(list, runtime.new_int(0)), replacement);

    EXPECT_EQ(cxxpython::object_length(tuple), 2);
    EXPECT_EQ(cxxpython::object_get_item(tuple, runtime.new_int(0)), first);
    EXPECT_TRUE(cxxpython::object_contains(tuple, runtime.new_int(2)));

    try
    {
        cxxpython::object_set_item(tuple, runtime.new_int(0), replacement);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'tuple' object does not support item assignment");
    }

    try
    {
        cxxpython::object_delete_item(tuple, runtime.new_int(0));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'tuple' object does not support item deletion");
    }
}

TEST(CoreObjects, IteratesSequencesAndRaisesStopIterationAtEnd)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *second = runtime.new_str("second");
    cxxpython::ListObject *list = runtime.new_list({first, second});

    cxxpython::Object *iterator = cxxpython::object_iter(list);
    ASSERT_NE(iterator, nullptr);
    EXPECT_EQ(iterator->type(), runtime.sequence_iterator_type());
    EXPECT_EQ(cxxpython::object_iter(iterator), iterator);
    EXPECT_EQ(cxxpython::object_next(iterator), first);
    EXPECT_EQ(cxxpython::object_next(iterator), second);

    try
    {
        (void)cxxpython::object_next(iterator);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.stop_iteration_type());
        ASSERT_NE(error.value(), nullptr);
        EXPECT_EQ(error.value()->args()->size(), 0U);
    }

    std::vector<cxxpython::Object *> visited;
    iterator->visit_children([&visited](cxxpython::Object *object)
                             { visited.push_back(object); });
    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], list);
}

TEST(CoreObjects, ListConstructorConsumesIterableProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *second = runtime.new_str("second");

    auto *from_tuple = dynamic_cast<cxxpython::ListObject *>(
        cxxpython::object_call(runtime.list_type(), {runtime.new_tuple({first, second})}));
    ASSERT_NE(from_tuple, nullptr);
    ASSERT_EQ(from_tuple->size(), 2U);
    EXPECT_EQ(from_tuple->get(0), first);
    EXPECT_EQ(from_tuple->get(1), second);

    cxxpython::DictObject *dict = runtime.new_dict();
    dict->set(first, runtime.new_int(1));
    dict->set(second, runtime.new_int(2));
    auto *from_dict = dynamic_cast<cxxpython::ListObject *>(
        cxxpython::object_call(runtime.list_type(), {dict}));
    ASSERT_NE(from_dict, nullptr);
    ASSERT_EQ(from_dict->size(), 2U);
    EXPECT_EQ(from_dict->get(0), first);
    EXPECT_EQ(from_dict->get(1), second);

    try
    {
        (void)cxxpython::object_call(runtime.list_type(), {runtime.new_int(1)});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'int' object is not iterable");
    }
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

    try
    {
        (void)cxxpython::object_get_item(list, runtime.new_str("bad"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "list indices must be integers");
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
