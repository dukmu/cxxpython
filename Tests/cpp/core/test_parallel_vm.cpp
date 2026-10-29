#include "cxxpython/core/execution.hpp"

#include <gtest/gtest.h>

#include <future>
#include <latch>

namespace
{
    using namespace cxxpython::core;

    template <class Work>
    void run_shared(Interpreter &interpreter, Attachment &source, Handle packet, Work work)
    {
        std::array inputs{source.make_transfer(packet), source.make_transfer(packet)};
        std::latch ready(2), start(1); std::array<std::future<void>, 2> workers;
        NativeWait wait(source);
        for (std::size_t i = 0; i < workers.size(); ++i)
            workers[i] = std::async(std::launch::async, [&, i, input = std::move(inputs[i])]() mutable {
                Attachment context(interpreter); ReturnSlot message(context); context.receive(input, message);
                ready.count_down(); start.wait(); work(context, message.result().value(), i);
            });
        ready.wait(); EXPECT_EQ(interpreter.gc_state().active, 2U); start.count_down();
        for (auto &worker : workers) EXPECT_NO_THROW(worker.get());
    }

    Handle function(Attachment &context, RootScope &roots, CodeBuilder &builder)
    {
        ReturnSlot out(context); builder.freeze(out); const auto code = roots.root(out.result().value());
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
        const auto globals = roots.root(out.result().value()); make_function(context, {code, globals}, out);
        return roots.root(out.result().value());
    }

    TEST(ParallelVm, SameFunctionAndCodeRunWithTwoActiveWorkersAndGc)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        CodeSpec spec; spec.signature.parameters = {{U"n"}}; spec.locals = {U"n", U"total"}; CodeBuilder builder(source, spec);
        const auto zero = builder.constant(roots.root(source.builtin(CoreObject::IntZero)));
        const auto one = builder.constant(roots.root(source.builtin(CoreObject::IntOne)));
        auto loop = builder.label(), done = builder.label();
        builder.emit(Opcode::LoadConst, zero); builder.emit(Opcode::StoreFast, 1); builder.bind(loop);
        builder.emit(Opcode::LoadFast); builder.jump(Opcode::JumpIfFalse, done);
        builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::LoadFast); builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add));
        builder.emit(Opcode::StoreFast, 1); builder.emit(Opcode::LoadFast); builder.emit(Opcode::LoadConst, one);
        builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Subtract)); builder.emit(Opcode::StoreFast);
        builder.jump(Opcode::Jump, loop); builder.bind(done); builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::Return);
        builder.freeze(out); const auto code = roots.root(out.result().value());
        source.allocate_instance<DictObject>(roots.root(source.builtin(CoreObject::DictType)), out); const auto globals = roots.root(out.result().value());
        make_function(source, {code, globals}, out); const auto function = roots.root(out.result().value());
        make_int(source, 20000, out); const auto input = roots.root(out.result().value());
        std::array packet{function.get(source), input.get(source)}; cxxpython::core::make_tuple(source, packet, out); const auto task = roots.root(out.result().value());
        std::array transfers{source.make_transfer(task), source.make_transfer(task)};
        std::latch attempted(2), start(1); std::array<std::future<Transfer>, 2> workers;
        {
            NativeWait wait(source);
            for (std::size_t i = 0; i < workers.size(); ++i)
                workers[i] = std::async(std::launch::async, [&, transfer = std::move(transfers[i])]() mutable {
                    bool announced = false;
                    try
                    {
                        Attachment context(interpreter); RootScope local(context); ReturnSlot message(context), result(context);
                        context.receive(transfer, message); const auto &values = context.payload<TupleObject>(message.result().value()).items;
                        const auto target = local.root(values[0]); std::array args{values[1]};
                        announced = true; attempted.count_down(); start.wait();
                        call(context, target, {args, {}}, result);
                        if (result.result().tag() != ResultTag::Ok) throw std::runtime_error("parallel call raised");
                        context.collect(); return context.make_transfer(local.root(result.result().value()));
                    }
                    catch (...) { if (!announced) attempted.count_down(); throw; }
                });
            attempted.wait(); EXPECT_EQ(interpreter.gc_state().active, 2U); start.count_down();
        }
        for (auto &worker : workers)
        {
            Transfer result;
            { NativeWait wait(source); EXPECT_NO_THROW(result = worker.get()); }
            if (!result) continue;
            source.receive(result, out); EXPECT_EQ(int_decimal(source, out.result().value()), "200010000");
        }
    }

    TEST(ParallelVm, TypeMutationSlotsAndDictionaryStaySafeAcrossCollectingCallbacks)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        auto text = [&](std::u32string name) { make_str(source, std::move(name), out); return roots.root(out.result().value()); };
        TypeSpec spec{U"Shared"}; spec.slots = std::vector<std::u32string>{U"slot", U"__dict__"};
        create_type(source, spec, out); const auto cls = roots.root(out.result().value());
        source.allocate_instance<InstanceObject>(cls, out); const auto instance = roots.root(out.result().value());
        std::array<Handle, 2> methods;
        for (std::size_t i = 0; i < methods.size(); ++i)
        {
            SignatureLayout signature; signature.parameters = {{U"self"}};
            std::array captures{source.builtin(i ? CoreObject::True : CoreObject::False)};
            make_native_function(source, "collecting method", signature,
                [](Attachment &context, Handle fn, std::span<const Value>, ReturnSlot &result) {
                    context.collect(); result.ok(context.payload<NativeFunctionObject>(fn).captures[0]);
                }, captures, out);
            methods[i] = roots.root(out.result().value());
        }
        const auto method = text(U"__bool__"), slot = text(U"slot"), field = text(U"field"), name = text(U"__name__");
        const auto first_name = text(U"First"), second_name = text(U"Second");
        const auto first_field = text(U"first"), second_field = text(U"second");
        set_attribute(source, cls, method, methods[0], out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        std::array values{cls.get(source), instance.get(source), method.get(source), slot.get(source), field.get(source),
                          methods[0].get(source), methods[1].get(source), name.get(source), first_name.get(source), second_name.get(source),
                          first_field.get(source), second_field.get(source)};
        cxxpython::core::make_tuple(source, values, out); const auto packet = roots.root(out.result().value());
        run_shared(interpreter, source, packet, [](Attachment &context, Value packet, std::size_t worker) {
            const auto &values = context.payload<TupleObject>(packet).items; RootScope local(context); ReturnSlot result(context);
            const auto cls = local.root(values[0]), self = local.root(values[1]);
            for (int iteration = 0; iteration < 40; ++iteration)
            {
                const auto index = (iteration + worker) % 2;
                set_attribute(context, cls, local.root(values[2]), local.root(values[5 + index]), result);
                ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                set_attribute(context, cls, local.root(values[7]), local.root(values[8 + index]), result);
                ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                get_attribute(context, self, local.root(values[2]), result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                call(context, local.root(result.result().value()), {}, result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                EXPECT_TRUE(result.result().value() == context.builtin(CoreObject::True) || result.result().value() == context.builtin(CoreObject::False));
                truth(context, self, result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                for (auto field : {values[3], values[4], values[10 + worker]})
                {
                    const auto key = local.root(field), value = local.root(context.builtin(index ? CoreObject::True : CoreObject::False));
                    set_attribute(context, self, key, value, result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                    get_attribute(context, self, key, result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                    EXPECT_TRUE(result.result().value() == context.builtin(CoreObject::True) || result.result().value() == context.builtin(CoreObject::False));
                }
            }
        });
        for (auto field : {first_field, second_field})
        { get_attribute(source, instance, field, out); EXPECT_EQ(out.result().tag(), ResultTag::Ok); }
    }

    TEST(ParallelVm, OppositeTracebackUpdatesCannotCreateACycle)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        raise_error(source, CoreObject::ValueErrorType, U"frame source", out); const auto error = roots.root(out.result().exception());
        CodeBuilder builder(source); builder.emit(Opcode::LoadConst, builder.constant(error)); builder.emit(Opcode::RaiseInstance);
        call(source, function(source, roots, builder), {}, out); ASSERT_EQ(out.result().tag(), ResultTag::Raised);
        source.load(error.get(source), source.payload<ExceptionObject>(error).traceback, out);
        const auto frame = roots.root(source.payload<TracebackObject>(out.result().value()).frame);
        std::array<Handle, 2> nodes;
        for (auto &node : nodes)
        {
            source.allocate_instance<TracebackObject>(roots.root(source.builtin(CoreObject::TracebackType)), out,
                std::optional<Value>{}, frame.get(source), 0, 1);
            node = roots.root(out.result().value());
        }
        make_str(source, U"tb_next", out); const auto name = roots.root(out.result().value());
        std::array values{nodes[0].get(source), nodes[1].get(source), name.get(source)};
        cxxpython::core::make_tuple(source, values, out); const auto packet = roots.root(out.result().value());
        std::atomic<int> committed{0};
        run_shared(interpreter, source, packet, [&](Attachment &context, Value packet, std::size_t worker) {
            const auto &values = context.payload<TupleObject>(packet).items; RootScope roots(context); ReturnSlot result(context);
            set_attribute(context, roots.root(values[worker]), roots.root(values[2]), roots.root(values[1 - worker]), result);
            if (result.result().tag() == ResultTag::Ok) ++committed;
            else { EXPECT_EQ(context.type_of(result.result().exception()), context.builtin(CoreObject::ValueErrorType)); }
            context.collect();
        });
        EXPECT_EQ(committed.load(), 1);
    }

    TEST(ParallelVm, SameFunctionStoresIndependentBindingsInSharedGlobalsDuringGc)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        CodeSpec spec; spec.signature.parameters = {{U"worker"}, {U"value"}}; spec.locals = {U"worker", U"value"};
        CodeBuilder builder(source, spec); const auto left = builder.name(U"left"), right = builder.name(U"right");
        const auto second = builder.label(); builder.emit(Opcode::LoadFast); builder.jump(Opcode::JumpIfFalse, second);
        builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::StoreGlobal, right); builder.emit(Opcode::LoadGlobal, right); builder.emit(Opcode::Return);
        builder.bind(second); builder.emit(Opcode::LoadFast, 1); builder.emit(Opcode::StoreGlobal, left); builder.emit(Opcode::LoadGlobal, left); builder.emit(Opcode::Return);
        const auto target = function(source, roots, builder);
        run_shared(interpreter, source, target, [](Attachment &context, Value fn, std::size_t worker) {
            RootScope roots(context); const auto target = roots.root(fn); ReturnSlot argument(context), result(context);
            for (int i = 0; i < 40; ++i)
            {
                make_int(context, i + 100 * worker, argument);
                std::array args{context.builtin(worker ? CoreObject::True : CoreObject::False), argument.result().value()};
                call(context, target, {args, {}}, result); ASSERT_EQ(result.result().tag(), ResultTag::Ok);
                EXPECT_EQ(result.result().value(), argument.result().value()); context.collect();
            }
        });
        const auto globals = source.payload<FunctionObject>(target).globals;
        EXPECT_EQ(int_decimal(source, *detail::name_lookup(source, globals, U"left")), "39");
        EXPECT_EQ(int_decimal(source, *detail::name_lookup(source, globals, U"right")), "139");
    }

    TEST(ParallelVm, SharedExceptionSurvivesOrdinaryCallsWithAndFinally)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        const auto none = roots.root(source.builtin(CoreObject::None));
        raise_error(source, CoreObject::ValueErrorType, U"shared failure", out); const auto error = roots.root(out.result().exception());
        make_native_function(source, "collect in cleanup", {},
            [](Attachment &context, Handle, std::span<const Value>, ReturnSlot &result) { context.collect(); result.ok(context.builtin(CoreObject::None)); }, {}, out);
        const auto cleanup = roots.root(out.result().value());
        create_type(source, {U"Manager"}, out); const auto cls = roots.root(out.result().value());
        for (bool exit : {false, true})
        {
            SignatureLayout signature; signature.parameters = {{U"self"}};
            if (exit) for (auto name : {U"kind", U"error", U"traceback"}) signature.parameters.push_back({name});
            make_native_function(source, "manager protocol", signature,
                [](Attachment &context, Handle, std::span<const Value> args, ReturnSlot &result) {
                    context.collect();
                    if (args.size() == 4) { EXPECT_EQ(context.type_of(args[3]), context.builtin(CoreObject::TracebackType)); }
                    result.ok(context.builtin(CoreObject::None));
                }, {}, out);
            const auto method = roots.root(out.result().value()); make_str(source, exit ? U"__exit__" : U"__enter__", out);
            set_attribute(source, cls, roots.root(out.result().value()), method, out); ASSERT_EQ(out.result().tag(), ResultTag::Ok);
        }
        source.allocate_instance<InstanceObject>(cls, out); const auto manager = roots.root(out.result().value());
        CodeBuilder leaf(source); leaf.emit(Opcode::LoadConst, leaf.constant(error)); leaf.emit(Opcode::RaiseInstance);
        const auto target = function(source, roots, leaf);
        CodeBuilder builder(source); const auto final = builder.region(RegionKind::Finally), with = builder.region(RegionKind::With);
        builder.enter(final); builder.emit(Opcode::LoadConst, builder.constant(manager)); builder.enter(with); builder.emit(Opcode::PopTop);
        builder.emit(Opcode::LoadConst, builder.constant(target)); builder.call(0); builder.emit(Opcode::PopTop); builder.leave(with); builder.leave(final);
        builder.handler(final); builder.emit(Opcode::LoadConst, builder.constant(cleanup)); builder.call(0); builder.emit(Opcode::PopTop); builder.end_handler(final);
        builder.emit(Opcode::LoadConst, builder.constant(none)); builder.emit(Opcode::Return); const auto shared = function(source, roots, builder);
        std::array values{shared.get(source), error.get(source)}; cxxpython::core::make_tuple(source, values, out);
        const auto packet = roots.root(out.result().value());
        run_shared(interpreter, source, packet, [](Attachment &context, Value packet, std::size_t) {
            RootScope roots(context); ReturnSlot result(context); const auto &values = context.payload<TupleObject>(packet).items;
            const auto target = roots.root(values[0]);
            for (int i = 0; i < 20; ++i)
            {
                call(context, target, {}, result); ASSERT_EQ(result.result().tag(), ResultTag::Raised);
                EXPECT_EQ(result.result().exception(), values[1]); EXPECT_FALSE(context.current_frame()); EXPECT_FALSE(context.handled_exception());
            }
        });
        ReturnSlot trace(source); source.load(error.get(source), source.payload<ExceptionObject>(error).traceback, trace);
        std::size_t count = 0;
        while (trace.result().tag() == ResultTag::Ok)
        {
            const auto node = trace.result().value(); const auto &data = source.payload<TracebackObject>(node);
            EXPECT_EQ(source.payload<FrameObject>(data.frame).state, FrameState::Completed);
            source.load(node, data.next, trace); ASSERT_LE(++count, 80U);
        }
        EXPECT_EQ(count, 80U);
    }

    TEST(ParallelVm, AllocationFreeBytecodeLoopAcknowledgesCollectionAndPublishesFrameLine)
    {
        Interpreter interpreter; Attachment source(interpreter); source.bootstrap_core(); RootScope roots(source); ReturnSlot out(source);
        source.allocate_instance<CellObject>(roots.root(source.builtin(CoreObject::CellType)), out); const auto seen = roots.root(out.result().value());
        source.allocate_instance<CellObject>(roots.root(source.builtin(CoreObject::CellType)), out, source.builtin(CoreObject::False)); const auto stop = roots.root(out.result().value());
        std::array captures{seen.get(source), stop.get(source)};
        make_native_function(source, "observe loop", {},
            [](Attachment &context, Handle fn, std::span<const Value>, ReturnSlot &result) {
                const auto &captures = context.payload<NativeFunctionObject>(fn).captures;
                write_cell(context, captures[0], context.current_frame()); read_cell(context, captures[1], result);
            }, captures, out);
        const auto probe = roots.root(out.result().value()); CodeSpec spec; spec.first_line = 47; CodeBuilder builder(source, spec);
        auto loop = builder.label(); builder.bind(loop); builder.emit(Opcode::LoadConst, builder.constant(probe)); builder.call(0);
        builder.jump(Opcode::JumpIfFalse, loop); builder.emit(Opcode::LoadConst, builder.constant(roots.root(source.builtin(CoreObject::None)))); builder.emit(Opcode::Return);
        const auto target = function(source, roots, builder); auto transfer = source.make_transfer(target);
        auto worker = std::async(std::launch::async, [&, input = std::move(transfer)]() mutable {
            Attachment context(interpreter); RootScope roots(context); ReturnSlot target(context), result(context); context.receive(input, target);
            call(context, roots.root(target.result().value()), {}, result); EXPECT_EQ(result.result().tag(), ResultTag::Ok);
        });
        ReturnSlot frame(source);
        while (!read_cell(source, seen.get(source), frame)) { source.poll(); std::this_thread::yield(); }
        const auto before = interpreter.gc_state().collections; source.collect(); EXPECT_GT(interpreter.gc_state().collections, before);
        make_str(source, U"f_lineno", out); get_attribute(source, roots.root(frame.result().value()), roots.root(out.result().value()), out);
        ASSERT_EQ(out.result().tag(), ResultTag::Ok); EXPECT_EQ(int_decimal(source, out.result().value()), "47");
        write_cell(source, stop.get(source), source.builtin(CoreObject::True));
        { NativeWait wait(source); EXPECT_NO_THROW(worker.get()); }
    }
}
