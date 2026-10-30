// Module operations follow CPython 3.12.14 Modules/_weakref.c.
#include "cxxpython/weakrefobject.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/objects.hpp"
#include "internal/weakproxy.hpp"

namespace cxxpython::core {
namespace {
void getweakrefcount(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto target = roots.root(args[0]);
    std::vector<Value> values;
    c.weakrefs(target, roots, values);
    make_int(c, static_cast<std::int64_t>(values.size()), out);
}

void getweakrefs(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto target = roots.root(args[0]);
    std::vector<Value> values;
    c.weakrefs(target, roots, values);
    make_list(c, values, out);
}
void make_proxy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    detail::make_weakproxy(c, roots.root(args[0]), roots.root(args[1]), out);
}
} // namespace

void detail::initialize_weakref(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot type(c), key(c), function(c), result(c);
    auto ref_type = roots.root(c.builtin(CoreObject::WeakRefType));
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    auto publish = [&](const char32_t *name, Handle value) {
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), value, result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    if (!publish(U"ref", ref_type) || !publish(U"ReferenceType", ref_type) ||
        !publish(U"ProxyType", roots.root(c.builtin(CoreObject::WeakProxyType))) ||
        !publish(U"CallableProxyType", roots.root(c.builtin(CoreObject::CallableWeakProxyType))))
        return;
    SignatureLayout unary;
    unary.parameters = {{U"object", ParameterKind::PositionalOnly}};
    make_native_function(c, "getweakrefcount", unary, getweakrefcount, {}, function,
                         NativeFunctionBinding::Function);
    if (!publish(U"getweakrefcount", roots.root(function.value())))
        return;
    make_native_function(c, "getweakrefs", unary, getweakrefs, {}, function,
                         NativeFunctionBinding::Function);
    if (!publish(U"getweakrefs", roots.root(function.value())))
        return;
    unary.parameters.push_back(
        {U"callback", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)});
    make_native_function(c, "proxy", unary, make_proxy, {}, function,
                         NativeFunctionBinding::Function);
    if (!publish(U"proxy", roots.root(function.value())))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core
