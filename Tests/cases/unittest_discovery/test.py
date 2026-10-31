# Discovery and named loading use actual modules in a temporary working directory.
import os
import unittest

os.mkdir('sample_tests')
with open('sample_tests/__init__.py', 'w') as output:
    output.write('')
with open('sample_tests/test_sample.py', 'w') as output:
    output.write('import unittest\n'
                 'class Example(unittest.TestCase):\n'
                 '    def test_b(self): pass\n'
                 '    def test_a(self): pass\n')

loader = unittest.TestLoader()
suite = loader.discover('sample_tests', top_level_dir='.')
result = unittest.TestResult()
suite.run(result)
print('discovery', result.testsRun, result.wasSuccessful(), loader.errors)
suite = loader.loadTestsFromName('sample_tests.test_sample.Example.test_b')
result = unittest.TestResult()
suite.run(result)
print('named', result.testsRun, result.wasSuccessful(), loader.errors)
