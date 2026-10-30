#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "internal/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class FileImports : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    std::filesystem::path directory;

    void SetUp() override
    {
        context.bootstrap_core();
        directory = std::filesystem::temp_directory_path() /
                    ("cxxpython-import-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(directory);
        ReturnSlot out(context);
        const std::array paths{directory.string()};
        set_import_path(context, paths, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }
    void write(const std::string &path, const std::string &source)
    {
        auto file = directory / path;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file);
        stream << source;
    }
    Value attribute(Handle object, std::u32string name)
    {
        ReturnSlot key(context), out(context);
        make_str(context, std::move(name), key);
        get_attribute(context, object, roots.root(key.value()), out);
        if (out.tag() != ResultTag::Ok)
            throw std::runtime_error("attribute lookup failed");
        return roots.root(out.value()).get(context);
    }
};

TEST_F(FileImports, SourcePackagePrecedenceRelativeImportAndCache)
{
    write("pkg.py", "value = 1\n");
    write("pkg/__init__.py", "value = 40\n");
    write("pkg/part.py", "answer = 42\n");
    ReturnSlot out(context);
    import_module(context, U"pkg", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto package = roots.root(out.value());
    EXPECT_EQ(int_decimal(context, attribute(package, U"value")), "40");
    ReturnSlot text(context), from(context);
    make_str(context, U"part", text);
    auto part = roots.root(text.value());
    make_str(context, U"answer", text);
    cxxpython::core::make_tuple(context, std::array{text.value()}, from);
    import_name(context, part, roots.root(module_dict(context, package.get(context))),
                roots.root(from.value()), roots.root(context.builtin(CoreObject::IntOne)), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"answer")), "42");
    EXPECT_EQ(context.payload<StrObject>(attribute(package, U"__package__")).value, U"pkg");
    EXPECT_EQ(source_utf8(context.payload<StrObject>(attribute(package, U"__file__")).value),
              (directory / "pkg/__init__.py").string());
    import_module(context, U"pkg", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), package.get(context));
}

TEST_F(FileImports, SyntaxFailureCanBeRetried)
{
    write("broken.py", "value = (\n");
    ReturnSlot out(context);
    import_module(context, U"broken", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::SyntaxErrorType));
    EXPECT_FALSE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"broken"));
    write("broken.py", "value = 42\n");
    import_module(context, U"broken", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"value")), "42");
}

TEST_F(FileImports, FileBodyFailureAndManagedAllocationFailureRollbackAndRetry)
{
    write("dependency.py", "value = 40\n");
    write("target.py", "from dependency import value\nraise ValueError(value)\n");
    ReturnSlot out(context);
    import_module(context, U"target", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    EXPECT_FALSE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"target"));
    const auto dependency = roots.root(
        *detail::name_lookup(context, context.builtin(CoreObject::Modules), U"dependency"));
    write("target.py", "from dependency import value\nanswer = value + 2\n");
    bool success = false;
    for (std::size_t quota = 0; quota < 300; ++quota) {
        SCOPED_TRACE(quota);
        context.set_allocation_limit(quota);
        import_module(context, U"target", out);
        context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
        if (out.tag() == ResultTag::Ok) {
            EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"answer")), "42");
            success = true;
            break;
        }
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::MemoryErrorType));
        EXPECT_FALSE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"target"));
        EXPECT_EQ(
            *detail::name_lookup(context, context.builtin(CoreObject::Modules), U"dependency"),
            dependency.get(context));
    }
    EXPECT_TRUE(success);
}

TEST_F(FileImports, ReassignedPackagePathIsUsedForNextChild)
{
    write("pkg/__init__.py", "value = 1\n");
    write("other/child.py", "value = 42\n");
    ReturnSlot out(context), key(context), path(context);
    import_module(context, U"pkg", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto package = roots.root(out.value());
    make_str(context, U"__path__", key);
    auto field = roots.root(key.value());
    make_str(context, detail::decode_filename(SourceBuffer("", (directory / "other").string())),
             out);
    cxxpython::core::make_tuple(context, std::array{out.value()}, path);
    set_attribute(context, package, field, roots.root(path.value()), out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"pkg.child", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"value")), "42");
}

TEST_F(FileImports, NamespacePortionsAndIterablePathReassignment)
{
    write("one/ns/first.py", "value = 1\n");
    write("two/ns/second.py", "value = 2\n");
    write("other/third.py", "value = 3\n");
    ReturnSlot out(context), key(context), tuple(context), iterator(context);
    const std::array paths{(directory / "one").string(), (directory / "two").string()};
    set_import_path(context, paths, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"ns", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto package = roots.root(out.value());
    auto package_path = roots.root(attribute(package, U"__path__"));
    EXPECT_EQ(context.payload<TupleObject>(package_path).items.size(), 2u);
    EXPECT_FALSE(
        detail::name_lookup(context, module_dict(context, package.get(context)), U"__file__"));
    import_module(context, U"ns.second", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"value")), "2");

    make_str(context, detail::decode_filename(SourceBuffer("", (directory / "other").string())),
             out);
    cxxpython::core::make_tuple(context, std::array{context.builtin(CoreObject::None), out.value()},
                                tuple);
    get_iter(context, roots.root(tuple.value()), iterator);
    ASSERT_EQ(iterator.tag(), ResultTag::Ok);
    auto path_iterator = roots.root(iterator.value());
    make_str(context, U"__path__", key);
    set_attribute(context, package, roots.root(key.value()), path_iterator, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"ns.third", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"value")), "3");
}

TEST_F(FileImports, LaterConcreteModuleOverridesNamespacePortion)
{
    write("one/choice/child.py", "value = 1\n");
    write("two/choice.py", "value = 42\n");
    ReturnSlot out(context);
    const std::array paths{(directory / "one").string(), (directory / "two").string()};
    set_import_path(context, paths, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"choice", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto module = roots.root(out.value());
    EXPECT_EQ(int_decimal(context, attribute(module, U"value")), "42");
    EXPECT_FALSE(
        detail::name_lookup(context, module_dict(context, module.get(context)), U"__path__"));
}

TEST_F(FileImports, SysPathAcceptsIterableAndSkipsNonStrings)
{
    write("entry.py", "value = 42\n");
    ReturnSlot out(context), key(context), tuple(context), iterator(context);
    import_module(context, U"sys", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto sys = roots.root(out.value());
    make_str(context, detail::decode_filename(SourceBuffer("", directory.string())), out);
    cxxpython::core::make_tuple(context, std::array{context.builtin(CoreObject::None), out.value()},
                                tuple);
    get_iter(context, roots.root(tuple.value()), iterator);
    ASSERT_EQ(iterator.tag(), ResultTag::Ok);
    auto path_iterator = roots.root(iterator.value());
    make_str(context, U"path", key);
    set_attribute(context, sys, roots.root(key.value()), path_iterator, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    import_module(context, U"entry", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(int_decimal(context, attribute(roots.root(out.value()), U"value")), "42");
}

TEST_F(FileImports, SourceIndentationFailuresKeepPythonExceptionHierarchy)
{
    write("indent.py", "if True:\nvalue = 1\n");
    ReturnSlot out(context);
    import_module(context, U"indent", out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::IndentationErrorType));
    EXPECT_TRUE(is_subtype(context, context.type_of(out.exception()),
                           context.builtin(CoreObject::SyntaxErrorType)));
    EXPECT_FALSE(detail::name_lookup(context, context.builtin(CoreObject::Modules), U"indent"));
}
} // namespace
