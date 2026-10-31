# Generated from CPython 3.12.14 Lib/test/test_tokenize.py.
# PSF license: doc/licenses/python-3.12.14.txt.
# Regenerate with Tools/scripts/generate_tokenize_cases.py.
import _tokenize
import io
import tokenize

SAMPLES = (
    (2427, 'f"hola\\\\\\r\\ndfgf"'),
    (2433, "f'''__{\n    x:a\n}__'''"),
    (2448, "f'''__{\n    x:a\n    b\n     c\n      d\n}__'''"),
    (2466, "f'__{\n    x:d\n}__'"),
    (2483, 'def d22(a, b, c=2, d=2, *k): pass'),
    (2506, 'def d01v_(a=1, *k, **w): pass'),
    (2524, 'def d23(a: str, b: int=3) -> int: pass'),
    (2546, 'if 1 < 1 > 1 == 1 >= 5 <= 0x15 <= 0x12 != 1 and 5 in 1 not in 1 is 1 or 5 is not 1: pass'),
    (2584, 'x = 1 - y + 15 - 1 + 0x124 + z + a[5]'),
    (2607, 'x = 1//1*1/5*12%0x12@42'),
    (2627, '~1 ^ 1 & 1 |1 ^ -1'),
    (2641, '-1*1/1+1*1//1 - ---1**1'),
    (2665, "import sys, time\nx = sys.modules['time'].time()"),
    (2687, '@staticmethod\ndef foo(x,y): pass'),
    (2704, '@staticmethod\ndef foo(x,y): pass'),
    (2721, 'async = 1'),
    (2727, 'a = (async = 1)'),
    (2737, 'async()'),
    (2743, 'class async(Bar):pass'),
    (2753, 'class async:pass'),
    (2760, 'await = 1'),
    (2766, 'foo.async'),
    (2772, 'async for a in b: pass'),
    (2782, 'async with a as b: pass'),
    (2792, 'async.foo'),
    (2798, 'async'),
    (2802, 'async\n#comment\nawait'),
    (2808, 'async\n...\nawait'),
    (2816, 'async\nawait'),
    (2822, 'foo.async + 1'),
    (2830, 'async def foo(): pass'),
    (2840, 'async def foo():\n  def foo(await):\n    await = 1\n  if 1:\n    await\nasync += 1\n'),
    (2884, 'async def foo():\n  async for i in 1: pass'),
    (2903, 'async def foo(async): await'),
    (2914, 'def f():\n\n  def baz(): pass\n  async def bar(): pass\n\n  await = 2'),
    (2949, 'async def f():\n\n  def baz(): pass\n  async def bar(): pass\n\n  await = 2'),
    (2987, "\xd6rter = u'places'\ngr\xfcn = U'green'"),

)

for line, source in SAMPLES:
    for mode in ('text', 'bytes', 'native'):
        if mode == 'text':
            iterator = tokenize.generate_tokens(io.StringIO(source).readline)
        elif mode == 'bytes':
            iterator = tokenize.tokenize(io.BytesIO(source.encode()).readline)
        else:
            iterator = _tokenize.TokenizerIter(io.StringIO(source).readline, extra_tokens=False)
        values = []
        try:
            for item in iterator:
                values.append(tuple(item))
            error = None
        except Exception as exc:
            error = (type(exc).__name__, exc.args)
        print(line, mode, repr((values, error)))
