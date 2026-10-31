import gc
import re

p = re.compile(r'(?P<word>[a-z]+)(\d+)?')
text = 'abc12 def'
print(p.sub(r'\g<word>:[\2]', text))
print(p.subn(r'<\g<0>>', text, count=1))
print(re.sub(r'(a)?b', r'X\1Y', 'b ab'))
print(re.sub('x*', '-', 'abxd'))
m = p.match(text)
print(m.expand(r'\g<2>/\g<word>/\g<0>'))

def replace(match):
    cycle = []
    cycle.append(cycle)
    gc.collect()
    return match.expand(r'\g<word>') .upper()

print(p.sub(replace, text), p.subn(replace, text))
print(re.sub(b'(a)', br'<\1>', b'aba'))
for fn in (lambda: p.sub(r'\g<missing>', text),
           lambda: p.sub(r'\9', text),
           lambda: p.sub(lambda match: 42, text),
           lambda: p.sub(b'x', text)):
    try:
        fn()
    except Exception as exc:
        print(type(exc).__name__)
