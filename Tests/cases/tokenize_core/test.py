import io
import token
import tokenize


def show(label, tokens):
    print(label)
    for item in tokens:
        print(repr((token.tok_name[item.type], item.string, item.start,
                    item.end, item.line, token.tok_name[item.exact_type])))


source = '# coding: utf-8\r\n# comment\r\nif café:\r\n    π = (1 +\r\n         2)  # sum\r\n\r\n    pass\r\nlast = café'
byte_tokens = list(tokenize.tokenize(io.BytesIO(source.encode('utf-8')).readline))
text_tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
show('bytes', byte_tokens)
show('text', text_tokens)
print('untokenize bytes', repr(tokenize.untokenize(byte_tokens)))
print('untokenize text', repr(tokenize.untokenize(text_tokens)))
pairs = [(t.type, t.string) for t in text_tokens]
rebuilt = tokenize.untokenize(pairs)
print('untokenize pairs', repr(rebuilt))
print('pair roundtrip', pairs == [(t.type, t.string) for t in
                                 tokenize.generate_tokens(io.StringIO(rebuilt).readline)])
for op in ('+', '+=', '**', '**=', '...', ':=', '->', '@', '!='):
    info = tokenize.TokenInfo(token.OP, op, (1, 0), (1, len(op)), op)
    print('exact', repr((op, token.tok_name[info.exact_type])))
print('unknown exact', tokenize.TokenInfo(token.OP, '?', (1, 0), (1, 1), '?').exact_type == token.OP)
