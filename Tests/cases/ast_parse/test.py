import ast


samples = [
    ("exec", "x = a + b * c ** -d\ny: list[int] = []\nx += 2\ndel x, a[1:]"),
    ("eval", "(a and b or not c, x < y is not z not in q, a if b else c)"),
    ("eval", "(x := f(1, *a, k=2, **kw)).attr[1:2:3, ...]"),
    ("eval", "{**a, 'x': b}, {x for x in xs if x}, [x async for x in xs], (x for x in xs)"),
    ("eval", "lambda a, /, b=1, *c, d, e=2, **kw: (a, b, c, d, e, kw)"),
    ("exec", "@decorator(1)\nasync def f[T: int, *Ts, **P](a: T, /, b=1, *args, c, d=2, **kw) -> T:\n    return await g(a)\n"),
    ("exec", "@dec\nclass C[T](Base, metaclass=Meta, **kw):\n    type Alias[U] = tuple[T, U]\n"),
    ("exec", "for a, *b in xs:\n    if a: break\n    elif b: continue\n    else: pass\nelse:\n    while test:\n        test -= 1\n"),
    ("exec", "async def f():\n    async for x in xs:\n        async with cm() as y, other():\n            yield x\n    yield from xs\n"),
    ("exec", "try:\n    raise E() from cause\nexcept* ValueError as e:\n    assert e, 'bad'\nelse:\n    pass\nfinally:\n    cleanup()\n"),
    ("exec", "import a.b as c, d\nfrom ...pkg.sub import x as y, z\nglobal g, h\nnonlocal n\n"),
    ("exec", "match subject:\n    case None | True: pass\n    case [first, *rest]: pass\n    case {'x': value, **rest}: pass\n    case Point(x, y=var) as p if var: pass\n    case _: pass\n"),
    ("eval", "f'prefix {value=!r:>{width}} suffix' f'{other!s}'"),
    ("eval", "(None, True, False, ..., 0xff, 1.25, 2j, b'bytes', u'text')"),
    ("exec", "\u00e9 = '\u4e2d'\n\u7ed3\u679c = \u00e9[0]\n"),
    ("single", "x = 1; print(x)\n"),
    ("func_type", "(int, *str, **bytes) -> list[int]"),
    ("exec", "return 1\nbreak\ncontinue\nnonlocal missing\n"),
    ("exec", "def duplicate(x, x):\n    return x\n"),
    ("exec", "a = b = 1\n(a, [b, *c]) = values\nx.y += 1\nx[0]: T = v\n(x): T\ndel (x, y[2], obj.attr)\n"),
    ("eval", "(), [], {}, {1, 2}, (1,), [*items], {'x': 1, **kw}, a[1, *xs], a[:]"),
    ("exec", "def f(a, b=1, /, c=2, *args: int, d, e=3, **kw: str):\n    pass\n"),
    ("exec", "def f(*, a, b=1):\n    pass\n\ndef g(a, /, **kw):\n    pass\n"),
    ("eval", "[x+y for x in a if x for y in b if y if z]"),
    ("eval", "{k: v for k, v in items if k}, f(x for x in values)"),
    ("exec", "match x, y:\n    case 1 | -2 | 1+2j: pass\n    case (a, b): pass\n    case C(a, b=c): pass\n    case {'a': x, 1: y}: pass\n"),
    ("exec", "try:\n    f()\nexcept E:\n    pass\nexcept:\n    pass\nfinally:\n    pass\n"),
    ("exec", "from . import *\nfrom ..foo import (bar, baz as qux)\n"),
    ("eval", "f'{a}' f'{b!a}' 'z' f'{c:{w}.{p}f}'"),
    ("eval", "r'abc' 'def', b'a' b'b', u'a', 0xdead, .5, 3e2"),
    ("func_type", "() -> None"),
    ("func_type", "(int, str, *bytes, **float) -> tuple"),
    ("single", "if a:\n    b = 1\n"),
    ("exec", "async def f():\n    x = yield\n    yield from (1, 2)\n    return (yield 2)\n"),
    ("eval", "u'' 'x', '' u'x', U'x', b'' b'x', f'' 'x', f'{{{x=}}}'"),
    ("eval", "f'{x = } {y=!s} {z=:>{width}}'"),
    ("exec", "match item:\n    case [*_, x]: pass\n    case {'a': x, **rest}: pass\n    case pkg.C(a, b=x) | pkg.C(a, b=x): pass\n"),
    ("exec", "with (a() as (x, y), b() as obj.attr):\n    pass\n"),
    ("eval", "f(1, *xs, key=2, **kw, other=3)"),
    ("eval", "await worker()"),
    ("exec", "await worker()\nasync for x in xs:\n    pass\nasync with lock:\n    pass\n"),
    ("exec", "def f[*Ts](*args: *Ts):\n    pass\n"),
]

for mode, source in samples:
    print(mode, repr(source))
    tree = ast.parse(source, mode=mode)
    print(ast.dump(tree, include_attributes=True))

for source in ("b'' 'text'", "f'{x!q}'", "match x:\n    case 1+2: pass\n"):
    try:
        ast.parse(source)
    except SyntaxError as error:
        print(type(error).__name__)
    else:
        print("unexpected success")
