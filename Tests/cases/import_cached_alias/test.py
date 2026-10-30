import sys
from types import ModuleType

parent = ModuleType('plain_alias')
child = ModuleType('plain_alias.child')
child.answer = 42
sys.modules['plain_alias'] = parent
sys.modules['plain_alias.child'] = child
import plain_alias.child
print(plain_alias is parent, hasattr(parent, 'child'))
from plain_alias.child import answer
print(answer)
del sys.modules['plain_alias.child']
try:
    import plain_alias.child
except ModuleNotFoundError:
    print('not a package')
sys.modules['plain_alias.child'] = None
try:
    import plain_alias.child
except ModuleNotFoundError:
    print('halted by None')
