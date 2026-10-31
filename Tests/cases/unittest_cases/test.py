# Exercises public unittest behavior, including result bookkeeping and cleanup order.
import unittest

events = []


class Example(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        events.append('class setup')
        cls.addClassCleanup(events.append, 'class cleanup')

    @classmethod
    def tearDownClass(cls):
        events.append('class teardown')

    def setUp(self):
        events.append('setup')
        self.addCleanup(events.append, 'cleanup')

    def tearDown(self):
        events.append('teardown')

    def test_assertions(self):
        self.assertEqual([1, 2], [1, 2])
        self.assertIn('b', 'abc')
        self.assertRaisesRegex(ValueError, 'bad', int, 'bad')
        with self.subTest(value=1):
            self.assertTrue(True)
        with self.subTest(value=2):
            self.assertEqual(2, 3)

    @unittest.skip('not today')
    def test_skip(self):
        pass

    @unittest.expectedFailure
    def test_expected_failure(self):
        self.assertEqual(1, 2)

    @unittest.expectedFailure
    def test_unexpected_success(self):
        pass


suite = unittest.defaultTestLoader.loadTestsFromTestCase(Example)
result = unittest.TestResult()
suite.run(result)
print('tests', result.testsRun)
print('failures errors skipped expected unexpected', len(result.failures),
      len(result.errors), len(result.skipped), len(result.expectedFailures),
      len(result.unexpectedSuccesses))
print('success', result.wasSuccessful())
print('skip reason', result.skipped[0][1])
print('subtest', '(value=2)' in str(result.failures[0][0]))
print('events', events)
