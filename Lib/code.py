"""Interactive interpreter support.

This module intentionally owns REPL policy in the script layer.  The native
launcher supplies process integration such as readline-backed input; command
buffering, compilation mode selection, result display, and error rendering live
here so the CLI can grow toward CPython's ``code`` module without hard-coding
that behavior in ``Programs/cxxpython.cpp``.
"""

import sys
import traceback

__all__ = ['InteractiveConsole', 'InteractiveInterpreter', 'compile_command', 'interact']


def _write(stream, text):
    stream.write(text)
    stream.flush()


def _is_internal_traceback(tb):
    return tb.tb_frame.f_code.co_filename == __file__


def _print_exception(exc, stream=None):
    if stream is None:
        stream = sys.stderr
    visible = []
    current = exc.__traceback__
    while current is not None:
        if not _is_internal_traceback(current):
            visible.append(current)
        current = current.tb_next
    traceback.print_exception(exc, stream, visible)


def _is_incomplete_syntax_error(exc):
    message = str(exc)
    if 'expected indented block' in message:
        return True
    if 'expected except after try block' in message:
        return True
    if 'expected except or finally after try block' in message:
        return True
    return False


def _source_from_buffer(buffer):
    return '\n'.join(buffer) + '\n'


def compile_command(source, filename='<input>', symbol='single'):
    """Compile a command and return ``None`` when more input is required."""
    if symbol == 'eval':
        return compile(source, filename, 'eval')
    if symbol == 'exec':
        try:
            return compile(source, filename, 'exec')
        except SyntaxError as exc:
            if _is_incomplete_syntax_error(exc):
                return None
            raise exc
    if symbol != 'single':
        raise ValueError("symbol must be 'single', 'exec', or 'eval'")

    try:
        return compile(source, filename, 'eval')
    except SyntaxError:
        pass
    try:
        return compile(source, filename, 'exec')
    except SyntaxError as exc:
        if _is_incomplete_syntax_error(exc):
            return None
        raise exc


class InteractiveInterpreter:
    """Base class for executing Python code in a persistent namespace."""

    def __init__(self, locals=None, filename='<stdin>', stdout=None, stderr=None):
        if locals is None:
            locals = {}
        self.locals = locals
        self.filename = filename
        if stdout is None:
            stdout = sys.stdout
        if stderr is None:
            stderr = sys.stderr
        self.stdout = stdout
        self.stderr = stderr

    def showsyntaxerror(self, exc):
        _print_exception(exc, self.stderr)

    def showtraceback(self, exc):
        _print_exception(exc, self.stderr)

    def runcode(self, code):
        try:
            exec(code, self.locals)
        except SystemExit as exc:
            raise exc
        except BaseException as exc:
            self.showtraceback(exc)

    def runsource(self, source, filename=None, symbol='single'):
        if filename is None:
            filename = self.filename
        try:
            code = compile_command(source, filename, symbol)
        except SyntaxError as exc:
            self.showsyntaxerror(exc)
            return False
        if code is None:
            return True

        if symbol == 'single':
            try:
                value = eval(code, self.locals)
                if value is not None:
                    _write(self.stdout, repr(value) + '\n')
                return False
            except SyntaxError:
                pass
            except SystemExit as exc:
                raise exc
            except BaseException as exc:
                self.showtraceback(exc)
                return False

        self.runcode(code)
        return False


class InteractiveConsole(InteractiveInterpreter):
    """Interactive interpreter with CPython-style input buffering."""

    def __init__(self, locals=None, filename='<stdin>', stdout=None, stderr=None):
        InteractiveInterpreter.__init__(self, locals, filename, stdout, stderr)
        self.buffer = []
        self.more = False

    def resetbuffer(self):
        self.buffer = []
        self.more = False

    def push(self, line):
        self.buffer.append(line)
        source = _source_from_buffer(self.buffer)
        if self.more and line != '':
            return True
        if self.more and line == '':
            self.runcode(compile(source, self.filename, 'exec'))
            self.resetbuffer()
            return False
        self.more = self.runsource(source, self.filename, 'single')
        if self.more:
            pass
        else:
            self.resetbuffer()
        return self.more

    def finalize(self):
        if self.more:
            pass
        else:
            return False
        source = _source_from_buffer(self.buffer)
        self.resetbuffer()
        return self.runsource(source, self.filename, 'exec')


def interact(banner='cxxpython 0.1.0-dev', readfunc=None, local=None):
    console = InteractiveConsole(local)
    if banner is None:
        pass
    else:
        _write(sys.stdout, banner + '\n')
    while True:
        prompt = '... ' if console.more else '>>> '
        if readfunc is None:
            _write(sys.stdout, prompt)
            line = sys.stdin.readline()
            if line == '':
                break
            if line[-1:] == '\n':
                line = line[:-1]
        else:
            line = readfunc(prompt)
            if line is None:
                break
        console.push(line)
    console.finalize()
