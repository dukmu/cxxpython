import struct

for fmt in ('', 'i', '3i', '@ci', '=ci', '<ci', '>ci', '!ci', '@b0i', '0s', '12p', '@nNP', 'efd'):
    print(fmt, struct.calcsize(fmt))
for fmt in ('Z', '<P', '2', '1 i'):
    try:
        struct.calcsize(fmt)
    except struct.error as exc:
        print(type(exc).__name__, str(exc))
