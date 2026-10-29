#include "cxxpython/core/objects.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using namespace cxxpython::core;
    using Kind = ParameterKind;

    class CoreCalls : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true, .max_call_depth = 24}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle type(std::string name, std::initializer_list<Handle> bases = {})
        { ReturnSlot out(context); create_type(context, {std::move(name), bases}, out); return roots.root(out.result().value()); }
        Handle instance(Handle cls)
        { ReturnSlot out(context); context.allocate_instance<InstanceObject>(cls, out); return roots.root(out.result().value()); }
        Handle function(SignatureLayout signature, NativeEntry entry, std::initializer_list<Value> captures = {})
        {
            ReturnSlot out(context);
            make_native_function(context, "test function", std::move(signature), entry, captures, out);
            return roots.root(out.result().value());
        }
        void set(Handle object, std::u32string name, Handle value)
        {
            ReturnSlot out(context);
            auto key = text(std::move(name));
            ASSERT_EQ(set_attribute(context, object, key, value, out).tag(), ResultTag::Ok);
        }
        void expect_error(ReturnSlot &out, CoreObject error)
        {
            ASSERT_EQ(out.result().tag(), ResultTag::Raised);
            EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(error));
            ReturnSlot saved(context); context.load(out.result().exception(), context.payload<ExceptionObject>(out.result().exception()).args, saved);
            auto &args = context.payload<TupleObject>(saved.result().value());
            ASSERT_EQ(args.items.size(), 1U);
            EXPECT_TRUE(context.has_layout<StrObject>(args.items[0]));
            context.collect();
            EXPECT_NO_THROW(validate_exception(context, out.result().exception()));
        }
        static void return_first(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
        { out.ok(args[0]); }
        static void return_all(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
        { cxxpython::core::make_tuple(c, args, out); }
        static void captured(Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out)
        { c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]); }
        static void attribute_error(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
        { raise_error(c, CoreObject::AttributeErrorType, U"getter failed", out); }
        static void type_error(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
        { raise_error(c, CoreObject::TypeErrorType, U"getter failed", out); }
        static void recurse(Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out)
        {
            RootScope scope(c);
            auto self = scope.root(c.payload<NativeFunctionObject>(fn).captures[0]);
            call(c, self, {}, out);
        }
    };

    TEST(CoreBootstrap, FailureAtEveryManagedAllocationRollsBackAndRetries)
    {
        // Determine the production bootstrap's allocation count without freezing
        // an incidental number into its API.
        Interpreter reference;
        Attachment baseline(reference);
        baseline.bootstrap_core();
        const auto allocations = baseline.stats().objects - 2;
        for (std::size_t limit = 0; limit < allocations; ++limit)
        {
            Interpreter interpreter;
            Attachment context(interpreter);
            context.bootstrap();
            context.set_allocation_limit(limit);
            EXPECT_THROW(context.bootstrap_core(), std::bad_alloc) << limit;
            EXPECT_FALSE(context.core_ready());
            EXPECT_EQ(context.stats().objects, 2U) << limit;
            EXPECT_EQ(context.stats().roots, 0U);
            EXPECT_FALSE(context.payload<TypeObject>(context.object_type()).dictionary);
            EXPECT_TRUE(std::holds_alternative<std::u32string>(context.payload<TypeObject>(context.object_type()).name));
            EXPECT_TRUE(std::holds_alternative<std::u32string>(context.payload<TypeObject>(context.type_type()).qualname));
            context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            EXPECT_NO_THROW(context.bootstrap_core());
            EXPECT_TRUE(context.core_ready());
        }
    }

    TEST_F(CoreCalls, NativeReturnAndExceptionUseCallerRoots)
    {
        auto value = text(U"survives GC");
        auto fn = function({{{U"value"}}, {}, {}}, return_first);
        ReturnSlot out(context);
        std::array args{value.get(context)};
        call(context, fn, {args, {}}, out);
        EXPECT_EQ(out.result().value(), value.get(context));
        auto view = out.result();
        call(context, fn, {}, out);
        EXPECT_THROW(view.value(), std::logic_error);
        expect_error(out, CoreObject::TypeErrorType);
        EXPECT_THROW(out.raised(context.builtin(CoreObject::None)), std::logic_error);
    }

    TEST_F(CoreCalls, CompletePositionalNativeCallsRetainArgumentsThroughCollection)
    {
        auto cls = type("Receiver"); auto self = instance(cls);
        auto key = text(U"method"); auto value = text(U"argument"); ReturnSlot out(context);
        for (std::size_t count : {0U, 1U, 4U, 5U})
        {
            SignatureLayout signature;
            for (std::size_t i = 0; i < count; ++i)
                signature.parameters.push_back({U"arg" + std::u32string(i + 1, U'x'), Kind::PositionalOnly});
            auto fn = function(signature,
                [](Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out) {
                    c.collect(); cxxpython::core::make_tuple(c, args, out);
                });
            std::vector<Value> args(count, value.get(context));
            call(context, fn, {args, {}}, out);
            ASSERT_EQ(out.result().tag(), ResultTag::Ok);
            EXPECT_EQ(context.payload<TupleObject>(out.result().value()).items, args);
            if (!count) continue;
            set_attribute(context, cls, key, fn, out); get_attribute(context, self, key, out);
            auto bound = roots.root(out.result().value()); args.erase(args.begin());
            call(context, bound, {args, {}}, out);
            ASSERT_EQ(out.result().tag(), ResultTag::Ok);
            args.insert(args.begin(), self.get(context));
            EXPECT_EQ(context.payload<TupleObject>(out.result().value()).items, args);
        }
    }

    TEST_F(CoreCalls, BinderHandlesAllParameterKindsAndDefaults)
    {
        auto a = text(U"a"); auto b = text(U"b"); auto c = text(U"c");
        SignatureLayout signature;
        signature.parameters = {{U"a", Kind::PositionalOnly},
                                {U"b", Kind::PositionalOrKeyword, b.get(context)},
                                {U"c", Kind::KeywordOnly}};
        auto fn = function(signature, return_all);
        auto c_name = text(U"c");
        std::array args{a.get(context)};
        std::array keywords{KeywordArg{c_name.get(context), c.get(context)}};
        ReturnSlot out(context);
        call(context, fn, {args, keywords}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<TupleObject>(out.result().value()).items,
                  (std::vector<Value>{a.get(context), b.get(context), c.get(context)}));
        auto a_name = text(U"a");
        std::array wrong{KeywordArg{a_name.get(context), a.get(context)}, keywords[0]};
        call(context, fn, {{}, wrong}, out);
        expect_error(out, CoreObject::TypeErrorType);
        std::array duplicate{keywords[0], keywords[0]};
        call(context, fn, {args, duplicate}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, VariadicBindingPreservesKeywordOrderAndPositionalOnlyNames)
    {
        auto value = text(U"value");
        SignatureLayout signature;
        signature.parameters = {{U"x", Kind::PositionalOnly}};
        signature.varargs = U"args"; signature.varkwargs = U"kwargs";
        auto fn = function(signature, return_all);
        auto x = text(U"x"); auto z = text(U"z"); auto a = text(U"a");
        std::array args{value.get(context), value.get(context)};
        std::array keywords{KeywordArg{z.get(context), value.get(context)},
                            KeywordArg{a.get(context), value.get(context)},
                            KeywordArg{x.get(context), value.get(context)}};
        ReturnSlot out(context);
        call(context, fn, {args, keywords}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto &bound = context.payload<TupleObject>(out.result().value()).items;
        ASSERT_EQ(bound.size(), 3U);
        EXPECT_EQ(context.payload<TupleObject>(bound[1]).items, (std::vector<Value>{value.get(context)}));
        DictView dictionary(context, bound[2]);
        EXPECT_EQ(dictionary.entries().size(), 3U);
        std::vector<Value> keys;
        for (const auto &entry : dictionary.entries()) keys.push_back(entry.key);
        EXPECT_EQ(keys, (std::vector<Value>{z.get(context), a.get(context), x.get(context)}));
        EXPECT_EQ(detail::name_lookup(context, bound[2], U"x"), value.get(context));
    }

    TEST_F(CoreCalls, InstanceLookupBindsFunctionsAndClassLookupDoesNot)
    {
        auto cls = type("Receiver"); auto self = instance(cls);
        auto fn = function({{{U"self", Kind::PositionalOnly}}, {}, {}}, return_first);
        set(cls, U"method", fn);
        auto name = text(U"method");
        ReturnSlot out(context);
        get_attribute(context, cls, name, out);
        EXPECT_EQ(out.result().value(), fn.get(context));
        get_attribute(context, self, name, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto method = roots.root(out.result().value());
        EXPECT_EQ(context.payload<BoundMethodObject>(method).self, self.get(context));
        call(context, method, {}, out);
        EXPECT_EQ(out.result().value(), self.get(context));
        set(self, U"method", text(U"shadowed"));
        get_attribute(context, self, name, out);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"shadowed");
    }

    TEST_F(CoreCalls, NativeMethodDescriptorsValidateBoundAndExplicitReceivers)
    {
        auto cls = type("Owner"); auto self = instance(cls); auto wrong = text(U"wrong");
        auto fn = function({{{U"self", Kind::PositionalOnly}}, {}, {}}, return_first);
        ReturnSlot out(context);
        make_method_descriptor(context, cls, fn, out);
        auto descriptor = roots.root(out.result().value());
        set(cls, U"method", descriptor);
        auto name = text(U"method");
        get_attribute(context, self, name, out);
        auto method = roots.root(out.result().value());
        call(context, method, {}, out);
        EXPECT_EQ(out.result().value(), self.get(context));
        std::array args{wrong.get(context)};
        call(context, descriptor, {args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, UserDescriptorUsesItsOwnTypeAndDataDescriptorWins)
    {
        auto cls = type("Owner"); auto self = instance(cls);
        auto descriptor_type = type("Descriptor"); auto descriptor = instance(descriptor_type);
        auto expected = text(U"descriptor result");
        SignatureLayout get;
        get.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
        set(descriptor_type, U"__get__", function(get, captured, {expected.get(context)}));
        set(cls, U"value", descriptor);
        set(self, U"value", text(U"instance result"));
        auto name = text(U"value");
        ReturnSlot out(context);
        get_attribute(context, self, name, out);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"instance result");
        SignatureLayout setter;
        setter.parameters = {{U"self"}, {U"instance"}, {U"value"}};
        set(descriptor_type, U"__set__", function(setter, return_first));
        get_attribute(context, self, name, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
        del_attribute(context, self, name, out);
        expect_error(out, CoreObject::AttributeErrorType);
    }

    TEST_F(CoreCalls, AttributeFallbackAndBaseMutationRemainDynamic)
    {
        auto base = type("Base"); auto cls = type("Child", {base}); auto self = instance(cls);
        auto expected = text(U"fallback");
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"name"}};
        set(base, U"__getattr__", function(signature, captured, {expected.get(context)}));
        auto missing = text(U"missing"); ReturnSlot out(context);
        get_attribute(context, self, missing, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
        auto before = context.type_epoch();
        auto replacement = text(U"base value"); set(base, U"missing", replacement);
        EXPECT_GT(context.type_epoch(), before);
        get_attribute(context, self, missing, out);
        EXPECT_EQ(out.result().value(), replacement.get(context));
        auto object_type = roots.root(context.object_type());
        set_attribute(context, object_type, missing, replacement, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, ExplicitCallWrapperUsesTheSameBinderAndImplementation)
    {
        auto value = text(U"result"); auto fn = function({{{U"value"}}, {}, {}}, return_first);
        auto name = text(U"__call__"); ReturnSlot out(context);
        get_attribute(context, fn, name, out);
        auto wrapper = roots.root(out.result().value());
        std::array args{value.get(context)};
        call(context, wrapper, {args, {}}, out);
        EXPECT_EQ(out.result().value(), value.get(context));
    }

    TEST_F(CoreCalls, RootedCapturesAndAliasedOutputSurviveCollection)
    {
        ReturnSlot out(context);
        make_str(context, U"input only held by output", out);
        const auto value = out.result().value();
        auto fn = function({{{U"value"}}, {}, {}}, return_all);
        std::array args{value};
        call(context, fn, {args, {}}, out);
        EXPECT_EQ(context.payload<TupleObject>(out.result().value()).items[0], value);
        context.collect();
        EXPECT_EQ(context.payload<StrObject>(context.payload<TupleObject>(out.result().value()).items[0]).value,
                  U"input only held by output");
    }

    TEST_F(CoreCalls, OnlyAttributeErrorTriggersGetattrFallback)
    {
        auto cls = type("Hooks"); auto self = instance(cls); auto expected = text(U"fallback");
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"name"}};
        set(cls, U"__getattribute__", function(signature, attribute_error));
        set(cls, U"__getattr__", function(signature, captured, {expected.get(context)}));
        ReturnSlot out(context); auto name = text(U"anything");
        get_attribute(context, self, name, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
        set(cls, U"__getattribute__", function(signature, type_error));
        get_attribute(context, self, name, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, GenericCallUsesTheTypeAndBoundsNativeReentry)
    {
        auto cls = type("Callable"); auto self = instance(cls);
        auto fn = function({{{U"self"}}, {}, {}}, recurse, {self.get(context)});
        set(cls, U"__call__", fn);
        ReturnSlot out(context);
        call(context, self, {}, out);
        expect_error(out, CoreObject::RecursionErrorType);
        set(cls, U"__call__", function({{{U"self"}}, {}, {}}, return_first));
        call(context, self, {}, out);
        EXPECT_EQ(out.result().value(), self.get(context));
    }

    TEST_F(CoreCalls, FunctionIsTheOnlyOwnerOfDefaultsAndCaptures)
    {
        Handle fn;
        {
            RootScope local(context);
            ReturnSlot temporary(context);
            make_str(context, U"owned only by function", temporary);
            auto capture = temporary.result().value();
            SignatureLayout signature; signature.parameters = {{U"value", Kind::PositionalOrKeyword, capture}};
            make_native_function(context, "owner", signature, return_first, {}, temporary);
            fn = roots.root(temporary.result().value());
        }
        context.collect();
        ReturnSlot out(context);
        call(context, fn, {}, out);
        EXPECT_EQ(context.payload<StrObject>(out.result().value()).value, U"owned only by function");
    }

    TEST_F(CoreCalls, TemporaryMethodGraphsReturnToAFixedLiveSet)
    {
        context.collect();
        auto baseline = context.stats().objects;
        for (int i = 0; i < 15; ++i)
        {
            {
                RootScope local(context);
                ReturnSlot temporary(context);
                create_type(context, {"Temporary"}, temporary);
                auto cls = local.root(temporary.result().value());
                SignatureLayout signature; signature.parameters = {{U"self"}};
                make_native_function(context, "method", signature, return_first, {}, temporary);
                auto fn = local.root(temporary.result().value());
                make_str(context, U"method", temporary);
                auto key = local.root(temporary.result().value());
                set_attribute(context, cls, key, fn, temporary);
                context.allocate_instance<InstanceObject>(cls, temporary);
                auto self = local.root(temporary.result().value());
                get_attribute(context, self, key, temporary);
                auto method = local.root(temporary.result().value());
                call(context, method, {}, temporary);
                EXPECT_EQ(temporary.result().value(), self.get(context));
            }
            context.collect();
            EXPECT_EQ(context.stats().objects, baseline);
        }
    }

    TEST_F(CoreCalls, AllocationFailureUsesRootedMemoryErrorReserve)
    {
        auto fn = function({{{U"value"}}, {}, {}}, return_first);
        context.set_allocation_limit(0);
        ReturnSlot out(context);
        call(context, fn, {}, out);
        EXPECT_EQ(out.result().tag(), ResultTag::Raised);
        EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
        EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(CoreObject::MemoryErrorType));
    }

    TEST_F(CoreCalls, UnicodeNamesUseCodePointsAndCanContainSurrogates)
    {
        auto cls = type("Unicode"); auto self = instance(cls);
        auto name = text(U"中文😀"); auto value = text(std::u32string{char32_t{0xd800}});
        ReturnSlot out(context);
        set_attribute(context, self, name, value, out);
        get_attribute(context, self, name, out);
        EXPECT_EQ(out.result().value(), value.get(context));
        EXPECT_EQ(context.payload<StrObject>(name).value.size(), 3U);
        del_attribute(context, self, name, out);
        get_attribute(context, self, name, out);
        expect_error(out, CoreObject::AttributeErrorType);
    }

    TEST_F(CoreCalls, AttributeReadDoesNotRequireItsValueToBeCallable)
    {
        auto cls = type("Values"); auto self = instance(cls);
        auto key = text(U"answer"); auto value = text(U"plain value");
        ReturnSlot out(context);
        set_attribute(context, self, key, value, out);
        get_attribute(context, self, key, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        call(context, roots.root(out.result().value()), {}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, LayoutAloneDoesNotBypassAnIndependentTypesCallProtocol)
    {
        ReturnSlot out(context);
        create_native_type(context, {{"IndependentFunction"}, LayoutDescriptor::of<NativeFunctionObject>()}, out);
        auto independent = roots.root(out.result().value());
        auto expected = text(U"dynamic __call__");
        set(independent, U"__call__", function({{{U"self"}}, {}, {}}, captured, {expected.get(context)}));
        context.allocate_instance<NativeFunctionObject>(independent, out, "not builtin", SignatureLayout{},
                                                         return_all, std::vector<Value>{});
        auto self = roots.root(out.result().value());
        call(context, self, {}, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
        EXPECT_THROW(make_method_descriptor(context, independent, self, out), std::invalid_argument);
        auto builtin_function = roots.root(context.builtin(CoreObject::NativeFunctionType));
        auto name = text(U"__get__");
        get_attribute(context, builtin_function, name, out);
        auto getter = roots.root(out.result().value());
        std::array args{self.get(context), context.builtin(CoreObject::None), independent.get(context)};
        call(context, getter, {args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, NativeHostFaultsAreNotDisguisedAsPythonExceptions)
    {
        auto fn = function({}, [](Attachment &, Handle, std::span<const Value>, ReturnSlot &) {
            throw std::runtime_error("host fault");
        });
        ReturnSlot out(context);
        EXPECT_THROW(call(context, fn, {}, out), std::runtime_error);
        EXPECT_EQ(out.result().tag(), ResultTag::Empty);
        auto missing = function({}, [](Attachment &, Handle, std::span<const Value>, ReturnSlot &) {});
        EXPECT_THROW(call(context, missing, {}, out), std::logic_error);
    }

    TEST_F(CoreCalls, MetaclassDataDescriptorWinsOverClassDictionary)
    {
        auto type_type = roots.root(context.type_type());
        auto meta = type("Meta", {type_type});
        ReturnSlot out(context);
        create_type(context, {"Class", {}, meta}, out);
        auto cls = roots.root(out.result().value());
        set(cls, U"value", text(U"class dictionary"));
        auto descriptor_type = type("Descriptor"); auto descriptor = instance(descriptor_type);
        SignatureLayout signature; signature.parameters = {{U"self"}, {U"instance"}, {U"owner"}};
        set(descriptor_type, U"__get__", function(signature, return_all));
        set(descriptor_type, U"__set__", function(signature, return_first));
        set(meta, U"value", descriptor);
        auto name = text(U"value");
        get_attribute(context, cls, name, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.payload<TupleObject>(out.result().value()).items,
                  (std::vector<Value>{descriptor.get(context), cls.get(context), meta.get(context)}));
    }

    TEST_F(CoreCalls, InvalidKeywordKindsAndDuplicateBindingsAreTypeErrors)
    {
        auto fn = function({{{U"value"}}, {}, {}}, return_first);
        auto value = text(U"value");
        std::array args{value.get(context)};
        ReturnSlot out(context);
        std::array keywords{KeywordArg{value.get(context), value.get(context)}};
        call(context, fn, {args, keywords}, out);
        expect_error(out, CoreObject::TypeErrorType);
        std::array bad_name{KeywordArg{context.builtin(CoreObject::None), value.get(context)}};
        call(context, fn, {{}, bad_name}, out);
        expect_error(out, CoreObject::TypeErrorType);
        std::array extra{value.get(context), value.get(context)};
        call(context, fn, {extra, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreCalls, ForeignOutputsAndArgumentsAreRejectedBeforeNamespaceMutation)
    {
        auto cls = type("Local"); auto self = instance(cls); auto name = text(U"value");
        auto fn = function({}, captured, {self.get(context)});
        Interpreter other;
        Attachment foreign(other);
        foreign.bootstrap_core();
        RootScope foreign_roots(foreign);
        auto value = foreign_roots.root(foreign.builtin(CoreObject::None));
        ReturnSlot foreign_out(foreign);
        foreign_out.ok(foreign.builtin(CoreObject::None));
        EXPECT_THROW(call(context, fn, {}, foreign_out), std::logic_error);
        EXPECT_EQ(foreign_out.result().value(), foreign.builtin(CoreObject::None));
        ReturnSlot out(context);
        EXPECT_THROW(set_attribute(context, self, name, value, out), std::logic_error);
        ASSERT_NE(context.instance_dictionary(self.get(context)), nullptr);
        ReturnSlot dictionary(context); context.load(self.get(context), *context.instance_dictionary(self.get(context)), dictionary);
        EXPECT_EQ(dictionary.result().tag(), ResultTag::Empty);
    }

    TEST_F(CoreCalls, NativeStringLayoutDoesNotMakeAnUnrelatedTypeAStringName)
    {
        ReturnSlot out(context);
        create_native_type(context, {{"NotStr"}, LayoutDescriptor::of<StrObject>()}, out);
        auto not_str = roots.root(out.result().value());
        context.allocate_instance<StrObject>(not_str, out, std::u32string(U"value"));
        auto fake = roots.root(out.result().value());
        auto cls = type("Owner"); auto self = instance(cls);
        get_attribute(context, self, fake, out);
        expect_error(out, CoreObject::TypeErrorType);
        auto fn = function({{{U"value"}}, {}, {}}, return_first);
        std::array keywords{KeywordArg{fake.get(context), context.builtin(CoreObject::None)}};
        call(context, fn, {{}, keywords}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }
}
