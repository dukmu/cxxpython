import select

class NoFileno:
    pass

class BadResult:
    def fileno(self):
        return '1'

class Raises:
    def fileno(self):
        raise RuntimeError('fileno failure')

class GetterRaises:
    @property
    def fileno(self):
        raise RuntimeError('getter failure')

class BadIndex:
    def __index__(self):
        return '0'

class IndexRaises:
    def __index__(self):
        raise RuntimeError('index failure')

def check(label, reads, timeout=0):
    try:
        select.select(reads, [], [], timeout)
    except Exception as exc:
        print(label, type(exc).__name__)
    else:
        print(label, 'no error')

check('no fileno', [NoFileno()])
check('bad result', [BadResult()])
check('fileno raises', [Raises()])
check('getter raises', [GetterRaises()])
check('negative fd', [-1])
check('huge fd', [1 << 100])
check('out of range', [1024])
check('too many', [0] * 1025)
check('negative timeout', [], -1)
check('negative float', [], -0.5)
check('nan', [], float('nan'))
check('infinity', [], float('inf'))
check('huge timeout', [], 1 << 100)
check('bad timeout', [], '0')
check('bad index', [], BadIndex())
check('index raises', [], IndexRaises())
