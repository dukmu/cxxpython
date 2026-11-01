import getopt
import pdb
import shlex

print(shlex.split('break "file name.py:12" # comment', comments=True))
print(shlex.join(['file name.py', "a'b"]))
print(getopt.getopt(['-c', 'continue', '--module', 'target'], 'c:', ['module']))
try:
    getopt.getopt(['--unknown'], '', ['module'])
except getopt.GetoptError as error:
    print(type(error).__name__, str(error))
print(pdb.Pdb.__module__)
