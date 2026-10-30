"""Product entry point and interactive shell integration tests.

Run with: python test_main.py /path/to/cxxpython
"""

import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest

try:
    import pty
except ImportError:
    pty = None


class MainTests(unittest.TestCase):
    def execute(self, *args, input=None, cwd=None):
        return subprocess.run([self.binary, *args], input=input, text=True,
                              capture_output=True, cwd=cwd, timeout=20)

    def test_help_version_and_invalid_options(self):
        for flag in ("-h", "--help"):
            result = self.execute(flag)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("usage:", result.stdout)
        for flag in ("-V", "--version"):
            result = self.execute(flag)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("cxxpython", result.stdout)
            self.assertIn("3.12", result.stdout)
        for args in (("-c",), ("-m",), ("--unknown",), ("-E",)):
            self.assertEqual(self.execute(*args).returncode, 2)

    def test_command_argv_and_legacy_eval(self):
        result = self.execute("-c", "import sys", "--eval", "sys.argv", "a", "--flag")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "['-c', 'a', '--flag']")
        result = self.execute("-c", "import sys", "--", "--eval", "x")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.execute("-c", "import sys; print(sys.argv)", "--", "--eval", "x")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "['-c', '--eval', 'x']")

    def test_stdin(self):
        for args, argv0 in (((), ""), (("-", "arg"), "-")):
            result = self.execute(*args, input="import sys\nprint(sys.argv[0])\nprint(42)\n")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, argv0 + "\n42\n")

    def test_standard_stream_bootstrap_uses_the_configured_library(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "io.py").write_text("raise RuntimeError('shadowed io')\n")
            (root / "encodings").mkdir()
            (root / "encodings" / "__init__.py").write_text(
                "raise RuntimeError('shadowed encodings')\n")
            result = self.execute("-c", "import sys; print(type(sys.stdin).__name__)",
                                  cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "TextIOWrapper\n")

    @unittest.skipIf(os.name != "posix", "directory descriptors require POSIX")
    def test_stream_startup_failure_has_a_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            descriptor = os.open(directory, os.O_RDONLY)
            try:
                result = subprocess.run([self.binary, "-c", "pass"], stdin=descriptor,
                                        capture_output=True, text=True, timeout=20)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
            finally:
                os.close(descriptor)

    def test_script_import_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "helper.py").write_text("value = 42\n")
            script = root / "script.py"
            script.write_text("import helper\nimport sys\nprint(helper.value)\n"
                              "print(__file__)\nprint(sys.argv)\n")
            result = self.execute(str(script), "arg", cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(),
                             ["42", str(script), repr([str(script), "arg"])])

    def test_module_and_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "example.py").write_text("import sys\nprint(__name__)\nprint(sys.argv[1])\n")
            package = root / "pkg"
            package.mkdir()
            (package / "__init__.py").write_text("")
            (package / "__main__.py").write_text("print(__name__)\nprint(__package__)\n")
            for name, expected in (("example", ["__main__", "arg"]),
                                   ("pkg", ["__main__", "pkg"])):
                result = self.execute("-m", name, "arg", cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), expected)

    def test_interactive_recovers_from_task_error(self):
        result = self.execute("-i", "-c", "raise ValueError('task')",
                              input="print('recovered')\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ValueError", result.stderr)
        self.assertIn("recovered", result.stdout)

    def test_interactive_multiline_display_and_error_recovery(self):
        result = self.execute("-i", "-q", "-c", "value = 40", input=(
            "def add(n):\n    return n + 2\n\n"
            "add(value)\n1 / 0\nadd(1)\n'hello'\n_\n"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["42", "3", "'hello'", "'hello'"])
        self.assertIn("ZeroDivisionError", result.stderr)
        self.assertIn("... ", result.stderr)

    def test_exit_codes_and_module_errors(self):
        for source, code, message in (("sys.exit()", 0, ""), ("sys.exit(7)", 7, ""),
                                      ("sys.exit(-1)", 255, ""),
                                      ("sys.exit('bye')", 1, "bye\n")):
            result = self.execute("-c", "import sys\n" + source)
            self.assertEqual(result.returncode, code, result.stderr)
            self.assertEqual(result.stderr, message)
        result = self.execute("-m", "missing_entry_module")
        self.assertEqual(result.returncode, 1)
        self.assertIn("No module named missing_entry_module", result.stderr)
        self.assertNotIn("internal error", result.stderr)

    def test_relative_script_and_main_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "main.py"
            script.write_text("import sys\nprint(__file__)\nprint(__spec__)\n"
                              "print(__package__)\nprint(sys.argv[0])\n")
            result = self.execute("main.py", cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(),
                             [str(script), "None", "None", "main.py"])

    @unittest.skipUnless(pty is not None and os.name == "posix", "requires a PTY")
    def test_terminal_repl_displayhook(self):
        master, slave = pty.openpty()
        process = subprocess.Popen([self.binary, "-q"], stdin=slave, stdout=slave,
                                   stderr=slave, close_fds=True)
        os.close(slave)
        try:
            output = bytearray()
            sent = False
            while process.poll() is None:
                ready, _, _ = select.select([master], [], [], 10)
                if not ready:
                    self.fail("REPL timed out")
                try:
                    output.extend(os.read(master, 4096))
                except OSError:
                    break
                if not sent and b">>>" in output:
                    os.write(master, b"1 + 2\n\x04")
                    sent = True
            self.assertEqual(process.wait(timeout=10), 0)
            self.assertIn(b">>>", output)
            self.assertIn(b"3", output)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)


if __name__ == "__main__":
    MainTests.binary = str(Path(sys.argv.pop(1)).resolve())
    unittest.main()
