#include "cxxpython/superobject.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <array>

namespace cxxpython::core {
namespace {
void error(Attachment &c, CoreObject type, const char32_t *message, ReturnSlot &out)
{
    raise_error(c, type, message, out);
}
bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<SuperObject>(value) &&
        is_subtype(c, c.type_of(value), c.builtin(CoreObject::SuperType)))
        return true;
    error(c, CoreObject::TypeErrorType, U"super descriptor requires a super object", out);
    return false;
}
void super_new(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<TypeObject>(args[0]) ||
        !is_subtype(c, args[0], c.builtin(CoreObject::SuperType))) {
        error(c, CoreObject::TypeErrorType, U"super.__new__ requires a super subtype", out);
        return;
    }
    RootScope roots(c);
    c.allocate_instance<SuperObject>(roots.root(args[0]), out);
}
bool supercheck(Attachment &c, Handle type, Handle object, ReturnSlot &out)
{
    const auto value = object.get(c);
    if (c.has_layout<TypeObject>(value) && is_subtype(c, value, type.get(c))) {
        out.ok(value);
        return true;
    }
    const auto actual = c.type_of(value);
    if (is_subtype(c, actual, type.get(c))) {
        out.ok(actual);
        return true;
    }
    RootScope roots(c);
    ReturnSlot name(c), candidate(c);
    make_str(c, U"__class__", name);
    get_attribute(c, object, roots.root(name.value()), candidate);
    if (candidate.tag() == ResultTag::Raised) {
        if (!is_subtype(c, c.type_of(candidate.exception()),
                        c.builtin(CoreObject::AttributeErrorType))) {
            out.raised(candidate.exception());
            return false;
        }
    } else if (c.has_layout<TypeObject>(candidate.value()) && candidate.value() != actual &&
               is_subtype(c, candidate.value(), type.get(c))) {
        out.ok(candidate.value());
        return true;
    }
    error(c, CoreObject::TypeErrorType,
          U"super(type, obj): obj must be an instance or subtype of type", out);
    return false;
}
bool implicit_args(Attachment &c, ReturnSlot &type, ReturnSlot &object, ReturnSlot &out)
{
    auto current = c.current_frame();
    if (!current) {
        error(c, CoreObject::RuntimeErrorType, U"super(): no current frame", out);
        return false;
    }
    RootScope roots(c);
    auto frame_root = roots.root(*current);
    const auto &frame = c.payload<FrameObject>(frame_root);
    const auto &program = c.payload<CodeObject>(frame.code).program;
    const auto &signature = program.signature();
    if (signature.parameters.empty() ||
        signature.parameters[0].kind == ParameterKind::KeywordOnly) {
        error(c, CoreObject::RuntimeErrorType, U"super(): no arguments", out);
        return false;
    }
    const auto cell_locals = program.cell_locals();
    auto first_cell =
        std::find(cell_locals.begin(), cell_locals.end(), std::optional<std::uint32_t>{0});
    if (first_cell != cell_locals.end())
        read_cell(c, frame.cells[first_cell - cell_locals.begin()], object);
    else if (!frame.locals.empty() && frame.locals[0])
        object.ok(*frame.locals[0]);
    if (object.tag() == ResultTag::Empty) {
        error(c, CoreObject::RuntimeErrorType, U"super(): arg[0] deleted", out);
        return false;
    }
    const auto free = program.free_names();
    auto found = std::find(free.begin(), free.end(), U"__class__");
    if (found == free.end()) {
        error(c, CoreObject::RuntimeErrorType, U"super(): __class__ cell not found", out);
        return false;
    }
    const auto index = program.cell_names().size() + (found - free.begin());
    if (index >= frame.cells.size() || !c.has_layout<CellObject>(frame.cells[index])) {
        error(c, CoreObject::RuntimeErrorType, U"super(): bad __class__ cell", out);
        return false;
    }
    if (!read_cell(c, frame.cells[index], type)) {
        error(c, CoreObject::RuntimeErrorType, U"super(): empty __class__ cell", out);
        return false;
    }
    if (!c.has_layout<TypeObject>(type.value())) {
        error(c, CoreObject::RuntimeErrorType, U"super(): __class__ is not a type", out);
        return false;
    }
    return true;
}
void super_init(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    const auto &values = c.payload<TupleObject>(args[1]).items;
    if (values.size() > 2) {
        error(c, CoreObject::TypeErrorType, U"super expected at most 2 arguments", out);
        return;
    }
    ReturnSlot type(c), object(c), object_type(c), binding(c);
    if (values.empty()) {
        if (!implicit_args(c, type, object, out))
            return;
    } else {
        type.ok(values[0]);
        if (values.size() == 2)
            object.ok(values[1]);
    }
    if (!c.has_layout<TypeObject>(type.value())) {
        error(c, CoreObject::TypeErrorType, U"super() argument 1 must be a type", out);
        return;
    }
    const auto none = c.builtin(CoreObject::None);
    if (object.tag() == ResultTag::Ok && object.value() != none) {
        if (!supercheck(c, roots.root(type.value()), roots.root(object.value()), object_type)) {
            out.raised(object_type.exception());
            return;
        }
    }
    std::array tuple{type.value(), object_type.tag() == ResultTag::Ok ? object.value() : none,
                     object_type.tag() == ResultTag::Ok ? object_type.value() : none};
    cxxpython::core::make_tuple(c, tuple, binding);
    c.store(self.get(c), c.payload<SuperObject>(self).binding, binding.value());
    out.ok(none);
}
template <std::size_t Index> void super_field(Attachment &c, Value self, ReturnSlot &out)
{
    if (!receiver(c, self, out))
        return;
    ReturnSlot binding(c);
    c.load(self, c.payload<SuperObject>(self).binding, binding);
    out.ok(binding.tag() == ResultTag::Empty
               ? c.builtin(CoreObject::None)
               : c.payload<TupleObject>(binding.value()).items[Index]);
}
void super_get(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot binding(c);
    c.load(args[0], c.payload<SuperObject>(args[0]).binding, binding);
    const auto none = c.builtin(CoreObject::None);
    if (binding.tag() == ResultTag::Empty || args[1] == none ||
        c.payload<TupleObject>(binding.value()).items[1] != none) {
        out.ok(args[0]);
        return;
    }
    auto type = roots.root(c.payload<TupleObject>(binding.value()).items[0]);
    auto object = roots.root(args[1]);
    if (c.type_of(self) != c.builtin(CoreObject::SuperType)) {
        std::array positional{type.get(c), object.get(c)};
        call(c, roots.root(c.type_of(self)), {positional, {}}, out);
        return;
    }
    ReturnSlot object_type(c), result(c), tuple(c);
    if (!supercheck(c, type, object, object_type)) {
        out.raised(object_type.exception());
        return;
    }
    c.allocate_instance<SuperObject>(roots.root(c.builtin(CoreObject::SuperType)), result);
    std::array values{type.get(c), object.get(c), object_type.value()};
    cxxpython::core::make_tuple(c, values, tuple);
    c.store(result.value(), c.payload<SuperObject>(result.value()).binding, tuple.value());
    out.ok(result.value());
}
void super_getattribute(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto name = roots.root(args[1]);
    const auto &text = c.payload<StrObject>(name).value;
    ReturnSlot binding(c);
    c.load(self.get(c), c.payload<SuperObject>(self).binding, binding);
    if (text != U"__class__" && binding.tag() == ResultTag::Ok) {
        const auto &items = c.payload<TupleObject>(binding.value()).items;
        if (items[2] != c.builtin(CoreObject::None)) {
            const auto &mro = c.payload<TypeObject>(items[2]).mro;
            auto start = std::find(mro.begin(), mro.end(), items[0]);
            if (start != mro.end()) {
                for (++start; start != mro.end(); ++start) {
                    TypeView view(c, *start);
                    auto dictionary = view.dictionary();
                    if (!dictionary)
                        continue;
                    auto value = detail::name_lookup(c, *dictionary, text);
                    if (value) {
                        auto descriptor = roots.root(*value);
                        detail::bind_descriptor(c, descriptor.get(c),
                                                items[1] == items[2] ? std::optional<Value>{}
                                                                     : items[1],
                                                items[2], out);
                        return;
                    }
                }
            }
        }
    }
    auto generic = roots.root(*detail::type_attribute(c, c.object_type(), U"__getattribute__"));
    std::array positional{name.get(c)};
    detail::invoke_special(c, self, generic, {positional, {}}, out);
}
std::u32string type_name(Attachment &c, Value type)
{
    auto name = TypeView(c, type).name();
    if (auto text = std::get_if<std::u32string>(&name))
        return *text;
    return c.payload<StrObject>(std::get<Value>(name)).value;
}
void super_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    ReturnSlot binding(c);
    c.load(args[0], c.payload<SuperObject>(args[0]).binding, binding);
    std::u32string first = U"NULL", second = U"NULL";
    if (binding.tag() == ResultTag::Ok) {
        const auto &items = c.payload<TupleObject>(binding.value()).items;
        first = type_name(c, items[0]);
        if (items[2] != c.builtin(CoreObject::None))
            second = U"<" + type_name(c, items[2]) + U" object>";
    }
    make_str(c, U"<super: <class '" + first + U"'>, " + second + U">", out);
}
} // namespace

void detail::install_super(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::SuperType);
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"args";
    install_native_attribute(c, core, owner, U"__new__", variadic, super_new);
    ReturnSlot wrapper(c);
    auto constructor = roots.root(*name_lookup(c, *TypeView(c, owner).dictionary(), U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            constructor.get(c));
    auto static_new = roots.root(wrapper.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), wrapper, U"__new__");
    name_store(c, *TypeView(c, owner).dictionary(), wrapper.value(), static_new.get(c));
    install_native_attribute(c, core, owner, U"__init__", variadic, super_init);
    SignatureLayout get;
    get.parameters = {{U"self", ParameterKind::PositionalOnly},
                      {U"instance", ParameterKind::PositionalOnly},
                      {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)}};
    install_native_attribute(c, core, owner, U"__get__", get, super_get);
    SignatureLayout attribute;
    attribute.parameters = {{U"self", ParameterKind::PositionalOnly},
                            {U"name", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, owner, U"__getattribute__", attribute, super_getattribute);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, owner, U"__repr__", unary, super_repr);
    install_native_field(c, core, owner, U"__thisclass__", super_field<0>);
    install_native_field(c, core, owner, U"__self__", super_field<1>);
    install_native_field(c, core, owner, U"__self_class__", super_field<2>);
}
} // namespace cxxpython::core
