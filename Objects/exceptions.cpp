// OSError construction follows CPython 3.12.14 Objects/exceptions.c (PSF license).
#include "internal/exceptions.hpp"

#include "cxxpython/protocol.hpp"

#include <cerrno>

namespace cxxpython::core::detail {
namespace {
AtomicGcEdge &os_field(Attachment &ctx, Value self, std::size_t field)
{
    // Native tail slots are sorted by TypeReady: errno, filename, filename2, strerror.
    constexpr std::size_t indices[] = {0, 3, 1, 2};
    return ctx.instance_slot(self, indices[field]);
}
CoreObject errno_type(std::int64_t error)
{
    switch (error) {
    case ENOENT:
        return CoreObject::FileNotFoundErrorType;
    case EACCES:
    case EPERM:
        return CoreObject::PermissionErrorType;
    case EEXIST:
        return CoreObject::FileExistsErrorType;
    case EISDIR:
        return CoreObject::IsADirectoryErrorType;
    case ENOTDIR:
        return CoreObject::NotADirectoryErrorType;
    case EINTR:
        return CoreObject::InterruptedErrorType;
    case EAGAIN:
        return CoreObject::BlockingIOErrorType;
    case EPIPE:
        return CoreObject::BrokenPipeErrorType;
    default:
        return CoreObject::OSErrorType;
    }
}
void os_values(Attachment &, Value, Value, ReturnSlot &);
bool use_init(Attachment &ctx, Value type)
{
    const auto base = ctx.builtin(CoreObject::OSErrorType);
    return type_attribute(ctx, type, U"__init__") != type_attribute(ctx, base, U"__init__") &&
           type_attribute(ctx, type, U"__new__") == type_attribute(ctx, base, U"__new__");
}
void os_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    if (!ctx.has_layout<TypeObject>(args[0]) ||
        !is_subtype(ctx, args[0], ctx.builtin(CoreObject::OSErrorType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"OSError.__new__ requires OSError subtype",
                    out);
        return;
    }
    const bool deferred = use_init(ctx, type.get(ctx));
    if (!deferred && !DictView(ctx, args[2]).entries().empty()) {
        raise_error(ctx, CoreObject::TypeErrorType, U"OSError() takes no keyword arguments", out);
        return;
    }
    const auto &values = ctx.payload<TupleObject>(args[1]).items;
    if (values.size() >= 2 && values.size() <= 5 &&
        type.get(ctx) == ctx.builtin(CoreObject::OSErrorType)) {
        std::int64_t error;
        if (is_int(ctx, values[0]) && int_fits_signed(ctx, values[0], error))
            type = roots.root(ctx.builtin(errno_type(error)));
    }
    ctx.allocate_instance<ExceptionObject>(
        type, out, deferred ? ctx.builtin(CoreObject::EmptyTuple) : args[1]);
    const auto self = roots.root(out.value());
    if (deferred)
        return;
    ReturnSlot initialized(ctx);
    os_values(ctx, self.get(ctx), args[1], initialized);
    if (initialized.tag() == ResultTag::Raised)
        out.raised(initialized.exception());
}
void os_values(Attachment &ctx, Value receiver, Value arguments, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(receiver);
    const auto tuple = roots.root(arguments);
    const auto &values = ctx.payload<TupleObject>(tuple).items;
    const auto none = ctx.builtin(CoreObject::None);
    for (std::size_t i = 0; i < 4; ++i)
        ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), i), none);
    Value stored = tuple.get(ctx);
    ReturnSlot pair(ctx);
    if (values.size() >= 2 && values.size() <= 5) {
        ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 0), values[0]);
        ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 1), values[1]);
        if (values.size() >= 3 && values[2] != none) {
            ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 2), values[2]);
            cxxpython::core::make_tuple(ctx, std::span(values).first(2), pair);
            stored = pair.value();
        }
        if (values.size() == 5)
            ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 3), values[4]);
    }
    ctx.store(self.get(ctx), ctx.payload<ExceptionObject>(self).args, stored);
    out.ok(none);
}
void os_init(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (use_init(ctx, ctx.type_of(args[0])))
        os_values(ctx, args[0], args[1], out);
    else
        out.ok(ctx.builtin(CoreObject::None));
}
template <std::size_t Field> void os_get(Attachment &ctx, Value self, ReturnSlot &out)
{
    ctx.load(self, os_field(ctx, self, Field), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(ctx.builtin(CoreObject::None));
}
template <std::size_t Field>
void os_set(Attachment &ctx, Value self, std::optional<Value> value, ReturnSlot &out)
{
    ctx.store(self, os_field(ctx, self, Field), value);
    out.ok(ctx.builtin(CoreObject::None));
}
void os_str(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    std::array<ReturnSlot, 4> fields{ReturnSlot(ctx), ReturnSlot(ctx), ReturnSlot(ctx),
                                     ReturnSlot(ctx)};
    for (std::size_t i = 0; i < fields.size(); ++i)
        ctx.load(self.get(ctx), os_field(ctx, self.get(ctx), i), fields[i]);
    const auto none = ctx.builtin(CoreObject::None);
    const auto field = [&](std::size_t i) {
        return fields[i].tag() == ResultTag::Empty ? none : fields[i].value();
    };
    if (field(0) == none || field(1) == none) {
        ReturnSlot stored(ctx);
        ctx.load(self.get(ctx), ctx.payload<ExceptionObject>(self).args, stored);
        const auto &values = ctx.payload<TupleObject>(stored.value()).items;
        if (values.empty())
            make_str(ctx, U"", out);
        else
            str(ctx, roots.root(values.size() == 1 ? values[0] : stored.value()), out);
        return;
    }
    std::u32string text = U"[Errno ";
    ReturnSlot part(ctx);
    auto append = [&](Value value, bool quoted) {
        if (quoted)
            repr(ctx, roots.root(value), part);
        else
            str(ctx, roots.root(value), part);
        if (part.tag() == ResultTag::Raised) {
            out.raised(part.exception());
            return false;
        }
        text += ctx.payload<StrObject>(part.value()).value;
        return true;
    };
    if (!append(field(0), false))
        return;
    text += U"] ";
    if (!append(field(1), false))
        return;
    if (field(2) != none) {
        text += U": ";
        if (!append(field(2), true))
            return;
        if (field(3) != none) {
            text += U" -> ";
            if (!append(field(3), true))
                return;
        }
    }
    make_str(ctx, std::move(text), out);
}
} // namespace
void install_os_errors(Attachment &ctx, std::span<const Handle> core)
{
    const auto owner = core[static_cast<std::size_t>(CoreObject::OSErrorType)];
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(ctx, core, owner, U"__new__", constructor, os_new);
    RootScope roots(ctx);
    ReturnSlot wrapped(ctx), key(ctx);
    const auto dictionary = *ctx.payload<TypeObject>(owner).dictionary;
    const auto function = *name_lookup(ctx, dictionary, U"__new__");
    ctx.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapped, function);
    ctx.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                     U"__new__");
    name_store(ctx, dictionary, key.value(), wrapped.value());
    SignatureLayout init;
    init.parameters = {{U"self", ParameterKind::PositionalOnly}};
    init.varargs = U"args";
    install_native_attribute(ctx, core, owner, U"__init__", init, os_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, owner, U"__str__", unary, os_str);
    install_native_field(ctx, core, owner, U"errno", os_get<0>, os_set<0>);
    install_native_field(ctx, core, owner, U"strerror", os_get<1>, os_set<1>);
    install_native_field(ctx, core, owner, U"filename", os_get<2>, os_set<2>);
    install_native_field(ctx, core, owner, U"filename2", os_get<3>, os_set<3>);
}
} // namespace cxxpython::core::detail
