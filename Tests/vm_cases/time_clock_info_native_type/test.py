# Native C diagnostic names still use the short TypeView name. Remove this
# difference fixture when native qualified diagnostic metadata is supported.
import _io
import time

try:
    time.get_clock_info(_io.BytesIO())
except TypeError as error:
    print(type(error).__name__, str(error))
