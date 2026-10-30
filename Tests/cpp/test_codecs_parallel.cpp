#include "cxxpython/compiler.hpp"

#include <future>
#include <gtest/gtest.h>
#include <latch>

namespace {
using namespace cxxpython::core;

TEST(ParallelCodecs, RegistryMutationAndSearchCallbacksCollectInOneSharedHeap)
{
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    SourceUnit source{
        SourceBuffer("import codecs, gc\n"
                     "def encode(text, errors='strict'):\n"
                     "    return text.encode('ascii'), len(text)\n"
                     "def decode(data, errors='strict'):\n"
                     "    return data.decode('ascii'), len(data)\n"
                     "def search(name):\n"
                     "    gc.collect()\n"
                     "    if name == 'shared_codec':\n"
                     "        return codecs.CodecInfo(encode, decode, name='shared-codec')\n"
                     "def unused(name):\n"
                     "    return None\n"
                     "codecs.register(search)\n"
                     "def worker():\n"
                     "    for i in range(40):\n"
                     "        codecs.unregister(unused)\n"
                     "        codecs.register(unused)\n"
                     "        assert codecs.encode('value', 'shared-codec') == b'value'\n"
                     "        assert codecs.decode(b'value', 'shared-codec') == 'value'\n"
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
        for (auto &worker_result : workers)
            EXPECT_TRUE(worker_result.get());
    }
}
} // namespace
