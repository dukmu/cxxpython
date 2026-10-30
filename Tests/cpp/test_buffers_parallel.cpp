#include "cxxpython/compiler.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

TEST(ParallelBuffers, SharedBytearrayMemoryviewSurviveCollectionAndPreventResize)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    SourceUnit source{SourceBuffer("import gc\n"
                                   "data = bytearray(b'abcdefgh')\n"
                                   "view = memoryview(data)\n"
                                   "def worker():\n"
                                   "    for i in range(40):\n"
                                   "        view[:] = b'abcdefgh'\n"
                                   "        data[:] = b'abcdefgh'\n"
                                   "        gc.collect()\n"
                                   "        assert view.tobytes() == b'abcdefgh'\n"
                                   "        assert bytes(data) == b'abcdefgh'\n"
                                   "        try:\n"
                                   "            data.append(33)\n"
                                   "        except BufferError:\n"
                                   "            pass\n"
                                   "        else:\n"
                                   "            assert False\n"
                                   "    return True\n")};
    owner.allocate_instance<DictObject>(roots.root(owner.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    run(owner, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto worker = roots.root(*detail::name_lookup(owner, globals.get(owner), U"worker"));
    std::array transfers{owner.make_transfer(worker), owner.make_transfer(worker)};
    std::array<std::future<bool>, 2> workers;
    std::latch ready(2), start(1);
    {
        NativeWait parked(owner);
        for (unsigned id = 0; id < workers.size(); ++id)
            workers[id] =
                std::async(std::launch::async, [&, input = std::move(transfers[id])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope local(context);
                        ReturnSlot function(context), result(context);
                        context.receive(input, function);
                        {
                            NativeWait wait(context);
                            announced = true;
                            ready.count_down();
                            start.wait();
                        }
                        call(context, local.root(function.value()), {}, result);
                        return result.tag() == ResultTag::Ok &&
                               result.value() == context.builtin(CoreObject::True);
                    } catch (...) {
                        if (!announced)
                            ready.count_down();
                        throw;
                    }
                });
        ready.wait();
        start.count_down();
        for (auto &result : workers)
            EXPECT_TRUE(result.get());
    }
}

TEST(ParallelBuffers, SharedSha256UpdatesAndSnapshotsSurviveCollection)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    SourceUnit source{SourceBuffer("import gc, hashlib\n"
                                   "shared = hashlib.sha256()\n"
                                   "def worker():\n"
                                   "    for i in range(40):\n"
                                   "        shared.update(b'abc')\n"
                                   "        snapshot = shared.copy()\n"
                                   "        gc.collect()\n"
                                   "        assert len(snapshot.digest()) == 32\n"
                                   "        assert len(shared.digest()) == 32\n"
                                   "    return True\n"
                                   "def verify():\n"
                                   "    expected = hashlib.sha256()\n"
                                   "    for i in range(80):\n"
                                   "        expected.update(b'abc')\n"
                                   "    return shared.digest() == expected.digest()\n")};
    owner.allocate_instance<DictObject>(roots.root(owner.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    run(owner, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto worker = roots.root(*detail::name_lookup(owner, globals.get(owner), U"worker"));
    const auto verify = roots.root(*detail::name_lookup(owner, globals.get(owner), U"verify"));
    std::array transfers{owner.make_transfer(worker), owner.make_transfer(worker)};
    std::array<std::future<bool>, 2> workers;
    std::latch ready(2), start(1);
    {
        NativeWait parked(owner);
        for (unsigned id = 0; id < workers.size(); ++id)
            workers[id] =
                std::async(std::launch::async, [&, input = std::move(transfers[id])]() mutable {
                    bool announced = false;
                    try {
                        Attachment context(interpreter);
                        RootScope local(context);
                        ReturnSlot function(context), result(context);
                        context.receive(input, function);
                        {
                            NativeWait wait(context);
                            announced = true;
                            ready.count_down();
                            start.wait();
                        }
                        call(context, local.root(function.value()), {}, result);
                        return result.tag() == ResultTag::Ok &&
                               result.value() == context.builtin(CoreObject::True);
                    } catch (...) {
                        if (!announced)
                            ready.count_down();
                        throw;
                    }
                });
        ready.wait();
        start.count_down();
        for (auto &result : workers)
            EXPECT_TRUE(result.get());
    }
    call(owner, verify, {}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(out.value(), owner.builtin(CoreObject::True));
}
} // namespace
