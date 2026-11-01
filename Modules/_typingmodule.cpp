#include "cxxpython/moduleobject.hpp"
#include "cxxpython/typealiasobject.hpp"
#include "cxxpython/typevarobject.hpp"

namespace cxxpython::core::detail {
namespace {
void identity(Attachment &, Value, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
} // namespace
void initialize_typing(Attachment &context, Handle module, ReturnSlot &out)
{
    RootScope roots(context);
    auto dictionary = roots.root(module_dict(context, module.get(context)));
    ReturnSlot name(context);
    for (auto [label, id] : {std::pair{U"TypeAliasType", CoreObject::TypeAliasType},
                             {U"TypeVar", CoreObject::TypeVarType},
                             {U"ParamSpec", CoreObject::ParamSpecType},
                             {U"TypeVarTuple", CoreObject::TypeVarTupleType},
                             {U"ParamSpecArgs", CoreObject::ParamSpecArgsType},
                             {U"ParamSpecKwargs", CoreObject::ParamSpecKwargsType},
                             {U"Generic", CoreObject::GenericType}}) {
        make_str(context, label, name);
        name_store(context, dictionary.get(context), name.value(), context.builtin(id));
    }
    SignatureLayout signature;
    signature.parameters = {{U"x", ParameterKind::PositionalOnly}};
    ReturnSlot function(context);
    make_native_function(context, "_idfunc", signature, identity, {}, function);
    make_str(context, U"_idfunc", name);
    name_store(context, dictionary.get(context), name.value(), function.value());
    out.ok(context.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
