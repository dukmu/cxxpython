import re


def error(fn):
    try:
        fn()
    except Exception as exc:
        print(type(exc).__name__)


re.purge()
p = re.compile(r"a+", re.I)
print(type(p).__name__, p.pattern, bool(p.flags & re.I))
print(p is re.compile(r"a+", re.I), re.compile(p) is p)
re.purge()
print(p is re.compile(r"a+", re.I))
print(re.match("a", "Ab", re.I).group(), re.fullmatch("a+", "aaa").span())
print(re.search("b", "abc").span(), re.findall("a+", "aa b a"))
print(re.split(r"(,)", "a,b,c", maxsplit=1))
print([(m.group(), m.span()) for m in re.finditer(r"x*", "ab")])
s = re.Scanner([(r"[a-z]+", lambda scanner, token: token.upper()),
                (r"\s+", None), (r"\d+", lambda scanner, token: int(token))])
print(s.scan("hi 12 !"))
print(re.escape("a+b c"), int(re.RegexFlag.I | re.RegexFlag.M))
for fn in (lambda: re.compile("("), lambda: re.compile("[z-a]"),
           lambda: re.compile(p, re.I), lambda: re.search("a", b"a"),
           lambda: re.compile("a", re.L)):
    error(fn)
