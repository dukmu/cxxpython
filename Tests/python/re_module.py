import re

print(type(re))
print(re.__doc__.__contains__('regular expression'))
print(re.__all__.__contains__('compile'))
print(re.__all__.__contains__('match'))
print(re.__all__.__contains__('search'))

pattern = re.compile('ab')
print(type(pattern))
print(re.Pattern)
print(re.Match)
print(pattern.match('ab'))
print(re.match('ab', 'ab'))
print(re.match('ab', 'xx'))
print(re.search('b', 'abc'))
