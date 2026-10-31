#include "cxxpython/compiler.hpp"
#include "internal/fileio.hpp"

#include <cerrno>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <unistd.h>

namespace {
using namespace cxxpython::core;

TEST(ParallelFileIO, BlockingReadAndCloseParkWithoutHoldingCollectorOrReusingDescriptor)
{
    int descriptors[2];
    ASSERT_EQ(::pipe(descriptors), 0);
    Interpreter interpreter;
    Attachment owner(interpreter);
    owner.bootstrap_core();
    RootScope roots(owner);
    ReturnSlot out(owner);
    owner.allocate_instance<DictObject>(roots.root(owner.builtin(CoreObject::DictType)), out);
    auto globals = roots.root(out.value());
    SourceUnit source{SourceBuffer("import _io\n"
                                   "f = _io.FileIO(" +
                                   std::to_string(descriptors[0]) +
                                   ", 'r')\n"
                                   "def reader():\n    return f.read(1) == b'x'\n"
                                   "def closer():\n    f.close()\n    return f.closed\n")};
    run(owner, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto reader = roots.root(*detail::name_lookup(owner, globals.get(owner), U"reader"));
    auto closer = roots.root(*detail::name_lookup(owner, globals.get(owner), U"closer"));
    auto file = roots.root(*detail::name_lookup(owner, globals.get(owner), U"f"));
    auto &state = *owner.payload<FileIOObject>(file).state;
    auto start = [&](Handle callable) {
        return std::async(std::launch::async, [input = owner.make_transfer(callable)]() mutable {
            Attachment child(input);
            RootScope local(child);
            ReturnSlot received(child), result(child);
            child.receive(input, received);
            call(child, local.root(received.value()), {}, result);
            return result.tag() == ResultTag::Ok &&
                   result.value() == child.builtin(CoreObject::True);
        });
    };
    auto read = start(reader);
    for (;;) {
        bool reading;
        {
            NativeWait wait(owner);
            std::lock_guard lock(state.mutex);
            reading = state.active == 1;
        }
        if (reading)
            break;
        owner.poll();
    }
    auto close = start(closer);
    for (;;) {
        bool closing;
        {
            NativeWait wait(owner);
            std::lock_guard lock(state.mutex);
            closing = state.fd < 0 && state.active == 1;
        }
        if (closing)
            break;
        owner.poll();
    }
    owner.collect();
    {
        NativeWait wait(owner);
        EXPECT_EQ(::write(descriptors[1], "x", 1), 1);
        EXPECT_TRUE(read.get());
        EXPECT_TRUE(close.get());
    }
    EXPECT_EQ(::close(descriptors[1]), 0);
}

TEST(FileIOContracts, AllocationFailureDuringFlushLookupStillClosesDescriptor)
{
    const int descriptor = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(descriptor, 0);
    Interpreter interpreter;
    Attachment context(interpreter);
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    auto globals = roots.root(out.value());
    SourceUnit source{
        SourceBuffer("import _io\nf = _io.FileIO(" + std::to_string(descriptor) + ")\n")};
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    auto file = roots.root(*detail::name_lookup(context, globals.get(context), U"f"));
    auto descriptor_object =
        roots.root(*detail::type_attribute(context, context.type_of(file), U"close"));
    ASSERT_TRUE(context.has_layout<MethodDescriptorObject>(descriptor_object.get(context)));
    auto close = roots.root(context.payload<MethodDescriptorObject>(descriptor_object).function);
    const auto entry = context.payload<NativeFunctionObject>(close).entry;
    const Value arguments[] = {file.get(context)};
    context.set_allocation_limit(0);
    EXPECT_THROW(entry(context, close, std::span<const Value>(arguments, 1), out), std::bad_alloc);
    context.set_allocation_limit(std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(::fcntl(descriptor, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}
} // namespace
