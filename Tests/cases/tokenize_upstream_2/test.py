# Generated from CPython 3.12.14 Lib/test/test_tokenize.py.
# PSF license: doc/licenses/python-3.12.14.txt.
# Regenerate with Tools/scripts/generate_tokenize_cases.py.
import _tokenize
import io
import tokenize

SAMPLES = (
    (473, 'f"{{{1+1}}}"'),
    (484, 'f"""{f\'\'\'{f\'{f"{1+1}"}\'}\'\'\'}"""'),
    (505, 'f"""     x\nstr(data, encoding={invalid!r})\n"""'),
    (516, 'f"""123456789\nsomething{None}bad"""'),
    (525, 'f"""abc"""'),
    (530, 'f"abc\\\ndef"'),
    (536, 'Rf"abc\\\ndef"'),
    (542, "f'some words {a+b:.3f} more words {c+d=} final words'"),
    (562, "f'''{\n3\n=}'''"),
    (575, "f'''__{\n    x:a\n}__'''"),
    (590, "f'''__{\n    x:a\n    b\n     c\n      d\n}__'''"),
    (608, "f'__{\n    x:d\n}__'"),
    (625, "    '''Autorzy, kt\xf3rzy t\u0105 jednostk\u0119 maj\u0105 wpisani jako AKTUALNA -- czyli\n    aktualni pracownicy, obecni pracownicy'''\n"),
    (636, 'def d22(a, b, c=2, d=2, *k): pass'),
    (658, 'def d01v_(a=1, *k, **w): pass'),
    (675, 'def d23(a: str, b: int=3) -> int: pass'),
    (697, 'if 1 < 1 > 1 == 1 >= 5 <= 0x15 <= 0x12 != 1 and 5 in 1 not in 1 is 1 or 5 is not 1: pass'),
    (735, 'x = 1 << 1 >> 5'),
    (747, 'x = 1 - y + 15 - 1 + 0x124 + z + a[5]'),
    (770, 'x = 1//1*1/5*12%0x12@42'),
    (790, '~1 ^ 1 & 1 |1 ^ -1'),
    (803, '-1*1/1+1*1//1 - ---1**1'),
    (827, "import sys, time\nx = sys.modules['time'].time()"),
    (849, '@staticmethod\ndef foo(x,y): pass'),
    (866, 'def f():\n\tif x\n        \tpass'),
    (887, "\xd6rter = 'places'\ngr\xfcn = 'green'"),
    (899, "\xd6rter = u'places'\ngr\xfcn = U'green'"),
    (911, 'async = 1'),
    (917, 'a = (async = 1)'),
    (927, 'async()'),
    (933, 'class async(Bar):pass'),
    (943, 'class async:pass'),
    (950, 'await = 1'),
    (956, 'foo.async'),
    (962, 'async for a in b: pass'),
    (972, 'async with a as b: pass'),
    (982, 'async.foo'),
    (988, 'async'),
    (992, 'async\n#comment\nawait'),
    (1000, 'async\n...\nawait'),
    (1008, 'async\nawait'),
    (1014, 'foo.async + 1'),
    (1022, 'async def foo(): pass'),
    (1032, 'async def foo():\n  def foo(await):\n    await = 1\n  if 1:\n    await\nasync += 1\n'),
    (1076, 'async def foo():\n  async for i in 1: pass'),
    (1097, 'async def foo(async): await'),
    (1108, 'def f():\n\n  def baz(): pass\n  async def bar(): pass\n\n  await = 2'),
    (1145, 'async def f():\n\n  def baz(): pass\n  async def bar(): pass\n\n  await = 2'),

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
