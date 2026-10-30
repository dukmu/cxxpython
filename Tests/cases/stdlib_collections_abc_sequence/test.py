from collections.abc import Sequence

class MySequence(Sequence):
    def __init__(self, seq):
        self.seq = seq

    def __getitem__(self, index):
        return self.seq[index]

    def __len__(self):
        return len(self.seq)

seq = MySequence('abracadabra')
print(list(seq), list(reversed(seq)))
print('c' in seq, 'z' in seq, seq.count('a'))
print(seq.index('a'), seq.index('a', 2), seq.index('a', -4))
try:
    seq.index('z')
except ValueError as exc:
    print(type(exc).__name__, str(exc))
