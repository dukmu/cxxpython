#include "cxxpython/objects.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class CoreAttributes : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle text(std::u32string value)
    {
        ReturnSlot out(context);
        make_str(context, std::move(value), out);
        return roots.root(out.result().value());
    }
    Handle type(std::string name, std::initializer_list<Handle> bases = {})
    {
        ReturnSlot out(context);
        create_type(context, {std::move(name), bases}, out);
        return roots.root(out.result().value());
    }
    Handle instance(Handle cls)
    {
        ReturnSlot out(context);
        context.allocate_instance<InstanceObject>(cls, out);
        return roots.root(out.result().value());
    }
    Handle get(Handle target, std::u32string name)
    {
        ReturnSlot out(context);
        get_attribute(context, target, text(std::move(name)), out);
        if (out.result().tag() != ResultTag::Ok)
            throw std::logic_error("unexpected attribute error");
        return roots.root(out.result().value());
    }
    void set(Handle target, std::u32string name, Handle value)
    {
        ReturnSlot out(context);
        ASSERT_EQ(set_attribute(context, target, text(std::move(name)), value, out).tag(),
                  ResultTag::Ok);
    }
    void expect_error(ReturnSlot &out, CoreObject error)
    {
        ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(error));
        context.collect();
        EXPECT_NO_THROW(validate_exception(context, out.result().exception()));
    }
    static void identity(Attachment &, Value self, ReturnSlot &out)
    {
        out.ok(self);
    }
    static void collected_identity(Attachment &context, Value self, ReturnSlot &out)
    {
        context.collect();
        out.ok(self);
    }
};

TEST_F(CoreAttributes, ClassDescriptorUsesTheActualHeaderType)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    EXPECT_EQ(get(self, U"__class__").get(context), cls.get(context));
    EXPECT_EQ(get(cls, U"__class__").get(context), context.type_type());
    auto type_type = roots.root(context.type_type());
    EXPECT_EQ(get(type_type, U"__class__").get(context), context.type_type());
    EXPECT_EQ(get(text(U"value"), U"__class__").get(context), context.builtin(CoreObject::StrType));
    auto none = roots.root(context.builtin(CoreObject::None));
    EXPECT_EQ(get(none, U"__class__").get(context), context.builtin(CoreObject::NoneType));
}

TEST_F(CoreAttributes, EmptyPropertyIsDataDescriptorAndDocIsWritable)
{
    ReturnSlot out(context);
    auto property_type = roots.root(context.builtin(CoreObject::PropertyType));
    call(context, property_type, {{}, {}}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto property = roots.root(out.value());
    EXPECT_EQ(get(property, U"fget").get(context), context.builtin(CoreObject::None));
    auto doc = text(U"documentation");
    set(property, U"__doc__", doc);
    context.collect();
    EXPECT_EQ(get(property, U"__doc__").get(context), doc.get(context));
    auto cls = type("Owner");
    set(cls, U"value", property);
    EXPECT_EQ(get(cls, U"value").get(context), property.get(context));
    auto self = instance(cls);
    get_attribute(context, self, text(U"value"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    set_attribute(context, self, text(U"value"), doc, out);
    expect_error(out, CoreObject::AttributeErrorType);
    del_attribute(context, self, text(U"value"), out);
    expect_error(out, CoreObject::AttributeErrorType);
}

TEST_F(CoreAttributes, NativeDescriptorBindingDoesNotConfuseNoneWithAbsentReceiver)
{
    auto none = roots.root(context.builtin(CoreObject::None));
    auto getter = get(none, U"__getattribute__");
    EXPECT_EQ(context.payload<BoundMethodObject>(getter).self, none.get(context));
    auto name = text(U"__class__");
    std::array args{name.get(context)};
    ReturnSlot out(context);
    call(context, getter, {args, {}}, out);
    EXPECT_EQ(out.result().value(), context.builtin(CoreObject::NoneType));
}

TEST_F(CoreAttributes, NativeLayoutRetainsInheritedDictionaryInItsPlannedTail)
{
    auto mixin = type("Mixin");
    auto native = roots.root(context.builtin(CoreObject::StrType));
    std::array bases{mixin, native};
    ReturnSlot out(context);
    create_type(context, {"HasTailFields", bases}, out);
    auto cls = roots.root(out.result().value());
    context.allocate_instance<StrObject>(cls, out, U"native prefix");
    auto self = roots.root(out.result().value());
    auto value = text(U"inherited dictionary");
    set(self, U"value", value);
    EXPECT_EQ(get(self, U"value").get(context), value.get(context));
    EXPECT_EQ(detail::name_lookup(context, get(self, U"__dict__").get(context), U"value"),
              value.get(context));
    EXPECT_EQ(context.payload<StrObject>(self).value, U"native prefix");
    create_native_type(context, {{"KeepsFields", {&mixin, 1}}, LayoutDescriptor::of<StrObject>()},
                       out);
    EXPECT_TRUE(
        context.payload<TypeObject>(out.result().value()).instance_layout.fields.dictionary);
}

TEST_F(CoreAttributes, CompatibleClassAssignmentRetainsIdentityAndDictionary)
{
    auto a = type("A");
    auto b = type("B");
    auto self = instance(a);
    auto value = text(U"retained");
    set(self, U"value", value);
    auto dictionary = get(self, U"__dict__");
    auto before = self.get(context);
    set(self, U"__class__", b);
    EXPECT_EQ(self.get(context), before);
    EXPECT_EQ(context.type_of(self), b.get(context));
    EXPECT_EQ(get(self, U"__dict__").get(context), dictionary.get(context));
    EXPECT_EQ(get(self, U"value").get(context), value.get(context));
    context.collect();
    EXPECT_EQ(get(self, U"__class__").get(context), b.get(context));
}

TEST_F(CoreAttributes, IncompatibleAndImmutableClassChangesAreTypeErrors)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    auto name = text(U"__class__");
    ReturnSlot out(context);
    auto wrong = text(U"not a type");
    set_attribute(context, self, name, wrong, out);
    expect_error(out, CoreObject::TypeErrorType);
    auto object = roots.root(context.object_type());
    set_attribute(context, self, name, object, out);
    expect_error(out, CoreObject::TypeErrorType);
    auto type_type = roots.root(context.type_type());
    auto meta = type("Meta", {type_type});
    set_attribute(context, self, name, meta, out);
    expect_error(out, CoreObject::TypeErrorType);
    set_attribute(context, wrong, name, cls, out);
    expect_error(out, CoreObject::TypeErrorType);
    del_attribute(context, self, name, out);
    expect_error(out, CoreObject::TypeErrorType);
    EXPECT_EQ(context.type_of(self), cls.get(context));
}

TEST_F(CoreAttributes, InstanceDictionaryIsLiveReplaceableAndDeletable)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    auto key = text(U"value");
    auto first = text(U"first");
    auto second = text(U"second");
    auto dictionary = get(self, U"__dict__");
    EXPECT_TRUE(DictView(context, dictionary.get(context)).entries().empty());
    set(self, U"value", first);
    EXPECT_EQ(detail::name_lookup(context, dictionary.get(context), U"value"), first.get(context));
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    auto replacement = roots.root(out.result().value());
    detail::name_store(context, replacement.get(context), key.get(context), second.get(context));
    set(self, U"__dict__", replacement);
    EXPECT_EQ(get(self, U"value").get(context), second.get(context));
    EXPECT_EQ(detail::name_lookup(context, dictionary.get(context), U"value"), first.get(context));
    auto name = text(U"__dict__");
    del_attribute(context, self, name, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    auto fresh = get(self, U"__dict__");
    EXPECT_NE(fresh.get(context), replacement.get(context));
    EXPECT_TRUE(DictView(context, fresh.get(context)).entries().empty());
    EXPECT_EQ(detail::name_lookup(context, replacement.get(context), U"value"),
              second.get(context));
    set_attribute(context, self, name, first, out);
    expect_error(out, CoreObject::TypeErrorType);
    EXPECT_EQ(get(self, U"__dict__").get(context), fresh.get(context));
}

TEST_F(CoreAttributes, PlainObjectDoesNotAcquireAnInstanceDictionary)
{
    auto self = instance(roots.root(context.object_type()));
    ReturnSlot out(context);
    get_attribute(context, self, text(U"__dict__"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    set_attribute(context, self, text(U"value"), text(U"no storage"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    EXPECT_EQ(context.instance_dictionary(self.get(context)), nullptr);
}

TEST_F(CoreAttributes, InheritedDictionaryDescriptorDoesNotAddAnotherAuthority)
{
    auto base = type("Base");
    auto cls = type("Child", {base});
    auto self = instance(cls);
    auto own = TypeView(context, cls).dictionary();
    EXPECT_FALSE(detail::name_lookup(context, *own, U"__dict__"));
    auto descriptor = get(base, U"__dict__");
    EXPECT_EQ(context.type_of(descriptor), context.builtin(CoreObject::MappingProxyType));
    auto value = text(U"child value");
    set(self, U"value", value);
    EXPECT_EQ(detail::name_lookup(context, get(self, U"__dict__").get(context), U"value"),
              value.get(context));
}

TEST_F(CoreAttributes, TypeDictionaryIsALiveReadOnlyProxy)
{
    auto cls = type("Owner");
    auto proxy = get(cls, U"__dict__");
    ASSERT_EQ(context.type_of(proxy), context.builtin(CoreObject::MappingProxyType));
    const auto mapping = context.payload<MappingProxyObject>(proxy).mapping;
    EXPECT_EQ(mapping, *TypeView(context, cls).dictionary());
    auto value = text(U"visible through existing proxy");
    set(cls, U"value", value);
    context.collect();
    EXPECT_EQ(detail::name_lookup(context, mapping, U"value"), value.get(context));
    ReturnSlot out(context);
    auto name = text(U"__dict__");
    set_attribute(context, cls, name, get(instance(cls), U"__dict__"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    del_attribute(context, cls, name, out);
    expect_error(out, CoreObject::AttributeErrorType);
    set_attribute(context, proxy, text(U"value"), value, out);
    expect_error(out, CoreObject::AttributeErrorType);
    EXPECT_EQ(context.payload<MappingProxyObject>(proxy).mapping, mapping);
}

TEST_F(CoreAttributes, BoundMethodReflectionReturnsTracedCallableAndReceiver)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    SignatureLayout signature;
    signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    ReturnSlot out(context);
    make_native_function(
        context, "method", signature,
        [](Attachment &, Value, std::span<const Value> args, ReturnSlot &result) {
            result.ok(args[0]);
        },
        {}, out);
    auto fn = roots.root(out.result().value());
    set(cls, U"method", fn);
    auto method = get(self, U"method");
    EXPECT_EQ(get(method, U"__func__").get(context), fn.get(context));
    EXPECT_EQ(get(method, U"__self__").get(context), self.get(context));
    set_attribute(context, method, text(U"__func__"), self, out);
    expect_error(out, CoreObject::AttributeErrorType);
    del_attribute(context, method, text(U"__self__"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    context.collect();
    call(context, method, {}, out);
    EXPECT_EQ(out.result().value(), self.get(context));
}

TEST_F(CoreAttributes, GetSetDescriptorUsesGenericBindingAndChecksItsOwner)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    ReturnSlot out(context);
    auto descriptor_type = roots.root(context.builtin(CoreObject::GetSetDescriptorType));
    context.allocate_instance<GetSetDescriptorObject>(descriptor_type, out, cls.get(context),
                                                      text(U"field").get(context),
                                                      collected_identity, nullptr);
    auto descriptor = roots.root(out.result().value());
    set(cls, U"field", descriptor);
    EXPECT_EQ(get(cls, U"field").get(context), descriptor.get(context));
    EXPECT_EQ(get(self, U"field").get(context), self.get(context));
    auto getter = get(descriptor, U"__get__");
    auto wrong = text(U"wrong receiver");
    std::array args{wrong.get(context), cls.get(context)};
    call(context, getter, {args, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
    std::array absent{context.builtin(CoreObject::None), context.builtin(CoreObject::None)};
    call(context, getter, {absent, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
    set_attribute(context, self, text(U"field"), wrong, out);
    expect_error(out, CoreObject::AttributeErrorType);
}

TEST_F(CoreAttributes, GetSetDescriptorTracesOwnerWithoutDependingOnSweepOrder)
{
    ReturnSlot out(context);
    {
        RootScope local(context);
        create_type(context, {"TemporaryOwner"}, out);
        auto cls = local.root(out.result().value());
        auto descriptor_type = local.root(context.builtin(CoreObject::GetSetDescriptorType));
        ReturnSlot name(context);
        make_str(context, U"temporary_field", name);
        context.allocate_instance<GetSetDescriptorObject>(descriptor_type, out, cls.get(context),
                                                          name.value(), identity, nullptr);
    }
    context.collect();
    const auto owner = context.payload<GetSetDescriptorObject>(out.result().value()).owner;
    EXPECT_EQ(
        context
            .payload<StrObject>(context.payload<GetSetDescriptorObject>(out.result().value()).name)
            .value,
        U"temporary_field");
    EXPECT_EQ(context.payload<StrObject>(std::get<Value>(TypeView(context, owner).name())).value,
              U"TemporaryOwner");
    out.reset();
    context.collect();
}

TEST_F(CoreAttributes, GetterCanRemoveItsOwnDescriptorBeforeCollecting)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    {
        ReturnSlot temporary(context);
        auto callback = [](Attachment &c, Value receiver, ReturnSlot &out) {
            auto dictionary = *TypeView(c, c.type_of(receiver)).dictionary();
            auto field = *detail::name_lookup(c, dictionary, U"field");
            detail::name_erase(c, dictionary, U"field");
            c.collect();
            (void)c.payload<GetSetDescriptorObject>(field);
            out.ok(receiver);
        };
        auto key = text(U"field");
        context.allocate_instance<GetSetDescriptorObject>(
            roots.root(context.builtin(CoreObject::GetSetDescriptorType)), temporary,
            cls.get(context), key.get(context), callback, nullptr);
        detail::name_store(context, *TypeView(context, cls).dictionary(), key.get(context),
                           temporary.result().value());
    }
    EXPECT_EQ(get(self, U"field").get(context), self.get(context));
    EXPECT_FALSE(detail::name_lookup(context, *TypeView(context, cls).dictionary(), U"field"));
}

TEST_F(CoreAttributes, DefaultWrappersAreCallableAndHooksCanDelegate)
{
    auto cls = type("Owner");
    auto self = instance(cls);
    auto value = text(U"delegated");
    auto object = roots.root(context.object_type());
    auto get_default = get(object, U"__getattribute__");
    auto set_default = get(object, U"__setattr__");
    auto del_default = get(object, U"__delattr__");
    set(cls, U"__getattribute__", get_default);
    set(cls, U"__setattr__", set_default);
    set(cls, U"__delattr__", del_default);
    set(self, U"value", value);
    EXPECT_EQ(get(self, U"value").get(context), value.get(context));
    ReturnSlot out(context);
    del_attribute(context, self, text(U"value"), out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    get_attribute(context, self, text(U"value"), out);
    expect_error(out, CoreObject::AttributeErrorType);
    std::array wrong{self.get(context), context.builtin(CoreObject::None)};
    call(context, get_default, {wrong, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreAttributes, ExplicitObjectAndTypeLookupHaveDistinctClassSemantics)
{
    auto base = type("Base");
    auto cls = type("Child", {base});
    auto self = instance(cls);
    auto value = text(U"base attribute");
    set(base, U"inherited", value);
    auto object = roots.root(context.object_type());
    auto type_type = roots.root(context.type_type());
    auto object_get = get(object, U"__getattribute__");
    auto type_get = get(type_type, U"__getattribute__");
    auto name = text(U"inherited");
    ReturnSlot out(context);
    std::array args{cls.get(context), name.get(context)};
    call(context, object_get, {args, {}}, out);
    expect_error(out, CoreObject::AttributeErrorType);
    call(context, type_get, {args, {}}, out);
    EXPECT_EQ(out.result().value(), value.get(context));
    set(cls, U"inherited", value);
    call(context, object_get, {args, {}}, out);
    EXPECT_EQ(out.result().value(), value.get(context));
    std::array wrong{self.get(context), name.get(context)};
    call(context, type_get, {wrong, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
}

TEST_F(CoreAttributes, ObjectMutationWrapperCannotBypassTypeMutationRules)
{
    auto cls = type("Owner");
    auto value = text(U"value");
    auto name = text(U"field");
    auto object = roots.root(context.object_type());
    auto type_type = roots.root(context.type_type());
    auto object_set = get(object, U"__setattr__");
    auto type_set = get(type_type, U"__setattr__");
    ReturnSlot out(context);
    std::array args{cls.get(context), name.get(context), value.get(context)};
    call(context, object_set, {args, {}}, out);
    expect_error(out, CoreObject::TypeErrorType);
    call(context, type_set, {args, {}}, out);
    ASSERT_EQ(out.result().tag(), ResultTag::Ok);
    EXPECT_EQ(get(cls, U"field").get(context), value.get(context));
}
} // namespace
