# CPython 3.12.14 Python/ast_unparse.c is the annotation string reference.
expressions = [
    'None', 'True', 'False', '...', '0xff', '1_000_000', '2.0', '1e-5', '1e16',
    '1e400', '2j', '1e400j', '-0.0', '-2j', '1 + 2j',
    "'hello'", '"a\\nb"', "b'\\x00a'", "'\\u200b'", "'a\\\'b'",
    'a + b * c', '(a + b) * c', 'a - (b - c)', '(a ** b) ** c', 'a ** b ** c',
    'a | b ^ c & d << e', 'not a and b or c', 'a and b and c',
    '(a and b) and c', 'a and (b and c)', '(a < b) < c', 'a is not b not in c',
    'a if b else c if d else e', '(a if b else c) if d else e',
    '(1,)', '()', '(a, b)', '[a, *items]', '{a, *items}', "{'x': a, **kw}",
    'obj.attr', '(1).real', 'obj[a, b:c, ::d]', 'obj[(a,)]', 'obj[()]',
    'f(a, *items, name=b, **kw)', 'f(x for x in values)',
    'lambda: None', 'lambda x=1, /, *args, z=2, **kw: (x, z)',
    'lambda *, x=1: x', 'lambda: (yield 1)', 'lambda: (x := 1)',
    '[x + 1 for x in values if x if test(x)]',
    '{x for x in values for y in other if y}',
    '{x: y for x, y in values}', '(x for x in values)',
    '[await a for a in b]', '[a async for a in b]', 'lambda: (await value)',
    'f"{name}"', 'f"{name:}"', 'f"{name!r:>{width}}"',
    'f"{{text}} { value = }"', 'f"{ {1: 2} }"', 'f"{(lambda x: x)(1)}"',
]
for item in expressions:
    namespace = {}
    code = compile('from __future__ import annotations\nx: ' + item,
                   '<annotation>', 'exec', dont_inherit=True)
    exec(code, namespace)
    print(namespace['__annotations__']['x'])

for item in ('(x := 1)', '(yield 1)', '(yield from values)', '(await value)'):
    try:
        compile('from __future__ import annotations\nx: ' + item,
                '<annotation>', 'exec', dont_inherit=True)
    except SyntaxError as error:
        print(type(error).__name__, error.msg)
