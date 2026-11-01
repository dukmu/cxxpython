// Adapted from CPython 3.12.14 Objects/namespaceobject.c, PSF license:
// doc/licenses/python-3.12.14.txt.
#include "cxxpython/namespaceobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
namespace {
bool is_namespace(Attachment &context, Value value)
{
    return context.has_layout<NamespaceObject>(value) &&
           is_subtype(context, context.type_of(value),
                      context.builtin(CoreObject::SimpleNamespaceType));
}

bool receiver(Attachment &context, Value value, ReturnSlot &out)
{
    if (is_namespace(context, value))
        return true;
    raise_error(context, CoreObject::TypeErrorType,
                U"namespace descriptor requires a SimpleNamespace", out);
    return false;
}

void namespace_dict(Attachment &context, Value self, ReturnSlot &out)
{
    if (!receiver(context, self, out))
        return;
    context.load(self, *context.instance_dictionary(self), out);
}

void namespace_new(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(args[0]);
    if (!context.has_layout<TypeObject>(type.get(context)) ||
        !is_subtype(context, type.get(context), context.builtin(CoreObject::SimpleNamespaceType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"SimpleNamespace.__new__ requires a SimpleNamespace type", out);
        return;
    }
    context.allocate_instance<NamespaceObject>(type, out);
    auto self = roots.root(out.value());
    ReturnSlot dictionary(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          dictionary);
    context.store(self.get(context), *context.instance_dictionary(self.get(context)),
                  dictionary.value());
    out.ok(self.get(context));
}

void namespace_init(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    if (!context.payload<TupleObject>(args[1]).items.empty()) {
        raise_error(context, CoreObject::TypeErrorType, U"no positional arguments expected", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto keywords = roots.root(args[2]);
    ReturnSlot dictionary(context);
    namespace_dict(context, self.get(context), dictionary);
    auto target = roots.root(dictionary.value());
    detail::dict_merge(context, target, keywords, false, out);
}

struct ReprGuard {
    Attachment &context;
    Value value;
    bool entered;
    ReprGuard(Attachment &context, Handle self)
        : context(context), value(self.get(context)), entered(context.enter_repr(self))
    {}
    ~ReprGuard()
    {
        if (entered)
            context.leave_repr(value);
    }
};

void namespace_repr(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto type = context.type_of(self);
    std::u32string name = U"namespace";
    if (type != context.builtin(CoreObject::SimpleNamespaceType)) {
        TypeView view(context, type);
        name = context.payload<StrObject>(std::get<Value>(view.name())).value;
    }
    ReprGuard guard(context, self);
    if (!guard.entered) {
        make_str(context, name + U"(...)", out);
        return;
    }
    ReturnSlot dictionary(context), value(context), text(context);
    namespace_dict(context, self.get(context), dictionary);
    auto target = roots.root(dictionary.value());
    DictView keys(context, target.get(context));
    std::u32string result = name + U"(";
    bool first = true;
    for (const auto &entry : keys.entries()) {
        if (!context.has_layout<StrObject>(entry.key) ||
            !is_subtype(context, context.type_of(entry.key), context.builtin(CoreObject::StrType)))
            continue;
        auto key = roots.root(entry.key);
        auto label = context.payload<StrObject>(key).value;
        if (label.empty())
            continue;
        detail::dict_lookup_hash(context, target, key, entry.hash, value);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return;
        }
        if (value.tag() == ResultTag::Empty)
            continue;
        repr(context, roots.root(value.value()), text);
        if (text.tag() == ResultTag::Raised) {
            out.raised(text.exception());
            return;
        }
        if (!first)
            result += U", ";
        first = false;
        result += label + U"=" + context.payload<StrObject>(text.value()).value;
    }
    make_str(context, result + U")", out);
}

template <CompareOp Op>
void namespace_compare(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    if (!is_namespace(context, args[1])) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    // Namespace dictionaries are exact dicts, whose ordering slots return
    // NotImplemented; rich_compare would instead perform the final TypeError.
    if constexpr (Op != CompareOp::Eq && Op != CompareOp::Ne) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto other = roots.root(args[1]);
    ReturnSlot left(context), right(context);
    namespace_dict(context, self.get(context), left);
    namespace_dict(context, other.get(context), right);
    rich_compare(context, roots.root(left.value()), roots.root(right.value()), Op, out);
}

void namespace_reduce(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    auto self = roots.root(args[0]);
    ReturnSlot dictionary(context), empty(context);
    namespace_dict(context, self.get(context), dictionary);
    auto state = roots.root(dictionary.value());
    make_tuple(context, {}, empty);
    const std::array values{context.type_of(self), empty.value(), state.get(context)};
    cxxpython::core::make_tuple(context, values, out);
}
} // namespace

namespace detail {
void install_namespace(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto owner = at(CoreObject::SimpleNamespaceType);
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install_native_attribute(context, core, owner, U"__new__", variadic, namespace_new);
    RootScope roots(context);
    ReturnSlot wrapper(context), key(context);
    auto dict = *TypeView(context, owner).dictionary();
    auto function = roots.root(*name_lookup(context, dict, U"__new__"));
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  function.get(context));
    auto wrapped = roots.root(wrapper.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(context, dict, key.value(), wrapped.get(context));
    install_native_attribute(context, core, owner, U"__init__", variadic, namespace_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, owner, U"__repr__", unary, namespace_repr);
    install_native_attribute(context, core, owner, U"__reduce__", unary, namespace_reduce);
    auto pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    install_native_attribute(context, core, owner, U"__eq__", pair,
                             namespace_compare<CompareOp::Eq>);
    install_native_attribute(context, core, owner, U"__ne__", pair,
                             namespace_compare<CompareOp::Ne>);
    install_native_attribute(context, core, owner, U"__lt__", pair,
                             namespace_compare<CompareOp::Lt>);
    install_native_attribute(context, core, owner, U"__le__", pair,
                             namespace_compare<CompareOp::Le>);
    install_native_attribute(context, core, owner, U"__gt__", pair,
                             namespace_compare<CompareOp::Gt>);
    install_native_attribute(context, core, owner, U"__ge__", pair,
                             namespace_compare<CompareOp::Ge>);
    install_native_field(context, core, owner, U"__dict__", namespace_dict);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
    name_store(context, dict, key.value(), at(CoreObject::None).get(context));
}
} // namespace detail
} // namespace cxxpython::core
