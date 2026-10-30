// Proxy semantics follow CPython 3.12.14 Objects/weakrefobject.c (PSF license).
#include "internal/weakproxy.hpp"

#include "cxxpython/protocol.hpp"
#include "cxxpython/weakrefobject.hpp"

#include <cstdio>

namespace cxxpython::core {
namespace {
bool proxy(Attachment &c, Value value)
{
    const auto type = c.type_of(value);
    return type == c.builtin(CoreObject::WeakProxyType) ||
           type == c.builtin(CoreObject::CallableWeakProxyType);
}
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (proxy(c, value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a weak proxy", out);
    return false;
}
bool unwrap(Attachment &c, Value value, ReturnSlot &out)
{
    if (!proxy(c, value)) {
        out.ok(value);
        return true;
    }
    c.load(value, c.payload<WeakRefObject>(value).target, out);
    if (out.tag() == ResultTag::Ok)
        return true;
    raise_error(c, CoreObject::ReferenceErrorType, U"weakly-referenced object no longer exists",
                out);
    return false;
}
void attribute_get(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c);
    if (unwrap(c, args[0], target))
        get_attribute(c, roots.root(target.value()), roots.root(args[1]), out);
    else
        out.raised(target.exception());
}
template <bool Delete>
void attribute_set(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c);
    if (!unwrap(c, args[0], target)) {
        out.raised(target.exception());
        return;
    }
    if constexpr (Delete)
        del_attribute(c, roots.root(target.value()), roots.root(args[1]), out);
    else
        set_attribute(c, roots.root(target.value()), roots.root(args[1]), roots.root(args[2]), out);
}
template <Protocol Op>
void unary_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c);
    if (!unwrap(c, args[0], target)) {
        out.raised(target.exception());
        return;
    }
    auto object = roots.root(target.value());
    if constexpr (Op == Protocol::Bool)
        truth(c, object, out);
    else if constexpr (Op == Protocol::Str)
        str(c, object, out);
    else if constexpr (Op == Protocol::Len)
        length(c, object, out);
    else if constexpr (Op == Protocol::Index)
        index(c, object, out);
    else if constexpr (Op == Protocol::Iter)
        get_iter(c, object, out);
    else if constexpr (Op == Protocol::Next) {
        if (iter_next(c, object, out) == NextStatus::Done)
            detail::stop_iteration(c, out);
    } else if constexpr (Op == Protocol::Pos || Op == Protocol::Neg || Op == Protocol::Invert)
        unary(c, object,
              Op == Protocol::Pos   ? UnaryOp::Positive
              : Op == Protocol::Neg ? UnaryOp::Negative
                                    : UnaryOp::Invert,
              out);
}
template <BinaryOp Op, bool Reverse, bool InPlace>
void binary_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot first(c), second(c);
    if (!unwrap(c, args[0], first)) {
        out.raised(first.exception());
        return;
    }
    if (!unwrap(c, args[1], second)) {
        out.raised(second.exception());
        return;
    }
    auto left = roots.root(Reverse ? second.value() : first.value());
    auto right = roots.root(Reverse ? first.value() : second.value());
    if constexpr (InPlace)
        inplace(c, left, right, Op, out);
    else
        binary(c, left, right, Op, out);
}
template <CompareOp Op>
void compare_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot left(c), right(c);
    if (!unwrap(c, args[0], left)) {
        out.raised(left.exception());
        return;
    }
    if (!unwrap(c, args[1], right)) {
        out.raised(right.exception());
        return;
    }
    rich_compare(c, roots.root(left.value()), roots.root(right.value()), Op, out);
}
void contains(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot object(c), item(c);
    if (!unwrap(c, args[0], object) || !unwrap(c, args[1], item)) {
        out.raised(object.tag() == ResultTag::Raised ? object.exception() : item.exception());
        return;
    }
    rich_compare(c, roots.root(item.value()), roots.root(object.value()), CompareOp::In, out);
}
enum class ItemOp { Get, Set, Delete };
template <ItemOp Operation>
void item_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot object(c), key(c), value(c);
    if (!unwrap(c, args[0], object) || !unwrap(c, args[1], key)) {
        out.raised(object.tag() == ResultTag::Raised ? object.exception() : key.exception());
        return;
    }
    auto target = roots.root(object.value()), index = roots.root(key.value());
    if constexpr (Operation == ItemOp::Get)
        get_item(c, target, index, out);
    else if constexpr (Operation == ItemOp::Set) {
        if (!unwrap(c, args[2], value)) {
            out.raised(value.exception());
            return;
        }
        set_item(c, target, index, roots.root(value.value()), out);
    } else
        del_item(c, target, index, out);
    if constexpr (Operation != ItemOp::Get)
        if (out.tag() != ResultTag::Raised)
            out.ok(c.builtin(CoreObject::None));
}
void call_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c);
    if (!unwrap(c, args[0], target)) {
        out.raised(target.exception());
        return;
    }
    auto callable = roots.root(target.value());
    DictView keywords(c, args[2]);
    std::vector<KeywordArg> named;
    for (const auto &entry : keywords.entries())
        named.push_back({entry.key, entry.value});
    call(c, callable, {c.payload<TupleObject>(args[1]).items, named}, out);
}
enum class Method { Bytes, Reversed, Abs, Int, Float };
template <bool Power, bool Reverse>
void numeric_call(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot left(c), right(c), modulus(c);
    if (!unwrap(c, args[0], left) || !unwrap(c, args[1], right)) {
        out.raised(left.tag() == ResultTag::Raised ? left.exception() : right.exception());
        return;
    }
    auto first = roots.root(Reverse ? right.value() : left.value());
    auto second = roots.root(Reverse ? left.value() : right.value());
    if constexpr (Power) {
        if (!unwrap(c, args[2], modulus)) {
            out.raised(modulus.exception());
            return;
        }
        power(c, first, second, roots.root(modulus.value()), out);
    } else
        divmod(c, first, second, out);
}
template <Method Name>
void method_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c), method(c), key(c);
    if (!unwrap(c, args[0], target)) {
        out.raised(target.exception());
        return;
    }
    auto object = roots.root(target.value());
    if constexpr (Name == Method::Int || Name == Method::Float) {
        const Value values[] = {object.get(c)};
        call(c,
             roots.root(
                 c.builtin(Name == Method::Int ? CoreObject::IntType : CoreObject::FloatType)),
             {values}, out);
    } else {
        const auto name = Name == Method::Bytes      ? U"__bytes__"
                          : Name == Method::Reversed ? U"__reversed__"
                                                     : U"__abs__";
        if constexpr (Name == Method::Abs) {
            auto slot = detail::type_attribute(c, c.type_of(object), name);
            if (!slot) {
                raise_error(c, CoreObject::TypeErrorType, U"bad operand type for abs()", out);
                return;
            }
            detail::invoke_special(c, object, roots.root(*slot), {}, out);
        } else {
            make_str(c, name, key);
            get_attribute(c, object, roots.root(key.value()), method);
            if (method.tag() == ResultTag::Raised)
                out.raised(method.exception());
            else
                call(c, roots.root(method.value()), {}, out);
        }
    }
}
std::u32string address(Value value)
{
    char buffer[2 + sizeof(void *) * 2 + 1];
    std::snprintf(buffer, sizeof(buffer), "%p", static_cast<void *>(value.object()));
    return {buffer, buffer + std::char_traits<char>::length(buffer)};
}
void repr_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    ReturnSlot target(c);
    c.load(args[0], c.payload<WeakRefObject>(args[0]).target, target);
    auto object =
        roots.root(target.tag() == ResultTag::Ok ? target.value() : c.builtin(CoreObject::None));
    auto name = TypeView(c, c.type_of(object)).name();
    auto text = std::holds_alternative<Value>(name)
                    ? c.payload<StrObject>(std::get<Value>(name)).value
                    : std::get<std::u32string>(name);
    make_str(c,
             U"<weakproxy at " + address(args[0]) + U" to " + text + U" at " +
                 address(object.get(c)) + U">",
             out);
}
} // namespace
namespace detail {
void make_weakproxy(Attachment &c, Handle target, Handle callback, ReturnSlot &out)
{
    RootScope roots(c);
    target = roots.root(target.get(c));
    callback = roots.root(callback.get(c));
    if (!c.supports_weakrefs(target)) {
        raise_error(c, CoreObject::TypeErrorType, U"cannot create weak reference to this object",
                    out);
        return;
    }
    const bool cache = callback.get(c) == c.builtin(CoreObject::None);
    if (cache)
        for (auto id : {CoreObject::WeakProxyType, CoreObject::CallableWeakProxyType}) {
            c.weakref_lookup(target, roots.root(c.builtin(id)), out);
            if (out.tag() != ResultTag::Empty)
                return;
        }
    auto type = roots.root(c.builtin(type_attribute(c, c.type_of(target), U"__call__")
                                         ? CoreObject::CallableWeakProxyType
                                         : CoreObject::WeakProxyType));
    ReturnSlot candidate(c);
    c.allocate_instance<WeakRefObject>(type, candidate, target.get(c), callback.get(c));
    auto created = roots.root(candidate.value());
    if (cache)
        c.clear(created, c.payload<WeakRefObject>(created).callback);
    c.register_weakref(created, target, cache, out);
}
void install_weakproxies(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    auto set = pair;
    set.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    for (auto id : {CoreObject::WeakProxyType, CoreObject::CallableWeakProxyType}) {
        auto owner = at(id);
        auto install = [&](const char32_t *name, NativeEntry entry, const SignatureLayout &sig) {
            install_native_attribute(c, core, owner, name, sig, entry);
        };
        install(U"__getattribute__", attribute_get, pair);
        install(U"__setattr__", attribute_set<false>, set);
        install(U"__delattr__", attribute_set<true>, pair);
        install(U"__repr__", repr_proxy, unary);
        install(U"__str__", unary_proxy<Protocol::Str>, unary);
        install(U"__bool__", unary_proxy<Protocol::Bool>, unary);
        install(U"__len__", unary_proxy<Protocol::Len>, unary);
        install(U"__iter__", unary_proxy<Protocol::Iter>, unary);
        install(U"__next__", unary_proxy<Protocol::Next>, unary);
        install(U"__index__", unary_proxy<Protocol::Index>, unary);
        install(U"__pos__", unary_proxy<Protocol::Pos>, unary);
        install(U"__neg__", unary_proxy<Protocol::Neg>, unary);
        install(U"__invert__", unary_proxy<Protocol::Invert>, unary);
        install(U"__int__", method_proxy<Method::Int>, unary);
        install(U"__float__", method_proxy<Method::Float>, unary);
        install(U"__abs__", method_proxy<Method::Abs>, unary);
        install(U"__bytes__", method_proxy<Method::Bytes>, unary);
        install(U"__reversed__", method_proxy<Method::Reversed>, unary);
        install(U"__getitem__", item_proxy<ItemOp::Get>, pair);
        install(U"__setitem__", item_proxy<ItemOp::Set>, set);
        install(U"__delitem__", item_proxy<ItemOp::Delete>, pair);
        install(U"__contains__", contains, pair);
        install(U"__eq__", compare_proxy<CompareOp::Eq>, pair);
        install(U"__ne__", compare_proxy<CompareOp::Ne>, pair);
        install(U"__lt__", compare_proxy<CompareOp::Lt>, pair);
        install(U"__le__", compare_proxy<CompareOp::Le>, pair);
        install(U"__gt__", compare_proxy<CompareOp::Gt>, pair);
        install(U"__ge__", compare_proxy<CompareOp::Ge>, pair);
#define PROXY_BINARY(name, op)                                                                     \
    install(U"__" name U"__", binary_proxy<BinaryOp::op, false, false>, pair);                     \
    install(U"__r" name U"__", binary_proxy<BinaryOp::op, true, false>, pair);                     \
    install(U"__i" name U"__", binary_proxy<BinaryOp::op, false, true>, pair)
        PROXY_BINARY(U"add", Add);
        PROXY_BINARY(U"sub", Subtract);
        PROXY_BINARY(U"mul", Multiply);
        PROXY_BINARY(U"truediv", TrueDivide);
        PROXY_BINARY(U"floordiv", FloorDivide);
        PROXY_BINARY(U"mod", Modulo);
        PROXY_BINARY(U"lshift", LeftShift);
        PROXY_BINARY(U"rshift", RightShift);
        PROXY_BINARY(U"and", BitAnd);
        PROXY_BINARY(U"or", BitOr);
        PROXY_BINARY(U"xor", BitXor);
        PROXY_BINARY(U"matmul", MatrixMultiply);
#undef PROXY_BINARY
        auto power = pair;
        power.parameters.push_back(
            {U"modulus", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
        install(U"__pow__", numeric_call<true, false>, power);
        install(U"__rpow__", numeric_call<true, true>, power);
        install(U"__ipow__", binary_proxy<BinaryOp::Power, false, true>, pair);
        install(U"__divmod__", numeric_call<false, false>, pair);
        install(U"__rdivmod__", numeric_call<false, true>, pair);
        RootScope roots(c);
        ReturnSlot key(c);
        c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
        name_store(c, *TypeView(c, owner).dictionary(), key.value(), at(CoreObject::None).get(c));
    }
    auto variable = unary;
    variable.varargs = U"args";
    variable.varkwargs = U"kwargs";
    install_native_attribute(c, core, at(CoreObject::CallableWeakProxyType), U"__call__", variable,
                             call_proxy);
}
} // namespace detail
} // namespace cxxpython::core
