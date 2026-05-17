#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"
#include "test_support.hpp"

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
    ASSERT_NE(manual_error->args(), nullptr);
    ASSERT_EQ(manual_error->args()->size(), 1U);
    auto *message_arg = dynamic_cast<cxxpython::StrObject *>(manual_error->args()->items()[0]);
    ASSERT_NE(message_arg, nullptr);
    EXPECT_EQ(message_arg->value, "bad\\operand\n");
    EXPECT_TRUE(cxxpython::is_instance(manual_error, runtime.base_exception_type()));
    EXPECT_EQ(cxxpython::object_repr(manual_error), "TypeError('bad\\\\operand\\n')");

    cxxpython::ExceptionObject *multi_arg_error =
        runtime.new_exception(runtime.type_error_type(), {runtime.new_str("left"), runtime.new_int(7)});
    ASSERT_NE(multi_arg_error->args(), nullptr);
    ASSERT_EQ(multi_arg_error->args()->size(), 2U);
    EXPECT_EQ(cxxpython::object_repr(multi_arg_error), "TypeError('left', 7)");

    std::size_t exception_children = 0;
    multi_arg_error->visit_children([&](cxxpython::Object *object)
                                    {
                                        if (object)
                                            ++exception_children;
                                    });
    EXPECT_EQ(exception_children, 1U);
}
