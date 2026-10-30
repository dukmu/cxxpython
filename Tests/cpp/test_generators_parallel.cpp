#include "cxxpython/compiler.hpp"
#include "cxxpython/genobject.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

Handle evaluate(Attachment &context, RootScope &roots, Handle globals, const char *expression)
{
    ReturnSlot out(context);
    SourceUnit unit(SourceBuffer(expression), InputMode::Eval);
    run(context, {&unit, globals}, out);
    if (out.tag() != ResultTag::Ok)
        throw std::runtime_error("generator test expression raised");
    return roots.root(out.value());
}
Handle setup(Attachment &context, RootScope &roots, const char *source)
{
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit unit{SourceBuffer(source)};
    run(context, {&unit, globals}, out);
    if (out.tag() != ResultTag::Ok)
        throw std::runtime_error("generator test setup raised");
    return globals;
}

TEST(ParallelGenerators, SuspendedFramesTransferExceptionStateBetweenActiveWorkers)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    const auto globals = setup(owner, roots,
                               "def f():\n"
                               "    for i in range(100):\n"
                               "        try:\n"
                               "            raise\n"
                               "        except LookupError as e:\n"
                               "            yield (e.args[0], i)\n"
                               "def advance(g, tag):\n"
                               "    try:\n"
                               "        raise LookupError(tag)\n"
                               "    except LookupError:\n"
                               "        return next(g)\n"
                               "g = f()\n");
    const auto packet = evaluate(owner, roots, globals, "(g, advance)");
    std::array inputs{owner.make_transfer(packet), owner.make_transfer(packet)};
    std::array<std::future<void>, 2> workers;
    std::latch ready(2), start(1);
    std::atomic<unsigned> turn{0};
    std::atomic<bool> failed{false};
    {
        NativeWait parked(owner);
        for (unsigned id = 0; id < workers.size(); ++id)
            workers[id] =
                std::async(std::launch::async, [&, id, input = std::move(inputs[id])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope roots(context);
                        ReturnSlot message(context), out(context);
                        context.receive(input, message);
                        const auto &items = context.payload<TupleObject>(message.value()).items;
                        const auto generator = roots.root(items[0]);
                        const auto advance = roots.root(items[1]);
                        const auto tag = roots.root(
                            context.builtin(id ? CoreObject::IntOne : CoreObject::IntZero));
                        announced = true;
                        ready.count_down();
                        start.wait();
                        for (unsigned i = id; i < 100; i += 2) {
                            while (turn.load(std::memory_order_acquire) != id && !failed.load()) {
                                context.poll();
                                std::this_thread::yield();
                            }
                            if (failed.load())
                                return;
                            const Value args[] = {generator.get(context), tag.get(context)};
                            call(context, advance, {args}, out);
                            if (out.tag() != ResultTag::Ok)
                                throw std::runtime_error("cross-thread generator resume raised");
                            const auto &result = context.payload<TupleObject>(out.value()).items;
                            EXPECT_EQ(int_decimal(context, result[0]), std::to_string(id));
                            EXPECT_EQ(int_decimal(context, result[1]), std::to_string(i));
                            EXPECT_FALSE(context.current_frame());
                            EXPECT_FALSE(context.handled_exception());
                            if (i % 11 == 0)
                                context.collect();
                            turn.store(1 - id, std::memory_order_release);
                        }
                    } catch (...) {
                        failed.store(true);
                        if (!announced)
                            ready.count_down();
                        throw;
                    }
                });
        ready.wait();
        EXPECT_EQ(interpreter.gc_state().active, 2U);
        start.count_down();
        for (auto &worker : workers)
            EXPECT_NO_THROW(worker.get());
    }
    const auto result =
        evaluate(owner, roots, globals,
                 "(g.gi_suspended, g.gi_running, g.gi_frame.f_back, g.close(), g.gi_frame)");
    ReturnSlot out(owner);
    repr(owner, result, out);
    EXPECT_EQ(owner.payload<StrObject>(out.value()).value, U"(True, False, None, None, None)");
}

struct ResumeGate {
    ObjectHeader header;
    std::latch *entered, *release;
    ResumeGate(InitialType type, std::latch *entered, std::latch *release)
        : header(type), entered(entered), release(release)
    {}
    void trace(Tracer &) const noexcept {}
};

TEST(ParallelGenerators, RunningResumesRejectInsteadOfWaitingForTheFunctionBody)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    std::latch entered(1), release(1);
    owner.allocate<ResumeGate>(out, &entered, &release);
    const auto gate = roots.root(out.value());
    const Value captures[] = {gate.get(owner)};
    make_native_function(
        owner, "block_generator", {},
        [](Attachment &context, Handle function, std::span<const Value>, ReturnSlot &out) {
            const auto &data = context.payload<ResumeGate>(
                context.payload<NativeFunctionObject>(function).captures[0]);
            data.entered->count_down();
            {
                NativeWait parked(context);
                data.release->wait();
            }
            out.ok(context.builtin(CoreObject::None));
        },
        captures, out);
    const auto block = roots.root(out.value());
    make_str(owner, U"block_generator", out);
    detail::name_store(owner, owner.builtin(CoreObject::Builtins), out.value(), block.get(owner));
    const auto globals = setup(owner, roots,
                               "def f():\n"
                               "    block_generator()\n"
                               "    yield 42\n"
                               "g = f()\n");
    const auto generator = evaluate(owner, roots, globals, "g");
    std::future<Transfer> worker;
    {
        auto input = owner.make_transfer(generator);
        NativeWait parked(owner);
        worker = std::async(std::launch::async, [&, input = std::move(input)]() mutable {
            Attachment context(interpreter);
            RootScope roots(context);
            ReturnSlot received(context), out(context);
            context.receive(input, received);
            const auto generator = roots.root(received.value());
            iter_next(context, generator, out);
            if (out.tag() != ResultTag::Ok)
                throw std::runtime_error("blocked generator raised");
            return context.make_transfer(roots.root(out.value()));
        });
        entered.wait();
    }
    for (const char *expression : {"next(g)", "g.send(None)", "g.throw(ValueError)", "g.close()"}) {
        SCOPED_TRACE(expression);
        SourceUnit unit(SourceBuffer(expression), InputMode::Eval);
        run(owner, {&unit, globals}, out);
        EXPECT_EQ(out.tag(), ResultTag::Raised);
        if (out.tag() == ResultTag::Raised) {
            EXPECT_EQ(owner.type_of(out.exception()), owner.builtin(CoreObject::ValueErrorType));
        }
        owner.collect();
    }
    const auto running = evaluate(owner, roots, globals, "(g.gi_running, g.gi_suspended)");
    repr(owner, running, out);
    EXPECT_EQ(owner.payload<StrObject>(out.value()).value, U"(True, False)");
    release.count_down();
    std::optional<Transfer> result;
    {
        NativeWait parked(owner);
        EXPECT_NO_THROW(result.emplace(worker.get()));
    }
    ASSERT_TRUE(result);
    owner.receive(*result, out);
    EXPECT_EQ(int_decimal(owner, out.value()), "42");
    evaluate(owner, roots, globals, "g.close()");
}
} // namespace
