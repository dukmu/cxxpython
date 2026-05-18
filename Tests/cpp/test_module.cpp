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
    ASSERT_NE(name, nullptr);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(name->value, "__main__");
    EXPECT_EQ(file->value, "/tmp/example.py");
    EXPECT_EQ(module->repr(), "<module '__main__' from '/tmp/example.py'>");
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
