import package_fixture.child
import package_fixture.child as child_alias
from package_fixture.child import value
from package_fixture import sibling

print(package_fixture.label)
print(package_fixture.path_len)
print(package_fixture.spec_name)
print(package_fixture.spec_locations_len)
print(package_fixture.child.value)
print(child_alias is package_fixture.child)
print(value)
print(sibling.value)

top = __import__('package_fixture.child')
leaf = __import__('package_fixture.child', None, None, ('value',), 0)
print(top is package_fixture)
print(leaf is package_fixture.child)
