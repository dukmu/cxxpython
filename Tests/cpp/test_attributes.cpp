#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"

TEST(Attributes, ReadsModuleDictionaryBeforeTypeDictionary)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *module = runtime.new_module("sample", "sample.py");
    cxxpython::StrObject *value = runtime.new_str("module-value");
    module->dict()->set(runtime.new_str("answer"), value);

    EXPECT_EQ(cxxpython::object_get_attr(module, "answer"), value);
}

TEST(Attributes, ReadsTypeDictionariesAndMro)
{
    cxxpython::Runtime runtime;
    cxxpython::StrObject *from_int = runtime.new_str("numeric");
    cxxpython::StrObject *from_metatype = runtime.new_str("metatype");
    runtime.int_type()->dict->set(runtime.new_str("category"), from_int);
    runtime.type_type()->dict->set(runtime.new_str("meta_marker"), from_metatype);

    auto *bool_name = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(runtime.bool_type(), "__name__"));
    ASSERT_NE(bool_name, nullptr);
    EXPECT_EQ(bool_name->value, "bool");
    EXPECT_EQ(cxxpython::object_get_attr(runtime.bool_type(), "category"), from_int);
    EXPECT_EQ(cxxpython::object_get_attr(runtime.bool_type(), "meta_marker"), from_metatype);
}

TEST(Attributes, ReadsInstanceAttributesFromTypeMro)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type());
    cxxpython::StrObject *value = runtime.new_str("from-type");
    custom_type->dict->set(runtime.new_str("marker"), value);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(custom_type);

    EXPECT_EQ(cxxpython::object_get_attr(instance, "marker"), value);
}

TEST(Attributes, RaisesAttributeErrorForMissingAttribute)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *object = runtime.allocate<cxxpython::Object>(runtime.object_type());

    try
    {
        (void)cxxpython::object_get_attr(object, "missing");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'object' object has no attribute 'missing'");
    }
}
