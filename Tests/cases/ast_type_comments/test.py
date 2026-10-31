# Type-comment cases follow CPython 3.12.14 Lib/test/test_type_comments.py.
# PSF license: doc/licenses/python-3.12.14.txt.
import ast


sources = [
    'a = 0  # type: int\na = b = 1  # type: int\n(a, b) = (1, 2)  # type: tuple[int, int]\n',
    'for a in []:  # type: int\n    pass\nelse:\n    pass\n',
    'with context() as a:  # type: int\n    pass\n',
    'with a() as x, b() as y:  # type: tuple[A, B]\n    pass\n',
    'def foo():\n    # type: () -> int\n    pass\n\ndef bar():  # type: () -> None\n    pass\n',
    'async def foo():\n    # type: () -> int\n    return await bar()\n\nasync def bar():  # type: () -> int\n    return await bar()\n',
    'async def foo(xs):\n    async for a in xs:  # type: int\n        pass\n    async with context() as a:  # type: int\n        pass\n',
    'def foo():\n    # type: () -> \u00e0\u00e7\u010d\u00e9\u00f1t\n    pass\n',
    'def foo(\n    a: int = 1,  # type: A\n    /,\n    b = 2,  # type: B\n    *args: *Ts,  # type: V\n    c = 3,  # type: C\n    d,  # type: D\n    **kw: str  # type: K\n):  # type: (...) -> Result\n    pass\n',
    'def foo():\n    # before the signature\n\n    # type: () -> int\n    pass\n',
    '# type: ignore\nx = 1  # type: ignore[attr-defined]\n# type: ignore=excuse\n# type: ignore [excuse]\n# type: ignore whatever\n',
    'def foo():\n    pass  # type: ignore\n\ndef bar():\n    x = 1  # type: ignore\n\ndef baz():\n    pass  # type: ignore[excuse]\n    pass  # type: ignore=excuse\n    pass  # type: ignore [excuse]\n    x = 1  # type: ignore whatever\n',
    'x = 1  # type:\ny = 2  # type:  int  \n',
    '\u00e9 = 1  # type: \u7c7b\u578b\n\u00e9 = 2  # type: ignore[\u8bf4\u660e]\n',
    'def type():  # type: () -> int\n    return 1\n',
    'if True:\n    x = 1  # type: int\n    # type: ignore[tag]\n# type: ignore[end]\n',
    '# type: ignore[only]\n# type: ignore\n',
    'def foo(\n    a,  # type: ignore[arg]\n    b  # type: B\n):  # type: ignore[function]\n    pass\n',
]

# All long-form argument arrangements in the fixed upstream tests, including
# positional-only parameters, a trailing comma, and comments before ')'.
long_arguments = [
    'a = 1,  # type: A\n',
    'a = 1  # type: A\n',
    'a = 1,  # type: A\n    /\n',
    'a,  # type: A\n    b,  # type: B\n',
    'a,  # type: A\n    /,\n    b,  # type: B\n',
    'a,  # type: A\n    b  # type: B\n',
    '*v,  # type: V\n',
    '*v  # type: V\n',
    '**k,  # type: K\n',
    '**k  # type: K\n',
    '*v,  # type: V\n    **k,  # type: K\n',
    '*v,  # type: V\n    **k  # type: K\n',
    'a,  # type: A\n    *v,  # type: V\n',
    'a,  # type: A\n    /,\n    *v,  # type: V\n',
    'a,  # type: A\n    *v  # type: V\n',
    'a,  # type: A\n    **k,  # type: K\n',
    'a,  # type: A\n    /,\n    **k,  # type: K\n',
    'a,  # type: A\n    **k  # type: K\n',
    'a,  # type: A\n    *v,  # type: V\n    **k,  # type: K\n',
    'a,  # type: A\n    /,\n    *v,  # type: V\n    **k,  # type: K\n',
    'a,  # type: A\n    *v,  # type: V\n    **k  # type: K\n',
]
for args in long_arguments:
    sources.append('def foo(\n    ' + args + '):\n    pass\n')

for index, source in enumerate(sources):
    tree = ast.parse(source, type_comments=True)
    print('case', index)
    print(ast.dump(tree, include_attributes=True))
    from_flags = compile(source, '<types>', 'exec', ast.PyCF_ONLY_AST | ast.PyCF_TYPE_COMMENTS)
    assert ast.dump(from_flags, include_attributes=True) == ast.dump(tree, include_attributes=True)
    from_ast = compile(tree, '<types>', 'exec')
    from_source = compile(source, '<types>', 'exec', ast.PyCF_TYPE_COMMENTS)
    print('compiled', from_ast.co_names == from_source.co_names)
    ordinary = ast.parse(source, type_comments=False)
    assert not ordinary.type_ignores
    for node in ast.walk(ordinary):
        if hasattr(node, 'type_comment'):
            assert node.type_comment is None

invalid_sources = [
    'pass  # type: int\n',
    'foo()  # type: int\n',
    'x += 1  # type: int\n',
    'x: int = 1  # type: int\n',
    'while True:  # type: int\n    continue\n',
    'while True:\n    continue  # type: int\n',
    'try:  # type: int\n    pass\nfinally:\n    pass\n',
    'try:\n    pass\nfinally:  # type: int\n    pass\n',
    'pass  # type: ignorewhatever\n',
    'pass  # type: ignore\u00e9\n',
    'def foo():  # type: () -> int\n    # type: () -> str\n    return ""\n',
    'async def foo():  # type: () -> int\n    # type: () -> str\n    return 1\n',
    'def foo():\n    # type: () -> int\n    # type: () -> str\n    pass\n',
    'def foo(\n    *,  # type: int\n    x\n):\n    pass\n',
    'def foo(): pass  # type: () -> int\n',
    '# type: int\nx = 1\n',
    'with (context() as a):  # type: int\n    pass\n',
    'async def foo():\n    async with (context() as a):  # type: int\n        pass\n',
]
for index, source in enumerate(invalid_sources):
    ast.parse(source, type_comments=False)
    print('invalid', index)
    for flags in (ast.PyCF_ONLY_AST | ast.PyCF_TYPE_COMMENTS, ast.PyCF_TYPE_COMMENTS):
        try:
            compile(source, '<types>', 'exec', flags)
        except SyntaxError as error:
            print(type(error).__name__, error.msg)
        else:
            raise AssertionError('invalid type comment accepted')

executable = '''\
total = 0  # type: int
for value in range(4):  # type: int
    total += value
def add(value):  # type: (int) -> int
    return total + value
answer = add(4)  # type: ignore[tag]
'''
for program in (ast.parse(executable, type_comments=True), executable):
    namespace = {}
    exec(compile(program, '<types>', 'exec', ast.PyCF_TYPE_COMMENTS), namespace)
    print('executed', namespace['answer'])

single = compile('x = 1  # type: int\n', '<types>', 'single', ast.PyCF_ONLY_AST | ast.PyCF_TYPE_COMMENTS)
print('single', ast.dump(single, include_attributes=True))
print('eval', ast.dump(ast.parse('1  # type: ignore[tag]', mode='eval', type_comments=True)))
print('function type', ast.dump(ast.parse('(int, *str, **Any) -> float', mode='func_type', type_comments=True)))
