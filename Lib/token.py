"""Token constants."""

__all__ = ['tok_name', 'ISTERMINAL', 'ISNONTERMINAL', 'ISEOF']

ENDMARKER = 0
NAME = 1
NUMBER = 2
STRING = 3
NEWLINE = 4
INDENT = 5
DEDENT = 6
LPAR = 7
RPAR = 8
LSQB = 9
RSQB = 10
COLON = 11
COMMA = 12
SEMI = 13
PLUS = 14
MINUS = 15
STAR = 16
SLASH = 17
VBAR = 18
AMPER = 19
LESS = 20
GREATER = 21
EQUAL = 22
DOT = 23
PERCENT = 24
LBRACE = 25
RBRACE = 26
EQEQUAL = 27
NOTEQUAL = 28
LESSEQUAL = 29
GREATEREQUAL = 30
TILDE = 31
CIRCUMFLEX = 32
LEFTSHIFT = 33
RIGHTSHIFT = 34
DOUBLESTAR = 35
PLUSEQUAL = 36
MINEQUAL = 37
STAREQUAL = 38
SLASHEQUAL = 39
PERCENTEQUAL = 40
AMPEREQUAL = 41
VBAREQUAL = 42
CIRCUMFLEXEQUAL = 43
LEFTSHIFTEQUAL = 44
RIGHTSHIFTEQUAL = 45
DOUBLESTAREQUAL = 46
DOUBLESLASH = 47
DOUBLESLASHEQUAL = 48
AT = 49
ATEQUAL = 50
RARROW = 51
ELLIPSIS = 52
COLONEQUAL = 53
OP = 54
AWAIT = 55
ASYNC = 56
TYPE_IGNORE = 57
TYPE_COMMENT = 58
SOFT_KEYWORD = 59
ERRORTOKEN = 60
COMMENT = 61
NL = 62
ENCODING = 63
N_TOKENS = 64
NT_OFFSET = 256

tok_name = {
    0: 'ENDMARKER',
    1: 'NAME',
    2: 'NUMBER',
    3: 'STRING',
    4: 'NEWLINE',
    5: 'INDENT',
    6: 'DEDENT',
    7: 'LPAR',
    8: 'RPAR',
    9: 'LSQB',
    10: 'RSQB',
    11: 'COLON',
    12: 'COMMA',
    13: 'SEMI',
    14: 'PLUS',
    15: 'MINUS',
    16: 'STAR',
    17: 'SLASH',
    18: 'VBAR',
    19: 'AMPER',
    20: 'LESS',
    21: 'GREATER',
    22: 'EQUAL',
    23: 'DOT',
    24: 'PERCENT',
    25: 'LBRACE',
    26: 'RBRACE',
    27: 'EQEQUAL',
    28: 'NOTEQUAL',
    29: 'LESSEQUAL',
    30: 'GREATEREQUAL',
    31: 'TILDE',
    32: 'CIRCUMFLEX',
    33: 'LEFTSHIFT',
    34: 'RIGHTSHIFT',
    35: 'DOUBLESTAR',
    36: 'PLUSEQUAL',
    37: 'MINEQUAL',
    38: 'STAREQUAL',
    39: 'SLASHEQUAL',
    40: 'PERCENTEQUAL',
    41: 'AMPEREQUAL',
    42: 'VBAREQUAL',
    43: 'CIRCUMFLEXEQUAL',
    44: 'LEFTSHIFTEQUAL',
    45: 'RIGHTSHIFTEQUAL',
    46: 'DOUBLESTAREQUAL',
    47: 'DOUBLESLASH',
    48: 'DOUBLESLASHEQUAL',
    49: 'AT',
    50: 'ATEQUAL',
    51: 'RARROW',
    52: 'ELLIPSIS',
    53: 'COLONEQUAL',
    54: 'OP',
    55: 'AWAIT',
    56: 'ASYNC',
    57: 'TYPE_IGNORE',
    58: 'TYPE_COMMENT',
    59: 'SOFT_KEYWORD',
    60: 'ERRORTOKEN',
    61: 'COMMENT',
    62: 'NL',
    63: 'ENCODING',
    64: 'N_TOKENS',
    256: 'NT_OFFSET',
}

for _number in tok_name:
    __all__.append(tok_name[_number])

EXACT_TOKEN_TYPES = {
    '!=': NOTEQUAL,
    '%': PERCENT,
    '%=': PERCENTEQUAL,
    '&': AMPER,
    '&=': AMPEREQUAL,
    '(': LPAR,
    ')': RPAR,
    '*': STAR,
    '**': DOUBLESTAR,
    '**=': DOUBLESTAREQUAL,
    '*=': STAREQUAL,
    '+': PLUS,
    '+=': PLUSEQUAL,
    ',': COMMA,
    '-': MINUS,
    '-=': MINEQUAL,
    '->': RARROW,
    '.': DOT,
    '...': ELLIPSIS,
    '/': SLASH,
    '//': DOUBLESLASH,
    '//=': DOUBLESLASHEQUAL,
    '/=': SLASHEQUAL,
    ':': COLON,
    ':=': COLONEQUAL,
    ';': SEMI,
    '<': LESS,
    '<<': LEFTSHIFT,
    '<<=': LEFTSHIFTEQUAL,
    '<=': LESSEQUAL,
    '=': EQUAL,
    '==': EQEQUAL,
    '>': GREATER,
    '>=': GREATEREQUAL,
    '>>': RIGHTSHIFT,
    '>>=': RIGHTSHIFTEQUAL,
    '@': AT,
    '@=': ATEQUAL,
    '[': LSQB,
    ']': RSQB,
    '^': CIRCUMFLEX,
    '^=': CIRCUMFLEXEQUAL,
    '{': LBRACE,
    '|': VBAR,
    '|=': VBAREQUAL,
    '}': RBRACE,
    '~': TILDE,
}


def ISTERMINAL(x):
    return x < NT_OFFSET


def ISNONTERMINAL(x):
    return x >= NT_OFFSET


def ISEOF(x):
    return x == ENDMARKER
