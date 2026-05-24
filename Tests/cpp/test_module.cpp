#include <gtest/gtest.h>

#include <csignal>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <thread>

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
    auto *print_name = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(print, "__name__"));
    auto *print_doc = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(print, "__doc__"));
    auto *print_signature =
        dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(print, "__text_signature__"));
    ASSERT_NE(print_name, nullptr);
    ASSERT_NE(print_doc, nullptr);
    ASSERT_NE(print_signature, nullptr);
    EXPECT_EQ(print_name->value, "print");
    EXPECT_NE(print_doc->value.find("Print objects"), std::string::npos);
    EXPECT_EQ(print_signature->value, "($module, /, *args, sep=' ', end='\\n')");

    cxxpython::Object *len = runtime.builtins_dict()->get(runtime.new_str("len"));
    ASSERT_NE(len, nullptr);
    EXPECT_EQ(len->type(), runtime.native_function_type());
    EXPECT_EQ(cxxpython::object_repr(len), "<built-in function len>");
    auto *len_signature =
        dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(len, "__text_signature__"));
    ASSERT_NE(len_signature, nullptr);
    EXPECT_EQ(len_signature->value, "($module, object, /)");

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
                             "format", "compile", "eval", "exec", "__build_class__", "__import__"})
    {
        cxxpython::Object *function = runtime.builtins_dict()->get(runtime.new_str(name));
        ASSERT_NE(function, nullptr) << name;
        EXPECT_EQ(function->type(), runtime.native_function_type()) << name;
        auto *doc = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(function, "__doc__"));
        auto *signature =
            dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(function, "__text_signature__"));
        ASSERT_NE(doc, nullptr) << name;
        ASSERT_NE(signature, nullptr) << name;
        EXPECT_FALSE(doc->value.empty()) << name;
        EXPECT_FALSE(signature->value.empty()) << name;
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
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("RecursionError")),
              runtime.recursion_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("AttributeError")), runtime.attribute_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("ImportError")), runtime.import_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("OSError")), runtime.os_error_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("KeyboardInterrupt")), runtime.keyboard_interrupt_type());
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
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("slice")), runtime.slice_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("dict")), runtime.dict_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("range")), runtime.range_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("enumerate")), runtime.enumerate_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("zip")), runtime.zip_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("map")), runtime.map_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("filter")), runtime.filter_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("property")), runtime.property_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("staticmethod")), runtime.staticmethod_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("classmethod")), runtime.classmethod_type());
    EXPECT_EQ(runtime.builtins_dict()->get(runtime.new_str("super")), runtime.super_type());
}

TEST(Module, ImportsPythonModulesThroughRuntimeRegistry)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path module_path = directory / "cxxpython_import_runtime_module.py";
    {
        std::ofstream file(module_path, std::ios::binary);
        file << "value = 'loaded:' + __name__ + ':' + __file__\n"
                "loader_name = __loader__.__class__.__name__\n"
                "spec_name = __spec__.name\n"
                "spec_origin = __spec__.origin\n"
                "spec_loader_name = __spec__.loader.__class__.__name__\n"
                "has_location = __spec__.has_location\n"
                "package = __package__\n";
    }

    runtime.add_module_search_path(directory.string());
    cxxpython::ModuleObject *module = runtime.import_module("cxxpython_import_runtime_module");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->name(), "cxxpython_import_runtime_module");
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_import_runtime_module")), module);

    auto *value = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("value")));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, "loaded:cxxpython_import_runtime_module:" + module_path.string());
    auto *loader_name = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("loader_name")));
    auto *spec_name = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("spec_name")));
    auto *spec_origin = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("spec_origin")));
    auto *spec_loader_name = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("spec_loader_name")));
    auto *package = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("package")));
    ASSERT_NE(loader_name, nullptr);
    ASSERT_NE(spec_name, nullptr);
    ASSERT_NE(spec_origin, nullptr);
    ASSERT_NE(spec_loader_name, nullptr);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(loader_name->value, "SourceFileLoader");
    EXPECT_EQ(spec_name->value, "cxxpython_import_runtime_module");
    EXPECT_EQ(spec_origin->value, module_path.string());
    EXPECT_EQ(spec_loader_name->value, "SourceFileLoader");
    EXPECT_EQ(module->dict()->get(runtime.new_str("has_location")), runtime.true_value());
    EXPECT_EQ(package->value, "");
    EXPECT_EQ(runtime.import_module("cxxpython_import_runtime_module"), module);

    std::filesystem::remove(module_path);
}

TEST(Module, ConcurrentImportsShareSingleModuleRegistryEntry)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path module_path = directory / "cxxpython_concurrent_import_module.py";
    {
        std::ofstream file(module_path, std::ios::binary);
        file << "value = 'loaded:' + __name__\n";
    }

    runtime.add_module_search_path(directory.string());

    std::array<cxxpython::ModuleObject *, 2> modules{nullptr, nullptr};
    std::array<std::exception_ptr, 2> errors{};
    std::thread first([&] {
        try
        {
            modules[0] = runtime.import_module("cxxpython_concurrent_import_module");
        }
        catch (...)
        {
            errors[0] = std::current_exception();
        }
    });
    std::thread second([&] {
        try
        {
            modules[1] = runtime.import_module("cxxpython_concurrent_import_module");
        }
        catch (...)
        {
            errors[1] = std::current_exception();
        }
    });

    first.join();
    second.join();
    if (errors[0])
        std::rethrow_exception(errors[0]);
    if (errors[1])
        std::rethrow_exception(errors[1]);

    ASSERT_NE(modules[0], nullptr);
    EXPECT_EQ(modules[0], modules[1]);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_concurrent_import_module")), modules[0]);

    auto *value = dynamic_cast<cxxpython::StrObject *>(modules[0]->dict()->get(runtime.new_str("value")));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, "loaded:cxxpython_concurrent_import_module");

    std::filesystem::remove(module_path);
}

TEST(Module, ImportsPackagesAndDottedSubmodules)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path package_dir = directory / "cxxpython_import_pkg";
    const std::filesystem::path extra_root = directory / "cxxpython_import_extra";
    const std::filesystem::path extra_package_dir = extra_root / "cxxpython_import_pkg";
    const std::filesystem::path init_path = package_dir / "__init__.py";
    const std::filesystem::path child_path = package_dir / "child.py";
    const std::filesystem::path sibling_path = package_dir / "sibling.py";
    const std::filesystem::path extra_path = extra_package_dir / "extra.py";
    std::filesystem::create_directories(package_dir);
    std::filesystem::create_directories(extra_package_dir);
    {
        std::ofstream file(init_path, std::ios::binary);
        file << "package_value = 'package:' + __name__\n"
                "package_name = __package__\n"
                "path_size = len(__path__)\n";
    }
    {
        std::ofstream file(child_path, std::ios::binary);
        file << "value = 'child:' + __name__\n"
                "package_name = __package__\n";
    }
    {
        std::ofstream file(sibling_path, std::ios::binary);
        file << "value = 'sibling'\n";
    }
    {
        std::ofstream file(extra_path, std::ios::binary);
        file << "value = 'extra'\n";
    }

    runtime.add_module_search_path(directory.string());
    cxxpython::ModuleObject *child = runtime.import_module("cxxpython_import_pkg.child");
    ASSERT_NE(child, nullptr);
    cxxpython::Object *package_object = runtime.modules_dict()->get(runtime.new_str("cxxpython_import_pkg"));
    auto *package = dynamic_cast<cxxpython::ModuleObject *>(package_object);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_import_pkg.child")), child);
    EXPECT_EQ(cxxpython::object_get_attr(package, "child"), child);

    auto *package_value = dynamic_cast<cxxpython::StrObject *>(
        package->dict()->get(runtime.new_str("package_value")));
    auto *package_name = dynamic_cast<cxxpython::StrObject *>(
        package->dict()->get(runtime.new_str("package_name")));
    auto *path_size = dynamic_cast<cxxpython::IntObject *>(package->dict()->get(runtime.new_str("path_size")));
    auto *child_value = dynamic_cast<cxxpython::StrObject *>(child->dict()->get(runtime.new_str("value")));
    auto *child_package = dynamic_cast<cxxpython::StrObject *>(child->dict()->get(runtime.new_str("package_name")));
    ASSERT_NE(package_value, nullptr);
    ASSERT_NE(package_name, nullptr);
    ASSERT_NE(path_size, nullptr);
    ASSERT_NE(child_value, nullptr);
    ASSERT_NE(child_package, nullptr);
    EXPECT_EQ(package_value->value, "package:cxxpython_import_pkg");
    EXPECT_EQ(package_name->value, "cxxpython_import_pkg");
    EXPECT_EQ(path_size->value, 1);
    EXPECT_EQ(child_value->value, "child:cxxpython_import_pkg.child");
    EXPECT_EQ(child_package->value, "cxxpython_import_pkg");

    auto *path = dynamic_cast<cxxpython::ListObject *>(package->dict()->get(runtime.new_str("__path__")));
    ASSERT_NE(path, nullptr);
    path->append(runtime.new_str(extra_package_dir.string()));
    cxxpython::ModuleObject *extra = runtime.import_module("cxxpython_import_pkg.extra");
    ASSERT_NE(extra, nullptr);
    auto *extra_value = dynamic_cast<cxxpython::StrObject *>(extra->dict()->get(runtime.new_str("value")));
    ASSERT_NE(extra_value, nullptr);
    EXPECT_EQ(extra_value->value, "extra");
    EXPECT_EQ(cxxpython::object_get_attr(package, "extra"), extra);

    std::filesystem::remove(child_path);
    std::filesystem::remove(sibling_path);
    std::filesystem::remove(extra_path);
    std::filesystem::remove(init_path);
    std::filesystem::remove(extra_package_dir);
    std::filesystem::remove(extra_root);
    std::filesystem::remove(package_dir);
}

TEST(Module, ImportNameOwnsDottedFromlistAndRelativeSemantics)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path package_dir = directory / "cxxpython_import_name_pkg";
    const std::filesystem::path init_path = package_dir / "__init__.py";
    const std::filesystem::path child_path = package_dir / "child.py";
    std::filesystem::create_directories(package_dir);
    {
        std::ofstream file(init_path, std::ios::binary);
        file << "label = 'package'\n";
    }
    {
        std::ofstream file(child_path, std::ios::binary);
        file << "value = 'child:' + __name__\n";
    }

    runtime.add_module_search_path(directory.string());

    cxxpython::Object *top = runtime.import_name(cxxpython::ImportRequest{
        .name = "cxxpython_import_name_pkg.child",
        .fromlist = runtime.new_tuple({}),
    });
    auto *package = dynamic_cast<cxxpython::ModuleObject *>(top);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->name(), "cxxpython_import_name_pkg");

    cxxpython::Object *leaf = runtime.import_name(cxxpython::ImportRequest{
        .name = "cxxpython_import_name_pkg.child",
        .fromlist = runtime.new_tuple({runtime.new_str("value")}),
    });
    auto *child = dynamic_cast<cxxpython::ModuleObject *>(leaf);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->name(), "cxxpython_import_name_pkg.child");
    EXPECT_EQ(cxxpython::object_get_attr(package, "child"), child);

    cxxpython::Object *relative_leaf = runtime.import_name(cxxpython::ImportRequest{
        .name = "child",
        .globals = package->dict(),
        .fromlist = runtime.new_tuple({runtime.new_str("value")}),
        .level = 1,
    });
    EXPECT_EQ(relative_leaf, child);

    auto *value = dynamic_cast<cxxpython::StrObject *>(child->dict()->get(runtime.new_str("value")));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, "child:cxxpython_import_name_pkg.child");

    std::filesystem::remove(child_path);
    std::filesystem::remove(init_path);
    std::filesystem::remove(package_dir);
}

TEST(Module, SourceModulesAreVisibleDuringCircularImport)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path first_path = directory / "cxxpython_circular_first.py";
    const std::filesystem::path second_path = directory / "cxxpython_circular_second.py";
    {
        std::ofstream file(first_path, std::ios::binary);
        file << "before = 'visible'\n"
                "import cxxpython_circular_second\n"
                "after = cxxpython_circular_second.seen\n";
    }
    {
        std::ofstream file(second_path, std::ios::binary);
        file << "import cxxpython_circular_first\n"
                "seen = cxxpython_circular_first.before\n";
    }

    runtime.add_module_search_path(directory.string());
    cxxpython::ModuleObject *first = runtime.import_module("cxxpython_circular_first");
    ASSERT_NE(first, nullptr);
    auto *second = dynamic_cast<cxxpython::ModuleObject *>(
        runtime.modules_dict()->get(runtime.new_str("cxxpython_circular_second")));
    ASSERT_NE(second, nullptr);

    auto *after = dynamic_cast<cxxpython::StrObject *>(first->dict()->get(runtime.new_str("after")));
    auto *seen = dynamic_cast<cxxpython::StrObject *>(second->dict()->get(runtime.new_str("seen")));
    ASSERT_NE(after, nullptr);
    ASSERT_NE(seen, nullptr);
    EXPECT_EQ(after->value, "visible");
    EXPECT_EQ(seen->value, "visible");

    std::filesystem::remove(second_path);
    std::filesystem::remove(first_path);
}

TEST(Module, FailedImportsDoNotPoisonModuleRegistry)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path module_path = directory / "cxxpython_import_failing_module.py";
    {
        std::ofstream file(module_path, std::ios::binary);
        file << "ready = 'yes'\nmissing_name\n";
    }

    runtime.add_module_search_path(directory.string());
    try
    {
        (void)runtime.import_module("cxxpython_import_failing_module");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.name_error_type());
    }

    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_import_failing_module")), nullptr);
    std::filesystem::remove(module_path);
}

TEST(Module, ExtensionImportUsesSharedLoaderAndReportsMissingEntryPoint)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path module_path = directory / "cxxpython_missing_extension.so";
    {
        std::ofstream file(module_path, std::ios::binary);
        file << "not a shared object\n";
    }

    runtime.add_module_search_path(directory.string());
    try
    {
        (void)runtime.import_module("cxxpython_missing_extension");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.import_error_type());
        EXPECT_NE(std::string(error.what()).find("cannot load extension module 'cxxpython_missing_extension'"),
                  std::string::npos);
    }

    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_missing_extension")), nullptr);
    std::filesystem::remove(module_path);
}

TEST(Module, ImportsExtensionModuleThroughNarrowCAbi)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path extension_path =
        std::filesystem::path(CXXPYTHON_SAMPLE_EXTENSION_PATH);
    runtime.add_module_search_path(extension_path.parent_path().string());

    cxxpython::ModuleObject *module = runtime.import_module("cxxpython_sample_extension");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->name(), "cxxpython_sample_extension");

    auto *answer = dynamic_cast<cxxpython::IntObject *>(module->dict()->get(runtime.new_str("answer")));
    auto *label = dynamic_cast<cxxpython::StrObject *>(module->dict()->get(runtime.new_str("label")));
    ASSERT_NE(answer, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(answer->value, 42);
    EXPECT_EQ(label->value, "from-extension");
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_sample_extension")), module);

    cxxpython::Object *loader = module->dict()->get(runtime.new_str("__loader__"));
    ASSERT_NE(loader, nullptr);
    auto *loader_class = dynamic_cast<cxxpython::TypeObject *>(cxxpython::object_get_attr(loader, "__class__"));
    ASSERT_NE(loader_class, nullptr);
    auto *loader_name = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(loader_class, "__name__"));
    ASSERT_NE(loader_name, nullptr);
    EXPECT_EQ(loader_name->value, "ExtensionFileLoader");
}

TEST(Module, FailedExtensionInitializationDoesNotPoisonModuleRegistry)
{
    cxxpython::Runtime runtime;
    const std::filesystem::path extension_path =
        std::filesystem::path(CXXPYTHON_FAILING_EXTENSION_PATH);
    runtime.add_module_search_path(extension_path.parent_path().string());

    try
    {
        (void)runtime.import_module("cxxpython_failing_extension");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.import_error_type());
        EXPECT_STREQ(error.what(), "sample extension failed during init");
    }

    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("cxxpython_failing_extension")), nullptr);
}

TEST(Module, MissingImportRaisesImportError)
{
    cxxpython::Runtime runtime;
    runtime.add_module_search_path(std::filesystem::temp_directory_path().string());

    try
    {
        (void)runtime.import_module("cxxpython_import_missing_module");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.import_error_type());
        EXPECT_STREQ(error.what(), "No module named 'cxxpython_import_missing_module'");
    }
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

TEST(Module, ImportsStdlibModulesFromLibPath)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *inspect = runtime.import_module("inspect");
    cxxpython::ModuleObject *gc = runtime.import_module("gc");
    cxxpython::ModuleObject *collections = runtime.import_module("collections");
    cxxpython::ModuleObject *sys = runtime.import_module("sys");
    cxxpython::ModuleObject *posix = runtime.import_module("posix");
    cxxpython::ModuleObject *signal = runtime.import_module("signal");
    cxxpython::ModuleObject *io = runtime.import_module("_io");

    ASSERT_NE(inspect, nullptr);
    ASSERT_NE(gc, nullptr);
    ASSERT_NE(collections, nullptr);
    ASSERT_NE(sys, nullptr);
    ASSERT_NE(posix, nullptr);
    ASSERT_NE(signal, nullptr);
    ASSERT_NE(io, nullptr);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("inspect")), inspect);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("gc")), gc);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("collections")), collections);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("sys")), sys);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("posix")), posix);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("signal")), signal);
    EXPECT_EQ(runtime.modules_dict()->get(runtime.new_str("_io")), io);

    EXPECT_NE(inspect->dict()->get(runtime.new_str("currentframe")), nullptr);
    EXPECT_NE(gc->dict()->get(runtime.new_str("collect")), nullptr);
    EXPECT_NE(collections->dict()->get(runtime.new_str("namedtuple")), nullptr);
    EXPECT_NE(sys->dict()->get(runtime.new_str("argv")), nullptr);
    EXPECT_NE(posix->dict()->get(runtime.new_str("getcwd")), nullptr);
    EXPECT_NE(signal->dict()->get(runtime.new_str("signal")), nullptr);
    EXPECT_NE(io->dict()->get(runtime.new_str("TextIOWrapper")), nullptr);

    auto *getframe_signature = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(sys->dict()->get(runtime.new_str("_getframe")), "__text_signature__"));
    auto *gc_doc = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(gc->dict()->get(runtime.new_str("collect")), "__doc__"));
    auto *posix_signature = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(posix->dict()->get(runtime.new_str("listdir")), "__text_signature__"));
    auto *posix_kill_signature = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(posix->dict()->get(runtime.new_str("kill")), "__text_signature__"));
    auto *signal_signature = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(signal->dict()->get(runtime.new_str("signal")), "__text_signature__"));
    auto *io_signature = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(io->dict()->get(runtime.new_str("open")), "__text_signature__"));
    auto *namedtuple_doc = dynamic_cast<cxxpython::StrObject *>(
        cxxpython::object_get_attr(collections->dict()->get(runtime.new_str("namedtuple")), "__doc__"));
    ASSERT_NE(getframe_signature, nullptr);
    ASSERT_NE(gc_doc, nullptr);
    ASSERT_NE(posix_signature, nullptr);
    ASSERT_NE(posix_kill_signature, nullptr);
    ASSERT_NE(signal_signature, nullptr);
    ASSERT_NE(io_signature, nullptr);
    ASSERT_NE(namedtuple_doc, nullptr);
    EXPECT_EQ(getframe_signature->value, "($module, depth=0, /)");
    EXPECT_NE(gc_doc->value.find("garbage collection"), std::string::npos);
    EXPECT_EQ(posix_signature->value, "($module, path='.', /)");
    EXPECT_EQ(posix_kill_signature->value, "($module, pid, signal, /)");
    EXPECT_EQ(signal_signature->value, "($module, signalnum, handler, /)");
    EXPECT_EQ(io_signature->value, "($module, file, mode='r', /)");
    EXPECT_NE(namedtuple_doc->value.find("tuple subclass"), std::string::npos);

    cxxpython::Object *read_descriptor = cxxpython::object_get_attr(runtime.text_io_type(), "read");
    auto *read_signature =
        dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(read_descriptor, "__text_signature__"));
    ASSERT_NE(read_signature, nullptr);
    EXPECT_EQ(read_signature->value, "($self, /)");

    EXPECT_NE(sys->dict()->get(runtime.new_str("__loader__")), nullptr);
    EXPECT_NE(sys->dict()->get(runtime.new_str("__spec__")), nullptr);
    auto *sys_package = dynamic_cast<cxxpython::StrObject *>(sys->dict()->get(runtime.new_str("__package__")));
    ASSERT_NE(sys_package, nullptr);
    EXPECT_EQ(sys_package->value, "");
    auto *sys_spec = sys->dict()->get(runtime.new_str("__spec__"));
    ASSERT_NE(sys_spec, nullptr);
    auto *origin = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(sys_spec, "origin"));
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(origin->value, "built-in");
}

TEST(Module, SignalDeliveryRaisesKeyboardInterruptAtSafePoints)
{
#ifdef SIGINT
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *signal = runtime.import_module("signal");
    ASSERT_NE(signal, nullptr);
    ASSERT_NE(signal->dict()->get(runtime.new_str("default_int_handler")), nullptr);

    std::raise(SIGINT);
    cxxpython::CodeObject *code = cxxpython::compile_exec_input(runtime, "1 + 1\n", "<signal-test>");
    cxxpython::VirtualMachine vm(runtime);
    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected KeyboardInterrupt";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.keyboard_interrupt_type());
        ASSERT_NE(error.value(), nullptr);
        ASSERT_FALSE(error.value()->traceback_entries().empty());
        const auto &entry = error.value()->traceback_entries().back();
        EXPECT_EQ(entry.filename, "<signal-test>");
        EXPECT_EQ(entry.line, 1U);
    }
#else
    GTEST_SKIP() << "SIGINT is not available on this platform";
#endif
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

    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(visited[0], runtime.module_type());
    EXPECT_EQ(visited[1], module->dict());
}
