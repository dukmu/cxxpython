# API scenarios adapted from CPython 3.12.14 Lib/test/test_code.py.
# PSF license: doc/licenses/python-3.12.14.txt.
import code
import gc
import io
import sys


class Console(code.InteractiveConsole):
    def __init__(self):
        super().__init__()
        self.writes = []
        self.prompts = []
        self.lines = iter(['value = 3', 'def f():', '    return value', '',
                           KeyboardInterrupt(), 'value = f() + 4'])

    def write(self, text):
        gc.collect()
        self.writes.append(text)

    def raw_input(self, prompt):
        gc.collect()
        self.prompts.append(prompt)
        try:
            value = next(self.lines)
        except StopIteration:
            raise EOFError
        if isinstance(value, BaseException):
            raise value
        return value


console = Console()
console.interact(banner='test console', exitmsg='finished')
print('prompts', console.prompts)
print('messages', console.writes)
print('namespace', console.locals['value'], console.locals['f']())
print('buffer', console.buffer)

interpreter = code.InteractiveInterpreter({'__name__': '__console__'})
print('complete', interpreter.runsource('number = 4'))
print('incomplete', interpreter.runsource('if True:'))
print('separate incomplete', interpreter.runsource('if True:\n number = 8'))
print('namespace unchanged', interpreter.locals['number'])
print('completed block', interpreter.runsource('if True:\n number = 8\n'))
print('namespace changed', interpreter.locals['number'])

events = []
previous = sys.excepthook


def hook(kind, value, tb):
    gc.collect()
    events.append((kind.__name__, value.args,
                   tb.tb_frame.f_code.co_name if tb is not None else None))


sys.excepthook = hook
try:
    print('runtime error', interpreter.runsource('raise ValueError("failed")'))
    print('syntax error', interpreter.runsource('number = @'))
finally:
    sys.excepthook = previous
print('hook events', [(kind, args) for kind, args, frame in events])
print('runtime hook frame', events[0][2])
print('last exception', type(sys.last_exc).__name__)
try:
    interpreter.runsource('raise SystemExit(7)')
except SystemExit as error:
    print('SystemExit propagated', error.args)
else:
    raise AssertionError('SystemExit must propagate')

for name in ('InteractiveInterpreter', 'InteractiveConsole', 'interact', 'compile_command'):
    print('export', name, name in code.__all__)
