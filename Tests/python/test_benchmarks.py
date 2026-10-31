"""Contracts of the host benchmark recorder, not performance thresholds."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from Tools.benchmarks import record


def result(runtime='cxx', workers=1, size=4, times=(4_000_000,), loops=1):
    return {'kind': 'run', 'runtime': runtime, 'process': 0, 'duration_ns': 1,
            'returncode': 0, 'error': None,
            'payload': {'case': 'nbody', 'group': 'cpu', 'size': size,
                        'workers': workers, 'loops': loops, 'check': 'ok',
                        'warmups': [], 'values': [{'wall_ns': n, 'process_cpu_ns': n * workers}
                                                 for n in times],
                        'python': {'version': 'test', 'implementation': runtime,
                                   'executable': '/test/' + runtime}}}


class BenchmarkReportTests(unittest.TestCase):
    def test_worker_schema_checks_shape_and_results(self):
        payload = result()['payload']
        expected = {key: payload[key] for key in ('case', 'size', 'workers', 'loops')}
        record.validate_payload(payload, expected, 0, 1)
        for key, bad in [('check', 'failed'), ('workers', 2), ('values', []), ('python', None)]:
            changed = copy.deepcopy(payload)
            changed[key] = bad
            with self.assertRaises(ValueError):
                record.validate_payload(changed, expected, 0, 1)
        payload['values'][0]['wall_ns'] = 0
        with self.assertRaises(ValueError):
            record.validate_payload(payload, expected, 0, 1)

    def test_warmups_and_calibration_are_not_measurements(self):
        measured = result(times=(4_000_000,), loops=2)
        measured['payload']['warmups'] = [{'wall_ns': 999, 'process_cpu_ns': 999}]
        calibration = copy.deepcopy(measured)
        calibration['kind'] = 'calibration'
        rows = record.sample_rows([calibration, measured])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['wall_ns'], 2_000_000)

    def test_reports_separate_sizes_and_use_same_runtime_baseline(self):
        records = [result(workers=1, times=(4_000_000,)),
                   result(workers=2, times=(2_500_000,)),
                   result(size=8, workers=1, times=(9_000_000,)),
                   result('cpython', workers=2, times=(1_000_000,))]
        report = record.render_report(records, 'cpython')
        self.assertIn('| cxx | nbody | 4 | 2 | kernel |', report)
        self.assertIn('1.600', report)
        self.assertIn('2.500', report)
        self.assertEqual(report, record.render_report(list(reversed(records)), 'cpython'))

    def test_failure_is_retained_but_never_becomes_a_timing(self):
        failed = result()
        failed['error'] = 'worker result differs'
        self.assertEqual(record.sample_rows([failed]), [])
        self.assertIn('Failed processes: 1', record.render_report([failed]))

    def test_warns_about_variation_and_cpu_starvation_without_dropping_samples(self):
        data = result(times=(1_000_000, 4_000_000))
        for value in data['payload']['values']:
            value['process_cpu_ns'] = 100_000
        report = record.render_report([data])
        self.assertIn('variation', report)
        self.assertIn('CPU/wall', report)
        self.assertEqual(len(record.sample_rows([data])), 2)

    def test_worker_timeout_saves_diagnostics(self):
        with tempfile.TemporaryDirectory() as folder:
            raw = Path(folder) / 'runs.jsonl'
            with self.assertRaises(RuntimeError):
                record.capture([sys.executable, '-c', 'import time; print("started", flush=True); time.sleep(5)'],
                               Path(folder), 0.02, raw, 'test', 0, 'run')
            text = raw.read_text()
            self.assertIn('timeout', text)
            self.assertIn('returncode', text)

    def test_timeout_terminates_the_profile_target_descendant(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / 'orphan-ran'
            descendant = 'import time; from pathlib import Path; time.sleep(0.4); Path(%r).touch()' % str(marker)
            parent = 'import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",%r]); print("spawned",flush=True); time.sleep(5)' % descendant
            raw = Path(folder) / 'runs.jsonl'
            with self.assertRaises(RuntimeError):
                record.capture([sys.executable, '-c', parent], Path(folder), 0.15, raw, 'test', 0, 'profile')
            self.assertIn('spawned', json.loads(raw.read_text())['stdout'])
            time.sleep(0.5)
            self.assertFalse(marker.exists())

    def test_rejects_duplicate_runtime_labels(self):
        with self.assertRaises(ValueError):
            record.parse_runtimes(['same=' + sys.executable, 'same=' + sys.executable])

    def test_relative_executable_is_resolved_before_using_scratch_cwd(self):
        relative = os.path.relpath(sys.executable)
        self.assertEqual(record.parse_runtimes(['host=' + relative])['host'], os.path.abspath(sys.executable))

    def test_interpreter_symlink_is_not_replaced_by_its_base_executable(self):
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / 'python'
            executable.symlink_to(sys.executable)
            self.assertEqual(record.parse_runtimes(['venv=' + str(executable)])['venv'], str(executable))

    def test_fixed_work_calibration_uses_one_loop_count_for_all_workers(self):
        self.assertEqual(record.calibrated_loops([20_000_000, 7_000_000], 0.1), 16)
        self.assertEqual(record.calibrated_loops([200_000_000], 0.1), 1)

    def test_source_changes_are_visible_without_rewriting_or_rejecting_evidence(self):
        headers = [{'kind': 'header', 'schema': 1, 'sources': {'worker': source}}
                   for source in ('before', 'after')]
        report = record.render_report(headers + [result()])
        self.assertIn('source snapshots differ', report)
        self.assertEqual(headers[0]['sources']['worker'], 'before')

    def test_gc_report_does_not_attribute_a_setup_pause_to_a_gc_free_window(self):
        data = result()
        snapshot = {'stats': [{'collections': 0}],
                    'heap_stats': {'collections': 9, 'last_pause_ns': 999999,
                                   'last_rendezvous_ns': 100, 'max_rendezvous_ns': 100}}
        data['payload']['gc_before'] = copy.deepcopy(snapshot)
        data['payload']['gc_after'] = copy.deepcopy(snapshot)
        self.assertNotIn('999999', record.render_report([data]))
        data['payload']['gc_after']['heap_stats']['collections'] += 1
        self.assertIn('| 1 | 999999 | 100 | 100 |', record.render_report([data]))

    @unittest.skipUnless(importlib.util.find_spec('pyperf'), 'optional host pyperf dependency')
    def test_standard_pyperf_export_preserves_independent_runs(self):
        import pyperf
        with tempfile.TemporaryDirectory() as folder:
            first = result(loops=2)
            second = result(times=(6_000_000,), loops=2)
            second['process'] = 1
            record.export_pyperf([first, second], Path(folder))
            suite = pyperf.BenchmarkSuite.load(str(Path(folder) / 'cxx.pyperf.json'))
            bench = suite.get_benchmarks()[0]
            self.assertEqual(bench.get_nrun(), 2)
            self.assertEqual(bench.get_values(), (0.002, 0.003))

    def test_target_worker_checks_results_across_threads(self):
        worker = Path(__file__).resolve().parents[2] / 'Tools/benchmarks/workloads.py'
        with tempfile.TemporaryDirectory() as folder:
            for case in ('allocate', 'cycles', 'file_io'):
                command = [sys.executable, str(worker), '--case', case, '--size', '5',
                           '--workers', '2', '--loops', '1', '--warmups', '0', '--values', '1']
                child = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=15)
                self.assertEqual(child.returncode, 0, child.stderr)
                payload = record.decode_payload(child.stdout)
                record.validate_payload(payload, {'case': case, 'size': 5, 'workers': 2, 'loops': 1}, 0, 1)


if __name__ == '__main__':
    unittest.main()
