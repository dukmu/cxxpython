import io
import token
import tokenize

for source in ("x = '\\x0'", "x = '\\N{LATIN SMALL LETTER A'",
               "x = b'\\\u00e9'", "x = b'\u00e9'", "x = f'\\x0'"):
    print([token.tok_name[item.type] for item in tokenize.generate_tokens(io.StringIO(source).readline)])
    try:
        compile(source, '<literal>', 'exec')
    except SyntaxError as exc:
        print(type(exc).__name__)
print(eval("'\\N{LATIN SMALL LETTER A}\\N{NULL}'"))
print(repr(eval("r'\\N{LATIN SMALL LETTER A'")))
print(eval("f'{{{{x}}}}'"))
print(repr(eval("'''one\r\ntwo'''")))
