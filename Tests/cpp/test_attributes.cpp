#include <gtest/gtest.h>

#include "cxxpython/runtime.hpp"

namespace
{
    class DescriptorObject final : public cxxpython::Object
    {
    public:
        DescriptorObject(cxxpython::TypeObject *type, cxxpython::Object *result)
            : Object(type), result(result)
        {
        }

        cxxpython::Object *result;
        cxxpython::Object *last_instance = nullptr;
        cxxpython::TypeObject *last_owner = nullptr;
    };

    cxxpython::Object *descriptor_get(cxxpython::Object &object,
                                      cxxpython::Object *instance,
                                      cxxpython::TypeObject *owner)
    {
        auto &descriptor = static_cast<DescriptorObject &>(object);
        descriptor.last_instance = instance;
        descriptor.last_owner = owner;
        return descriptor.result;
    }
}

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

TEST(Attributes, InvokesDescriptorsFromTypeDictionaries)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeSlots descriptor_slots;
    descriptor_slots.descr_get = descriptor_get;
    cxxpython::TypeObject *descriptor_type =
        runtime.new_heap_type("Descriptor", runtime.object_type(), descriptor_slots);
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *result = runtime.new_str("computed");
    auto *descriptor = runtime.allocate<DescriptorObject>(descriptor_type, result);
    owner_type->dict->set(runtime.new_str("field"), descriptor);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    EXPECT_EQ(cxxpython::object_get_attr(instance, "field"), result);
    EXPECT_EQ(descriptor->last_instance, instance);
    EXPECT_EQ(descriptor->last_owner, owner_type);

    EXPECT_EQ(cxxpython::object_get_attr(owner_type, "field"), result);
    EXPECT_EQ(descriptor->last_instance, nullptr);
    EXPECT_EQ(descriptor->last_owner, owner_type);

    cxxpython::TypeObject *derived_descriptor_type =
        runtime.new_heap_type("DerivedDescriptor", descriptor_type);
    cxxpython::Object *derived_result = runtime.new_str("derived");
    auto *derived_descriptor = runtime.allocate<DescriptorObject>(derived_descriptor_type, derived_result);
    owner_type->dict->set(runtime.new_str("derived_field"), derived_descriptor);

    EXPECT_EQ(cxxpython::object_get_attr(instance, "derived_field"), derived_result);
    EXPECT_EQ(derived_descriptor->last_instance, instance);
    EXPECT_EQ(derived_descriptor->last_owner, owner_type);
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
