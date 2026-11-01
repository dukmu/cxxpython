#!/usr/bin/env python3
"""Fixed-total-work benchmark kernels, runnable in one shared interpreter.

file_io uses the current working directory as host-provided scratch space. It
measures filesystem and page-cache I/O, not durable storage (no fsync).
"""

import gc
import sys
import threading
import time


def _parallel(size, workers, job):
    def part(start, end):
        total = 0
        for index in range(start, end):
            total += job(index)
        return total

    if workers == 1:
        return part(0, size)
    results = [0] * workers
    errors = [None] * workers

    def worker(index):
        try:
            results[index] = part(size * index // workers, size * (index + 1) // workers)
        except BaseException as exc:
            errors[index] = exc

    started = []
    try:
        for index in range(workers):
            thread = threading.Thread(target=worker, args=(index,))
            thread.start()
            started.append(thread)
    finally:
        for thread in started:
            thread.join()
    for exc in errors:
        if exc is not None:
            raise exc
    return sum(results)


def _prepare(size, job, expected):
    def run(workers):
        return _parallel(size, workers, job)

    def check(result):
        if result != expected:
            raise AssertionError((result, expected))

    return run, check


def _nbody(size):
    if __package__:
        from . import nbody
    else:
        import nbody

    def job(index):
        before, after = nbody.job(200)
        return after

    def run(workers):
        return _parallel(size, workers, job)

    def check(result):
        if abs(result / size + 0.16902690858133146) > 0.00000001:
            raise AssertionError(result)

    return run, check


def _fannkuch(size):
    if __package__:
        from . import fannkuch
    else:
        import fannkuch

    def job(index):
        return fannkuch.fannkuch(7)

    return _prepare(size, job, 16 * size)


def _allocate(size):
    def job(index):
        obj = [index, {"value": index}, (index, index + 1)]
        return obj[1]["value"] == obj[2][0]

    return _prepare(size, job, size)


def _dict_int(size):
    def job(index):
        table = {key: key for key in range(64)}
        for key in range(64):
            table[key] += 1
        for key in range(0, 64, 8):
            del table[key]
        return sum(table.values())

    expected = sum(key + 1 for key in range(64) if key % 8)
    return _prepare(size, job, expected * size)


def _math_reduce(size):
    import math
    values = tuple(range(256))

    def job(index):
        return math.fsum(values)

    return _prepare(size, job, sum(values) * size)


def _object_protocol(size):
    class Record:
        def __init__(self, value):
            self.value = value

        @property
        def current(self):
            return self.value

        def read(self):
            return self.current

    marker = object()

    def job(index):
        obj = Record(marker)
        for unused in range(32):
            value = obj.read()
            del obj.value
            obj.value = value
        return obj.current is marker

    return _prepare(size, job, size)


def _cycles(size):
    def job(index):
        obj = [index]
        obj.append(obj)
        return obj[1][0] == index

    run, check = _prepare(size, job, size)

    def collect_run(workers):
        result = run(workers)
        gc.collect()
        return result

    return collect_run, check


def _file_io(size):
    import os
    payload = b"cxxpython benchmark page cache\n" * 128
    prefix = ".cxxpython-bench-%s-" % os.getpid()

    def job(index):
        path = prefix + str(index)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            try:
                offset = 0
                while offset < len(payload):
                    count = os.write(fd, payload[offset:])
                    if count <= 0:
                        raise OSError("short write")
                    offset += count
            finally:
                os.close(fd)
            fd = os.open(path, os.O_RDONLY)
            try:
                data = b""
                while len(data) < len(payload):
                    chunk = os.read(fd, len(payload) - len(data))
                    if not chunk:
                        break
                    data += chunk
            finally:
                os.close(fd)
            return data == payload
        finally:
            os.unlink(path)

    return _prepare(size, job, size)


def _regex(size):
    import re
    pattern = re.compile('a*$')
    text = 'a' * 2000000

    def job(index):
        match = pattern.fullmatch(text)
        return match is not None and match.end() == len(text)

    return _prepare(size, job, size)


def _tokenize(size):
    import io
    import tokenize
    source = '\u03c0=0;' * 5000 + '\n'

    def job(index):
        return sum(1 for item in tokenize.generate_tokens(io.StringIO(source).readline))

    return _prepare(size, job, size * 20002)


def prepare_matrix(size, path):
    namespace = {'__name__': '<benchmark>', '__file__': path, '__package__': None}
    with open(path, 'r', encoding='utf-8') as source:
        code = compile(source.read(), path, 'exec')
    exec(code, namespace)
    make_matrix = namespace['make_matrix']
    matmul_mt = namespace['matmul_mt']
    left = make_matrix(size, 0)
    right = make_matrix(size, 1)
    reference = matmul_mt(left, right, 1)

    def run(workers):
        return matmul_mt(left, right, workers)

    def check(result):
        if result != reference:
            raise AssertionError('matrix result differs from serial reference')

    return run, check


CASES = {
    'nbody': {'group': 'cpu', 'size': 4, 'description': 'pyperformance n-body algorithm, independent 200-step jobs (upstream default 20000)', 'prepare': _nbody},
    'fannkuch': {'group': 'cpu', 'size': 4, 'description': 'pyperformance fannkuch algorithm, independent n=7 jobs (upstream default n=9)', 'prepare': _fannkuch},
    'allocate': {'group': 'gc', 'size': 100000, 'description': 'Short-lived containers with natural GC', 'prepare': _allocate},
    'dict_int': {'group': 'gc', 'size': 512, 'description': 'Independent 64-key integer dictionaries: construct, update, delete, sum; natural GC', 'prepare': _dict_int},
    'math_reduce': {'group': 'library', 'size': 128, 'description': 'math.fsum over 256 integer values per fixed-total job; natural GC', 'prepare': _math_reduce},
    'object_protocol': {'group': 'cpu', 'size': 512, 'description': 'Generic instance attributes, property descriptors and Python method calls; fixed-total jobs', 'prepare': _object_protocol},
    'cycles': {'group': 'gc', 'size': 100000, 'description': 'Self-cycles followed by explicit gc.collect', 'prepare': _cycles},
    'file_io': {'group': 'io', 'size': 128, 'description': 'Create, write, read and unlink files in cwd; page cache, not durable storage', 'prepare': _file_io},
    'regex': {'group': 'library', 'size': 128, 'description': 'Shared compiled regex full matches', 'prepare': _regex},
    'tokenize': {'group': 'library', 'size': 4, 'description': 'Tokenize non-ASCII assignment lines', 'prepare': _tokenize},
    'matrix': {'group': 'cpu', 'size': 64, 'description': 'External user matrix workload', 'prepare': None},
}


def _gc_snapshot():
    snapshot = {'enabled': gc.isenabled(), 'stats': gc.get_stats()}
    if hasattr(gc, 'get_heap_stats'):
        snapshot['heap_stats'] = gc.get_heap_stats()
    return snapshot


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', required=True, choices=sorted(CASES))
    parser.add_argument('--size', type=int)
    parser.add_argument('--workers', type=int, default=1)
    parser.add_argument('--loops', type=int, default=1)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--values', type=int, default=1)
    parser.add_argument('--matrix', help='path to the external user matrix.py')
    args = parser.parse_args()
    case = CASES[args.case]
    size = case['size'] if args.size is None else args.size
    if size < 1 or args.workers < 1 or args.loops < 1 or args.values < 1 or args.warmups < 0:
        parser.error('size, workers, loops and values must be positive; warmups must be nonnegative')
    if args.case == 'matrix' and not args.matrix:
        parser.error('--case matrix requires --matrix PATH')
    if args.case != 'matrix' and args.matrix:
        parser.error('--matrix is only valid with --case matrix')
    if not gc.isenabled():
        gc.enable()
    if args.case == 'matrix':
        run, check = prepare_matrix(size, args.matrix)
    else:
        run, check = case['prepare'](size)
    before = _gc_snapshot()

    def sample():
        wall = 0
        cpu = 0
        for unused in range(args.loops):
            start_wall = time.perf_counter_ns()
            start_cpu = time.process_time_ns()
            result = run(args.workers)
            cpu += time.process_time_ns() - start_cpu
            wall += time.perf_counter_ns() - start_wall
            check(result)
        return {'wall_ns': wall, 'process_cpu_ns': cpu}

    warmups = [sample() for unused in range(args.warmups)]
    values = [sample() for unused in range(args.values)]
    after = _gc_snapshot()
    print(repr({'case': args.case, 'group': case['group'], 'size': size,
                'workers': args.workers, 'loops': args.loops, 'check': 'ok',
                'warmups': warmups, 'values': values,
                'python': {'version': sys.version, 'implementation': sys.implementation.name,
                           'executable': sys.executable},
                'gc_before': before, 'gc_after': after}))


if __name__ == '__main__':
    main()
