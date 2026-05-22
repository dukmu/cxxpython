from star_all_source import *

print(public)
print(renamed)
try:
    print(hidden)
except NameError:
    print('hidden missing')

from star_public_source import *

print(visible)
print(another)
try:
    print(_hidden)
except NameError:
    print('private missing')
