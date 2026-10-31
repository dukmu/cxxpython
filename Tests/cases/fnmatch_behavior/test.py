import fnmatch

print(fnmatch.translate('a*[!0-9]?.txt'))
print(fnmatch.filter(['a1.txt', 'abx.txt', 'ab3.txt', 'zz'], 'a*[!0-9]?.txt'))
print(fnmatch.fnmatchcase('Report.TXT', '*.txt'))
print(fnmatch.fnmatchcase(b'abc.py', b'*.py'))
try:
    fnmatch.fnmatchcase(b'abc', '*.py')
except TypeError as exc:
    print(type(exc).__name__, str(exc))
