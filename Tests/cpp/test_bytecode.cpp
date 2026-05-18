#include <gtest/gtest.h>

#include <sstream>

#include "cxxpython/vm.hpp"

TEST(Bytecode, MetadataNamesAndStackEffects)
{
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadConst), "LOAD_CONST");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::LoadName), "LOAD_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::StoreName), "STORE_NAME");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::PopTop), "POP_TOP");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::BinaryAdd), "BINARY_ADD");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::Call), "CALL");
    EXPECT_STREQ(cxxpython::opcode_name(cxxpython::Opcode::ReturnValue), "RETURN_VALUE");

    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadConst, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::LoadName, 0}), 1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::StoreName, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::PopTop, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::BinaryAdd, 0}), -1);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 0}), 0);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::Call, 2}), -2);
    EXPECT_EQ(cxxpython::stack_effect({cxxpython::Opcode::ReturnValue, 0}), -1);
}

TEST(Bytecode, ExecutesLoadConstAndReturn)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::IntObject *value = runtime.new_int(42);
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->type(), runtime.code_type());
    EXPECT_EQ(vm.execute(*code), value);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
    EXPECT_EQ(vm.thread_state().current_frame(), nullptr);
    EXPECT_EQ(&vm.interpreter_state().runtime(), &runtime);
}

TEST(Bytecode, ExecutesIntegerBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_int(2), runtime.new_int(3)},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryAdd, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 5);
}

TEST(Bytecode, ExecutesStringBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_str("py"), runtime.new_str("thon")},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryAdd, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::StrObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, "python");
}

TEST(Bytecode, RaisesPythonTypeErrorForUnsupportedBinaryAdd)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code({runtime.new_int(1), runtime.new_str("x")},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryAdd, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "unsupported operand type(s) for +: 'int' and 'str'");
    }
}

TEST(Bytecode, DispatchesBinaryAddThroughTypeSlots)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    auto add_repr = [](const cxxpython::Object &object) -> std::string
    {
        return "<adder " + std::to_string(static_cast<const cxxpython::IntObject &>(object).value) + ">";
    };
    auto add_binary = [](const cxxpython::Object &left, const cxxpython::Object &right) -> cxxpython::Object *
    {
        auto &left_value = static_cast<const cxxpython::IntObject &>(left);
        auto *right_value = dynamic_cast<const cxxpython::IntObject *>(&right);
        if (!right_value)
            cxxpython::raise_type_error(left.runtime(), "adder expects int right operand");
        return left.runtime()->new_int(left_value.value * 10 + right_value->value);
    };
    auto add_traverse = [](const cxxpython::Object &object, const cxxpython::VisitChildren &visitor)
    {
        (void)object;
        (void)visitor;
    };

    cxxpython::TypeObject *adder_type = runtime.new_heap_type(
        "Adder",
        runtime.int_type(),
        cxxpython::TypeSlots{
            .repr = add_repr,
            .hash = cxxpython::make_int_slots().hash,
            .equal = cxxpython::make_int_slots().equal,
            .truth = cxxpython::make_int_slots().truth,
            .traverse = add_traverse,
            .binary_add = add_binary,
        });

    cxxpython::IntObject *left = runtime.allocate<cxxpython::IntObject>(adder_type, 7);
    cxxpython::CodeObject *code = runtime.new_code({left, runtime.new_int(3)},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::LoadConst, 1},
                                                    {cxxpython::Opcode::BinaryAdd, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    auto *result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 73);
}

TEST(Bytecode, CodeObjectsTraverseReferencedConstants)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *first = runtime.new_int(1);
    cxxpython::StrObject *second = runtime.new_str("x");
    cxxpython::CodeObject *code = runtime.new_code({first, second},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(code->name(), "<module>");
    EXPECT_EQ(code->filename(), "<string>");

    std::vector<cxxpython::Object *> visited;
    code->visit_children([&visited](cxxpython::Object *object)
                         { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(visited[0], first);
    EXPECT_EQ(visited[1], second);
    EXPECT_EQ(code->repr().find("<code object <module> at "), 0U);
    EXPECT_NE(code->repr().find("file '<string>'"), std::string::npos);
}

TEST(Bytecode, FrameObjectsTrackCodeAndStackReferences)
{
    cxxpython::Runtime runtime;
    cxxpython::IntObject *value = runtime.new_int(9);
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.new_dict();
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code, locals, globals, builtins);
    frame->push(value);

    EXPECT_EQ(frame->type(), runtime.frame_type());
    EXPECT_EQ(frame->code(), code);
    EXPECT_EQ(frame->locals(), locals);
    EXPECT_EQ(frame->globals(), globals);
    EXPECT_EQ(frame->builtins(), builtins);
    EXPECT_EQ(frame->stack_size(), 1U);
    EXPECT_EQ(frame->repr().find("<frame at "), 0U);

    std::vector<cxxpython::Object *> visited;
    frame->visit_children([&visited](cxxpython::Object *object)
                          { visited.push_back(object); });

    ASSERT_EQ(visited.size(), 5U);
    EXPECT_EQ(visited[0], code);
    EXPECT_EQ(visited[1], locals);
    EXPECT_EQ(visited[2], globals);
    EXPECT_EQ(visited[3], builtins);
    EXPECT_EQ(visited[4], value);
}

TEST(Bytecode, LoadNameResolvesLocalsGlobalsThenBuiltins)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::StrObject *name = runtime.new_str("x");
    cxxpython::CodeObject *code = runtime.new_code({},
                                                   {name},
                                                   {{cxxpython::Opcode::LoadName, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.new_dict();
    builtins->set(runtime.new_str("x"), runtime.new_int(1));
    globals->set(runtime.new_str("x"), runtime.new_int(2));
    locals->set(runtime.new_str("x"), runtime.new_int(3));

    auto *local_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(local_result, nullptr);
    EXPECT_EQ(local_result->value, 3);

    locals = runtime.new_dict();
    auto *global_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(global_result, nullptr);
    EXPECT_EQ(global_result->value, 2);

    globals = runtime.new_dict();
    auto *builtin_result = dynamic_cast<cxxpython::IntObject *>(vm.execute(*code, *locals, *globals, *builtins));
    ASSERT_NE(builtin_result, nullptr);
    EXPECT_EQ(builtin_result->value, 1);
}

TEST(Bytecode, CallsCallableObjectsThroughTypeSlot)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::Object *callable = runtime.builtins_dict()->get(runtime.new_str("print"));
    ASSERT_NE(callable, nullptr);

    std::ostringstream output;
    runtime.set_stdout(output);
    cxxpython::CodeObject *code = runtime.new_code(
        {callable, runtime.new_str("answer"), runtime.new_int(42)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::LoadConst, 1},
         {cxxpython::Opcode::LoadConst, 2},
         {cxxpython::Opcode::Call, 2},
         {cxxpython::Opcode::ReturnValue, 0}});

    EXPECT_EQ(vm.execute(*code), runtime.none());
    EXPECT_EQ(output.str(), "answer 42\n");
}

TEST(Bytecode, RaisesPythonTypeErrorForNonCallable)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::CodeObject *code = runtime.new_code(
        {runtime.new_int(1)},
        {{cxxpython::Opcode::LoadConst, 0},
         {cxxpython::Opcode::Call, 0},
         {cxxpython::Opcode::ReturnValue, 0}});

    try
    {
        (void)vm.execute(*code);
        FAIL() << "expected PythonError";
    }
    catch (const cxxpython::PythonError &error)
    {
        EXPECT_EQ(error.type(), runtime.type_error_type());
        EXPECT_STREQ(error.what(), "'int' object is not callable");
    }
}

TEST(Bytecode, ThreadStateVisitsActiveFramesAsRoots)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);
    cxxpython::IntObject *value = runtime.new_int(11);
    cxxpython::DictObject *locals = runtime.new_dict();
    cxxpython::DictObject *globals = runtime.new_dict();
    cxxpython::DictObject *builtins = runtime.builtins_dict();
    cxxpython::CodeObject *code = runtime.new_code({value},
                                                   {{cxxpython::Opcode::LoadConst, 0},
                                                    {cxxpython::Opcode::ReturnValue, 0}});
    cxxpython::Frame *frame = runtime.new_frame(code, locals, globals, builtins);
    frame->push(value);

    vm.thread_state().push_frame(frame);

    std::vector<cxxpython::Object *> active_roots;
    vm.thread_state().visit_frames([&active_roots](cxxpython::Object *object)
                                   { active_roots.push_back(object); });

    ASSERT_EQ(active_roots.size(), 1U);
    EXPECT_EQ(active_roots[0], frame);

    std::vector<cxxpython::Object *> frame_children;
    active_roots[0]->visit_children([&frame_children](cxxpython::Object *object)
                                    { frame_children.push_back(object); });
    ASSERT_EQ(frame_children.size(), 5U);
    EXPECT_EQ(frame_children[0], code);
    EXPECT_EQ(frame_children[1], locals);
    EXPECT_EQ(frame_children[2], globals);
    EXPECT_EQ(frame_children[3], builtins);
    EXPECT_EQ(frame_children[4], value);

    vm.thread_state().pop_frame(frame);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
}

TEST(Bytecode, RejectsMalformedBytecode)
{
    cxxpython::Runtime runtime;
    cxxpython::VirtualMachine vm(runtime);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{cxxpython::Opcode::ReturnValue, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{cxxpython::Opcode::LoadConst, 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);

    EXPECT_THROW((void)cxxpython::stack_effect({static_cast<cxxpython::Opcode>(255), 0}),
                 cxxpython::BytecodeError);

    EXPECT_THROW((void)vm.execute(*runtime.new_code({}, {{static_cast<cxxpython::Opcode>(255), 0}})),
                 cxxpython::BytecodeError);
    EXPECT_EQ(vm.thread_state().frame_depth(), 0U);
}
