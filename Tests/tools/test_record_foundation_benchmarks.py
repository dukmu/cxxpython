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
