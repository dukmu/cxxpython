#include "internal/iomodule.hpp"

#include "cxxpython/import.hpp"
#include "internal/bufferedio.hpp"
#include "internal/ioopen.hpp"
#include "internal/memoryio.hpp"
#include "internal/textio.hpp"

namespace cxxpython::core::detail {
namespace {
void state_anchor(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(c.builtin(CoreObject::None));
}
} // namespace
void initialize_io(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c), value(c);
    if (auto state = name_lookup(c, dictionary.get(c), U"_io_state")) {
        if (c.has_layout<NativeFunctionObject>(*state) &&
            c.payload<NativeFunctionObject>(*state).entry == state_anchor) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
    }
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
    if (out.tag() != ResultTag::Raised)
        initialize_bufferedio(c, module, out);
    if (out.tag() != ResultTag::Raised)
        initialize_textio(c, module, out);
    if (out.tag() != ResultTag::Raised)
        initialize_textio_wrapper(c, module, out);
    if (out.tag() != ResultTag::Raised)
        initialize_open(c, module, out);
    if (out.tag() != ResultTag::Raised) {
        make_native_function(c, "_io_state", {}, state_anchor, {}, value,
                             NativeFunctionBinding::Function);
        auto state = roots.root(value.value());
        make_str(c, U"_io_state", key);
        dict_store(c, dictionary, roots.root(key.value()), state, out);
    }
}
} // namespace cxxpython::core::detail
