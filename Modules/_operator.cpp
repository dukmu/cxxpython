// PyNumber_Index kernel from CPython 3.12.14 Modules/_operator.c (PSF license).
#include "internal/operatormodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
namespace {
void index_entry(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    index(ctx, roots.root(args[0]), out);
}
} // namespace
void initialize_operator(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot key(ctx), function(ctx);
    SignatureLayout unary;
    unary.parameters = {{U"a", ParameterKind::PositionalOnly}};
    make_native_function(ctx, "index", unary, index_entry, {}, function,
                         NativeFunctionBinding::Function);
    if (function.tag() == ResultTag::Raised) {
        out.raised(function.exception());
        return;
    }
    const auto kernel = roots.root(function.value());
    make_str(ctx, U"index", key);
    dict_store(ctx, dictionary, roots.root(key.value()), kernel, out);
    if (out.tag() == ResultTag::Raised)
        return;
    make_str(ctx,
             U"Operator interface.\n\nThis module exports a set of functions implemented in C "
             U"corresponding\nto the intrinsic operators of Python.  For example, operator.add(x, "
             U"y)\nis equivalent to the expression x+y.  The function names are those\nused for "
             U"special methods; variants without leading and trailing\n'__' are also provided for "
             U"convenience.",
             function);
    const auto documentation = roots.root(function.value());
    make_str(ctx, U"__doc__", key);
    dict_store(ctx, dictionary, roots.root(key.value()), documentation, out);
}
} // namespace cxxpython::core::detail
