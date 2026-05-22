import package_fixture.child
import package_fixture.child as child_alias
from package_fixture.child import value
from package_fixture import sibling
from package_fixture.relative_imports import sibling_value, child_value, child_alias_value, package_name

print(package_fixture.label)
print(package_fixture.path_len)
print(package_fixture.spec_name)
print(package_fixture.spec_locations_len)
print(package_fixture.child.value)
print(child_alias is package_fixture.child)
print(value)
print(sibling.value)
print(sibling_value)
print(child_value)
print(child_alias_value)
print(package_name)

top = __import__('package_fixture.child')
leaf = __import__('package_fixture.child', None, None, ('value',), 0)
print(top is package_fixture)
print(leaf is package_fixture.child)

package_fixture.__path__.append(package_fixture.__path__[0] + '/../package_extra/package_fixture')
import package_fixture.extra
print(package_fixture.extra.value)
