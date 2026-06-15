#include <gtest/gtest.h>

#include <algorithm>

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"
#include "test_support.hpp"

TEST(CoreObjects, RuntimeBootstrapsBuiltinTypes)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(runtime.object_type()->type(), runtime.type_type());
    EXPECT_EQ(runtime.type_type()->type(), runtime.type_type());
    EXPECT_EQ(runtime.none()->type(), runtime.none_type());
    EXPECT_EQ(runtime.not_implemented()->type(), runtime.not_implemented_type());
    EXPECT_EQ(runtime.true_value()->type(), runtime.bool_type());
    EXPECT_EQ(runtime.false_value()->type(), runtime.bool_type());

    EXPECT_EQ(runtime.list_type()->base, runtime.object_type());
    EXPECT_EQ(runtime.module_type()->base, runtime.object_type());
    EXPECT_EQ(runtime.function_type()->base, runtime.object_type());
    EXPECT_EQ(runtime.method_type()->base, runtime.object_type());
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

TEST(CoreObjects, BuiltinTypesHaveRuntimeDictionaries)
{
    cxxpython::Runtime runtime;

    ASSERT_NE(runtime.object_type()->dict, nullptr);
    ASSERT_NE(runtime.type_type()->dict, nullptr);
    ASSERT_NE(runtime.int_type()->dict, nullptr);

    auto *object_name = dynamic_cast<cxxpython::StrObject *>(
        runtime.object_type()->dict->get(runtime.new_str("__name__")));
    auto *object_module = dynamic_cast<cxxpython::StrObject *>(
        runtime.object_type()->dict->get(runtime.new_str("__module__")));
    ASSERT_NE(object_name, nullptr);
    ASSERT_NE(object_module, nullptr);
    EXPECT_EQ(object_name->value, "object");
    EXPECT_EQ(object_module->value, "builtins");

    std::vector<cxxpython::Object *> children;
    runtime.object_type()->visit_children([&children](cxxpython::Object *object)
                                          { children.push_back(object); });
    EXPECT_NE(std::find(children.begin(), children.end(), runtime.object_type()->dict), children.end());
}

TEST(CoreObjects, RuntimeBootstrapsExceptionHierarchy)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(runtime.base_exception_type()->base, runtime.object_type());
    EXPECT_EQ(runtime.exception_type()->base, runtime.base_exception_type());
    EXPECT_EQ(runtime.type_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.index_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.key_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.syntax_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.name_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.unbound_local_error_type()->base, runtime.name_error_type());
    EXPECT_EQ(runtime.recursion_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.attribute_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.import_error_type()->base, runtime.exception_type());
    EXPECT_EQ(runtime.os_error_type()->base, runtime.exception_type());
    EXPECT_TRUE(cxxpython::is_subclass(runtime.type_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.index_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.key_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.syntax_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.name_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.unbound_local_error_type(), runtime.name_error_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.unbound_local_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.recursion_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.attribute_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.import_error_type(), runtime.base_exception_type()));
    EXPECT_TRUE(cxxpython::is_subclass(runtime.os_error_type(), runtime.base_exception_type()));
}

TEST(CoreObjects, ReprAndExceptionObjects)
{
    cxxpython::Runtime runtime;

    EXPECT_EQ(cxxpython::object_repr(runtime.none()), "None");
    EXPECT_EQ(cxxpython::object_repr(runtime.not_implemented()), "NotImplemented");
    EXPECT_EQ(cxxpython::object_repr(runtime.true_value()), "True");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_int(1)), "1");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str("hello")), "'hello'");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str("it's\\ok\n\t")),
              "'it\\'s\\\\ok\\n\\t'");
    EXPECT_EQ(cxxpython::object_repr(runtime.new_str(std::string("a") + std::string(1, '\x01') + "z")),
              "'a\\x01z'");
    EXPECT_EQ(cxxpython::object_str(runtime.new_str("hello")), "hello");
    EXPECT_EQ(cxxpython::object_str(runtime.new_int(7)), "7");
    EXPECT_EQ(cxxpython::object_repr(runtime.type_type()), "<class 'type'>");
    EXPECT_EQ(runtime.new_int(-5), runtime.new_int(-5));
    EXPECT_EQ(runtime.new_int(256), runtime.new_int(256));
    EXPECT_NE(runtime.new_int(2048), runtime.new_int(2048));

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
    EXPECT_GE(exception_children, 2U);
}

TEST(CoreObjects, ExceptionAndTracebackOwnReferencedObjects)
{
    cxxpython::Runtime runtime;
    cxxpython::CodeObject *code = runtime.new_code(
        "fn",
        "script.py",
        {runtime.none()},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code);
    cxxpython::TracebackObject *first = runtime.new_traceback(nullptr, frame, 0, 1);
    cxxpython::TracebackObject *second = runtime.new_traceback(first, frame, 1, 2);

    EXPECT_EQ(frame->ref_count(), 2U);
    EXPECT_EQ(first->ref_count(), 1U);
    EXPECT_EQ(second->ref_count(), 0U);

    cxxpython::ExceptionObject *error = runtime.new_exception(runtime.type_error_type(), "boom");
    ASSERT_NE(error->args(), nullptr);
    EXPECT_EQ(error->args()->ref_count(), 1U);

    error->prepend_traceback(second);
    EXPECT_EQ(second->ref_count(), 1U);
    EXPECT_EQ(error->traceback(), second);

    error->prepend_traceback(nullptr);
    EXPECT_EQ(second->ref_count(), 0U);
    EXPECT_EQ(error->traceback(), nullptr);
}

TEST(CoreObjects, PythonErrorOwnsExceptionValue)
{
    cxxpython::Runtime runtime;
    cxxpython::ExceptionObject *exception = runtime.new_exception(runtime.type_error_type(), "owned");

    EXPECT_EQ(exception->ref_count(), 0U);
    {
        cxxpython::PythonError error(exception);
        EXPECT_EQ(error.value(), exception);
        EXPECT_EQ(exception->ref_count(), 1U);

        cxxpython::PythonError copy(error);
        EXPECT_EQ(copy.value(), exception);
        EXPECT_EQ(exception->ref_count(), 2U);

        cxxpython::PythonError moved(std::move(copy));
        EXPECT_EQ(moved.value(), exception);
        EXPECT_EQ(exception->ref_count(), 2U);
    }
    EXPECT_EQ(exception->ref_count(), 0U);
}

TEST(CoreObjects, SmallIntegerCacheIsRootedAcrossGarbageCollection)
{
    cxxpython::Runtime runtime;

    cxxpython::IntObject *cached = runtime.new_int(256);
    cxxpython::IntObject *negative_cached = runtime.new_int(-5);
    cxxpython::IntObject *uncached = runtime.new_int(2048);

    std::vector<cxxpython::Object *> roots;
    runtime.visit_roots([&roots](cxxpython::Object *object)
                        { roots.push_back(object); });

    EXPECT_NE(std::find(roots.begin(), roots.end(), cached), roots.end());
    EXPECT_NE(std::find(roots.begin(), roots.end(), negative_cached), roots.end());
    EXPECT_EQ(std::find(roots.begin(), roots.end(), uncached), roots.end());

    (void)runtime.collect_garbage();

    EXPECT_EQ(runtime.new_int(256), cached);
    EXPECT_EQ(runtime.new_int(-5), negative_cached);
}

TEST(CoreObjects, PrimitiveHeapObjectsDoNotCountTowardAutomaticCycleGarbageCollection)
{
    cxxpython::Runtime runtime;

    (void)runtime.collect_garbage();
    const std::size_t baseline = runtime.garbage_collection_count();

    for (std::int64_t value = 2048; value < 2058; ++value)
        (void)runtime.new_int(value);
    (void)runtime.new_float(1.25);
    (void)runtime.new_str("temporary");
    (void)runtime.new_bytes("temporary");

    EXPECT_EQ(runtime.garbage_collection_count(), baseline);
}

TEST(CoreObjects, TrackedContainersCountTowardAutomaticCycleGarbageCollection)
{
    cxxpython::Runtime runtime;

    (void)runtime.collect_garbage();
    const std::size_t baseline = runtime.garbage_collection_count();

    (void)runtime.new_list({});
    (void)runtime.new_tuple({});
    (void)runtime.new_dict();

    EXPECT_GE(runtime.garbage_collection_count(), baseline + 3U);
}

TEST(CoreObjects, ReferenceCountsKeepUntrackedLeafObjectsAlive)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *value = runtime.new_int(4096);
    const std::size_t after_allocation = runtime.heap_size();

    EXPECT_EQ(value->ref_count(), 0U);
    EXPECT_EQ(value->gc_generation(), cxxpython::GCGeneration::Young);

    value->incref();
    EXPECT_EQ(value->ref_count(), 1U);
    (void)runtime.collect_garbage();
    EXPECT_EQ(value->ref_count(), 1U);
    EXPECT_GE(runtime.heap_size(), 1U);
    value->decref();
    (void)runtime.collect_garbage();
    EXPECT_LT(runtime.heap_size(), after_allocation);
}

TEST(CoreObjects, RootedObjectsAreTemporaryGarbageCollectionRoots)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *value = runtime.new_int(4096);

    EXPECT_EQ(value->ref_count(), 0U);

    {
        auto root = runtime.root(value);
        EXPECT_EQ(value->ref_count(), 0U);
        (void)runtime.collect_garbage();
        EXPECT_EQ(value->ref_count(), 0U);
        EXPECT_EQ(value->gc_generation(), cxxpython::GCGeneration::Young);
    }

    EXPECT_EQ(value->ref_count(), 0U);
}

TEST(CoreObjects, GarbageCollectionCollectsReferenceCycles)
{
    cxxpython::Runtime runtime;
    (void)runtime.collect_garbage();
    const std::size_t before = runtime.heap_size();
    cxxpython::ListObject *list = runtime.new_list({});
    list->append(list);
    EXPECT_EQ(list->ref_count(), 1U);
    EXPECT_EQ(runtime.heap_size(), before + 1U);

    (void)runtime.collect_garbage();
    EXPECT_EQ(runtime.heap_size(), before);
}
