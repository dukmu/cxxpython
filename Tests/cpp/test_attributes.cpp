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

TEST(Attributes, CallsGetAttrFallbackAfterNormalLookupFails)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::MethodDescriptorObject *getattr = runtime.new_method_descriptor(
        "__getattr__",
        owner_type,
        [](cxxpython::Runtime &runtime, cxxpython::Object *, cxxpython::ObjectSpan args)
        {
            if (args.size() != 1)
                cxxpython::raise_type_error(&runtime, "__getattr__ expected name");
            auto *name = dynamic_cast<cxxpython::StrObject *>(args[0]);
            if (!name)
                cxxpython::raise_type_error(&runtime, "__getattr__ expected str name");
            return runtime.new_str("dynamic:" + name->value);
        });
    owner_type->dict->set(runtime.new_str("__getattr__"), getattr);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    auto *computed = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(instance, "missing"));
    ASSERT_NE(computed, nullptr);
    EXPECT_EQ(computed->value, "dynamic:missing");

    cxxpython::Object *stored = runtime.new_str("stored");
    cxxpython::object_set_attr(instance, "present", stored);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "present"), stored);
}

TEST(Attributes, CallsModuleGetAttrFallback)
{
    cxxpython::Runtime runtime;
    cxxpython::ModuleObject *module = runtime.new_module("mod", "mod.py");
    cxxpython::NativeFunctionObject *getattr = runtime.new_native_function(
        "__getattr__",
        [](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 1 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "__getattr__ expected name");
            auto *name = dynamic_cast<cxxpython::StrObject *>(args.positional[0]);
            if (!name)
                cxxpython::raise_type_error(&runtime, "__getattr__ expected str name");
            return runtime.new_str("module:" + name->value);
        });
    module->dict()->set(runtime.new_str("__getattr__"), getattr);

    auto *computed = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(module, "missing"));
    ASSERT_NE(computed, nullptr);
    EXPECT_EQ(computed->value, "module:missing");
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

TEST(Attributes, DeleteAttrUsesInstanceDictAndDataDescriptorProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeSlots descriptor_slots;
    descriptor_slots.descr_get = descriptor_get;
    descriptor_slots.descr_set = descriptor_set;
    cxxpython::TypeObject *descriptor_type =
        runtime.new_heap_type("DataDescriptor", runtime.object_type(), descriptor_slots);
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    auto *descriptor = runtime.allocate<DescriptorObject>(descriptor_type, runtime.new_str("from-descriptor"));
    owner_type->dict->set(runtime.new_str("field"), descriptor);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    cxxpython::Object *plain_value = runtime.new_str("plain");
    cxxpython::object_set_attr(instance, "plain", plain_value);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "plain"), plain_value);
    cxxpython::object_delete_attr(instance, "plain");

    try
    {
        (void)cxxpython::object_get_attr(instance, "plain");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "'Owner' object has no attribute 'plain'");
    }

    cxxpython::object_delete_attr(instance, "field");
    EXPECT_EQ(descriptor->last_set_instance, instance);
    EXPECT_EQ(descriptor->last_set_value, nullptr);
}

TEST(Attributes, PropertyObjectsUseDescriptorProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *computed = runtime.new_str("computed");
    cxxpython::Object *set_marker = runtime.new_str("set-marker");
    cxxpython::Object *last_set = nullptr;

    cxxpython::NativeFunctionObject *getter = runtime.new_native_function(
        "get_field",
        [computed](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 1 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "get_field() expected self");
            return computed;
        });
    cxxpython::NativeFunctionObject *setter = runtime.new_native_function(
        "set_field",
        [&last_set, set_marker](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 2 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "set_field() expected self and value");
            last_set = args.positional[1];
            return set_marker;
        });
    cxxpython::PropertyObject *property = runtime.new_property(getter, setter);
    owner_type->dict->set(runtime.new_str("field"), property);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);
    cxxpython::Object *value = runtime.new_str("new-value");

    EXPECT_EQ(property->type(), runtime.property_type());
    EXPECT_EQ(cxxpython::object_repr(property), "<property getter=set, setter=set, deleter=none>");
    EXPECT_EQ(cxxpython::object_get_attr(owner_type, "field"), property);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "field"), computed);

    cxxpython::object_set_attr(instance, "field", value);
    EXPECT_EQ(last_set, value);
    EXPECT_EQ(instance->instance_dict(), nullptr);

    std::vector<cxxpython::Object *> children;
    property->visit_children([&children](cxxpython::Object *object)
                             { children.push_back(object); });
    ASSERT_EQ(children.size(), 2U);
    EXPECT_EQ(children[0], getter);
    EXPECT_EQ(children[1], setter);
}

TEST(Attributes, PropertyDeleterUsesDescriptorDeleteProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *deleted_self = nullptr;

    cxxpython::NativeFunctionObject *getter = runtime.new_native_function(
        "get_field",
        [](cxxpython::Runtime &runtime, const cxxpython::CallArguments &)
        {
            return runtime.new_str("computed");
        });
    cxxpython::NativeFunctionObject *deleter = runtime.new_native_function(
        "delete_field",
        [&deleted_self](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 1 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "delete_field() expected self");
            deleted_self = args.positional[0];
            return runtime.none();
        });
    cxxpython::PropertyObject *property = runtime.new_property(getter, nullptr, deleter);
    owner_type->dict->set(runtime.new_str("field"), property);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    cxxpython::object_delete_attr(instance, "field");
    EXPECT_EQ(deleted_self, instance);

    cxxpython::PropertyObject *read_only = runtime.new_property(getter);
    owner_type->dict->set(runtime.new_str("read_only"), read_only);
    try
    {
        cxxpython::object_delete_attr(instance, "read_only");
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "can't delete attribute");
    }
}

TEST(Attributes, ReadOnlyPropertyRejectsAssignment)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::NativeFunctionObject *getter = runtime.new_native_function(
        "get_field",
        [](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            (void)args;
            return runtime.new_str("computed");
        });
    cxxpython::PropertyObject *property = runtime.new_property(getter);
    owner_type->dict->set(runtime.new_str("field"), property);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    try
    {
        cxxpython::object_set_attr(instance, "field", runtime.new_str("new-value"));
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.attribute_error_type());
        EXPECT_STREQ(error.what(), "can't set attribute");
        EXPECT_EQ(instance->instance_dict(), nullptr);
    }
}

TEST(Attributes, PropertyHelperMethodsCreateUpdatedDescriptors)
{
    cxxpython::Runtime runtime;
    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *second = runtime.new_str("second");
    cxxpython::Object *deleted = runtime.new_str("deleted");
    cxxpython::NativeFunctionObject *getter = runtime.new_native_function(
        "get_first",
        [first](cxxpython::Runtime &, const cxxpython::CallArguments &)
        {
            return first;
        });
    cxxpython::NativeFunctionObject *replacement_getter = runtime.new_native_function(
        "get_second",
        [second](cxxpython::Runtime &, const cxxpython::CallArguments &)
        {
            return second;
        });
    cxxpython::NativeFunctionObject *setter = runtime.new_native_function(
        "set_value",
        [](cxxpython::Runtime &runtime, const cxxpython::CallArguments &)
        {
            return runtime.none();
        });
    cxxpython::NativeFunctionObject *deleter = runtime.new_native_function(
        "delete_value",
        [deleted](cxxpython::Runtime &, const cxxpython::CallArguments &)
        {
            return deleted;
        });

    cxxpython::PropertyObject *property = runtime.new_property(getter);
    cxxpython::Object *getter_method = cxxpython::object_get_attr(property, "getter");
    cxxpython::Object *setter_method = cxxpython::object_get_attr(property, "setter");
    cxxpython::Object *deleter_method = cxxpython::object_get_attr(property, "deleter");
    ASSERT_NE(getter_method, nullptr);
    ASSERT_NE(setter_method, nullptr);
    ASSERT_NE(deleter_method, nullptr);
    EXPECT_EQ(getter_method->type(), runtime.native_function_type());
    EXPECT_EQ(setter_method->type(), runtime.native_function_type());
    EXPECT_EQ(deleter_method->type(), runtime.native_function_type());

    auto *with_getter = dynamic_cast<cxxpython::PropertyObject *>(
        cxxpython::object_call(getter_method, {replacement_getter}));
    ASSERT_NE(with_getter, nullptr);
    EXPECT_EQ(with_getter->getter(), replacement_getter);
    EXPECT_EQ(with_getter->setter(), nullptr);

    auto *with_setter = dynamic_cast<cxxpython::PropertyObject *>(
        cxxpython::object_call(setter_method, {setter}));
    ASSERT_NE(with_setter, nullptr);
    EXPECT_EQ(with_setter->getter(), getter);
    EXPECT_EQ(with_setter->setter(), setter);

    auto *with_deleter = dynamic_cast<cxxpython::PropertyObject *>(
        cxxpython::object_call(deleter_method, {deleter}));
    ASSERT_NE(with_deleter, nullptr);
    EXPECT_EQ(with_deleter->getter(), getter);
    EXPECT_EQ(with_deleter->deleter(), deleter);
}

TEST(Attributes, StaticMethodObjectsReturnWrappedCallable)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *result = runtime.new_str("static-result");
    cxxpython::NativeFunctionObject *callable = runtime.new_native_function(
        "utility",
        [result](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 1 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "utility() expected value");
            return result;
        });
    cxxpython::StaticMethodObject *descriptor = runtime.new_staticmethod(callable);
    owner_type->dict->set(runtime.new_str("utility"), descriptor);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    EXPECT_EQ(descriptor->type(), runtime.staticmethod_type());
    EXPECT_EQ(cxxpython::object_get_attr(owner_type, "utility"), callable);
    EXPECT_EQ(cxxpython::object_get_attr(instance, "utility"), callable);
    EXPECT_EQ(cxxpython::object_call(cxxpython::object_get_attr(instance, "utility"),
                                     {runtime.new_str("arg")}),
              result);

    std::vector<cxxpython::Object *> children;
    descriptor->visit_children([&children](cxxpython::Object *object)
                               { children.push_back(object); });
    ASSERT_EQ(children.size(), 1U);
    EXPECT_EQ(children[0], callable);
}

TEST(Attributes, ClassMethodObjectsBindOwnerType)
{
    cxxpython::Runtime runtime;
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    cxxpython::Object *result = runtime.new_str("class-result");
    cxxpython::Object *seen_self = nullptr;
    cxxpython::NativeFunctionObject *callable = runtime.new_native_function(
        "factory",
        [&seen_self, result](cxxpython::Runtime &runtime, const cxxpython::CallArguments &args)
        {
            if (args.positional.size() != 2 || (args.keywords && args.keywords->size() != 0))
                cxxpython::raise_type_error(&runtime, "factory() expected cls and value");
            seen_self = args.positional[0];
            return result;
        });
    cxxpython::ClassMethodObject *descriptor = runtime.new_classmethod(callable);
    owner_type->dict->set(runtime.new_str("factory"), descriptor);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    EXPECT_EQ(descriptor->type(), runtime.classmethod_type());
    auto *from_type = dynamic_cast<cxxpython::PythonMethodObject *>(cxxpython::object_get_attr(owner_type, "factory"));
    ASSERT_NE(from_type, nullptr);
    EXPECT_EQ(from_type->self(), owner_type);
    EXPECT_EQ(cxxpython::object_call(from_type, {runtime.new_str("arg")}), result);
    EXPECT_EQ(seen_self, owner_type);

    auto *from_instance =
        dynamic_cast<cxxpython::PythonMethodObject *>(cxxpython::object_get_attr(instance, "factory"));
    ASSERT_NE(from_instance, nullptr);
    EXPECT_EQ(from_instance->self(), owner_type);
    EXPECT_EQ(cxxpython::object_call(from_instance, {runtime.new_str("arg")}), result);
    EXPECT_EQ(seen_self, owner_type);

    std::vector<cxxpython::Object *> children;
    descriptor->visit_children([&children](cxxpython::Object *object)
                               { children.push_back(object); });
    ASSERT_EQ(children.size(), 1U);
    EXPECT_EQ(children[0], callable);
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
