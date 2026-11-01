// Object reduction follows CPython 3.12.14 Objects/typeobject.c (PSF license).
#include "internal/reduction.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"

#include <limits>

namespace cxxpython::core::detail {
namespace {
bool failed(const ReturnSlot &slot)
{
    return slot.tag() == ResultTag::Raised;
}
bool attribute(Attachment &c, Handle self, std::u32string_view name, ReturnSlot &out,
               bool optional = false)
{
    RootScope roots(c);
    ReturnSlot key(c);
    make_str(c, std::u32string(name), key);
    if (failed(key)) {
        out.raised(key.exception());
        return false;
    }
    get_attribute(c, self, roots.root(key.value()), out);
    if (optional && failed(out) &&
        is_subtype(c, c.type_of(out.exception()), c.builtin(CoreObject::AttributeErrorType)))
        out.reset(c);
    return !failed(out);
}
bool copyreg(Attachment &c, std::u32string_view name, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot module(c);
    import_module(c, U"copyreg", module);
    if (failed(module)) {
        out.raised(module.exception());
        return false;
    }
    return attribute(c, roots.root(module.value()), name, out);
}
void default_state(Attachment &c, Handle self, bool required, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = roots.root(c.type_of(self));
    if (required && c.payload<TypeObject>(type).instance_layout.size > sizeof(InstanceObject)) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"cannot pickle this native object without constructor arguments", out);
        return;
    }
    ReturnSlot state(c), slotnames(c), slots(c), name(c), value(c);
    state.ok(c.builtin(CoreObject::None));
    if (auto dictionary = c.instance_dictionary(self.get(c))) {
        c.load(self.get(c), *dictionary, value);
        if (value.tag() == ResultTag::Ok && dictionary_size(c, value.value()))
            state.ok(value.value());
    }
    if (!copyreg(c, U"_slotnames", slotnames)) {
        out.raised(slotnames.exception());
        return;
    }
    const Value args[] = {type.get(c)};
    call(c, roots.root(slotnames.value()), {args}, slotnames);
    if (failed(slotnames)) {
        out.raised(slotnames.exception());
        return;
    }
    if (slotnames.value() == c.builtin(CoreObject::None)) {
        out.ok(state.value());
        return;
    }
    if (!c.has_layout<ListObject>(slotnames.value())) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"copyreg._slotnames didn't return a list or None", out);
        return;
    }
    auto names = roots.root(slotnames.value());
    const auto count = list_size(c, names);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), slots);
    auto fields = roots.root(slots.value());
    for (std::size_t i = 0; i < count; ++i) {
        RootScope iteration(c);
        list_item(c, names, i, name);
        if (name.tag() == ResultTag::Empty) {
            raise_error(c, CoreObject::RuntimeErrorType,
                        U"__slotsname__ changed size during iteration", out);
            return;
        }
        auto key = iteration.root(name.value());
        get_attribute(c, self, key, value);
        if (failed(value)) {
            if (!is_subtype(c, c.type_of(value.exception()),
                            c.builtin(CoreObject::AttributeErrorType))) {
                out.raised(value.exception());
                return;
            }
        } else {
            dict_store(c, fields, key, iteration.root(value.value()), out);
            if (failed(out))
                return;
        }
        if (list_size(c, names) != count) {
            raise_error(c, CoreObject::RuntimeErrorType,
                        U"__slotsname__ changed size during iteration", out);
            return;
        }
    }
    if (dictionary_size(c, fields.get(c))) {
        const Value pair[] = {state.value(), fields.get(c)};
        cxxpython::core::make_tuple(c, pair, out);
    } else
        out.ok(state.value());
}
void object_getstate(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    default_state(c, roots.root(args[0]), false, out);
}
void common_reduce(Attachment &c, Handle self, std::int64_t protocol, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot helper(c), value(c), arguments(c), keywords(c), state(c), items(c), pairs(c);
    if (protocol < 2) {
        if (!copyreg(c, U"_reduce_ex", helper)) {
            out.raised(helper.exception());
            return;
        }
        make_int(c, protocol, value);
        const Value args[] = {self.get(c), value.value()};
        call(c, roots.root(helper.value()), {args}, out);
        return;
    }
    bool hasargs = false;
    if (!attribute(c, self, U"__getnewargs_ex__", helper, true)) {
        out.raised(helper.exception());
        return;
    }
    if (helper.tag() == ResultTag::Ok) {
        call(c, roots.root(helper.value()), {}, value);
        if (failed(value)) {
            out.raised(value.exception());
            return;
        }
        if (!c.has_layout<TupleObject>(value.value()) ||
            c.payload<TupleObject>(value.value()).items.size() != 2) {
            raise_error(c, CoreObject::TypeErrorType,
                        U"__getnewargs_ex__ should return a tuple of length 2", out);
            return;
        }
        const auto &values = c.payload<TupleObject>(value.value()).items;
        arguments.ok(values[0]);
        keywords.ok(values[1]);
        if (!c.has_layout<TupleObject>(arguments.value()) ||
            !c.has_layout<DictObject>(keywords.value())) {
            raise_error(c, CoreObject::TypeErrorType,
                        U"__getnewargs_ex__ should return a tuple and a dict", out);
            return;
        }
        hasargs = true;
    } else {
        if (!attribute(c, self, U"__getnewargs__", helper, true)) {
            out.raised(helper.exception());
            return;
        }
        if (helper.tag() == ResultTag::Ok) {
            call(c, roots.root(helper.value()), {}, arguments);
            if (failed(arguments)) {
                out.raised(arguments.exception());
                return;
            }
            if (!c.has_layout<TupleObject>(arguments.value())) {
                raise_error(c, CoreObject::TypeErrorType, U"__getnewargs__ should return a tuple",
                            out);
                return;
            }
            hasargs = true;
        } else
            arguments.ok(c.builtin(CoreObject::EmptyTuple));
    }
    auto type = roots.root(c.type_of(self));
    const bool with_keywords =
        keywords.tag() == ResultTag::Ok && dictionary_size(c, keywords.value()) != 0;
    if (!copyreg(c, with_keywords ? U"__newobj_ex__" : U"__newobj__", helper)) {
        out.raised(helper.exception());
        return;
    }
    auto constructor = roots.root(helper.value());
    if (with_keywords) {
        const Value args[] = {type.get(c), arguments.value(), keywords.value()};
        cxxpython::core::make_tuple(c, args, value);
    } else {
        std::vector<Value> args{type.get(c)};
        const auto &values = c.payload<TupleObject>(arguments.value()).items;
        args.insert(args.end(), values.begin(), values.end());
        cxxpython::core::make_tuple(c, args, value);
    }
    auto newargs = roots.root(value.value());
    if (!attribute(c, self, U"__getstate__", helper)) {
        out.raised(helper.exception());
        return;
    }
    auto getter = roots.root(helper.value());
    const auto original = type_attribute(c, c.object_type(), U"__getstate__");
    bool default_getter = c.has_layout<BoundMethodObject>(getter.get(c)) &&
                          c.payload<BoundMethodObject>(getter).function == *original;
    if (c.has_layout<NativeFunctionObject>(getter.get(c))) {
        const auto &bound = c.payload<NativeFunctionObject>(getter);
        const auto definition = c.has_layout<MethodDescriptorObject>(*original)
                                    ? c.payload<MethodDescriptorObject>(*original).function
                                    : *original;
        const auto &native = c.payload<NativeFunctionObject>(definition);
        default_getter = bound.bound_self == self.get(c) && bound.entry == native.entry &&
                         bound.captures == native.captures;
    }
    if (default_getter) {
        default_state(c, self,
                      !hasargs && !c.has_layout<ListObject>(self.get(c)) &&
                          !c.has_layout<DictObject>(self.get(c)),
                      state);
    } else
        call(c, getter, {}, state);
    if (failed(state)) {
        out.raised(state.exception());
        return;
    }
    items.ok(c.builtin(CoreObject::None));
    pairs.ok(c.builtin(CoreObject::None));
    if (c.has_layout<ListObject>(self.get(c))) {
        get_iter(c, self, items);
        if (failed(items)) {
            out.raised(items.exception());
            return;
        }
    }
    if (c.has_layout<DictObject>(self.get(c))) {
        if (!attribute(c, self, U"items", helper)) {
            out.raised(helper.exception());
            return;
        }
        call(c, roots.root(helper.value()), {}, pairs);
        if (!failed(pairs))
            get_iter(c, roots.root(pairs.value()), pairs);
        if (failed(pairs)) {
            out.raised(pairs.exception());
            return;
        }
    }
    const Value result[] = {constructor.get(c), newargs.get(c), state.value(), items.value(),
                            pairs.value()};
    cxxpython::core::make_tuple(c, result, out);
}
void object_reduce(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    common_reduce(c, roots.root(args[0]), 0, out);
}
void object_reduce_ex(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot protocol(c), method(c), class_method(c);
    index(c, roots.root(args[1]), protocol);
    if (failed(protocol)) {
        out.raised(protocol.exception());
        return;
    }
    std::int64_t number;
    if (!int_fits_signed(c, protocol.value(), number) || number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return;
    }
    if (!attribute(c, self, U"__reduce__", method, true)) {
        out.raised(method.exception());
        return;
    }
    if (method.tag() == ResultTag::Ok) {
        if (!attribute(c, roots.root(c.type_of(self)), U"__reduce__", class_method)) {
            out.raised(class_method.exception());
            return;
        }
        if (class_method.value() != c.payload<NativeFunctionObject>(function).captures[0]) {
            call(c, roots.root(method.value()), {}, out);
            return;
        }
    }
    common_reduce(c, self, number, out);
}

// CPython 3.12.14 methodobject.c, classobject.c and descrobject.c reductions.
// Reconstruct by the actual receiver/owner and attribute name, not by locating
// a same-named callable in an unrelated module or class.
void getattr_reduction(Attachment &c, Handle receiver, Handle name, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot arguments(c);
    const auto getattr = name_lookup(c, c.builtin(CoreObject::Builtins), U"getattr");
    if (!getattr) {
        raise_error(c, CoreObject::RuntimeErrorType, U"lost builtins.getattr", out);
        return;
    }
    const auto reconstruct = roots.root(*getattr);
    const Value values[] = {receiver.get(c), name.get(c)};
    cxxpython::core::make_tuple(c, values, arguments);
    if (failed(arguments)) {
        out.raised(arguments.exception());
        return;
    }
    const Value result[] = {reconstruct.get(c), arguments.value()};
    cxxpython::core::make_tuple(c, result, out);
}
void native_reduce(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (c.type_of(args[0]) != c.builtin(CoreObject::NativeFunctionType)) {
        raise_error(c, CoreObject::TypeErrorType, U"__reduce__ requires a native function", out);
        return;
    }
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto &function = c.payload<NativeFunctionObject>(self);
    ReturnSlot name(c);
    if (!attribute(c, self, U"__name__", name)) {
        out.raised(name.exception());
        return;
    }
    if (!function.bound_self || is_module(c, *function.bound_self))
        out.ok(name.value());
    else
        getattr_reduction(c, roots.root(*function.bound_self), roots.root(name.value()), out);
}
void bound_method_reduce(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (c.type_of(args[0]) != c.builtin(CoreObject::BoundMethodType)) {
        raise_error(c, CoreObject::TypeErrorType, U"__reduce__ requires a bound method", out);
        return;
    }
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto &method = c.payload<BoundMethodObject>(self);
    const auto function = roots.root(method.function);
    ReturnSlot name(c);
    if (!attribute(c, function, U"__name__", name)) {
        out.raised(name.exception());
        return;
    }
    getattr_reduction(c, roots.root(method.self), roots.root(name.value()), out);
}
void method_descriptor_reduce(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (c.type_of(args[0]) != c.builtin(CoreObject::MethodDescriptorType)) {
        raise_error(c, CoreObject::TypeErrorType, U"__reduce__ requires a method descriptor", out);
        return;
    }
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    ReturnSlot name(c);
    if (!attribute(c, self, U"__name__", name)) {
        out.raised(name.exception());
        return;
    }
    getattr_reduction(c, roots.root(c.payload<MethodDescriptorObject>(self).owner),
                      roots.root(name.value()), out);
}
} // namespace

void install_reduction(Attachment &c, std::span<const Handle> core)
{
    RootScope roots(c);
    auto type = roots.root(c.object_type());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, type, U"__getstate__", unary, object_getstate);
    install_native_attribute(c, core, type, U"__reduce__", unary, object_reduce);
    auto original = name_lookup(c, *TypeView(c, type).dictionary(), U"__reduce__");
    auto pair = unary;
    pair.parameters.push_back({U"protocol", ParameterKind::PositionalOnly});
    install_native_attribute(c, core, type, U"__reduce_ex__", pair, object_reduce_ex, {*original});
    install_native_attribute(c, core,
                             core[static_cast<std::size_t>(CoreObject::NativeFunctionType)],
                             U"__reduce__", unary, native_reduce);
    install_native_attribute(c, core, core[static_cast<std::size_t>(CoreObject::BoundMethodType)],
                             U"__reduce__", unary, bound_method_reduce);
    install_native_attribute(c, core,
                             core[static_cast<std::size_t>(CoreObject::MethodDescriptorType)],
                             U"__reduce__", unary, method_descriptor_reduce);
}
} // namespace cxxpython::core::detail
