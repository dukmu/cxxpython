#include "internal/abstract.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core {
namespace {
// The entry owns all inputs in roots before dispatch can reset out or run Python.
// Internal binding and callable unwrapping share that same scope.
void invoke(Attachment &, Handle, CallArgsView, std::optional<Value>, ReturnSlot &, RootScope &,
            std::optional<detail::PreparedPythonCall> * = nullptr);
void invoke_special_impl(Attachment &, Value, Handle, CallArgsView, ReturnSlot &, RootScope &,
                         std::optional<detail::PreparedPythonCall> * = nullptr,
                         NativeCallKind = NativeCallKind::Generic);
void bind_native_callable(Attachment &, Value, std::optional<Value>, ReturnSlot &);
using detail::bind_descriptor;
using detail::special;
using detail::type_attribute;
using detail::validate_return;
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void root_arguments(RootScope &roots, CallArgsView args)
{
    for (auto value : args.positional)
        roots.root(value);
    for (const auto &keyword : args.keywords) {
        roots.root(keyword.name);
        roots.root(keyword.value);
    }
}

template <class F> ResultView guarded(Attachment &context, ReturnSlot &out, F &&function)
{
    out.check_caller(context);
    try {
        function();
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
} // namespace
namespace detail {
std::optional<TypeAttribute> find_type_attribute(Attachment &context, Value type,
                                                 const std::u32string &name)
{
    RootScope roots(context);
    roots.root(type);
    for (;;) {
        const auto epoch = context.type_epoch();
        if (!(epoch & 1)) {
            std::optional<TypeAttribute> result;
            for (auto base : context.payload<TypeObject>(type).mro) {
                TypeView metadata(context, base);
                DictView dictionary(context, metadata.storage());
                if (auto value = dictionary.find(name)) {
                    result = TypeAttribute{base, *value};
                    break;
                }
            }
            if (context.type_epoch() == epoch)
                return result;
        }
        context.poll();
        std::this_thread::yield();
    }
}
std::optional<Value> type_attribute(Attachment &context, Value type, const std::u32string &name)
{
    auto found = find_type_attribute(context, type, name);
    return found ? std::optional<Value>{found->value} : std::optional<Value>{};
}
void special(Attachment &context, Value receiver, const std::u32string &name, CallArgsView args,
             ReturnSlot &out)
{
    RootScope roots(context);
    auto self = roots.root(receiver);
    root_arguments(roots, args);
    auto type = context.type_of(self);
    auto attribute = type_attribute(context, type, name);
    if (!attribute) {
        type_error(context, U"missing special method", out);
        return;
    }
    auto target = roots.root(*attribute);
    invoke_special_impl(context, self.get(context), target, args, out, roots);
}
void invoke_special(Attachment &context, Handle self, Handle target, CallArgsView args,
                    ReturnSlot &out)
{
    RootScope roots(context);
    auto receiver = roots.root(self.get(context));
    auto descriptor = roots.root(target.get(context));
    root_arguments(roots, args);
    invoke_special_impl(context, receiver.get(context), descriptor, args, out, roots);
}
void bind_descriptor(Attachment &context, Value descriptor, std::optional<Value> receiver,
                     Value owner, ReturnSlot &out)
{
    RootScope roots(context);
    roots.root(descriptor);
    roots.root(owner);
    if (receiver)
        roots.root(*receiver);
    const auto type = context.type_of(descriptor);
    // Native binding retains the absent/None distinction. Python-level
    // __get__ wrappers translate None only at their explicit boundary.
    if (type == context.builtin(CoreObject::GetSetDescriptorType)) {
        detail::get_native_field(context, descriptor, receiver, out);
        return;
    }
    if (type == context.builtin(CoreObject::MemberDescriptorType)) {
        detail::get_native_member(context, descriptor, receiver, out);
        return;
    }
    if (type == context.builtin(CoreObject::NativeFunctionType) ||
        type == context.builtin(CoreObject::FunctionType) ||
        type == context.builtin(CoreObject::MethodDescriptorType)) {
        bind_native_callable(context, descriptor, receiver, out);
        return;
    }
    if (!type_attribute(context, type, U"__get__")) {
        out.ok(descriptor);
        return;
    }
    std::array args{receiver.value_or(context.builtin(CoreObject::None)), owner};
    special(context, descriptor, U"__get__", {args, {}}, out);
}
} // namespace detail
namespace detail {
bool bind_arguments(Attachment &context, const SignatureLayout &signature, CallArgsView args,
                    std::optional<Value> receiver, RootScope &roots, std::vector<Value> &bound,
                    ReturnSlot &out, std::optional<FunctionDefaults> defaults)
{
    const std::size_t prefix = receiver ? 1 : 0;
    const std::size_t given = args.positional.size() + prefix;
    auto positional = [&](std::size_t index) {
        return receiver && index == 0 ? *receiver : args.positional[index - prefix];
    };
    if (args.keywords.empty() && signature.accepts_complete_positional(given)) {
        bound.reserve(given);
        for (std::size_t i = 0; i < given; ++i) {
            const auto value = positional(i);
            roots.root(value);
            bound.push_back(value);
        }
        return true;
    }
    const auto positional_count = static_cast<std::size_t>(
        std::count_if(signature.parameters.begin(), signature.parameters.end(),
                      [](const Parameter &p) { return p.kind != ParameterKind::KeywordOnly; }));
    if (given > positional_count && !signature.varargs) {
        type_error(context, U"too many positional arguments", out);
        return false;
    }
    std::vector<std::optional<Value>> slots(signature.parameters.size());
    DictView keyword_defaults(context, defaults ? defaults->keywords : std::optional<Value>{});
    for (std::size_t i = 0; i < std::min(given, positional_count); ++i)
        slots[i] = positional(i);
    ReturnSlot extra_keywords(context);
    if (signature.varkwargs)
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              extra_keywords);
    std::unordered_set<std::u32string> seen;
    for (const auto &keyword : args.keywords) {
        if (!context.has_layout<StrObject>(keyword.name) ||
            !is_subtype(context, context.type_of(keyword.name),
                        context.builtin(CoreObject::StrType))) {
            type_error(context, U"keywords must be strings", out);
            return false;
        }
        const auto &name = context.payload<StrObject>(keyword.name).value;
        if (!seen.insert(name).second) {
            type_error(context, U"duplicate keyword argument: " + name, out);
            return false;
        }
        auto found = std::find_if(signature.parameters.begin(), signature.parameters.end(),
                                  [&name](const Parameter &p) { return p.name == name; });
        if (found != signature.parameters.end() && found->kind != ParameterKind::PositionalOnly) {
            auto index = static_cast<std::size_t>(found - signature.parameters.begin());
            if (slots[index]) {
                type_error(context, U"multiple values for argument: " + name, out);
                return false;
            }
            slots[index] = keyword.value;
        } else if (signature.varkwargs)
            detail::name_store(context, extra_keywords.value(), keyword.name, keyword.value);
        else {
            type_error(context, U"unexpected or positional-only keyword: " + name, out);
            return false;
        }
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        auto value = slots[i] ? slots[i] : signature.parameters[i].default_value;
        if (!value && defaults) {
            if (i < positional_count) {
                const auto count = defaults->positional.size();
                if (i >= (positional_count > count ? positional_count - count : 0))
                    value = defaults->positional[i + count - positional_count];
            } else if (defaults->keywords)
                value = keyword_defaults.find(signature.parameters[i].name);
        }
        if (!value) {
            type_error(context, U"missing required argument: " + signature.parameters[i].name, out);
            return false;
        }
        roots.root(*value);
        bound.push_back(*value);
    }
    ReturnSlot extra_positional(context);
    if (signature.varargs) {
        std::vector<Value> rest;
        for (std::size_t i = positional_count; i < given; ++i)
            rest.push_back(positional(i));
        cxxpython::core::make_tuple(context, rest, extra_positional);
        bound.push_back(roots.root(extra_positional.value()).get(context));
    }
    if (signature.varkwargs)
        bound.push_back(roots.root(extra_keywords.value()).get(context));
    return true;
}
std::optional<PreparedPythonCall> prepare_call(Attachment &context, Value callable,
                                               CallArgsView args, RootScope &roots, ReturnSlot &out)
{
    out.check_caller(context);
    try {
        auto target = roots.root(callable);
        root_arguments(roots, args);
        std::optional<PreparedPythonCall> preparation;
        invoke(context, target, args, {}, out, roots, &preparation);
        return preparation;
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
        return {};
    }
}
} // namespace detail
namespace detail {
void validate_return(Attachment &context, ReturnSlot &out)
{
    if (out.tag() == ResultTag::Empty)
        throw std::logic_error("call entry did not write a result");
    if (out.tag() == ResultTag::Ok) {
        auto value = out.value();
        auto type = context.type_of(value);
        auto &metadata = context.payload<TypeObject>(type);
        if (metadata.state != TypeState::Ready ||
            (metadata.instance_layout.native_family == &detail::native_layout_id<TypeObject> &&
             is_subtype(context, type, context.type_type()) &&
             context.payload<TypeObject>(value).state != TypeState::Ready)) {
            out.reset(context);
            throw std::logic_error("call entry returned an unready Python value");
        }
    }
}
} // namespace detail
namespace {
void invoke_native(Attachment &context, Value function, const NativeFunctionObject &native,
                   CallArgsView args, std::optional<Value> receiver, ReturnSlot &out,
                   RootScope &roots)
{
    std::vector<Value> bound;
    const auto count = args.positional.size() + (receiver ? 1 : 0);
    if (args.keywords.empty() && native.signature.accepts_complete_positional(count)) {
        if (!receiver)
            native.entry(context, function, args.positional, out);
        else {
            // A bounded stack prefix covers small calls; large arities use the same ABI.
            std::array prefixed{*receiver, *receiver, *receiver, *receiver};
            if (count <= prefixed.size()) {
                std::copy(args.positional.begin(), args.positional.end(), prefixed.begin() + 1);
                native.entry(context, function, std::span<const Value>{prefixed}.first(count), out);
            } else {
                bound.reserve(count);
                bound.push_back(*receiver);
                bound.insert(bound.end(), args.positional.begin(), args.positional.end());
                native.entry(context, function, bound, out);
            }
        }
        validate_return(context, out);
        return;
    }
    if (!detail::bind_arguments(context, native.signature, args, receiver, roots, bound, out))
        return;
    native.entry(context, function, bound, out);
    validate_return(context, out);
}

[[gnu::cold, gnu::noinline]] void method_descriptor_error(Attachment &context,
                                                          const MethodDescriptorObject &descriptor,
                                                          std::optional<Value> receiver,
                                                          ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot bytes(context), decoded(context);
    make_bytes(context, context.payload<NativeFunctionObject>(descriptor.function).name, bytes);
    if (raised(bytes)) {
        out.raised(bytes.exception());
        return;
    }
    detail::decode_primitive(context, roots.root(bytes.value()), detail::TextEncoding::Utf8,
                             roots.root(context.builtin(CoreObject::None)), true, decoded);
    if (raised(decoded)) {
        out.raised(decoded.exception());
        return;
    }
    const auto &name =
        context.payload<StrObject>(context.payload<TupleObject>(decoded.value()).items[0]).value;
    const auto owner = detail::diagnostic_type_name(context, roots.root(descriptor.owner));
    if (!receiver) {
        type_error(context, U"unbound method " + owner + U"." + name + U"() needs an argument",
                   out);
        return;
    }
    const auto actual =
        detail::diagnostic_type_name(context, roots.root(context.type_of(*receiver)));
    type_error(context,
               U"descriptor '" + name + U"' for '" + owner + U"' objects doesn't apply to a '" +
                   actual + U"' object",
               out);
}

void invoke_method_descriptor(Attachment &context, Value callable, CallArgsView args,
                              std::optional<Value> receiver, ReturnSlot &out, RootScope &roots)
{
    const auto &descriptor = context.payload<MethodDescriptorObject>(callable);
    auto self = receiver ? receiver
                         : (args.positional.empty() ? std::optional<Value>{} : args.positional[0]);
    if (!self || !is_subtype(context, context.type_of(*self), descriptor.owner)) {
        method_descriptor_error(context, descriptor, self, out);
        return;
    }
    const auto function = descriptor.function;
    invoke_native(context, function, context.payload<NativeFunctionObject>(function), args,
                  receiver, out, roots);
}

struct CallDepth {
    Attachment &context;
    bool entered = false;
    CallDepth(Attachment &c, ReturnSlot &out) : context(c)
    {
        out.reset(context);
        entered = context.enter_call();
        if (entered && !context.enter_native_call()) {
            context.leave_call();
            entered = false;
        }
        if (!entered)
            raise_error(context, CoreObject::RecursionErrorType, U"maximum call depth exceeded",
                        out);
    }
    ~CallDepth()
    {
        if (entered) {
            context.leave_native_call();
            context.leave_call();
        }
    }
};
void invoke_special_impl(Attachment &context, Value receiver, Handle target, CallArgsView args,
                         ReturnSlot &out, RootScope &roots,
                         std::optional<detail::PreparedPythonCall> *preparation,
                         NativeCallKind native_call)
{
    const auto descriptor = target.get(context);
    std::optional<Value> type;
    if (native_call == NativeCallKind::Generic) {
        type = context.type_of(descriptor);
        if (*type == context.builtin(CoreObject::NativeFunctionType))
            native_call = context.payload<NativeFunctionObject>(descriptor).binding ==
                                  NativeFunctionBinding::Descriptor
                              ? NativeCallKind::Descriptor
                              : NativeCallKind::Function;
        else if (*type == context.builtin(CoreObject::MethodDescriptorType))
            native_call = NativeCallKind::MethodDescriptor;
    }
    if (native_call != NativeCallKind::Generic) {
        CallDepth depth(context, out);
        if (!depth.entered)
            return;
        if (native_call == NativeCallKind::MethodDescriptor)
            invoke_method_descriptor(context, descriptor, args, receiver, out, roots);
        else
            invoke_native(
                context, descriptor, context.payload<NativeFunctionObject>(descriptor), args,
                native_call == NativeCallKind::Descriptor ? std::optional{receiver} : std::nullopt,
                out, roots);
        return;
    }
    if (*type == context.builtin(CoreObject::FunctionType))
        invoke(context, target, args, receiver, out, roots, preparation);
    else {
        ReturnSlot bound(context);
        bind_descriptor(context, descriptor, receiver, context.type_of(receiver), bound);
        if (raised(bound)) {
            out.raised(bound.exception());
            return;
        }
        invoke(context, roots.root(bound.value()), args, {}, out, roots, preparation);
    }
}
void invoke(Attachment &context, Handle target, CallArgsView args, std::optional<Value> receiver,
            ReturnSlot &out, RootScope &roots,
            std::optional<detail::PreparedPythonCall> *preparation)
{
    CallDepth depth(context, out);
    if (!depth.entered)
        return;
    Value callable = target.get(context);
    const auto type = context.type_of(callable);
    if (type == context.builtin(CoreObject::NativeFunctionType)) {
        invoke_native(context, callable, context.payload<NativeFunctionObject>(callable), args,
                      receiver, out, roots);
        return;
    }
    if (type == context.builtin(CoreObject::FunctionType)) {
        if (preparation) {
            detail::PreparedPythonCall prepared{target,
                                                {},
                                                {args.positional.begin(), args.positional.end()},
                                                {args.keywords.begin(), args.keywords.end()}};
            if (receiver)
                prepared.receiver = roots.root(*receiver);
            *preparation = std::move(prepared);
            return;
        }
        context.call_executor()(context, target, args, receiver, out);
        validate_return(context, out);
        return;
    }
    if (receiver && type != context.builtin(CoreObject::MethodDescriptorType)) {
        std::vector<Value> prefixed{*receiver};
        prefixed.insert(prefixed.end(), args.positional.begin(), args.positional.end());
        invoke(context, target, {prefixed, args.keywords}, {}, out, roots, preparation);
        return;
    }
    if (type == context.builtin(CoreObject::BoundMethodType)) {
        auto &method = context.payload<BoundMethodObject>(callable);
        invoke(context, roots.root(method.function), args, method.self, out, roots, preparation);
        return;
    }
    if (type == context.builtin(CoreObject::MethodDescriptorType)) {
        invoke_method_descriptor(context, callable, args, receiver, out, roots);
        return;
    }
    auto attribute = type_attribute(context, type, U"__call__");
    if (!attribute) {
        auto name = TypeView(context, context.type_of(callable)).name();
        auto text = std::holds_alternative<Value>(name)
                        ? context.payload<StrObject>(std::get<Value>(name)).value
                        : std::get<std::u32string>(name);
        type_error(context, U"'" + text + U"' object is not callable", out);
        return;
    }
    invoke_special_impl(context, target.get(context), roots.root(*attribute), args, out, roots,
                        preparation);
}

bool data_descriptor(Attachment &context, Value attribute)
{
    auto type = context.type_of(attribute);
    return type_attribute(context, type, U"__set__").has_value() ||
           type_attribute(context, type, U"__delete__").has_value();
}
bool dictionary_field(Attachment &context, Value object, ReturnSlot &out)
{
    if (is_subtype(context, context.type_of(object), context.type_type())) {
        TypeView metadata(context, object);
        if (!metadata.dictionary())
            throw std::logic_error("ready type has no namespace");
        out.ok(*metadata.dictionary());
        return true;
    }
    auto *field = context.instance_dictionary(object);
    if (!field)
        return false;
    context.load(object, *field, out);
    return true;
}
bool attribute_name(Attachment &context, Handle name, ReturnSlot &out)
{
    if (context.has_layout<StrObject>(name.get(context)) &&
        is_subtype(context, context.type_of(name), context.builtin(CoreObject::StrType)))
        return true;
    type_error(context, U"attribute name must be string", out);
    return false;
}
void lookup_attribute(Attachment &context, Value type, Handle name, ReturnSlot &out)
{
    RootScope roots(context);
    roots.root(type);
    out.reset(context);
    for (auto base : context.payload<TypeObject>(type).mro) {
        TypeView view(context, base);
        if (auto dictionary = view.dictionary()) {
            dict_lookup(context, roots.root(*dictionary), name, out);
            if (out.tag() != ResultTag::Empty)
                return;
        }
    }
}
void get_default(Attachment &context, Handle object, Handle name, bool type_lookup, ReturnSlot &out)
{
    auto value = object.get(context);
    RootScope roots(context);
    auto type_root = roots.root(context.type_of(value));
    const auto type = type_root.get(context);
    const auto &text = context.payload<StrObject>(name).value;
    ReturnSlot lookup(context);
    lookup_attribute(context, type, name, lookup);
    if (raised(lookup)) {
        out.raised(lookup.exception());
        return;
    }
    const auto attribute =
        lookup.tag() == ResultTag::Ok ? std::optional<Value>{lookup.value()} : std::nullopt;
    if (attribute)
        roots.root(*attribute);
    if (attribute && data_descriptor(context, *attribute) &&
        type_attribute(context, context.type_of(*attribute), U"__get__")) {
        bind_descriptor(context, *attribute, value, type, out);
        return;
    }
    if (type_lookup) {
        lookup_attribute(context, value, name, lookup);
        if (raised(lookup)) {
            out.raised(lookup.exception());
            return;
        }
        if (lookup.tag() == ResultTag::Ok) {
            bind_descriptor(context, lookup.value(), {}, value, out);
            return;
        }
    } else {
        ReturnSlot dictionary(context);
        if (dictionary_field(context, value, dictionary) && dictionary.tag() == ResultTag::Ok) {
            dict_lookup(context, roots.root(dictionary.value()), name, out);
            if (out.tag() != ResultTag::Empty)
                return;
        }
    }
    if (attribute) {
        bind_descriptor(context, *attribute, value, type, out);
        return;
    }
    if (type == context.builtin(CoreObject::BoundMethodType)) {
        get_attribute(context, roots.root(context.payload<BoundMethodObject>(value).function), name,
                      out);
        return;
    }
    const auto label =
        detail::diagnostic_type_name(context, roots.root(type_lookup ? value : type));
    raise_error(context, CoreObject::AttributeErrorType,
                (type_lookup ? U"type object '" : U"'") + label +
                    (type_lookup ? U"' has no attribute '" : U"' object has no attribute '") +
                    text + U"'",
                out);
}

void change_default(Attachment &context, Handle object, Handle name, std::optional<Handle> value,
                    bool type_mutation, ReturnSlot &out)
{
    auto target = object.get(context);
    const auto &text = context.payload<StrObject>(name).value;
    RootScope roots(context);
    if (type_mutation) {
        auto &type = context.payload<TypeObject>(target);
        if (type.immutable) {
            type_error(context, U"cannot modify immutable type", out);
            return;
        }
    }
    ReturnSlot lookup(context);
    lookup_attribute(context, context.type_of(target), name, lookup);
    if (raised(lookup)) {
        out.raised(lookup.exception());
        return;
    }
    const auto descriptor =
        lookup.tag() == ResultTag::Ok ? std::optional<Value>{lookup.value()} : std::nullopt;
    if (descriptor)
        roots.root(*descriptor);
    if (descriptor && data_descriptor(context, *descriptor)) {
        const auto method = value ? U"__set__" : U"__delete__";
        if (!type_attribute(context, context.type_of(*descriptor), method)) {
            raise_error(context, CoreObject::AttributeErrorType, method, out);
            return;
        }
        std::vector<Value> args{target};
        if (value)
            args.push_back(value->get(context));
        special(context, *descriptor, method, {args, {}}, out);
        if (!raised(out))
            out.ok(context.builtin(CoreObject::None));
        return;
    }
    ReturnSlot dictionary(context);
    if (!dictionary_field(context, target, dictionary)) {
        const auto label =
            detail::diagnostic_type_name(context, roots.root(context.type_of(target)));
        raise_error(context, CoreObject::AttributeErrorType,
                    U"'" + label +
                        (descriptor ? U"' object attribute '" : U"' object has no attribute '") +
                        text + (descriptor ? U"' is read-only" : U"'"),
                    out);
        return;
    }
    if (dictionary.tag() == ResultTag::Empty) {
        if (!value) {
            raise_error(context, CoreObject::AttributeErrorType, text, out);
            return;
        }
        ReturnSlot created(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              created);
        for (;;) {
            if (context.compare_exchange(target, *context.instance_dictionary(target), {},
                                         created.value())) {
                dictionary.ok(created.value());
                break;
            }
            context.load(target, *context.instance_dictionary(target), dictionary);
            if (dictionary.tag() == ResultTag::Ok)
                break;
            context.poll();
        }
    }
    if (value)
        dict_store(context, roots.root(dictionary.value()), name, *value, out);
    else {
        dict_erase(context, roots.root(dictionary.value()), name, out);
        if (out.tag() == ResultTag::Empty)
            raise_error(context, CoreObject::AttributeErrorType, text, out);
        else if (!raised(out))
            out.ok(context.builtin(CoreObject::None));
    }
}

bool type_receiver(Attachment &context, Value value)
{
    return is_subtype(context, context.type_of(value), context.type_type());
}

enum class AttributeOperation { Get, Set, Delete };
void attribute_bridge(Attachment &context, Value function, std::span<const Value> args,
                      AttributeOperation operation, ReturnSlot &out)
{
    const auto owner = context.payload<NativeFunctionObject>(function).captures[0];
    const bool type_mode = owner == context.type_type();
    if (!is_subtype(context, context.type_of(args[0]), owner) ||
        (operation != AttributeOperation::Get && !type_mode && type_receiver(context, args[0]))) {
        type_error(context, U"attribute wrapper requires a compatible receiver", out);
        return;
    }
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto name = roots.root(args[1]);
    if (!attribute_name(context, name, out))
        return;
    if (operation == AttributeOperation::Get)
        get_default(context, self, name, type_mode, out);
    else
        change_default(context, self, name,
                       operation == AttributeOperation::Delete ? std::optional<Handle>{}
                                                               : roots.root(args[2]),
                       type_mode, out);
}
void attribute_get(Attachment &context, Value fn, std::span<const Value> args, ReturnSlot &out)
{
    attribute_bridge(context, fn, args, AttributeOperation::Get, out);
}
void attribute_set(Attachment &context, Value fn, std::span<const Value> args, ReturnSlot &out)
{
    attribute_bridge(context, fn, args, AttributeOperation::Set, out);
}
void attribute_del(Attachment &context, Value fn, std::span<const Value> args, ReturnSlot &out)
{
    attribute_bridge(context, fn, args, AttributeOperation::Delete, out);
}

void bind_native_callable(Attachment &context, Value callable, std::optional<Value> receiver,
                          ReturnSlot &out)
{
    if (context.type_of(callable) == context.builtin(CoreObject::NativeFunctionType) &&
        context.payload<NativeFunctionObject>(callable).binding ==
            NativeFunctionBinding::Function) {
        out.ok(callable);
        return;
    }
    if (!receiver) {
        out.ok(callable);
        return;
    }
    if (context.type_of(callable) == context.builtin(CoreObject::MethodDescriptorType) &&
        !is_subtype(context, context.type_of(*receiver),
                    context.payload<MethodDescriptorObject>(callable).owner)) {
        method_descriptor_error(context, context.payload<MethodDescriptorObject>(callable),
                                receiver, out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<BoundMethodObject>(
        roots.root(context.builtin(CoreObject::BoundMethodType)), out, callable, *receiver,
        context.type_of(callable) != context.builtin(CoreObject::FunctionType));
}
void function_get(Attachment &context, Value wrapper, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.payload<NativeFunctionObject>(wrapper).captures[0]) {
        type_error(context, U"function descriptor requires a function", out);
        return;
    }
    bind_native_callable(
        context, args[0],
        args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
}
void method_get(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.builtin(CoreObject::MethodDescriptorType)) {
        type_error(context, U"method descriptor requires a descriptor", out);
        return;
    }
    bind_native_callable(
        context, args[0],
        args[1] == context.builtin(CoreObject::None) ? std::optional<Value>{} : args[1], out);
}
void method_new(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != context.builtin(CoreObject::BoundMethodType)) {
        type_error(context, U"method.__new__ requires method type", out);
        return;
    }
    if (!detail::type_attribute(context, context.type_of(args[1]), U"__call__")) {
        type_error(context, U"first argument must be callable", out);
        return;
    }
    if (args[2] == context.builtin(CoreObject::None)) {
        type_error(context, U"instance must not be None", out);
        return;
    }
    RootScope roots(context);
    context.allocate_instance<BoundMethodObject>(roots.root(args[0]), out, args[1], args[2]);
}
void call_bridge(Attachment &context, Value function, std::span<const Value> args, ReturnSlot &out)
{
    auto owner = context.payload<NativeFunctionObject>(function).captures[0];
    if (!is_subtype(context, context.type_of(args[0]), owner)) {
        type_error(context, U"call wrapper requires a compatible receiver", out);
        return;
    }
    auto &positional = context.payload<TupleObject>(args[1]).items;
    std::vector<KeywordArg> keywords;
    DictView named(context, args[2]);
    for (const auto &entry : named.entries())
        keywords.push_back({entry.key, entry.value});
    RootScope roots(context);
    call(context, roots.root(args[0]), {positional, keywords}, out);
}
Value protocol_type(Attachment &context, const ResolvedSlot &slot)
{
    return slot.type.get(context);
}
Value protocol_type(Attachment &context, const detail::BuiltinSlot &slot)
{
    if (slot.state != SlotState::Missing)
        (void)slot.descriptor.get(context);
    return context.builtin(slot.type);
}
template <class Slot>
void invoke_protocol_impl(Attachment &context, Value self, const Slot &slot,
                          std::span<const Value> args, ReturnSlot &out, RootScope &roots,
                          bool own_descriptor)
{
    if (args.size() != protocol_definition(slot.protocol).arity)
        throw std::invalid_argument("protocol arity mismatch");
    const auto original_type = protocol_type(context, slot);
    const auto receiver_type = context.type_of(self);
    auto state = slot.state;
    auto target = slot.descriptor;
    auto native_call = slot.native_call;
    if (slot.epoch != context.type_epoch() || original_type != receiver_type) {
        const auto refreshed =
            detail::resolve_protocol(context, receiver_type, slot.protocol, roots);
        state = refreshed.state;
        target = refreshed.descriptor;
        native_call = refreshed.native_call;
    }
    if (state != SlotState::Missing) {
        const auto descriptor = target.get(context);
        // Internal ObjectOps keeps the snapshot scope alive; public inputs may be released.
        if (own_descriptor)
            target = roots.root(descriptor);
    }
    out.reset(context);
    if (state == SlotState::Missing || state == SlotState::Disabled) {
        type_error(context, U"operation is missing or explicitly disabled", out);
        return;
    }
    invoke_special_impl(context, self, target, {args, {}}, out, roots, nullptr, native_call);
}
} // namespace

bool detail::try_native_operation(Attachment &context, const BuiltinSlot &slot,
                                  std::span<const Value> args, NativeEntry expected,
                                  NativeEntry kernel, ReturnSlot &out)
{
    if (slot.state != SlotState::Native || slot.native_call != NativeCallKind::MethodDescriptor ||
        args.size() != protocol_definition(slot.protocol).arity + 1)
        return false;
    const auto selected = slot.descriptor.get(context);
    const auto type = context.type_of(args[0]);
    if (context.builtin(slot.type) != type || slot.epoch != context.type_epoch())
        return false;
    const auto &descriptor = context.payload<MethodDescriptorObject>(selected);
    if (descriptor.owner != type)
        return false;
    const auto function = descriptor.function;
    const auto &native = context.payload<NativeFunctionObject>(function);
    if (native.entry != expected || native.binding != NativeFunctionBinding::Descriptor ||
        !native.signature.accepts_complete_positional(args.size()))
        return false;
    CallDepth depth(context, out);
    if (depth.entered) {
        kernel(context, function, args, out);
        validate_return(context, out);
    }
    return true;
}

ResultView call(Attachment &context, Handle callable, CallArgsView args, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto target = roots.root(callable.get(context));
        root_arguments(roots, args);
        invoke(context, target, args, {}, out, roots);
    });
}
void detail::invoke_protocol(Attachment &context, Handle self, const ResolvedSlot &slot,
                             std::span<const Value> args, ReturnSlot &out, RootScope &roots)
{
    detail::invoke_protocol(context, self.get(context), slot, args, out, roots);
}
void detail::invoke_protocol(Attachment &context, Value self, const ResolvedSlot &slot,
                             std::span<const Value> args, ReturnSlot &out, RootScope &roots)
{
    invoke_protocol_impl(context, self, slot, args, out, roots, false);
}
void detail::invoke_protocol(Attachment &context, Value self, const BuiltinSlot &slot,
                             std::span<const Value> args, ReturnSlot &out, RootScope &roots)
{
    invoke_protocol_impl(context, self, slot, args, out, roots, false);
}
ResultView invoke_protocol(Attachment &context, Handle receiver, const ResolvedSlot &slot,
                           std::span<const Value> args, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(receiver.get(context));
        root_arguments(roots, {args, {}});
        invoke_protocol_impl(context, self.get(context), slot, args, out, roots, true);
    });
}
ResultView get_attribute(Attachment &context, Handle object, Handle name, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        out.reset(context);
        if (!attribute_name(context, name, out))
            return;
        auto value = object.get(context);
        auto type = context.type_of(value);
        std::array args{name.get(context)};
        special(context, value, U"__getattribute__", {args, {}}, out);
        if (raised(out) &&
            is_subtype(context, context.type_of(out.exception()),
                       context.builtin(CoreObject::AttributeErrorType)) &&
            type_attribute(context, type, U"__getattr__")) {
            special(context, value, U"__getattr__", {args, {}}, out);
        }
    });
}
ResultView set_attribute(Attachment &context, Handle object, Handle name, Handle value,
                         ReturnSlot &out)
{
    return guarded(context, out, [&] {
        out.reset(context);
        if (!attribute_name(context, name, out))
            return;
        (void)value.get(context);
        std::array args{name.get(context), value.get(context)};
        special(context, object.get(context), U"__setattr__", {args, {}}, out);
        if (!raised(out))
            out.ok(context.builtin(CoreObject::None));
    });
}
ResultView del_attribute(Attachment &context, Handle object, Handle name, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        out.reset(context);
        if (!attribute_name(context, name, out))
            return;
        std::array args{name.get(context)};
        special(context, object.get(context), U"__delattr__", {args, {}}, out);
        if (!raised(out))
            out.ok(context.builtin(CoreObject::None));
    });
}

namespace detail {
void install_native_attribute(Attachment &context, std::span<const Handle> core, Handle owner,
                              std::u32string name, SignatureLayout signature, NativeEntry entry,
                              std::vector<Value> captures, NativeSlotKind kind)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(context);
    ReturnSlot function(context);
    allocate_function(
        context, at(CoreObject::NativeFunctionType), std::string(name.begin(), name.end()),
        std::move(signature), entry, std::move(captures), function,
        kind == NativeSlotKind::Sequence ? std::optional<Value>{owner.get(context)} : std::nullopt,
        NativeFunctionBinding::Descriptor,
        name == U"__new__" ? std::optional<Value>{owner.get(context)} : std::nullopt);
    auto callable = roots.root(function.value());
    if (name != U"__new__") {
        context.allocate_instance<MethodDescriptorObject>(at(CoreObject::MethodDescriptorType),
                                                          function, callable.get(context),
                                                          owner.get(context));
        callable = roots.root(function.value());
    }
    ReturnSlot key(context);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, std::move(name));
    name_store(context, *TypeView(context, owner).dictionary(), key.value(), callable.get(context));
}
void install_call_descriptors(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    {
        RootScope roots(context);
        const auto method = at(CoreObject::BoundMethodType);
        SignatureLayout constructor;
        constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                                  {U"function", ParameterKind::PositionalOnly},
                                  {U"instance", ParameterKind::PositionalOnly}};
        install_native_attribute(context, core, method, U"__new__", constructor, method_new);
        const auto dictionary = *TypeView(context, method).dictionary();
        auto function = roots.root(*name_lookup(context, dictionary, U"__new__"));
        ReturnSlot wrapper(context), key(context);
        context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                      function.get(context));
        context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
        name_store(context, dictionary, key.value(), wrapper.value());
    }
    SignatureLayout get;
    get.parameters = {{U"self", ParameterKind::PositionalOnly},
                      {U"instance", ParameterKind::PositionalOnly},
                      {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)}};
    for (auto owner : {CoreObject::NativeFunctionType, CoreObject::FunctionType})
        install_native_attribute(context, core, at(owner), U"__get__", get, function_get,
                                 {at(owner).get(context)});
    install_native_attribute(context, core, at(CoreObject::MethodDescriptorType), U"__get__", get,
                             method_get);
    SignatureLayout invoke_signature;
    invoke_signature.parameters = {{U"self", ParameterKind::PositionalOnly}};
    invoke_signature.varargs = U"args";
    invoke_signature.varkwargs = U"kwargs";
    for (auto owner : {CoreObject::NativeFunctionType, CoreObject::MethodDescriptorType,
                       CoreObject::BoundMethodType, CoreObject::FunctionType})
        install_native_attribute(context, core, at(owner), U"__call__", invoke_signature,
                                 call_bridge, {at(owner).get(context)});

    RootScope roots(context);
    for (auto owner : {roots.root(context.object_type()), roots.root(context.type_type())}) {
        SignatureLayout read;
        read.parameters = {{U"self", ParameterKind::PositionalOnly},
                           {U"name", ParameterKind::PositionalOnly}};
        install_native_attribute(context, core, owner, U"__getattribute__", read, attribute_get,
                                 {owner.get(context)});
        install_native_attribute(context, core, owner, U"__delattr__", read, attribute_del,
                                 {owner.get(context)});
        read.parameters.push_back({U"value", ParameterKind::PositionalOnly});
        install_native_attribute(context, core, owner, U"__setattr__", read, attribute_set,
                                 {owner.get(context)});
    }
}
} // namespace detail
} // namespace cxxpython::core
