# Generated from CPython 3.12.14 Lib/test/test_tokenize.py.
# PSF license: doc/licenses/python-3.12.14.txt.
# Regenerate with Tools/scripts/generate_tokenize_cases.py.
import _tokenize
import io
import tokenize

SAMPLES = (
    (83, '1 + 1'),
    (88, 'if False:\n    # NL\n    \n    True = False # NEWLINE\n'),
    (108, "if True:\r\n    # NL\r\n    foo='bar'\r\n\r\n"),
    (124, 'x = 1 + \\\r\n1\r\n'),
    (156, '0xff <= 255'),
    (161, '0b10 <= 255'),
    (166, '0o123 <= 0O123'),
    (171, '1234567 > ~0x15'),
    (177, '2134568 != 1231515'),
    (182, '(-124561-1) & 200000000'),
    (192, '0xdeadbeef != -1'),
    (198, '0xdeadc0de & 12345'),
    (203, '0xFF & 0x15 | 1234'),
    (213, 'x = 0'),
    (218, 'x = 0xfffffffffff'),
    (223, 'x = 123141242151251616110'),
    (228, 'x = -15921590215012591'),
    (237, 'x = 3.14159'),
    (242, 'x = 314159.'),
    (247, 'x = .314159'),
    (252, 'x = 3e14159'),
    (257, 'x = 3E123'),
    (262, 'x+y = 3e-1230'),
    (269, 'x = 3.14e159'),
    (301, 'x = \'\'; y = ""'),
    (310, 'x = \'"\'; y = "\'"'),
    (319, 'x = "doesn\'t "shrink", does it"'),
    (326, "x = 'abc' + 'ABC'"),
    (333, 'y = "ABC" + "ABC"'),
    (340, "x = r'abc' + r'ABC' + R'ABC' + R'ABC'"),
    (351, 'y = r"abc" + r"ABC" + R"ABC" + R"ABC"'),
    (363, "u'abc' + U'abc'"),
    (368, 'u"abc" + U"abc"'),
    (374, "b'abc' + B'abc'"),
    (379, 'b"abc" + B"abc"'),
    (384, "br'abc' + bR'abc' + Br'abc' + BR'abc'"),
    (393, 'br"abc" + bR"abc" + Br"abc" + BR"abc"'),
    (402, "rb'abc' + rB'abc' + Rb'abc' + RB'abc'"),
    (411, 'rb"abc" + rB"abc" + Rb"abc" + RB"abc"'),
    (421, '"a\\\nde\\\nfg"'),
    (426, 'u"a\\\nde"'),
    (430, 'rb"a\\\nd"'),
    (434, '"""a\\\nb"""'),
    (438, 'u"""a\\\nb"""'),
    (442, 'rb"""a\\\nb\\\nc"""'),
    (447, 'f"abc"'),
    (452, 'fR"a{b}c"'),
    (461, 'fR"a{{{b!r}}}c"'),

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
