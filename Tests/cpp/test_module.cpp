#include <gtest/gtest.h>

#include "cxxpython/compiler.hpp"
#include "cxxpython/vm.hpp"

TEST(Module, CreatesModuleWithInitializedGlobals)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *module = runtime.new_module("__main__", "/tmp/example.py");

    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->type(), runtime.module_type());
    EXPECT_EQ(module->name(), "__main__");
    EXPECT_EQ(module->filename(), "/tmp/example.py");
    ASSERT_NE(module->dict(), nullptr);

    auto *name = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("__name__")));
    auto *file = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("__file__")));
    auto *builtins = dynamic_cast<cxxpython::ModuleObject *>(module->dict()->get(runtime.new_str("__builtins__")));
    ASSERT_NE(name, nullptr);
    ASSERT_NE(file, nullptr);
    ASSERT_NE(builtins, nullptr);
    EXPECT_EQ(name->value, "__main__");
    EXPECT_EQ(file->value, "/tmp/example.py");
    EXPECT_EQ(builtins, runtime.builtins_module());
    EXPECT_EQ(module->repr(), "<module '__main__' from '/tmp/example.py'>");
}

TEST(Module, BootstrapsBuiltinsModule)
{
    cxxpython::Runtime runtime;

    cxxpython::ModuleObject *builtins = runtime.builtins_module();
    ASSERT_NE(builtins, nullptr);
    EXPECT_EQ(builtins->type(), runtime.module_type());
    EXPECT_EQ(builtins->name(), "builtins");
    EXPECT_EQ(runtime.builtins_dict(), builtins->dict());

    cxxpython::Object *print = runtime.builtins_dict()->get(runtime.new_str("print"));
    ASSERT_NE(print, nullptr);
    EXPECT_EQ(print->type(), runtime.native_function_type());
    EXPECT_EQ(cxxpython::object_repr(print), "<built-in function print>");

    cxxpython::Object *len = runtime.builtins_dict()->get(runtime.new_str("len"));
    ASSERT_NE(len, nullptr);
    EXPECT_EQ(len->type(), runtime.native_function_type());
    EXPECT_EQ(cxxpython::object_repr(len), "<built-in function len>");

    cxxpython::Object *iter = runtime.builtins_dict()->get(runtime.new_str("iter"));
    ASSERT_NE(iter, nullptr);
    EXPECT_EQ(iter->type(), runtime.native_function_type());
    EXPECT_EQ(cxxpython::object_repr(iter), "<built-in function iter>");

    cxxpython::Object *next = runtime.builtins_dict()->get(runtime.new_str("next"));
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->type(), runtime.native_function_type());
    EXPECT_EQ(cxxpython::object_repr(next), "<built-in function next>");

    for (const char *name : {"all", "any", "sum", "min", "max", "sorted", "dir", "vars",
                             "isinstance", "issubclass", "callable", "id", "globals", "locals",
                             "format", "compile", "eval", "exec"})
    {
        cxxpython::Object *function = runtime.builtins_dict()->get(runtime.new_str(name));
        ASSERT_NE(function, nullptr) << name;
        EXPECT_EQ(function->type(), runtime.native_function_type()) << name;
    }

    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("StopIteration")), runtime.stop_iteration_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("BaseException")), runtime.base_exception_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("Exception")), runtime.exception_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("TypeError")), runtime.type_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("ValueError")), runtime.value_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("IndexError")), runtime.index_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("KeyError")), runtime.key_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("SyntaxError")), runtime.syntax_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("NameError")), runtime.name_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("UnboundLocalError")),
              runtime.unbound_local_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("AttributeError")), runtime.attribute_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("None")), runtime.none());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("True")), runtime.true_value());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("False")), runtime.false_value());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("object")), runtime.object_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("type")), runtime.type_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("int")), runtime.int_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("bool")), runtime.bool_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("str")), runtime.str_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("list")), runtime.list_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("tuple")), runtime.tuple_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("dict")), runtime.dict_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("range")), runtime.range_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("enumerate")), runtime.enumerate_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("zip")), runtime.zip_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("map")), runtime.map_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("filter")), runtime.filter_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("property")), runtime.property_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("staticmethod")), runtime.staticmethod_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("classmethod")), runtime.classmethod_type());
}

TEST(Module, BuiltinConstructorsUseSharedNativeArgumentValidation)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *keywords = runtime.new_dict();
    keywords->set(runtime.new_str("value"), runtime.new_str("x"));

    try
    {
        (void)cxxpython::object_call(runtime.str_type(), cxxpython::CallArguments{{}, keywords});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "str() takes no keyword arguments");
    }

    try
    {
        (void)cxxpython::object_call(runtime.property_type(),
                                     {runtime.none(),
                                      runtime.none(),
                                      runtime.none(),
                                      runtime.none(),
                                      runtime.none()});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "property() takes at most 4 arguments");
    }

    try
    {
        (void)cxxpython::object_call(runtime.staticmethod_type(), {});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "staticmethod() takes exactly one argument (0 given)");
    }
}

TEST(Module, BuiltinCoreConstructorsCreateProtocolObjects)
{
    cxxpython::Runtime runtime;

    auto *zero = dynamic_cast<cxxpython::IntObject *>(cxxpython::object_call(runtime.int_type(), {}));
    ASSERT_NE(zero, nullptr);
    EXPECT_EQ(zero->type(), runtime.int_type());
    EXPECT_EQ(zero->value, 0);

    auto *from_bool =
        dynamic_cast<cxxpython::IntObject *>(cxxpython::object_call(runtime.int_type(), {runtime.true_value()}));
    ASSERT_NE(from_bool, nullptr);
    EXPECT_EQ(from_bool->type(), runtime.int_type());
    EXPECT_EQ(from_bool->value, 1);

    auto *from_str =
        dynamic_cast<cxxpython::IntObject *>(cxxpython::object_call(runtime.int_type(), {runtime.new_str("-42")}));
    ASSERT_NE(from_str, nullptr);
    EXPECT_EQ(from_str->value, -42);

    EXPECT_EQ(cxxpython::object_call(runtime.bool_type(), {}), runtime.false_value());
    EXPECT_EQ(cxxpython::object_call(runtime.bool_type(), {runtime.new_list({runtime.new_int(1)})}),
              runtime.true_value());
    EXPECT_EQ(cxxpython::object_call(runtime.bool_type(), {runtime.new_list({})}),
              runtime.false_value());

    auto *tuple = dynamic_cast<cxxpython::TupleObject *>(
        cxxpython::object_call(runtime.tuple_type(), {runtime.new_list({runtime.new_int(1), runtime.new_int(2)})}));
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->type(), runtime.tuple_type());
    EXPECT_EQ(tuple->size(), 2U);

    auto *dict = dynamic_cast<cxxpython::DictObject *>(cxxpython::object_call(runtime.dict_type(), {}));
    ASSERT_NE(dict, nullptr);
    EXPECT_EQ(dict->type(), runtime.dict_type());
    EXPECT_EQ(dict->size(), 0U);

    try
    {
        (void)cxxpython::object_call(runtime.int_type(), {runtime.new_str("not-an-int")});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.value_error_type());
        EXPECT_STREQ(error.what(), "invalid literal for int() with base 10: 'not-an-int'");
    }
}

TEST(Module, ExecutesCodeUsingModuleGlobals)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::ModuleObject *module = runtime.new_module("__main__", "script.py");
    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "x = __name__ + ':' + __file__\n");

    EXPECT_EQ(vm.execute(*code, *module->dict()), runtime.none());

    auto *x = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("x")));
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->value, "__main__:script.py");
}

TEST(Module, TraversesModuleDictionary)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *module = runtime.new_module("mod", "mod.py");

    std::vector<cxxpython::Object *> visited;
    module->visit_children([&visited](cxxpython::Object *object)
                           { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], module->dict());
}
