import ast


source = 'def f(*args: *Ts):\n    return args\n'
for tree in (source, ast.parse(source)):
    namespace = {'Ts': (int,)}
    exec(compile(tree, '<annotations>', 'exec'), namespace)
    function = namespace['f']
    print(function.__annotations__, function(1, 2))

for values in ((), (int, str), 42):
    try:
        exec(source, {'Ts': values})
    except (TypeError, ValueError) as error:
        print(type(error).__name__)
    else:
        raise AssertionError('star annotation must unpack exactly one value')

namespace = {}
exec('from __future__ import annotations\n' + source, namespace)
print(namespace['f'].__annotations__)

events = []
class Annotation:
    def __iter__(self):
        events.append('iter')
        yield str

namespace = {'Ts': Annotation()}
exec(source, namespace)
print(events, namespace['f'].__annotations__)
