import time

class Custom:
    pass

class CustomInt(int):
    pass

class StringSubclass(str):
    pass

LongName = type("x" * 60, (), {})
UnicodeName = type("é" * 30, (), {})
SplitName = type("x" * 49 + "é", (), {})

for value in (None, False, 1, 1.0, b"time", bytearray(), [], (), {}, object(),
              Custom(), CustomInt(), Custom, LongName(), UnicodeName(), SplitName()):
    try:
        time.get_clock_info(value)
    except (TypeError, UnicodeDecodeError) as error:
        print(type(error).__name__, str(error))

print(time.get_clock_info(StringSubclass("time")).adjustable)
