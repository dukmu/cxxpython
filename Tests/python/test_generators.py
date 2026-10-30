"""Differential generator tests against a fixed CPython executable."""

import argparse
import subprocess
from textwrap import dedent
import unittest


class GeneratorDifferential(unittest.TestCase):
    def check_program(self, source, expression):
        source = dedent(source).strip()
        actual = subprocess.run(
            [self.binary, "-c", source, "--eval", expression],
            capture_output=True, text=True, timeout=15,
        )
        expected = subprocess.run(
            [self.reference, "-c", source + "\nprint(repr((" + expression + ")))"],
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, expected.stdout)

    def test_yield_send_and_tuple_return(self):
        self.check_program("""
            def gen():
                first = yield
                second = yield first
                return first, second
            g = gen()
            initial = next(g)
            sent = g.send(7)
            try:
                g.send(9)
            except StopIteration as error:
                result = error.value
            """, "initial, sent, result")

    def test_empty_return_has_empty_stop_iteration_arguments(self):
        self.check_program("""
            def gen():
                if False:
                    yield
            g = gen()
            try:
                next(g)
            except StopIteration as error:
                first = error.args, error.value
            try:
                next(g)
            except StopIteration as error:
                second = error.args, error.value
            """, "first, second, g.gi_frame")

    def test_yield_from_generic_send_and_return(self):
        self.check_program("""
            events = []
            class Delegate:
                def __iter__(self): return self
                def __next__(self):
                    events.append('next')
                    return 'ready'
                def send(self, value):
                    events.append(('send', value))
                    raise StopIteration(('done', value))
            def gen():
                result = yield from Delegate()
                yield result
            g = gen()
            first = next(g)
            last = g.send(42)
            """, "first, last, events")

    def test_delegation_reads_the_native_stop_iteration_value(self):
        self.check_program("""
            class End(StopIteration):
                @property
                def value(self): raise AssertionError('must not invoke property')
            class Delegate:
                def __iter__(self): return self
                def __next__(self): raise End((42, 9))
            def gen():
                return (yield from Delegate())
            g = gen()
            try:
                next(g)
            except StopIteration as error:
                result = error.value
            """, "result, g.gi_frame")

    def test_yield_from_forwards_throw_arguments(self):
        self.check_program("""
            events = []
            class Delegate:
                def __iter__(self): return self
                def __next__(self): return 'ready'
                def throw(self, *args):
                    events.append((len(args), args[0] is ValueError,
                                   len(args) > 1 and args[1] is payload,
                                   len(args) > 2 and args[2] is traceback))
                    return 'handled'
            def gen():
                yield from Delegate()
            try:
                raise RuntimeError('trace')
            except RuntimeError as error:
                traceback = error.__traceback__
            payload = ('a', 'b')
            g = gen()
            next(g)
            a = g.throw(ValueError)
            b = g.throw(ValueError, payload)
            c = g.throw(ValueError, payload, traceback)
            """, "a, b, c, events")

    def test_close_forwards_generator_exit(self):
        self.check_program("""
            events = []
            class Delegate:
                def __iter__(self): return self
                def __next__(self): return 1
                def close(self): events.append('delegate close')
            def gen():
                try:
                    yield from Delegate()
                except GeneratorExit:
                    events.append('generator exit')
                    raise
                finally:
                    events.append('finally')
            g = gen()
            first = next(g)
            result = g.close()
            """, "first, result, events")

    def test_rejected_delegated_throw_keeps_the_suspension(self):
        self.check_program("""
            events = []
            class Delegate:
                def __iter__(self): return self
                def __next__(self): return 7
                @property
                def throw(self): raise LookupError('lookup')
            def gen(value):
                try:
                    yield from value
                finally:
                    events.append('closed')
            g = gen(iter((1, 2)))
            first = next(g)
            try:
                g.throw('invalid')
            except TypeError:
                pass
            middle = events[:], g.gi_suspended, next(g)
            h = gen(Delegate())
            next(h)
            try:
                h.throw(ValueError)
            except LookupError:
                pass
            last = events[:], h.gi_suspended, next(h)
            g.close()
            h.close()
            """, "first, middle, last, events")

    def test_pep479_converts_escaping_stop_iteration(self):
        self.check_program("""
            def gen():
                yield 1
                raise StopIteration('escaped')
            g = gen()
            first = next(g)
            try:
                next(g)
            except RuntimeError as error:
                result = (str(error), type(error.__cause__) is StopIteration,
                          error.__cause__.value, error.__context__ is error.__cause__)
            """, "first, result")

    def test_genexpr_outer_eager_inner_lazy(self):
        self.check_program("""
            events = []
            class Outer:
                def __iter__(self):
                    events.append('outer iter')
                    return iter((1, 2))
            def inner(x):
                events.append(('inner', x))
                return (x, x + 10)
            def element(x, y):
                events.append(('element', x, y))
                return x + y
            g = (element(x, y) for x in Outer() for y in inner(x) if y > 1)
            before = events[:]
            first = next(g)
            after = events[:]
            rest = list(g)
            """, "before, first, after, rest, events")

    def test_genexpr_outer_iterator_error_is_eager(self):
        self.check_program("""
            events = []
            class Outer:
                def __iter__(self):
                    events.append('iter')
                    raise ValueError('outer')
            try:
                g = (x for x in Outer())
            except ValueError as error:
                result = error.args
            """, "events, result")

    def test_genexpr_walrus_and_closure(self):
        self.check_program("""
            def outer():
                value = 0
                offset = 10
                g = ((value := x + offset) for x in (1, 2))
                before = value
                first = next(g)
                middle = value
                rest = list(g)
                return before, first, middle, rest, value
            result = outer()
            """, "result")

    def test_throw_links_the_generators_own_handled_exception(self):
        self.check_program("""
            def gen():
                try:
                    raise ValueError('own')
                except ValueError:
                    yield 'paused'
            g = gen()
            next(g)
            try:
                g.throw(TypeError('injected'))
            except TypeError as error:
                result = type(error.__context__).__name__, error.__context__.args
            """, "result")

    def test_exception_context_survives_yield(self):
        self.check_program("""
            def gen():
                try:
                    raise ValueError('original')
                except ValueError:
                    yield 'paused'
                    raise TypeError('resumed')
            g = gen()
            first = next(g)
            try:
                next(g)
            except TypeError as error:
                result = (error.args, type(error.__context__) is ValueError,
                          error.__context__.args)
            """, "first, result")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference", required=True)
    args, remaining = parser.parse_known_args()
    GeneratorDifferential.binary = args.binary
    GeneratorDifferential.reference = args.reference
    version = subprocess.check_output([args.reference, "--version"], text=True).strip()
    print("Reference:", version, flush=True)
    suite = unittest.main(argv=[__file__, *remaining], exit=False)
    raise SystemExit(not suite.result.wasSuccessful())
