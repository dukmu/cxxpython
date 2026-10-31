import ast


def run(source, mode='exec'):
    tree = ast.parse(source, mode=mode)
    code = compile(tree, '<ast>', mode)
    if mode == 'eval':
        print(eval(code))
    else:
        namespace = {}
        exec(code, namespace)
        print(namespace['result'])


run('result = [x * x for x in range(8) if x % 2]')
run('result = {x: x + 1 for x in range(3)}')
run('result = sum(x for x in range(5))')
run('def f(a, /, b=2, *, c=3):\n    return a + b + c\nresult = f(1, c=4)')
run('class C:\n    def method(self, x):\n        return x + 1\nresult = C().method(4)')
run('match [1, 2, 3]:\n    case [first, *rest]: result = (first, rest)')
run('try:\n    raise ValueError("bad")\nexcept ValueError as error:\n    result = str(error)')
run("result = f'{3!r:>4}'")
run('(lambda x: x + 1)(2)', 'eval')
run('def outer():\n    x = 1\n    def inner():\n        nonlocal x\n        x += 2\n    inner()\n    return x\nresult = outer()')
run('import sys\nfrom sys import version as v\nresult = v == sys.version')
run('result = 0\nfor x in range(4):\n    result += x\nelse:\n    result += 10')
run('class Manager:\n    def __enter__(self): return 7\n    def __exit__(self, *args): return False\nwith Manager() as value:\n    result = value')
run('values = [1, 2, 3]\ndel values[1]\nresult = values')

tree = ast.Expression(body=ast.BinOp(left=ast.Constant(value=7), op=ast.Mult(), right=ast.Constant(value=6)))
ast.fix_missing_locations(tree)
print(eval(compile(tree, '<ast>', 'eval')))

for value in (-12345678901234567890, 3 + 4j, (1, 'x', None), frozenset((1, 2))):
    tree = ast.fix_missing_locations(ast.Expression(body=ast.Constant(value=value)))
    print(eval(compile(tree, '<ast>', 'eval')) == value)

for source in ('def dec(value): return value\n@dec\ndef f(): pass\n',
               'def dec(value): return value\n@(\n dec\n)\ndef f(): pass\n'):
    for tree in (source, ast.parse(source)):
        namespace = {}
        exec(compile(tree, '<decorated>', 'exec'), namespace)
        print('decorator line', namespace['f'].__code__.co_firstlineno)

tree = ast.parse('def dec(value): return value\n@dec\ndef f(): pass\n')
tree.body[1].decorator_list[0].lineno = 20
tree.body[1].decorator_list[0].end_lineno = 20
namespace = {}
exec(compile(tree, '<non-monotonic-positions>', 'exec'), namespace)
print('independent decorator position', namespace['f'].__code__.co_firstlineno)

for expected, tree in (
    (SyntaxError, ast.parse('f(a=1, a=2)')),
    (ValueError, ast.Expression(body=ast.Name(id='x', ctx=ast.Store()))),
    (ValueError, ast.Expression(body=ast.Tuple(elts=[None], ctx=ast.Load()))),
    (ValueError, ast.Module(body=[None], type_ignores=[])),
    (ValueError, ast.Module(body=[ast.Assign(targets=[ast.Name(id='x', ctx=ast.Load())], value=ast.Constant(value=1))], type_ignores=[])),
    (ValueError, ast.Module(body=[ast.Expr(value=ast.BoolOp(op=ast.And(), values=[ast.Constant(value=True)]))], type_ignores=[])),
    (TypeError, ast.Expression(body=ast.NamedExpr(target=ast.Attribute(value=ast.Name(id='x', ctx=ast.Load()), attr='a', ctx=ast.Store()), value=ast.Constant(value=1)))),
    (ValueError, ast.Module(body=[ast.Match(subject=ast.Constant(value=1), cases=[ast.match_case(pattern=ast.MatchStar(name='x'), body=[ast.Pass()])])], type_ignores=[])),
    (ValueError, ast.Module(body=[ast.Match(subject=ast.Constant(value=1), cases=[ast.match_case(pattern=ast.MatchValue(value=ast.Name(id='x', ctx=ast.Load())), body=[ast.Pass()])])], type_ignores=[])),
):
    ast.fix_missing_locations(tree)
    try:
        compile(tree, '<ast>', 'eval' if isinstance(tree, ast.Expression) else 'exec')
    except (TypeError, ValueError, SyntaxError) as error:
        print('invalid AST rejected', type(error) is expected)
    else:
        print('unexpected acceptance')
