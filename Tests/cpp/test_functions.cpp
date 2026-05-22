#include <gtest/gtest.h>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

TEST(Functions, CallsPythonFunctionObjectsWithCapturedGlobals)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::Object *answer = runtime.new_str("from-globals");
    globals->set(runtime.new_str("answer"), answer);

    cxxpython::CodeObject *code = runtime.new_code(
        "f",
        "test.py",
        {},
        {runtime.new_str("answer")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("f", code, globals);

    EXPECT_EQ(function->type(), runtime.function_type());
    EXPECT_EQ(cxxpython::object_repr(function), "<function f>");
    auto *signature = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(function, "__text_signature__"));
    ASSERT_NE(signature, nullptr);
    EXPECT_EQ(signature->value, "()");
    EXPECT_EQ(cxxpython::object_get_attr(function, "__doc__"), runtime.none());
    EXPECT_EQ(cxxpython::object_call(function, {}), answer);

    std::vector<cxxpython::Object *> children;
    function->visit_children([&children](cxxpython::Object *object)
                             { children.push_back(object); });
    ASSERT_EQ(children.size(), 2U);
    EXPECT_EQ(children[0], code);
    EXPECT_EQ(children[1], globals);

    try
    {
        (void)cxxpython::object_call(function, {runtime.new_int(1)});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "f() takes 0 positional arguments but 1 argument was given");
    }
}

TEST(Functions, PythonFunctionsBindAsMethodsThroughDescriptorProtocol)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::CodeObject *code = runtime.new_code(
        "method",
        "test.py",
        1,
        {runtime.new_str("self")},
        {},
        {runtime.new_str("self")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("method", code, globals);
    auto *signature = dynamic_cast<cxxpython::StrObject *>(cxxpython::object_get_attr(function, "__text_signature__"));
    ASSERT_NE(signature, nullptr);
    EXPECT_EQ(signature->value, "(self)");
    cxxpython::TypeObject *owner_type = runtime.new_heap_type("Owner", runtime.object_type());
    owner_type->dict->set(runtime.new_str("method"), function);
    cxxpython::Object *instance = runtime.allocate<cxxpython::Object>(owner_type);

    EXPECT_EQ(cxxpython::object_get_attr(owner_type, "method"), function);

    cxxpython::Object *bound = cxxpython::object_get_attr(instance, "method");
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->type(), runtime.method_type());
    EXPECT_TRUE(cxxpython::object_repr(bound).starts_with("<bound method method of <Owner object at "));

    std::vector<cxxpython::Object *> children;
    bound->visit_children([&children](cxxpython::Object *object)
                          { children.push_back(object); });
    ASSERT_EQ(children.size(), 2U);
    EXPECT_EQ(children[0], function);
    EXPECT_EQ(children[1], instance);
    EXPECT_EQ(cxxpython::object_call(bound, {}), instance);
}

TEST(Functions, PythonFunctionArgumentBindingUsesCodeObjectVarnames)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::CodeObject *code = runtime.new_code(
        "identity",
        "test.py",
        1,
        {runtime.new_str("value")},
        {},
        {runtime.new_str("value")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("identity", code, globals);
    cxxpython::Object *argument = runtime.new_str("arg");

    EXPECT_EQ(cxxpython::object_call(function, {argument}), argument);

    try
    {
        (void)cxxpython::object_call(function, {});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "identity() missing required positional argument: 'value'");
    }

    try
    {
        (void)cxxpython::object_call(function, {argument, argument});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "identity() takes 1 positional argument but 2 arguments were given");
    }
}

TEST(Functions, RejectsMalformedFunctionCodeObjectMetadata)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::CodeObject *missing_varname = runtime.new_code(
        "broken",
        "test.py",
        1,
        {},
        {runtime.none()},
        {},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("broken", missing_varname, globals);

    EXPECT_THROW((void)cxxpython::object_call(function, {runtime.none()}), cxxpython::BytecodeError);
}

TEST(Functions, PythonFunctionBindsVarargsAndVarkeywords)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::CodeObject *varargs_code = runtime.new_code(
        "collect_args",
        "test.py",
        1,
        cxxpython::CodeObject::FlagVarArgs,
        {runtime.new_str("first"), runtime.new_str("args")},
        {},
        {},
        {{cxxpython::Opcode::LoadFast, 1},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *collect_args =
        runtime.new_function("collect_args", varargs_code, globals);

    cxxpython::Object *first = runtime.new_str("first");
    cxxpython::Object *second = runtime.new_str("second");
    cxxpython::Object *third = runtime.new_str("third");
    auto *args_tuple = dynamic_cast<cxxpython::TupleObject *>(
        cxxpython::object_call(collect_args, {first, second, third}));
    ASSERT_NE(args_tuple, nullptr);
    ASSERT_EQ(args_tuple->items().size(), 2U);
    EXPECT_EQ(args_tuple->items()[0], second);
    EXPECT_EQ(args_tuple->items()[1], third);

    cxxpython::CodeObject *kwargs_code = runtime.new_code(
        "collect_kwargs",
        "test.py",
        1,
        cxxpython::CodeObject::FlagVarKeywords,
        {runtime.new_str("first"), runtime.new_str("kwargs")},
        {},
        {},
        {{cxxpython::Opcode::LoadFast, 1},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *collect_kwargs =
        runtime.new_function("collect_kwargs", kwargs_code, globals);

    cxxpython::DictObject *keywords = runtime.new_dict();
    cxxpython::Object *extra = runtime.new_str("extra");
    keywords->set(runtime.new_str("extra"), extra);
    auto *kwargs_dict = dynamic_cast<cxxpython::DictObject *>(
        cxxpython::object_call(collect_kwargs, cxxpython::CallArguments{{first}, keywords}));
    ASSERT_NE(kwargs_dict, nullptr);
    EXPECT_EQ(kwargs_dict->get(runtime.new_str("extra")), extra);
}

TEST(Functions, PythonFunctionBindsKeywordArgumentsToParameters)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::CodeObject *code = runtime.new_code(
        "identity",
        "test.py",
        1,
        {runtime.new_str("value")},
        {},
        {},
        {{cxxpython::Opcode::LoadFast, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("identity", code, globals);

    cxxpython::Object *value = runtime.new_str("kw");
    cxxpython::DictObject *keywords = runtime.new_dict();
    keywords->set(runtime.new_str("value"), value);
    EXPECT_EQ(cxxpython::object_call(function, cxxpython::CallArguments{{}, keywords}), value);

    keywords->set(runtime.new_str("other"), value);
    try
    {
        (void)cxxpython::object_call(function, cxxpython::CallArguments{{}, keywords});
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "identity() got an unexpected keyword argument 'other'");
    }
}

TEST(Functions, PythonFunctionCallsReuseActiveVirtualMachineFrames)
{
    cxxpython::Runtime runtime;
    cxxpython::DictObject *globals = runtime.new_dict();
    globals->set(runtime.new_str("probe"),
                 runtime.new_native_function("probe", [](cxxpython::Runtime &runtime,
                                                          const cxxpython::CallArguments &args)
                                             {
                                                 if (!args.positional.empty() ||
                                                     (args.keywords && args.keywords->size() != 0))
                                                     cxxpython::raise_type_error(&runtime, "probe() takes no arguments");
                                                 cxxpython::VirtualMachine *vm = runtime.active_vm();
                                                 if (!vm)
                                                     cxxpython::raise_type_error(&runtime, "probe() requires an active VM");
                                                 return runtime.new_int(static_cast<std::int64_t>(
                                                     vm->thread_state().frame_depth()));
                                             }));

    cxxpython::CodeObject *function_code = runtime.new_code(
        "f",
        "test.py",
        {},
        {runtime.new_str("probe")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::Call, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::PythonFunctionObject *function = runtime.new_function("f", function_code, globals);
    globals->set(runtime.new_str("f"), function);

    auto *host_depth = dynamic_cast<cxxpython::IntObject *>(cxxpython::object_call(function, {}));
    ASSERT_NE(host_depth, nullptr);
    EXPECT_EQ(host_depth->value, 1);

    cxxpython::CodeObject *caller_code = runtime.new_code(
        "caller",
        "test.py",
        {},
        {runtime.new_str("f")},
        {{cxxpython::Opcode::LoadName, 0},
         {cxxpython::Opcode::Call, 0},
         {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::VirtualMachine vm(runtime);
    auto *nested_depth = dynamic_cast<cxxpython::IntObject *>(vm.execute(*caller_code, *globals));
    ASSERT_NE(nested_depth, nullptr);
    EXPECT_EQ(nested_depth->value, 2);
}
