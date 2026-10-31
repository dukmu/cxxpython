# CPython 3.12.14 Lib/code.py InteractiveConsole policy, with real process input.
import code
import sys

console = code.InteractiveConsole()
console.interact(banner='', exitmsg='')
print('console closed', file=sys.stderr)
print('locals', console.locals['value'], console.locals['twice'](3))
print('last exception', type(sys.last_exc).__name__)
print('buffer', console.buffer)
