import sys
import gc

events = []


def local(frame, event, arg):
    if frame.f_code.co_name == "target":
        events.append(event)
        if event == "exception":
            print("exception", arg[0].__name__, str(arg[1]), arg[2] is not None)
        if event == "return":
            print("return", arg)
    return None


def global_trace(frame, event, arg):
    if frame.f_code.co_name == "target":
        print("call", event, frame.f_trace_lines, frame.f_trace_opcodes)
        gc.collect()
        return local
    return None


def target():
    try:
        raise ValueError("oops")
    except ValueError:
        return 42


sys.settrace(global_trace)
print("gettrace", sys.gettrace() is global_trace)
target()
sys.settrace(None)
print("events", events[0], "line" in events, "exception" in events, events[-1])
print("disabled", sys.gettrace() is None)


def broken(frame, event, arg):
    raise RuntimeError("trace failed")


sys.settrace(broken)
try:
    target()
except RuntimeError as error:
    print("failure", str(error), sys.gettrace() is None)


failed_frames = []


def memory_failure(frame, event, arg):
    if event == "call":
        return memory_failure
    failed_frames.append(frame)
    raise MemoryError("trace allocation")


sys.settrace(memory_failure)
try:
    target()
except MemoryError as error:
    print("memory failure", str(error), sys.gettrace() is None, failed_frames[0].f_trace is None)


def quiet(frame, event, arg):
    if frame.f_code.co_name == "target":
        frame.f_trace_lines = False
        frame.f_trace_opcodes = True
        print("flags", frame.f_trace_lines, frame.f_trace_opcodes)
        frame.f_trace_opcodes = False
        return opcode_trace
    return None


def opcode_trace(frame, event, arg):
    if event == "line":
        print("unexpected line")
    return opcode_trace


sys.settrace(quiet)
target()
sys.settrace(None)
