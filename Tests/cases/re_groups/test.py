import re


def show(pattern, text):
    m = re.search(pattern, text)
    print(None if m is None else (m.group(), m.groups(), m.span()))


show(r"(a)?(?(1)b|c)", "ab c")
show(r"(a)?(?(1)b|c)", "c")
show(r"(?P<x>\w+)-(?P=x)", "abc-abc")
show(r"(ab)\1", "abab")
show(r"(?<=a)b(?=c)", "abc")
show(r"(?<!a)b(?!c)", "xbx")
show(r"(?>a|ab)c", "abc")
show(r"a*+a", "aaa")
show(r"a*a", "aaa")
show(r"a+?", "aaa")
p = re.compile(r"(?P<first>a)(b)?(?P<last>c)")
m = p.search("-ac-")
print(p.groups, sorted(p.groupindex.items()), m.group(), m[0], m['first'])
print(m.group(1, 2, 'last'), m.groups('missing'), sorted(m.groupdict('missing').items()))
print(m.start('last'), m.end(2), m.span(2), m.pos, m.endpos, m.lastindex, m.lastgroup)
print(m.re is p, m.string, m.regs)

class Index:
    def __index__(self):
        return 1

print(m.group(Index()), m.start(Index()), m.end(Index()), m.span(Index()))
for fn in (lambda: m.group(9), lambda: m.group('missing'),
           lambda: re.compile(r'(?P<x>a)(?P<x>b)'),
           lambda: re.compile(r'\2(a)')):
    try:
        fn()
    except Exception as exc:
        print(type(exc).__name__)
