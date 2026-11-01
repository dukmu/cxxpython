import ast
import importlib.machinery
import importlib.util
import py_compile

source = '''"module doc"
from __future__ import annotations
events = []
def f(arg: Missing):
    "function doc"
    assert events.append("assert") or False
    return __debug__
class C:
    "class doc"
'''
for level in (0, 1, 2):
    namespace = {}
    exec(compile(source, 'level.py', 'exec', optimize=level), namespace)
    try:
        result = namespace['f'](0)
    except AssertionError:
        result = 'assertion'
    print(level, result, namespace['events'], namespace.get('__doc__'),
          namespace['f'].__doc__, namespace['C'].__doc__,
          namespace['f'].__annotations__['arg'])

tree = ast.parse('assert False\nanswer = __debug__\n')
namespace = {}
exec(compile(tree, 'ast.py', 'exec', optimize=1), namespace)
print('ast', namespace['answer'])

code = compile('def f():\n    assert (x := 1)\n    return x\n',
               'scope.py', 'exec', optimize=1)
namespace = {}
exec(code, namespace)
try:
    namespace['f']()
except UnboundLocalError:
    print('local preserved')

with open('optimized.py', 'w') as stream:
    stream.write('"doc"\nassert False\nanswer = __debug__\n')
for level in (1, 2):
    path = py_compile.compile('optimized.py', optimize=level, doraise=True)
    print(path.endswith('.opt-%s.pyc' % level))
    loader = importlib.machinery.SourcelessFileLoader('optimized', path)
    spec = importlib.util.spec_from_loader('optimized', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    print(module.answer, getattr(module, '__doc__', None))
