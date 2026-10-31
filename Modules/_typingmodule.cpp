#include "cxxpython/moduleobject.hpp"
#include "cxxpython/typealiasobject.hpp"

namespace cxxpython::core::detail {
void initialize_typing(Attachment &context, Handle module, ReturnSlot &out)
{
    RootScope roots(context);
    auto dictionary = roots.root(module_dict(context, module.get(context)));
    ReturnSlot name(context);
    make_str(context, U"TypeAliasType", name);
    name_store(context, dictionary.get(context), name.value(),
               context.builtin(CoreObject::TypeAliasType));
    out.ok(context.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
