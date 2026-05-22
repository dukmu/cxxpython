from . import sibling
from . import child as child_alias
from .child import value

sibling_value = sibling.value
child_value = value
child_alias_value = child_alias.value
package_name = __package__
