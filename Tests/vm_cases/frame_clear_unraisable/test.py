import gc
import sys
import types

events = []
sys.unraisablehook = lambda error: events.append(
    (error.exc_type.__name__, str(error.exc_value), isinstance(error.object, types.FrameType)))

def ignored():
    try:
        yield 1
    except GeneratorExit:
        gc.collect()
        yield 2

generator = ignored()
next(generator)
frame = generator.gi_frame
print('clear', frame.clear())
print('reported', events)
print('resumable', generator.gi_frame is frame, next(generator, 'finished'))
frame.clear()

async def asynchronous():
    yield 1

generator = asynchronous()
try:
    generator.ag_frame.clear()
except NotImplementedError as error:
    print('async gate', str(error))
request = generator.aclose()
try:
    next(request)
except StopIteration:
    print('async closed')
