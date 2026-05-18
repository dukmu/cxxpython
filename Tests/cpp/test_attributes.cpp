#include <gtest/gtest.h>

#include <string>
#include <vector>

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
        cxxpython::Object *last_set_instance = nullptr;
        cxxpython::Object *last_set_value = nullptr;
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

    void descriptor_set(cxxpython::Object &object,
                        cxxpython::Object *instance,
                        cxxpython::Object *value)
    {
        auto &descriptor = static_cast<DescriptorObject &>(object);
        descriptor.last_set_instance = instance;
        descriptor.last_set_value = value;
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

TEST(Attributes, SetsModuleAndTypeDictionariesThroughAttributeApi)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *module = runtime.new_module("sample", "sample.py");
    cxxpython::Object *module_value = runtime.new_str("module");
    cxxpython::Object *type_value = runtime.new_str("type");

    cxxpython::object_set_attr(module, "answer", module_value);
    cxxpython::object_set_attr(runtime.int_type(), "answer", type_value);

    EXPECT_EQ(module->dict()->get(runtime.new_str("answer")), module_value);
    EXPECT_EQ(runtime.int_type()->dict->get(runtime.new_str("answer")), type_value);
    EXPECT_EQ(cxxpython::object_get_attr(module, "answer"), module_value);
    EXPECT_EQ(cxxpython::object_get_attr(runtime.int_type(), "answer"), type_value);
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

TEST(Attributes, ReadsClassThroughDataDescriptor)
{
    cxxpython::Runtime runtime;
    cxxpython::ListObject *list = runtime.new_list({});
    cxxpython::TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type());
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(custom_type);
    cxxpython::Object *descriptor = runtime.object_type()->dict->get(runtime.new_str("__class__"));

    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->type(), runtime.getset_descriptor_type());
    EXPECT_EQ(cxxpython::object_repr(descriptor), "<attribute '__class__' of 'object' objects>");
    EXPECT_EQ(cxxpython::object_get_attr(list, "__class__"), runtime.list_type());
    EXPECT_EQ(cxxpython::object_get_attr(instance, "__class__"), custom_type);
    EXPECT_EQ(cxxpython::object_get_attr(runtime.list_type(), "__class__"), runtime.type_type());
    EXPECT_EQ(cxxpython::object_get_attr(runtime.object_type(), "__class__"), runtime.type_type());
}

TEST(Attributes, ClassDescriptorCannotBeShadowedByInstanceOrTypeAttributes)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type());
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(custom_type);
    runtime.list_type()->dict->set(runtime.new_str("__class__"), runtime.new_str("shadow"));

    EXPECT_EQ(cxxpython::object_get_attr(runtime.list_type(), "__class__"), runtime.type_type());

    try
    {
        cxxpython::object_set_attr(instance, "__class__", runtime.int_type());
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "__class__ assignment only supported for mutable types or ModuleType subclasses");
        EXPECT_EQ(instance->instance_dict(), nullptr);
        EXPECT_EQ(cxxpython::object_get_attr(instance, "__class__"), custom_type);
    }

    try
    {
        cxxpython::object_set_attr(runtime.list_type(), "__class__", runtime.int_type());
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_EQ(cxxpython::object_get_attr(runtime.list_type(), "__class__"), runtime.type_type());
    }
}

TEST(Attributes, TypeDictIsExposedAsMappingProxy)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *proxy_object = cxxpython::object_get_attr(runtime.list_type(), "__dict__");
    auto *proxy = dynamic_cast<cxxpython::MappingProxyObject *>(proxy_object);

    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type(), runtime.mappingproxy_type());
    EXPECT_EQ(proxy->mapping(), runtime.list_type()->dict);
    EXPECT_EQ(proxy->mapping()->get(runtime.new_str("__name__")),
              runtime.list_type()->dict->get(runtime.new_str("__name__")));

    cxxpython::Object *marker = runtime.new_str("visible");
    cxxpython::object_set_attr(runtime.list_type(), "marker", marker);
    EXPECT_EQ(proxy->mapping()->get(runtime.new_str("marker")), marker);
    const std::string repr = cxxpython::object_repr(proxy);
    EXPECT_TRUE(repr.starts_with("mappingproxy({"));
    EXPECT_NE(repr.find("'marker': 'visible'"), std::string::npos);

    std::vector<cxxpython::Object *> children;
    proxy->visit_children([&children](cxxpython::Object *object)
                          { children.push_back(object); });
    ASSERT_EQ(children.size(), 1U);
    EXPECT_EQ(children[0], runtime.list_type()->dict);

    try
    {
        cxxpython::object_set_attr(proxy, "marker", runtime.new_str("no"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'mappingproxy' object has no attribute 'marker'");
    }

    try
    {
        (void)cxxpython::object_hash(proxy);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unhashable type: 'mappingproxy'");
    }
}

TEST(Attributes, InstanceAndModuleDictDescriptorsExposeMutableDicts)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type());
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(custom_type);
    cxxpython::DictObject *instance_dict =
        dynamic_cast<cxxpython::DictObject *>(cxxpython::object_get_attr(instance, "__dict__"));
    cxxpython::Object *value = runtime.new_str("value");

    ASSERT_NE(instance_dict, nullptr);
    EXPECT_EQ(instance->instance_dict(), instance_dict);
    instance_dict->set(runtime.new_str("field"), value);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "field"), value);

    cxxpython::ModuleObject *module = runtime.new_module("sample", "sample.py");
    EXPECT_EQ(cxxpython::object_get_attr(module, "__dict__"), module->dict());
}

TEST(Attributes, CompactBuiltinInstancesDoNotExposeDict)
{
    cxxpython::Runtime runtime;
    cxxpython::ListObject *list = runtime.new_list({});

    try
    {
        (void)cxxpython::object_get_attr(list, "__dict__");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'list' object has no attribute '__dict__'");
    }
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

TEST(Attributes, StoresAndReadsInstanceDictionaries)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type());
    cxxpython::Object *class_value = runtime.new_str("from-type");
    cxxpython::Object *instance_value = runtime.new_str("from-instance");
    custom_type->dict->set(runtime.new_str("name"), class_value);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(custom_type);

    EXPECT_EQ(instance->instance_dict(), nullptr);
    cxxpython::object_set_attr(instance, "name", instance_value);

    ASSERT_NE(instance->instance_dict(), nullptr);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "name"), instance_value);

    std::vector<cxxpython::Object *> children;
    instance->visit_children([&children](cxxpython::Object *object)
                             { children.push_back(object); });
    ASSERT_EQ(children.size(), 1U);
    EXPECT_EQ(children[0], instance->instance_dict());
}

TEST(Attributes, BuiltinCompactInstancesRejectArbitraryAttributes)
{
    cxxpython::Runtime runtime;
    cxxpython::ListObject *list = runtime.new_list({});

    try
    {
        cxxpython::object_set_attr(list, "marker", runtime.new_int(1));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'list' object has no attribute 'marker'");
        EXPECT_EQ(list->instance_dict(), nullptr);
    }
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

TEST(Attributes, DataDescriptorsOverrideInstanceDictionaries)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeSlots descriptor_slots;
    descriptor_slots.descr_get = descriptor_get;
    descriptor_slots.descr_set = descriptor_set;
    cxxpython::TypeObject *descriptor_type =
        runtime.new_heap_type("DataDescriptor", runtime.object_type(), descriptor_slots);
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *descriptor_result = runtime.new_str("from-descriptor");
    cxxpython::Object *instance_value = runtime.new_str("from-instance");
    cxxpython::Object *set_value = runtime.new_str("set-value");
    auto *descriptor = runtime.allocate<DescriptorObject>(descriptor_type, descriptor_result);
    owner_type->dict->set(runtime.new_str("field"), descriptor);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    cxxpython::object_set_attr(instance, "field", instance_value);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "field"), descriptor_result);
    EXPECT_EQ(descriptor->last_set_instance, instance);
    EXPECT_EQ(descriptor->last_set_value, instance_value);
    EXPECT_EQ(instance->instance_dict(), nullptr);

    cxxpython::object_set_attr(instance, "plain", set_value);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "plain"), set_value);

    cxxpython::TypeObject *derived_descriptor_type =
        runtime.new_heap_type("DerivedDataDescriptor", descriptor_type);
    cxxpython::Object *derived_result = runtime.new_str("derived-descriptor");
    auto *derived_descriptor = runtime.allocate<DescriptorObject>(derived_descriptor_type, derived_result);
    owner_type->dict->set(runtime.new_str("derived_field"), derived_descriptor);
    cxxpython::object_set_attr(instance, "derived_field", set_value);

    EXPECT_EQ(cxxpython::object_get_attr(instance, "derived_field"), derived_result);
    EXPECT_EQ(derived_descriptor->last_set_instance, instance);
    EXPECT_EQ(derived_descriptor->last_set_value, set_value);
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
