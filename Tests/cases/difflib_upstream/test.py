import difflib

# CPython 3.12.14 test_difflib: test_one_insert, test_one_delete,
# test_one_insert_homogenous_sequence and test_matching_blocks_cache.
matcher = difflib.SequenceMatcher(None, 'b' * 100, 'a' + 'b' * 100)
print('insert', matcher.get_opcodes(), 0.995 < matcher.ratio() < 0.996)
matcher.set_seqs('a' * 40 + 'c' + 'b' * 40, 'a' * 40 + 'b' * 40)
print('delete', matcher.get_opcodes(), 0.993 < matcher.ratio() < 0.994)
matcher.set_seqs('abxcd', 'abcd')
print('blocks', [tuple(match) for match in matcher.get_matching_blocks()])
print('cache', matcher.get_matching_blocks() is matcher.get_matching_blocks())
auto = difflib.SequenceMatcher(None, 'b' * 200, 'a' + 'b' * 200)
manual = difflib.SequenceMatcher(None, 'b' * 200, 'a' + 'b' * 200, autojunk=False)
print('autojunk', auto.ratio(), 'b' in auto.bpopular, 0.997 < manual.ratio() < 0.998)
print('close', difflib.get_close_matches('appel', ['apple', 'ape', 'maple', 'orange']))

before = ['one\n', 'two\n', 'three\n', 'four\n']
after = ['one\n', 'TWO\n', 'three\n', 'five\n']
diff = difflib.unified_diff(before, after, fromfile='old', tofile='new', n=1)
print('unified')
print(''.join(diff), end='')
delta = list(difflib.ndiff(before, after))
print('restore', list(difflib.restore(delta, 1)) == before,
      list(difflib.restore(delta, 2)) == after)
print('empty', list(difflib.unified_diff([], [])))
