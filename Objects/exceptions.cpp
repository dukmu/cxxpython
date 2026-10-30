// OSError construction follows CPython 3.12.14 Objects/exceptions.c (PSF license).
#include "internal/exceptions.hpp"

#include "cxxpython/protocol.hpp"

#include <cerrno>

namespace cxxpython::core::detail {
namespace {
bool default_attribute(Attachment &c, Value type, const std::u32string &name)
{
    RootScope roots(c);
    auto method = type_attribute(c, type, name);
    if (!method)
        return false;
    const auto candidate = roots.root(*method);
    return type_attribute(c, c.object_type(), name) == candidate.get(c);
}
void add_note(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    add_exception_note(c, roots.root(args[0]), roots.root(args[1]), out);
}
AtomicGcEdge &os_field(Attachment &ctx, Value self, std::size_t field)
{
    // characters_written precedes errno, filename, filename2 and strerror.
    constexpr std::size_t indices[] = {1, 4, 2, 3};
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
bool written_value(Attachment &ctx, Handle value, ReturnSlot &out)
{
    index(ctx, value, out);
    if (out.tag() == ResultTag::Raised)
        return false;
    std::int64_t number;
    if (!int_fits_signed(ctx, out.value(), number)) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"cannot fit value into an index-sized integer", out);
        return false;
    }
    return true;
}
void written_get(Attachment &ctx, Value self, ReturnSlot &out)
{
    ctx.load(self, ctx.instance_slot(self, 0), out);
    if (out.tag() == ResultTag::Empty)
        raise_error(ctx, CoreObject::AttributeErrorType, U"characters_written", out);
}
void written_set(Attachment &ctx, Value self, std::optional<Value> value, ReturnSlot &out)
{
    if (!value) {
        ReturnSlot previous(ctx);
        written_get(ctx, self, previous);
        if (previous.tag() == ResultTag::Raised) {
            out.raised(previous.exception());
            return;
        }
        ctx.store(self, ctx.instance_slot(self, 0), {});
    } else {
        RootScope roots(ctx);
        ReturnSlot number(ctx);
        if (!written_value(ctx, roots.root(*value), number)) {
            out.raised(number.exception());
            return;
        }
        std::int64_t count;
        int_fits_signed(ctx, number.value(), count);
        ctx.store(self, ctx.instance_slot(self, 0),
                  count == -1 ? std::optional<Value>{} : number.value());
    }
    out.ok(ctx.builtin(CoreObject::None));
}
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
            const auto type = ctx.type_of(values[2]);
            const bool number = type_attribute(ctx, type, U"__index__") ||
                                type_attribute(ctx, type, U"__int__") ||
                                type_attribute(ctx, type, U"__float__") ||
                                is_subtype(ctx, type, ctx.builtin(CoreObject::ComplexType));
            if (ctx.type_of(self) == ctx.builtin(CoreObject::BlockingIOErrorType) && number) {
                written_set(ctx, self.get(ctx), values[2], out);
                if (out.tag() == ResultTag::Raised)
                    return;
            } else {
                ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 2), values[2]);
                if (values.size() == 5)
                    ctx.store(self.get(ctx), os_field(ctx, self.get(ctx), 3), values[4]);
                cxxpython::core::make_tuple(ctx, std::span(values).first(2), pair);
                stored = pair.value();
            }
        }
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
void add_exception_note(Attachment &c, Handle self, Handle note, ReturnSlot &out)
{
    RootScope roots(c);
    self = roots.root(self.get(c));
    note = roots.root(note.get(c));
    if (!c.has_layout<ExceptionObject>(self.get(c)) ||
        !is_subtype(c, c.type_of(self), c.builtin(CoreObject::BaseExceptionType))) {
        raise_error(c, CoreObject::TypeErrorType, U"add_note requires an exception", out);
        return;
    }
    if (!c.has_layout<StrObject>(note.get(c)) ||
        !is_subtype(c, c.type_of(note), c.builtin(CoreObject::StrType))) {
        raise_error(c, CoreObject::TypeErrorType, U"note must be a str", out);
        return;
    }
    ReturnSlot name(c), notes(c), dictionary(c), candidate(c);
    make_str(c, U"__notes__", name);
    const auto key = roots.root(name.value());
    get_attribute(c, self, key, notes);
    if (notes.tag() == ResultTag::Raised) {
        if (!is_subtype(c, c.type_of(notes.exception()),
                        c.builtin(CoreObject::AttributeErrorType))) {
            out.raised(notes.exception());
            return;
        }
        make_list(c, {}, candidate);
        const auto fresh = roots.root(candidate.value());
        if (auto field = c.instance_dictionary(self.get(c))) {
            c.load(self.get(c), *field, dictionary);
            if (dictionary.tag() == ResultTag::Empty) {
                c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)),
                                                candidate);
                for (;;) {
                    if (c.compare_exchange(self.get(c), *field, {}, candidate.value())) {
                        dictionary.ok(candidate.value());
                        break;
                    }
                    c.load(self.get(c), *field, dictionary);
                    if (dictionary.tag() == ResultTag::Ok)
                        break;
                }
            }
        }
        const auto type = roots.root(c.type_of(self));
        if (dictionary.tag() == ResultTag::Ok && c.has_layout<DictObject>(dictionary.value()) &&
            default_attribute(c, type.get(c), U"__getattribute__") &&
            default_attribute(c, type.get(c), U"__setattr__") &&
            !type_attribute(c, type.get(c), U"__getattr__") &&
            !type_attribute(c, type.get(c), U"__notes__")) {
            dict_setdefault(c, roots.root(dictionary.value()), key, fresh, notes);
        } else {
            set_attribute(c, self, key, fresh, out);
            if (out.tag() == ResultTag::Raised)
                return;
            notes.ok(fresh.get(c));
        }
    }
    if (notes.tag() == ResultTag::Raised) {
        out.raised(notes.exception());
        return;
    }
    if (!c.has_layout<ListObject>(notes.value()) ||
        !is_subtype(c, c.type_of(notes.value()), c.builtin(CoreObject::ListType))) {
        raise_error(c, CoreObject::TypeErrorType, U"Cannot add note: __notes__ is not a list", out);
        return;
    }
    list_append(c, roots.root(notes.value()), note, out);
}
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
    install_native_field(ctx, core, owner, U"characters_written", written_get, written_set);
    auto note = unary;
    note.parameters.push_back({U"note", ParameterKind::PositionalOnly});
    install_native_attribute(ctx, core,
                             core[static_cast<std::size_t>(CoreObject::BaseExceptionType)],
                             U"add_note", note, add_note);
}
} // namespace cxxpython::core::detail
