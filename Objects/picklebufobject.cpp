// Adapted from CPython 3.12.14 Objects/picklebufobject.c (PSF license).
#include "cxxpython/picklebufobject.hpp"

#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
namespace {
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<PickleBufferObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected PickleBuffer", out);
    return false;
}
void create(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto type = roots.root(args[0]);
    detail::BufferExport buffer(c);
    if (!detail::export_buffer(c, roots.root(args[1]), buffer, out)) {
        if (out.tag() != ResultTag::Raised)
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    c.allocate_instance<PickleBufferObject>(type, out, buffer.exporter.value(), buffer.lease);
}
void raw(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    detail::BufferExport buffer(c);
    if (detail::export_buffer(c, roots.root(args[0]), buffer, out))
        detail::memoryview_from_buffer(c, buffer, out, true);
}
void release(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    auto &buffer = c.payload<PickleBufferObject>(self);
    c.clear(self, buffer.exporter);
    std::atomic_exchange_explicit(&buffer.lease, std::shared_ptr<MemoryViewExport>{},
                                  std::memory_order_acq_rel);
    out.ok(c.builtin(CoreObject::None));
}
} // namespace

void detail::make_picklebuffer_type(Attachment &c, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"PickleBuffer"}, LayoutDescriptor::of<PickleBufferObject>()};
    spec.type.module = U"pickle";
    spec.type.immutable = true;
    spec.type.subclassable = false;
    spec.type.slots = std::vector<std::u32string>{U"__weakref__"};
    create_native_type(c, spec, out);
    if (out.tag() == ResultTag::Raised)
        return;
    const auto type = roots.root(out.value());
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"buffer", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, type, U"__new__", constructor, create);
    const auto dictionary = roots.root(*TypeView(c, type).dictionary());
    const auto function = roots.root(*name_lookup(c, dictionary.get(c), U"__new__"));
    ReturnSlot wrapper(c), name(c);
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            wrapper, function.get(c));
    make_str(c, U"__new__", name);
    name_store(c, dictionary.get(c), name.value(), wrapper.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, type, U"raw", unary, raw);
    install_native_attribute(c, core, type, U"release", unary, release);
    name_erase(c, dictionary.get(c), U"__weakref__");
    out.ok(type.get(c));
}
} // namespace cxxpython::core
