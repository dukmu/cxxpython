// Function metadata follows CPython 3.12.14 Objects/funcobject.c (PSF license).
#include "cxxpython/execution.hpp"

#include <cstdio>

namespace cxxpython::core {
void FunctionObject::trace(Tracer &tracer) const noexcept
{
    for (auto value : {code, globals, builtins})
        tracer.visit(value);
    if (closure)
        tracer.visit(*closure);
    for (auto edge :
         {&defaults, &kwdefaults, &name, &qualname, &annotations, &module, &doc, &type_params})
        tracer.visit(*edge);
}
namespace {
template <const Value FunctionObject::*Field>
void fixed_field(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<FunctionObject>(value).*Field);
}
void function_closure(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<FunctionObject>(value).closure.value_or(c.builtin(CoreObject::None)));
}
template <AtomicGcEdge FunctionObject::*Field>
void get_field(Attachment &c, Value value, ReturnSlot &out)
{
    c.load(value, c.payload<FunctionObject>(value).*Field, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(Field == &FunctionObject::type_params ? CoreObject::EmptyTuple
                                                               : CoreObject::None));
}
template <AtomicGcEdge FunctionObject::*Field>
void set_field(Attachment &c, Value self, std::optional<Value> value, ReturnSlot &out)
{
    if constexpr (Field == &FunctionObject::name || Field == &FunctionObject::qualname) {
        if (!value || !c.has_layout<StrObject>(*value)) {
            raise_error(c, CoreObject::TypeErrorType,
                        Field == &FunctionObject::name
                            ? U"__name__ must be set to a string object"
                            : U"__qualname__ must be set to a string object",
                        out);
            return;
        }
    } else if constexpr (Field == &FunctionObject::defaults ||
                         Field == &FunctionObject::kwdefaults) {
        if (value && *value == c.builtin(CoreObject::None))
            value.reset();
        const bool valid =
            !value || (Field == &FunctionObject::defaults ? c.has_layout<TupleObject>(*value)
                                                          : c.has_layout<DictObject>(*value));
        if (!valid) {
            raise_error(c, CoreObject::TypeErrorType,
                        Field == &FunctionObject::defaults
                            ? U"__defaults__ must be set to a tuple object"
                            : U"__kwdefaults__ must be set to a dict object",
                        out);
            return;
        }
    } else if constexpr (Field == &FunctionObject::type_params) {
        if (!value || !c.has_layout<TupleObject>(*value)) {
            raise_error(c, CoreObject::TypeErrorType, U"__type_params__ must be set to a tuple",
                        out);
            return;
        }
    }
    c.store(self, c.payload<FunctionObject>(self).*Field, value);
    out.ok(c.builtin(CoreObject::None));
}
void function_annotations(Attachment &context, Value value, ReturnSlot &out)
{
    auto &edge = context.payload<FunctionObject>(value).annotations;
    context.load(value, edge, out);
    if (out.tag() == ResultTag::Ok)
        return;
    RootScope roots(context);
    ReturnSlot candidate(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          candidate);
    for (;;) {
        if (context.compare_exchange(value, edge, {}, candidate.value())) {
            out.ok(candidate.value());
            return;
        }
        context.load(value, edge, out);
        if (out.tag() == ResultTag::Ok)
            return;
    }
}
void function_annotations_set(Attachment &context, Value receiver, std::optional<Value> value,
                              ReturnSlot &out)
{
    if (value && *value == context.builtin(CoreObject::None))
        value.reset();
    if (value && !context.has_layout<DictObject>(*value)) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"__annotations__ must be set to a dict object", out);
        return;
    }
    context.store(receiver, context.payload<FunctionObject>(receiver).annotations, value);
    out.ok(context.builtin(CoreObject::None));
}
void function_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<FunctionObject>(args[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"function repr requires a function", out);
        return;
    }
    ReturnSlot name(c);
    c.load(args[0], c.payload<FunctionObject>(args[0]).qualname, name);
    char buffer[2 + sizeof(void *) * 2 + 1];
    std::snprintf(buffer, sizeof(buffer), "%p", static_cast<void *>(args[0].object()));
    make_str(c,
             U"<function " + c.payload<StrObject>(name.value()).value + U" at " +
                 std::u32string(buffer, buffer + std::char_traits<char>::length(buffer)) + U">",
             out);
}
} // namespace
void detail::install_function_descriptors(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::FunctionType);
    auto field = [&](const char32_t *name, NativeGetter getter, NativeSetter setter = nullptr) {
        install_native_field(context, core, owner, name, getter, setter);
    };
    field(U"__code__", fixed_field<&FunctionObject::code>);
    install_native_member(context, core, owner, U"__globals__",
                          fixed_field<&FunctionObject::globals>);
    install_native_member(context, core, owner, U"__builtins__",
                          fixed_field<&FunctionObject::builtins>);
    install_native_member(context, core, owner, U"__closure__", function_closure);
    field(U"__name__", get_field<&FunctionObject::name>, set_field<&FunctionObject::name>);
    field(U"__qualname__", get_field<&FunctionObject::qualname>,
          set_field<&FunctionObject::qualname>);
    field(U"__module__", get_field<&FunctionObject::module>, set_field<&FunctionObject::module>);
    field(U"__doc__", get_field<&FunctionObject::doc>, set_field<&FunctionObject::doc>);
    field(U"__defaults__", get_field<&FunctionObject::defaults>,
          set_field<&FunctionObject::defaults>);
    field(U"__kwdefaults__", get_field<&FunctionObject::kwdefaults>,
          set_field<&FunctionObject::kwdefaults>);
    field(U"__type_params__", get_field<&FunctionObject::type_params>,
          set_field<&FunctionObject::type_params>);
    field(U"__annotations__", function_annotations, function_annotations_set);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, owner, U"__repr__", unary, function_repr);
}
} // namespace cxxpython::core
