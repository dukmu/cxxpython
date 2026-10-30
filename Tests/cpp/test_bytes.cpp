#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class Bytes : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    void check(std::string source)
    {
        SourceUnit unit{SourceBuffer(std::move(source))};
        ReturnSlot out(context);
        run(context, {&unit}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
};
TEST_F(Bytes, LiteralsProtocolsAndNativeSubtypeTail)
{
    check(R"(assert b'\x00\xff' == bytes([0, 255])
assert b'\777' == b'\xff'
assert br'\u1234' == b'\\u1234'
assert b'a' b'b' == b'ab'
assert len(b'\0\xff') == 2
assert b'abc'[1] == 98
assert b'abc'[::-1] == b'cba'
x = b'abc'
assert x[:] is x
assert b'x' * 3 == 3 * b'x' == b'xxx'
assert b'ab' + b'cd' == b'abcd'
assert 255 in b'\xff' and b'' in b'x'
assert b'\xff' > b'\x7f'
assert b'a' != 'a'
assert {b'a': 42}[bytes([97])] == 42
assert tuple(b'\0\xff') == (0, 255)
assert b':'.join([b'a', b'b']) == b'a:b'
assert b'\x01\xff'.hex() == '01ff'
assert b'12345'.hex(':', 2) == '31:3233:3435'
assert b'12345'.hex(':', -2) == '3132:3334:35'
assert bytes.fromhex('01 ff\t80') == b'\x01\xff\x80'
class B(bytes):
    pass
value = B([1, 2, 3])
value.data = (777, 888)
assert type(value) is B
assert value.data[1] == 888
assert type(value[:]) is bytes
assert type(B.fromhex('12')) is B
assert type(bytes(value)) is bytes
)");
}
TEST_F(Bytes, ConstructionOrderAndErrors)
{
    check(R"(events = []
class Source:
    def __bytes__(self):
        events.append('bytes')
        return b'custom'
    def __index__(self):
        raise AssertionError('index before bytes')
assert bytes(Source()) == b'custom'
assert events == ['bytes']
class Count:
    def __index__(self): return 3
assert bytes(Count()) == b'\0\0\0'
class Item:
    def __index__(self): return 255
assert bytes([Item()]) == b'\xff'
assert bytes(source=[1, 2]) == b'\x01\x02'
try:
    bytes(9223372036854775807)
except OverflowError:
    pass
else:
    raise AssertionError('oversized count accepted')
try:
    bytes.fromhex('0g')
except ValueError as error:
    assert str(error) == 'non-hexadecimal number found in fromhex() arg at position 1'
else:
    raise AssertionError('invalid hex accepted')
for source in ([-1], [256], [2**200]):
    try:
        bytes(source)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid byte accepted')
try:
    bytes(-1)
except ValueError:
    pass
else:
    raise AssertionError('negative count accepted')
for source in (None, 'text'):
    try:
        bytes(source)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid source accepted')
)");
}
} // namespace
