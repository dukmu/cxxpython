import doctest

source = '''
    >>> 2 + 3
    5
    >>> print("hello")
    hello
'''
parser = doctest.DocTestParser()
examples = parser.get_examples(source)
print(len(examples))
print(examples[0].source.strip(), examples[0].want.strip())
test = parser.get_doctest(source, {}, 'sample', 'sample.py', 0)
result = doctest.DocTestRunner(verbose=False).run(test)
print(result.failed, result.attempted)
