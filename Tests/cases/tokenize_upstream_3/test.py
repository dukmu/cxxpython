# Generated from CPython 3.12.14 Lib/test/test_tokenize.py.
# PSF license: doc/licenses/python-3.12.14.txt.
# Regenerate with Tools/scripts/generate_tokenize_cases.py.
import _tokenize
import io
import tokenize

SAMPLES = (
    (1184, '[\n    # A comment here\n    1\n]\n'),
    (1201, '); x'),
    (1208, "a = f'''\n    Autorzy, kt\xf3rzy t\u0105 jednostk\u0119 maj\u0105 wpisani jako AKTUALNA -- czyli'''"),
    (1219, "f'''\n    \U0001f517 This is a test {test_arg1}\U0001f517\n\U0001f517'''"),
    (2108, '0xff <= 255'),
    (2114, '0b10 <= 255'),
    (2120, '0o123 <= 0O123'),
    (2126, '1234567 > ~0x15'),
    (2133, '2134568 != 1231515'),
    (2139, '(-124561-1) & 200000000'),
    (2150, '0xdeadbeef != -1'),
    (2157, '0xdeadc0de & 12345'),
    (2163, '0xFF & 0x15 | 1234'),
    (2173, 'x = 3.14159'),
    (2179, 'x = 314159.'),
    (2185, 'x = .314159'),
    (2191, 'x = 3e14159'),
    (2197, 'x = 3E123'),
    (2203, 'x+y = 3e-1230'),
    (2211, 'x = 3.14e159'),
    (2219, 'x = \'\'; y = ""'),
    (2229, 'x = \'"\'; y = "\'"'),
    (2239, 'x = "doesn\'t "shrink", does it"'),
    (2247, "x = 'abc' + 'ABC'"),
    (2255, 'y = "ABC" + "ABC"'),
    (2263, "x = r'abc' + r'ABC' + R'ABC' + R'ABC'"),
    (2275, 'y = r"abc" + r"ABC" + R"ABC" + R"ABC"'),
    (2287, "u'abc' + U'abc'"),
    (2293, 'u"abc" + U"abc"'),
    (2299, "b'abc' + B'abc'"),
    (2305, 'b"abc" + B"abc"'),
    (2311, "br'abc' + bR'abc' + Br'abc' + BR'abc'"),
    (2321, 'br"abc" + bR"abc" + Br"abc" + BR"abc"'),
    (2331, "rb'abc' + rB'abc' + Rb'abc' + RB'abc'"),
    (2341, 'rb"abc" + rB"abc" + Rb"abc" + RB"abc"'),
    (2351, '"a\\\nde\\\nfg"'),
    (2355, 'u"a\\\nde"'),
    (2359, 'rb"a\\\nd"'),
    (2363, '"""a\\\nb"""'),
    (2367, 'u"""a\\\nb"""'),
    (2371, 'rb"""a\\\nb\\\nc"""'),
    (2377, '"hola\\\\\\r\\ndfgf"'),
    (2381, 'f"abc"'),
    (2387, 'fR"a{b}c"'),
    (2397, 'f"""abc"""'),
    (2403, 'f"abc\\\ndef"'),
    (2410, 'f"{\na}"'),
    (2420, 'Rf"abc\\\ndef"'),

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
