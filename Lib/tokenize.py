"""Tokenization helpers for Python source.

This is the script-level facade over the interpreter's tokenizer substrate.
The public surface intentionally starts small but follows CPython's shape:
``generate_tokens(readline)`` consumes a line reader and returns TokenInfo
records, while ``tokenize(source)`` is a convenience helper for source strings.
"""

import _tokenize
from collections import namedtuple
import token

__all__ = []
for _name in token.__all__:
    __all__.append(_name)
__all__.append('TokenInfo')
__all__.append('generate_tokens')
__all__.append('tokenize')
for _name in token.__all__:
    globals()[_name] = getattr(token, _name)

TokenInfo = namedtuple('TokenInfo', 'type string start end line')


def _token_info_exact_type(self):
    if self.type == token.OP and self.string in token.EXACT_TOKEN_TYPES:
        return token.EXACT_TOKEN_TYPES[self.string]
    return self.type


TokenInfo.exact_type = property(_token_info_exact_type)


def _public_type(exact_type):
    for text in token.EXACT_TOKEN_TYPES:
        if exact_type == token.EXACT_TOKEN_TYPES[text]:
            return token.OP
    return exact_type


def _tokens_from_source(source):
    result = []
    for raw in _tokenize.TokenizerIter(source):
        text = raw[0]
        exact_type = raw[1]
        start_row = raw[2]
        end_row = raw[3]
        start_column = raw[4]
        end_column = raw[5]
        line = raw[6]
        result.append(TokenInfo(_public_type(exact_type),
                                text,
                                (start_row, start_column),
                                (end_row, end_column),
                                line))
    return result


def tokenize(readline):
    """Tokenize source read from a readline-style callable."""
    return generate_tokens(readline)


def generate_tokens(readline):
    """Tokenize source read from a readline-style callable."""
    source = ''
    while True:
        line = readline()
        if line == '':
            break
        source = source + line
    return _tokens_from_source(source)
