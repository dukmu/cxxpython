import unittest

from Tools.record_foundation_benchmarks import parse_output, render_report


class BenchmarkReportTests(unittest.TestCase):
    def test_workers_and_checks_are_preserved(self):
        rows = parse_output("parallel_storage_workers=4 wall_ns=2000000 live_set_check=ok\n", "run", 1)
        self.assertIn(["run", 1, "parallel_storage_workers", "4", "wall_ns", "2000000"], rows)
        with self.assertRaises(ValueError):
            parse_output("live_set_check=failed", "run", 2)

    def test_report_is_deterministic_and_derives_statistics(self):
        rows = parse_output("iterations=10 wall_ns=1000000\n", "run", 1)
        rows += parse_output("iterations=10 wall_ns=3000000\n", "run", 2)
        report = render_report(rows, 1)
        self.assertIn("| ms | 2 | 2.000 | 1.000 | 3.000 |", report)
        self.assertIn("Failed runs retained in raw data: 1.", report)
        self.assertEqual(report, render_report(list(reversed(rows)), 1))

    def test_vm_scaling_uses_its_own_one_worker_baseline(self):
        rows = parse_output("parallel_vm_workers=1 wall_ns=4000000 process_cpu_ns=4000000\n", "vm", 1)
        rows += parse_output("parallel_vm_workers=2 wall_ns=2500000 process_cpu_ns=4500000\n", "vm", 1)
        self.assertIn("| vm | parallel_vm_workers | 2 | 2.500 | 1.800 | 1.600 | 0.800 |", render_report(rows, 0))

    def test_identical_source_comparison_is_derived_from_paired_series(self):
        rows = parse_output("parallel_source_workers=2 wall_ns=4000000\n", "source", 1)
        rows += parse_output("parallel_source_workers=2 wall_ns=1000000\n", "source-cpython", 1)
        self.assertIn("| source | 2 | 4.000 | 1.000 | 4.000 |", render_report(rows, 0))
