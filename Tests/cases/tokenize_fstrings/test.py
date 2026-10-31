import io
import token
import tokenize


sources = [
    'f"value={x!r:>{width}.{precision}f}"\n',
    'f"{f\'{x=}\'} {x = !s}"\n',
    'rf"path\\name {{literal}} {value:{width}}"\n',
    'f"""first {\n    data["key"] # comment\n    }\nlast {{ok}}"""\n',
    'f"{x:{y:{z}}}"\n',
]
for source in sources:
    print('source', repr(source))
    try:
        items = list(tokenize.generate_tokens(io.StringIO(source).readline))
        for item in items:
            print(repr((token.tok_name[item.type], item.string, item.start,
                        item.end, item.line, token.tok_name[item.exact_type])))
        print('untokenize', repr(tokenize.untokenize(items)))
    except Exception as exc:
        print('error', repr((type(exc).__name__, exc.args)))
