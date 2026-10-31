#include "internal/picklemodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/picklebufobject.hpp"

namespace cxxpython::core::detail {
void initialize_pickle(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    make_picklebuffer_type(c, out);
    if (out.tag() == ResultTag::Raised)
        return;
    const auto type = roots.root(out.value());
    ReturnSlot key(c);
    make_str(c, U"PickleBuffer", key);
    dict_store(c, roots.root(module_dict(c, module.get(c))), roots.root(key.value()), type, out);
    if (out.tag() != ResultTag::Raised)
        out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
