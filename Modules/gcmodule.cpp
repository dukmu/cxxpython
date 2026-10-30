#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
namespace {
void collect(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    index(context, roots.root(args[0]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    std::int64_t generation;
    if (!int_fits_signed(context, out.value(), generation) || generation < 0 || generation > 2) {
        raise_error(context, CoreObject::ValueErrorType, U"invalid generation", out);
        return;
    }
    // Generation arguments currently select the same full tracing collection;
    // the return count is physical reclamation, not invented CPython gen stats.
    const auto count = context.collect();
    make_int(context, count, out);
}
template <bool Enabled>
void set_enabled(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    context.set_gc_enabled(Enabled);
    out.ok(context.builtin(CoreObject::None));
}
void is_enabled(Attachment &context, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(context.builtin(context.gc_enabled() ? CoreObject::True : CoreObject::False));
}
void is_finalized(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    out.ok(context.builtin(context.is_finalized(roots.root(args[0])) ? CoreObject::True
                                                                     : CoreObject::False));
}
} // namespace
void initialize_gc(Attachment &context, Handle module, ReturnSlot &out)
{
    RootScope roots(context);
    const auto dictionary = roots.root(module_dict(context, module.get(context)));
    ReturnSlot name(context), function(context), two(context);
    make_int(context, 2, two);
    auto install = [&](const char32_t *key, SignatureLayout signature, NativeEntry entry) {
        const std::u32string_view text(key);
        make_native_function(context, std::string(text.begin(), text.end()), std::move(signature),
                             entry, {}, function);
        make_str(context, key, name);
        dict_store(context, dictionary, roots.root(name.value()), roots.root(function.value()),
                   out);
        return out.tag() != ResultTag::Raised;
    };
    SignatureLayout generation;
    generation.parameters = {{U"generation", ParameterKind::PositionalOrKeyword, two.value()}};
    if (!install(U"collect", generation, collect) || !install(U"enable", {}, set_enabled<true>) ||
        !install(U"disable", {}, set_enabled<false>) || !install(U"isenabled", {}, is_enabled))
        return;
    SignatureLayout unary;
    unary.parameters = {{U"obj", ParameterKind::PositionalOnly}};
    if (install(U"is_finalized", unary, is_finalized))
        out.ok(context.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
