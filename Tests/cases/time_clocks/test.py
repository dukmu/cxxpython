import time

for name in ("time", "monotonic", "perf_counter", "process_time", "thread_time"):
    clock = getattr(time, name)
    clock_ns = getattr(time, name + "_ns")
    info = time.get_clock_info(name)
    print(name, isinstance(clock(), float), isinstance(clock_ns(), int),
          isinstance(info.implementation, str), info.resolution > 0,
          info.monotonic, info.adjustable)

try:
    time.get_clock_info("missing")
except ValueError as error:
    print(type(error).__name__, str(error))

try:
    time.get_clock_info("time\0")
except ValueError as error:
    print(type(error).__name__, str(error))
