import io
import unittest


class Example(unittest.TestCase):
    def test_ok(self):
        self.assertEqual(1, 1)


stream = io.StringIO()
suite = unittest.TestSuite([Example('test_ok')])
result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
lines = stream.getvalue().splitlines()
assert lines[0].endswith(' ... ok')
assert lines[1] == lines[4] == ''
assert lines[2] == '-' * 70
assert lines[3].startswith('Ran 1 test in ') and lines[3].endswith('s')
assert lines[5] == 'OK'
print('result', result.testsRun, result.wasSuccessful())
print('runner output', len(lines), lines[0].endswith(' ... ok'), lines[5])
