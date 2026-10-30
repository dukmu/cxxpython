// Semantics follow CPython 3.12.14 Objects/weakrefobject.c.
#include "cxxpython/weakrefobject.hpp"

#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <array>
#include <cstdio>

namespace cxxpython::core {
namespace {
bool receiver(Attachment &c, Value self, ReturnSlot &out)
{
    if (c.has_layout<WeakRefObject>(self))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a weak reference", out);
    return false;
}

std::u32string type_name(Attachment &c, Value value)
{
    auto name = TypeView(c, c.type_of(value)).name();
    if (auto text = std::get_if<std::u32string>(&name))
        return *text;
    return c.payload<StrObject>(std::get<Value>(name)).value;
}

void ref_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(args[0]);
    auto base = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    if (!c.has_layout<TypeObject>(type.get(c)) || !is_subtype(c, type, base)) {
        raise_error(c, CoreObject::TypeErrorType, U"ref.__new__ requires a ref subtype", out);
        return;
    }
    if (detail::dictionary_size(c, args[2]) != 0 &&
        (type.get(c) == base.get(c) || detail::type_attribute(c, type.get(c), U"__init__") ==
                                           detail::type_attribute(c, base.get(c), U"__init__"))) {
        raise_error(c, CoreObject::TypeErrorType, U"ref() takes no keyword arguments", out);
        return;
    }
    const auto &values = c.payload<TupleObject>(args[1]).items;
    if (values.empty() || values.size() > 2) {
        raise_error(c, CoreObject::TypeErrorType, U"ref expected 1 or 2 arguments", out);
        return;
    }
    auto target = roots.root(values[0]);
    auto callback = roots.root(values.size() == 2 ? values[1] : c.builtin(CoreObject::None));
    if (!c.supports_weakrefs(target)) {
        raise_error(
            c, CoreObject::TypeErrorType,
            U"cannot create weak reference to '" + type_name(c, target.get(c)) + U"' object", out);
        return;
    }
    const bool cache = type.get(c) == base.get(c) && callback.get(c) == c.builtin(CoreObject::None);
    if (cache) {
        ReturnSlot existing(c);
        c.weakref_lookup(target, type, existing);
        if (existing.tag() == ResultTag::Raised) {
            out.raised(existing.exception());
            return;
        }
        if (existing.tag() == ResultTag::Ok) {
            out.ok(existing.value());
            return;
        }
    }
    ReturnSlot candidate(c);
    c.allocate_instance<WeakRefObject>(type, candidate, target.get(c), callback.get(c));
    auto self = roots.root(candidate.value());
    if (callback.get(c) == c.builtin(CoreObject::None))
        c.clear(self, c.payload<WeakRefObject>(self).callback);
    c.register_weakref(self, target, cache, out);
}

void ref_init(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (detail::dictionary_size(c, args[2]) != 0) {
        raise_error(c, CoreObject::TypeErrorType, U"ref() takes no keyword arguments", out);
        return;
    }
    const auto n = c.payload<TupleObject>(args[1]).items.size();
    if (n < 1 || n > 2) {
        raise_error(c, CoreObject::TypeErrorType, U"ref expected 1 or 2 arguments", out);
        return;
    }
    out.ok(c.builtin(CoreObject::None));
}

void ref_call(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    c.load(args[0], c.payload<WeakRefObject>(args[0]).target, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(CoreObject::None));
}

void ref_callback(Attachment &c, Value self, ReturnSlot &out)
{
    if (!receiver(c, self, out))
        return;
    c.load(self, c.payload<WeakRefObject>(self).callback, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(CoreObject::None));
}

void ref_hash(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &ref = c.payload<WeakRefObject>(self);
    auto cached = ref.hash.load(std::memory_order_relaxed);
    if (cached != -1) {
        make_int(c, cached, out);
        return;
    }
    ReturnSlot target(c), result(c);
    c.load(self.get(c), ref.target, target);
    if (target.tag() == ResultTag::Empty) {
        raise_error(c, CoreObject::TypeErrorType, U"weak object has gone away", out);
        return;
    }
    hash(c, roots.root(target.value()), result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    std::int64_t value;
    if (!int_fits_signed(c, result.value(), value))
        value = detail::int_hash_value(c, result.value());
    ref.hash.store(value, std::memory_order_relaxed);
    make_int(c, value, out);
}

template <CompareOp Op>
void ref_compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    if (!c.has_layout<WeakRefObject>(args[1])) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    ReturnSlot a(c), b(c);
    c.load(left.get(c), c.payload<WeakRefObject>(left).target, a);
    c.load(right.get(c), c.payload<WeakRefObject>(right).target, b);
    if (a.tag() == ResultTag::Empty || b.tag() == ResultTag::Empty) {
        bool equal = left.get(c) == right.get(c);
        if constexpr (Op == CompareOp::Ne)
            equal = !equal;
        out.ok(c.builtin(equal ? CoreObject::True : CoreObject::False));
        return;
    }
    rich_compare(c, roots.root(a.value()), roots.root(b.value()), Op, out);
}

std::u32string address(Value value)
{
    char buffer[2 + sizeof(void *) * 2 + 1];
    std::snprintf(buffer, sizeof(buffer), "%p", static_cast<void *>(value.object()));
    std::string text(buffer);
    return {text.begin(), text.end()};
}

void ref_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot target(c);
    c.load(self.get(c), c.payload<WeakRefObject>(self).target, target);
    std::u32string text = U"<weakref at " + address(self.get(c));
    if (target.tag() == ResultTag::Empty) {
        make_str(c, text + U"; dead>", out);
        return;
    }
    auto object = roots.root(target.value());
    text += U"; to '" + type_name(c, object.get(c)) + U"' at " + address(object.get(c));
    auto special = detail::type_attribute(c, c.type_of(object), U"__name__");
    if (special) {
        ReturnSlot name(c);
        detail::bind_descriptor(c, *special, object.get(c), c.type_of(object), name);
        if (name.tag() == ResultTag::Ok && c.has_layout<StrObject>(name.value()))
            text += U" (" + c.payload<StrObject>(name.value()).value + U")";
    }
    make_str(c, text + U">", out);
}
} // namespace

void detail::install_weakrefs(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    ReturnSlot wrapper(c), key(c);
    auto type = core[static_cast<std::size_t>(CoreObject::WeakRefType)];
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    const std::array captures{type.get(c)};
    install_native_attribute(c, core, type, U"__new__", variadic, ref_new,
                             std::vector<Value>(captures.begin(), captures.end()));
    auto constructor = roots.root(*name_lookup(c, *TypeView(c, type).dictionary(), U"__new__"));
    c.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapper, constructor.get(c));
    c.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                   U"__new__");
    name_store(c, *TypeView(c, type).dictionary(), key.value(), wrapper.value());
    install_native_attribute(c, core, type, U"__init__", variadic, ref_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto binary = unary;
    binary.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(c, core, type, U"__call__", unary, ref_call);
    install_native_attribute(c, core, type, U"__hash__", unary, ref_hash);
    install_native_attribute(c, core, type, U"__repr__", unary, ref_repr);
    install_native_attribute(c, core, type, U"__eq__", binary, ref_compare<CompareOp::Eq>);
    install_native_attribute(c, core, type, U"__ne__", binary, ref_compare<CompareOp::Ne>);
    install_native_field(c, core, type, U"__callback__", ref_callback);
}
} // namespace cxxpython::core
