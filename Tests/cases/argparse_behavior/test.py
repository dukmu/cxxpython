import argparse
import contextlib
import io

parser = argparse.ArgumentParser(prog='demo', description='Example')
parser.add_argument('--count', type=int, default=1, help='repeat count')
subs = parser.add_subparsers(dest='command', required=True)
run = subs.add_parser('run', help='run a task')
run.add_argument('name')
print(parser.parse_args(['--count', '3', 'run', 'job']))
print(parser.format_help())
err = io.StringIO()
try:
    with contextlib.redirect_stderr(err):
        parser.parse_args(['--count', 'bad', 'run', 'job'])
except SystemExit as exc:
    print('exit:', exc.code)
print(err.getvalue(), end='')
