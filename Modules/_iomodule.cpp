#include "internal/iomodule.hpp"

#include "cxxpython/import.hpp"
#include "internal/memoryio.hpp"

namespace cxxpython::core::detail {
void initialize_io(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c), value(c);
    for (const auto &[name, id] : {std::pair{U"_IOBase", CoreObject::IOBaseType},
                                   {U"_RawIOBase", CoreObject::RawIOBaseType},
                                   {U"_BufferedIOBase", CoreObject::BufferedIOBaseType},
                                   {U"_TextIOBase", CoreObject::TextIOBaseType},
                                   {U"FileIO", CoreObject::FileIOType},
                                   {U"UnsupportedOperation", CoreObject::UnsupportedOperationType},
                                   {U"BlockingIOError", CoreObject::BlockingIOErrorType}}) {
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), roots.root(c.builtin(id)), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    make_str(c, U"DEFAULT_BUFFER_SIZE", key);
    make_int(c, 8192, value);
    dict_store(c, dictionary, roots.root(key.value()), roots.root(value.value()), out);
    if (out.tag() != ResultTag::Raised)
        initialize_memoryio(c, module, out);
}
} // namespace cxxpython::core::detail
