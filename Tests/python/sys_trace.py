import sys

events = []


def tracer(frame, event, arg):
    if frame.f_code.co_name == 'target':
        events.append(event)
        if event == 'line':
            events.append(frame.f_lineno > 0)
        if event == 'return':
            events.append(arg == 3)
    if frame.f_code.co_name == 'fail':
        events.append(event)
        if event == 'line':
            events.append(frame.f_lineno > 0)
        if event == 'exception':
            events.append(arg[0] is TypeError)
            events.append(arg[2].tb_frame is frame)
    return tracer


def target(value):
    result = value + 1
    return result


def fail():
    len(1)


print(sys.gettrace() is None)
print(sys.settrace(tracer))
print(sys.gettrace() is tracer)
print(target(2))
try:
    fail()
except TypeError:
    pass
sys.settrace(None)
print(sys.gettrace() is None)
print(events.__contains__('call'))
print(events.__contains__('line'))
print(events.__contains__('return'))
print(events.__contains__('exception'))
print(events.__contains__(True))
