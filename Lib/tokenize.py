"""Tokenization helpers for Python source.

This is the script-level facade over the interpreter's tokenizer substrate.
The public surface intentionally starts small but follows CPython's shape:
``generate_tokens(readline)`` consumes a line reader and returns TokenInfo
records, while ``tokenize(source)`` is a convenience helper for source strings.
"""

from collections import namedtuple
import _tokenize

__all__ = ['TokenInfo', 'generate_tokens', 'tokenize']

TokenInfo = namedtuple('TokenInfo', 'type string start end line')


def _line_at(lines, number):
    if number <= 0:
        return ''
    if number > len(lines):
        return ''
    return lines[number - 1]


def _tokens_from_source(source, filename):
    lines = []
    current = ''
    for ch in source:
        current = current + ch
        if ch == '\n':
            lines.append(current)
            current = ''
    if current != '':
        lines.append(current)
    result = []
    for raw in _tokenize.tokens(source, filename):
        kind = raw[0]
        text = raw[1]
        row = raw[2]
        column = raw[3]
        result.append(TokenInfo(kind,
                                text,
                                (row, column),
                                (row, column + len(text)),
                                _line_at(lines, row)))
    return result


def tokenize(source, filename='<string>'):
    """Return TokenInfo records for a source string."""
    return _tokens_from_source(source, filename)


def generate_tokens(readline):
    """Tokenize source read from a readline-style callable."""
    source = ''
    while True:
        line = readline()
        if line == '':
            break
        source = source + line
    return _tokens_from_source(source, '<string>')
