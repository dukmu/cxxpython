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

    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("object")), runtime.object_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("list")), runtime.list_type());
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
