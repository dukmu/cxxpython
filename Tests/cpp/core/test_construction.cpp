#include "cxxpython/core/objects.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace cxxpython::core;
    using Kind = ParameterKind;
    class CoreConstruction : public testing::Test
    {
    protected:
        Interpreter interpreter{{.collect_every_allocation = true}};
        Attachment context{interpreter};
        RootScope roots{context};
        void SetUp() override { context.bootstrap_core(); }
        Handle text(std::u32string value)
        { ReturnSlot out(context); make_str(context, std::move(value), out); return roots.root(out.result().value()); }
        Handle type(std::string name, std::initializer_list<Handle> bases = {})
        { ReturnSlot out(context); create_type(context, {std::move(name), bases}, out); return roots.root(out.result().value()); }
        Handle tuple(std::initializer_list<Value> values = {})
        { ReturnSlot out(context); cxxpython::core::make_tuple(context, values, out); return roots.root(out.result().value()); }
        Handle dictionary()
        {
            ReturnSlot out(context);
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
            return roots.root(out.result().value());
        }
        Handle function(SignatureLayout signature, NativeEntry entry, std::initializer_list<Value> captures = {})
        {
            ReturnSlot out(context);
            make_native_function(context, "construction test", std::move(signature), entry, captures, out);
            return roots.root(out.result().value());
        }
        Handle get(Handle object, std::u32string name)
        {
            ReturnSlot out(context);
            get_attribute(context, object, text(std::move(name)), out);
            if (out.result().tag() != ResultTag::Ok) throw std::logic_error("unexpected attribute error");
            return roots.root(out.result().value());
        }
        void set(Handle object, std::u32string name, Handle value)
        {
            ReturnSlot out(context);
            ASSERT_EQ(set_attribute(context, object, text(std::move(name)), value, out).tag(), ResultTag::Ok);
        }
        void put(Handle dict, std::u32string name, Handle value)
        { detail::name_store(context, dict.get(context), text(std::move(name)).get(context), value.get(context)); }
        void define(Handle name, Handle bases, Handle space, ReturnSlot &out, std::span<const KeywordArg> keywords = {})
        {
            auto metaclass = roots.root(context.type_type());
            std::array args{name.get(context), bases.get(context), space.get(context)};
            call(context, metaclass, {args, keywords}, out);
        }
        void expect_error(ReturnSlot &out, CoreObject type)
        {
            ASSERT_EQ(out.result().tag(), ResultTag::Raised);
            EXPECT_EQ(context.type_of(out.result().exception()), context.builtin(type));
            context.collect();
            EXPECT_NO_THROW(validate_exception(context, out.result().exception()));
        }
        static void captured(Attachment &c, Handle fn, std::span<const Value>, ReturnSlot &out)
        { c.collect(); out.ok(c.payload<NativeFunctionObject>(fn).captures[0]); }
        static void none(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
        { c.collect(); out.ok(c.builtin(CoreObject::None)); }
        static void fail(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
        { raise_error(c, CoreObject::RuntimeErrorType, U"constructor failed", out); }
        static void allocate(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            RootScope scope(c);
            c.allocate_instance<InstanceObject>(scope.root(args[0]), out);
            c.collect();
        }
    };

    TEST_F(CoreConstruction, DefaultClassCallAndExplicitTypeCallAllocateTypedInstances)
    {
        auto cls = type("Plain"); ReturnSlot out(context);
        call(context, cls, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.type_of(out.result().value()), cls.get(context));
        auto first = roots.root(out.result().value());
        auto type_type = roots.root(context.type_type());
        auto wrapper = get(type_type, U"__call__");
        std::array args{cls.get(context)};
        call(context, wrapper, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.type_of(out.result().value()), cls.get(context));
        EXPECT_NE(out.result().value(), first.get(context));
    }

    TEST_F(CoreConstruction, InitReceivesKeywordsAndMayReenterAttributeOperations)
    {
        auto cls = type("Mutable"); auto expected = text(U"initialized"); auto name = text(U"value");
        SignatureLayout signature;
        signature.parameters = {{U"self", Kind::PositionalOnly}, {U"value", Kind::KeywordOnly}};
        auto init = function(signature, [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
            RootScope scope(c);
            auto key = scope.root(c.payload<NativeFunctionObject>(fn).captures[0]);
            set_attribute(c, scope.root(args[0]), key, scope.root(args[1]), out);
            c.collect();
        }, {name.get(context)});
        set(cls, U"__init__", init);
        std::array keywords{KeywordArg{name.get(context), expected.get(context)}}; ReturnSlot out(context);
        call(context, cls, {{}, keywords}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto self = roots.root(out.result().value());
        EXPECT_EQ(get(self, U"value").get(context), expected.get(context));
    }

    TEST_F(CoreConstruction, NewMayReturnAnUnrelatedValueWithoutCallingInit)
    {
        auto cls = type("Factory"); auto expected = text(U"unrelated");
        set(cls, U"__new__", function({{{U"cls"}}, {}, {}}, captured, {expected.get(context)}));
        set(cls, U"__init__", function({{{U"self"}}, {}, {}}, fail));
        ReturnSlot out(context); call(context, cls, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), expected.get(context));
    }

    TEST_F(CoreConstruction, NewReturningASubclassUsesTheReturnedObjectsInit)
    {
        auto base = type("Base"); auto child = type("Child", {base});
        ReturnSlot out(context); context.allocate_instance<InstanceObject>(child, out);
        auto self = roots.root(out.result().value()); auto marker = text(U"child init");
        set(base, U"__new__", function({{{U"cls"}}, {}, {}}, captured, {self.get(context)}));
        set(base, U"__init__", function({{{U"self"}}, {}, {}}, fail));
        set(child, U"__init__", function({{{U"self"}}, {}, {}},
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &result) {
                RootScope scope(c); auto value = scope.root(c.payload<NativeFunctionObject>(fn).captures[0]);
                ReturnSlot key(c); make_str(c, U"marker", key);
                set_attribute(c, scope.root(args[0]), scope.root(key.result().value()), value, result);
            }, {marker.get(context)}));
        call(context, base, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), self.get(context));
        EXPECT_EQ(get(self, U"marker").get(context), marker.get(context));
    }

    TEST_F(CoreConstruction, InitMustReturnNoneAndBothFailureStagesPropagate)
    {
        auto cls = type("Broken"); auto wrong = text(U"not None"); ReturnSlot out(context);
        set(cls, U"__init__", function({{{U"self"}}, {}, {}}, captured, {wrong.get(context)}));
        call(context, cls, {}, out); expect_error(out, CoreObject::TypeErrorType);
        set(cls, U"__init__", function({{{U"self"}}, {}, {}}, fail));
        call(context, cls, {}, out); expect_error(out, CoreObject::RuntimeErrorType);
        set(cls, U"__new__", function({{{U"cls"}}, {}, {}}, fail));
        call(context, cls, {}, out); expect_error(out, CoreObject::RuntimeErrorType);
    }

    TEST_F(CoreConstruction, DefaultNewAndInitEnforceTheirComplementaryArgumentRules)
    {
        auto cls = type("Plain"); auto value = text(U"argument"); std::array args{value.get(context)};
        ReturnSlot out(context); call(context, cls, {args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
        set(cls, U"__new__", function({{{U"cls"}, {U"value"}}, {}, {}}, allocate));
        call(context, cls, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto self = roots.root(out.result().value());
        auto object = roots.root(context.object_type()); auto object_new = get(object, U"__new__");
        std::array explicit_args{cls.get(context), value.get(context)};
        call(context, object_new, {explicit_args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
        auto object_init = get(object, U"__init__");
        std::array init_args{self.get(context), value.get(context)};
        call(context, object_init, {init_args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        set(cls, U"__init__", function({{{U"self"}, {U"value"}}, {}, {}}, none));
        call(context, object_init, {init_args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreConstruction, SingleArgumentTypeIsExactTypeOnly)
    {
        auto type_type = roots.root(context.type_type()); auto value = text(U"value");
        std::array args{value.get(context)}; ReturnSlot out(context);
        call(context, type_type, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), context.builtin(CoreObject::StrType));
        auto meta = type("Meta", {type_type});
        call(context, meta, {args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreConstruction, MetaclassCallOverrideUsesTheOrdinaryCallProtocol)
    {
        auto type_type = roots.root(context.type_type()); auto meta = type("Meta", {type_type});
        ReturnSlot out(context); create_type(context, {"Class", {}, meta}, out);
        auto cls = roots.root(out.result().value()); auto expected = text(U"metaclass result");
        set(meta, U"__call__", function({{{U"cls"}}, {}, {}}, captured, {expected.get(context)}));
        call(context, cls, {}, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
    }

    TEST_F(CoreConstruction, ObjectNewCannotAllocateAnUnrelatedNativeLayout)
    {
        auto object = roots.root(context.object_type()); auto object_new = get(object, U"__new__");
        auto str = roots.root(context.builtin(CoreObject::StrType));
        std::array args{str.get(context)}; ReturnSlot out(context);
        call(context, object_new, {args, {}}, out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreConstruction, TypeConstructionCopiesTheNamespaceAndPreservesUnicodeMetadata)
    {
        auto space = dictionary(); auto value = text(U"class value"); auto module = text(U"test_module");
        put(space, U"value", value); put(space, U"__module__", module);
        auto qualname = text(U"Scope.\u4e2d\u6587\U0001f600"); put(space, U"__qualname__", qualname);
        ReturnSlot out(context); auto name = text(U"\u4e2d\u6587\U0001f600"); auto bases = tuple();
        define(name, bases, space, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto cls = roots.root(out.result().value());
        EXPECT_EQ(get(cls, U"__name__").get(context), name.get(context));
        EXPECT_EQ(get(cls, U"__qualname__").get(context), qualname.get(context));
        EXPECT_EQ(context.payload<StrObject>(get(cls, U"__name__")).value, context.payload<StrObject>(name).value);
        EXPECT_EQ(context.payload<StrObject>(get(cls, U"__qualname__")).value, context.payload<StrObject>(qualname).value);
        EXPECT_EQ(get(cls, U"__module__").get(context), module.get(context));
        EXPECT_EQ(get(cls, U"value").get(context), value.get(context));
        EXPECT_NE(*TypeView(context, cls).dictionary(), space.get(context));
        EXPECT_FALSE(detail::name_lookup(context, *TypeView(context, cls).dictionary(), U"__qualname__"));
        EXPECT_EQ(detail::name_lookup(context, space.get(context), U"__qualname__"), qualname.get(context));
        put(space, U"value", text(U"changed source only"));
        EXPECT_EQ(get(cls, U"value").get(context), value.get(context));
        call(context, cls, {}, out);
        EXPECT_EQ(context.type_of(out.result().value()), cls.get(context));
    }

    TEST_F(CoreConstruction, InvalidTypeArgumentsAndDefinitionConflictsUseRaised)
    {
        auto bases = tuple(); auto space = dictionary(); auto name = text(U"Class"); ReturnSlot out(context);
        define(space, bases, space, out); expect_error(out, CoreObject::TypeErrorType);
        define(name, name, space, out); expect_error(out, CoreObject::TypeErrorType);
        define(name, bases, name, out); expect_error(out, CoreObject::TypeErrorType);
        auto duplicate = tuple({context.object_type(), context.object_type()});
        define(name, duplicate, space, out); expect_error(out, CoreObject::TypeErrorType);
        auto non_type = tuple({name.get(context)});
        define(name, non_type, space, out); expect_error(out, CoreObject::TypeErrorType);
        auto nul = text(std::u32string{U'a', U'\0', U'b'});
        define(nul, bases, space, out); expect_error(out, CoreObject::ValueErrorType);
        put(space, U"__qualname__", roots.root(context.builtin(CoreObject::None)));
        define(name, bases, space, out); expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreConstruction, TypeDefinitionFailuresDoNotHideHostFaults)
    {
        auto cls = type("Factory");
        set(cls, U"__new__", function({{{U"cls"}}, {}, {}},
            [](Attachment &, Handle, std::span<const Value>, ReturnSlot &) {
                throw std::invalid_argument("host invariant, not a definition error");
            }));
        ReturnSlot out(context);
        EXPECT_THROW(call(context, cls, {}, out), std::invalid_argument);
    }

    TEST_F(CoreConstruction, StaticAndClassMethodsBindThroughTheirOwnDescriptors)
    {
        auto cls = type("Owner"); auto child = type("Child", {cls}); auto expected = text(U"static");
        auto fn = function({}, captured, {expected.get(context)});
        auto static_type = roots.root(context.builtin(CoreObject::StaticMethodType));
        auto class_type = roots.root(context.builtin(CoreObject::ClassMethodType));
        ReturnSlot out(context); std::array args{fn.get(context)};
        call(context, static_type, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto wrapped = roots.root(out.result().value()); set(cls, U"static", wrapped);
        EXPECT_EQ(get(cls, U"static").get(context), fn.get(context));
        call(context, child, {}, out); auto self = roots.root(out.result().value());
        EXPECT_EQ(get(self, U"static").get(context), fn.get(context));
        auto owner = function({{{U"cls"}}, {}, {}},
                              [](Attachment &, Handle, std::span<const Value> bound, ReturnSlot &result) { result.ok(bound[0]); });
        std::array class_args{owner.get(context)};
        call(context, class_type, {class_args, {}}, out);
        auto class_wrapped = roots.root(out.result().value()); set(cls, U"owner", class_wrapped);
        auto method = get(child, U"owner"); call(context, method, {}, out);
        EXPECT_EQ(out.result().value(), child.get(context));
        method = get(self, U"owner"); call(context, method, {}, out);
        EXPECT_EQ(out.result().value(), child.get(context));
        EXPECT_EQ(get(wrapped, U"__func__").get(context), fn.get(context));
        EXPECT_EQ(get(class_wrapped, U"__func__").get(context), owner.get(context));
    }

    TEST_F(CoreConstruction, ConstructorNewFunctionsAreStaticAfterTypeCreation)
    {
        auto space = dictionary(); auto fn = function({{{U"cls"}}, {}, {}}, allocate);
        put(space, U"__new__", fn);
        ReturnSlot out(context); define(text(U"CustomNew"), tuple(), space, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto cls = roots.root(out.result().value());
        EXPECT_EQ(get(cls, U"__new__").get(context), fn.get(context));
        call(context, cls, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto self = roots.root(out.result().value());
        EXPECT_EQ(get(self, U"__new__").get(context), fn.get(context));
        const auto stored = *detail::name_lookup(context, *TypeView(context, cls).dictionary(), U"__new__");
        EXPECT_EQ(context.type_of(stored), context.builtin(CoreObject::StaticMethodType));
    }

    TEST_F(CoreConstruction, SetNameSnapshotsSurviveNamespaceMutationBeforeInitSubclass)
    {
        auto events = dictionary(); auto space = dictionary(); auto descriptor_type = type("Naming");
        auto naming = function({{{U"self"}, {U"owner"}, {U"name"}}, {}, {}},
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
                const auto &captures = c.payload<NativeFunctionObject>(fn).captures;
                const auto target_space = *TypeView(c, args[1]).dictionary();
                for (auto key : {U"first", U"second"})
                {
                    detail::name_erase(c, target_space, key);
                    detail::name_erase(c, captures[1], key);
                }
                c.collect();
                detail::name_store(c, captures[0], args[2], args[1]);
                out.ok(c.builtin(CoreObject::None));
            }, {events.get(context), space.get(context)});
        set(descriptor_type, U"__set_name__", naming);
        {
            RootScope local(context); ReturnSlot temporary(context);
            for (auto key : {U"first", U"second"})
            {
                context.allocate_instance<InstanceObject>(descriptor_type, temporary);
                auto value = local.root(temporary.result().value());
                put(space, key, value);
            }
        }
        auto token = text(U"keyword token"); auto marker = text(U"init");
        SignatureLayout init;
        init.parameters = {{U"cls", Kind::PositionalOnly}, {U"token", Kind::KeywordOnly}};
        auto hook = function(init, [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out) {
            const auto &captures = c.payload<NativeFunctionObject>(fn).captures;
            cxxpython::core::make_tuple(c, args, out);
            detail::name_store(c, captures[0], captures[1], out.result().value());
            out.ok(c.builtin(CoreObject::None));
        }, {events.get(context), marker.get(context)});
        auto base_space = dictionary(); put(base_space, U"__init_subclass__", hook);
        ReturnSlot out(context); define(text(U"Base"), tuple(), base_space, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto base = roots.root(out.result().value());
        auto base_hook = *detail::name_lookup(context, *TypeView(context, base).dictionary(), U"__init_subclass__");
        EXPECT_EQ(context.type_of(base_hook), context.builtin(CoreObject::ClassMethodType));
        auto keyword_name = text(U"token");
        std::array named{KeywordArg{keyword_name.get(context), token.get(context)}};
        define(text(U"Child"), tuple({base.get(context)}), space, out, named);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        auto cls = roots.root(out.result().value());
        DictView namespace_view(context, events.get(context)); const auto entries = namespace_view.entries();
        ASSERT_EQ(entries.size(), 3U);
        EXPECT_EQ(context.payload<StrObject>(entries[0].key).value, U"first");
        EXPECT_EQ(context.payload<StrObject>(entries[1].key).value, U"second");
        EXPECT_EQ(context.payload<StrObject>(entries[2].key).value, U"init");
        EXPECT_EQ(entries[0].value, cls.get(context));
        EXPECT_EQ(entries[1].value, cls.get(context));
        EXPECT_EQ(context.payload<TupleObject>(entries[2].value).items,
                  (std::vector<Value>{cls.get(context), token.get(context)}));
    }

    TEST_F(CoreConstruction, SetNameFailureIsChainedAndStopsClassInitialization)
    {
        auto descriptor_type = type("Naming"); auto space = dictionary();
        set(descriptor_type, U"__set_name__", function({{{U"self"}, {U"owner"}, {U"name"}}, {}, {}}, fail));
        ReturnSlot out(context); context.allocate_instance<InstanceObject>(descriptor_type, out);
        auto descriptor = roots.root(out.result().value()); put(space, U"field", descriptor);
        define(text(U"Failed"), tuple(), space, out);
        expect_error(out, CoreObject::RuntimeErrorType);
        auto &error = context.payload<ExceptionObject>(out.result().exception());
        ReturnSlot cause(context), linked(context); context.load(out.result().exception(), error.cause, cause);
        context.load(out.result().exception(), error.context, linked);
        ASSERT_EQ(cause.result().tag(), ResultTag::Ok);
        EXPECT_EQ(linked.result().value(), cause.result().value());
        EXPECT_TRUE(error.suppress_context.load());
        EXPECT_EQ(context.type_of(cause.result().value()), context.builtin(CoreObject::RuntimeErrorType));
        EXPECT_NE(cause.result().value(), out.result().exception());
    }

    TEST_F(CoreConstruction, InitSubclassErrorsPropagateAndDefaultRejectsKeywords)
    {
        auto space = dictionary(); auto name = text(U"Child"); auto token_name = text(U"token");
        std::array named{KeywordArg{token_name.get(context), context.builtin(CoreObject::None)}};
        ReturnSlot out(context); define(name, tuple(), space, out, named);
        expect_error(out, CoreObject::TypeErrorType);
        auto base = type("Base");
        set(base, U"__init_subclass__", function({{{U"cls"}}, {}, {}}, fail));
        define(name, tuple({base.get(context)}), space, out);
        expect_error(out, CoreObject::RuntimeErrorType);
        auto &error = context.payload<ExceptionObject>(out.result().exception());
        ReturnSlot cause(context); context.load(out.result().exception(), error.cause, cause);
        EXPECT_EQ(cause.result().tag(), ResultTag::Empty);
    }

    TEST_F(CoreConstruction, WinningMetaclassNewIsInvokedRatherThanBypassed)
    {
        auto type_type = roots.root(context.type_type()); auto meta = type("Meta", {type_type});
        ReturnSlot out(context); create_type(context, {"Base", {}, meta}, out);
        auto base = roots.root(out.result().value()); auto expected = text(U"winning metaclass new");
        auto constructor = function({{{U"meta"}, {U"name"}, {U"bases"}, {U"dict"}}, {}, {}},
                                    captured, {expected.get(context)});
        set(meta, U"__new__", constructor);
        define(text(U"Child"), tuple({base.get(context)}), dictionary(), out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), expected.get(context));
        auto space = dictionary(); put(space, U"__qualname__", roots.root(context.builtin(CoreObject::None)));
        put(space, U"__slots__", tuple());
        define(text(std::u32string{U'x', U'\0'}), tuple({base.get(context)}), space, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), expected.get(context));
    }

    TEST_F(CoreConstruction, ConflictingMetaclassesArePythonTypeErrors)
    {
        auto type_type = roots.root(context.type_type());
        auto left_meta = type("LeftMeta", {type_type}); auto right_meta = type("RightMeta", {type_type});
        ReturnSlot out(context); create_type(context, {"Left", {}, left_meta}, out);
        auto left = roots.root(out.result().value()); create_type(context, {"Right", {}, right_meta}, out);
        auto right = roots.root(out.result().value());
        define(text(U"Conflict"), tuple({left.get(context), right.get(context)}), dictionary(), out);
        expect_error(out, CoreObject::TypeErrorType);
    }

    TEST_F(CoreConstruction, UserNewUsesMetaclassLookupButDefaultObjectNewDoesNot)
    {
        auto type_type = roots.root(context.type_type()); auto meta = type("Meta", {type_type});
        ReturnSlot out(context); create_type(context, {"Class", {}, meta}, out);
        auto cls = roots.root(out.result().value()); auto expected = text(U"intercepted new");
        auto intercepted = function({{{U"cls"}}, {}, {}}, captured, {expected.get(context)});
        auto default_get = get(type_type, U"__getattribute__");
        auto hook = function({{{U"cls"}, {U"name"}}, {}, {}},
            [](Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &result) {
                const auto &captures = c.payload<NativeFunctionObject>(fn).captures;
                if (c.payload<StrObject>(args[1]).value == U"__new__") { result.ok(captures[0]); return; }
                RootScope scope(c); call(c, scope.root(captures[1]), {args, {}}, result);
            }, {intercepted.get(context), default_get.get(context)});
        set(meta, U"__getattribute__", hook);
        call(context, cls, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(context.type_of(out.result().value()), cls.get(context));
        set(cls, U"__new__", function({{{U"cls"}}, {}, {}}, allocate));
        call(context, cls, {}, out);
        EXPECT_EQ(out.result().value(), expected.get(context));
    }

    TEST_F(CoreConstruction, ClassmethodCanWrapAnArbitraryCallable)
    {
        auto callable_type = type("Callable"); auto owner = type("Owner");
        set(callable_type, U"__call__", function({{{U"self"}, {U"owner"}}, {}, {}},
            [](Attachment &, Handle, std::span<const Value> args, ReturnSlot &out) { out.ok(args[1]); }));
        ReturnSlot out(context); context.allocate_instance<InstanceObject>(callable_type, out);
        auto callable = roots.root(out.result().value());
        auto class_method = roots.root(context.builtin(CoreObject::ClassMethodType));
        std::array args{callable.get(context)};
        call(context, class_method, {args, {}}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        set(owner, U"method", roots.root(out.result().value()));
        auto method = get(owner, U"method"); call(context, method, {}, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_EQ(out.result().value(), owner.get(context));
    }

    TEST_F(CoreConstruction, TypeNameMutationKeepsManagedIdentityAndTheModuleNamespaceAuthoritative)
    {
        auto cls = type("Original"); auto name = text(U"Renamed"); auto qualname = text(U"");
        set(cls, U"__name__", name); set(cls, U"__qualname__", qualname);
        EXPECT_EQ(get(cls, U"__name__").get(context), name.get(context));
        EXPECT_EQ(get(cls, U"__qualname__").get(context), qualname.get(context));
        context.collect();
        EXPECT_EQ(get(cls, U"__name__").get(context), name.get(context));
        auto module = text(U"renamed_module"); set(cls, U"__module__", module);
        EXPECT_EQ(detail::name_lookup(context, *TypeView(context, cls).dictionary(), U"__module__"), module.get(context));
        ReturnSlot out(context); auto name_key = text(U"__name__");
        set_attribute(context, cls, name_key, roots.root(context.builtin(CoreObject::None)), out);
        expect_error(out, CoreObject::TypeErrorType);
        del_attribute(context, cls, name_key, out); expect_error(out, CoreObject::TypeErrorType);
        auto nul = text(std::u32string{U'x', U'\0'});
        set_attribute(context, cls, name_key, nul, out); expect_error(out, CoreObject::ValueErrorType);
        EXPECT_EQ(get(cls, U"__name__").get(context), name.get(context));
    }

    TEST_F(CoreConstruction, AllocationFailureAtEachConstructorStepLeavesNoPublishedClassOrRoots)
    {
        auto name = text(U"Temporary"); auto bases = tuple(); auto space = dictionary();
        context.collect(); const auto baseline = context.stats();
        bool completed = false;
        for (std::size_t quota = 0; quota < 100; ++quota)
        {
            {
                ReturnSlot out(context); context.set_allocation_limit(quota);
                std::array args{name.get(context), bases.get(context), space.get(context)};
                RootScope local(context); auto metaclass = local.root(context.type_type());
                call(context, metaclass, {args, {}}, out);
                if (out.result().tag() == ResultTag::Ok)
                { EXPECT_EQ(context.type_of(out.result().value()), context.type_type()); completed = true; }
                else EXPECT_EQ(out.result().exception(), context.builtin(CoreObject::MemoryError));
                context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
            }
            context.collect();
            EXPECT_EQ(context.stats().objects, baseline.objects) << quota;
            EXPECT_EQ(context.stats().roots, baseline.roots) << quota;
            if (completed) break;
        }
        EXPECT_TRUE(completed);
    }

    TEST_F(CoreConstruction, EmptySlotsWorkWhileClassCellsRequireTheirOwnContract)
    {
        auto space = dictionary(); auto name = text(U"NeedsLayout"); auto bases = tuple(); ReturnSlot out(context);
        put(space, U"__slots__", tuple());
        define(name, bases, space, out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        EXPECT_FALSE(context.payload<TypeObject>(out.result().value()).instance_layout.fields.dictionary);
        detail::name_erase(context, space.get(context), U"__slots__");
        put(space, U"__classcell__", roots.root(context.builtin(CoreObject::None)));
        define(name, bases, space, out); expect_error(out, CoreObject::TypeErrorType);
    }
}
