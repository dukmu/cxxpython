// Adapted from CPython 3.12.14 Objects/exceptions.c, BaseExceptionGroup
// construction/derive/split/subgroup (PSF license: doc/licenses/python-3.12.14.txt).
// ExceptionObject and traced tail edges replace CPython's RC-owned group fields.
#include "internal/exceptiongroup.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
namespace {
bool raised(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
std::u32string decimal(std::size_t value)
{
    const auto text = std::to_string(value);
    return {text.begin(), text.end()};
}
std::u32string type_name(Attachment &ctx, Value type)
{
    TypeView view(ctx, type);
    const auto name = view.name();
    if (const auto *text = std::get_if<std::u32string>(&name))
        return *text;
    return ctx.payload<StrObject>(std::get<Value>(name)).value;
}
bool exception_class(Attachment &ctx, Value value)
{
    return ctx.has_layout<TypeObject>(value) &&
           is_subtype(ctx, value, ctx.builtin(CoreObject::BaseExceptionType));
}
bool group(Attachment &ctx, Value value)
{
    return ctx.has_layout<ExceptionObject>(value) &&
           is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::BaseExceptionGroupType));
}
bool receiver(Attachment &ctx, Value self, ReturnSlot &out)
{
    if (group(ctx, self))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType,
                U"BaseExceptionGroup descriptor requires a BaseExceptionGroup instance", out);
    return false;
}
void ordinary_exception(Attachment &ctx, Handle child, ReturnSlot &out)
{
    const auto base = ctx.builtin(CoreObject::ExceptionType);
    if (is_subtype(ctx, ctx.type_of(child), base)) {
        out.ok(ctx.builtin(CoreObject::True));
        return;
    }
    // PyObject_IsInstance(child, Exception) also consults a different __class__.
    // Actual BaseException ancestry was already checked before this call.
    RootScope roots(ctx);
    ReturnSlot name(ctx), actual(ctx);
    make_str(ctx, U"__class__", name);
    get_attribute(ctx, child, roots.root(name.value()), actual);
    if (raised(actual)) {
        if (!is_subtype(ctx, ctx.type_of(actual.exception()),
                        ctx.builtin(CoreObject::AttributeErrorType))) {
            out.raised(actual.exception());
            return;
        }
        out.ok(ctx.builtin(CoreObject::False));
        return;
    }
    out.ok(ctx.builtin(ctx.has_layout<TypeObject>(actual.value()) &&
                               is_subtype(ctx, actual.value(), base)
                           ? CoreObject::True
                           : CoreObject::False));
}
// PySequence_Check excludes dicts even if they provide __getitem__, and does
// not accept an iterable that has no sequence item protocol.
bool sequence(Attachment &ctx, Value value)
{
    const auto type = ctx.type_of(value);
    return !is_subtype(ctx, type, ctx.builtin(CoreObject::DictType)) &&
           type_attribute(ctx, type, U"__getitem__").has_value();
}
template <std::size_t Index> void field(Attachment &ctx, Value self, ReturnSlot &out)
{
    if (!receiver(ctx, self, out))
        return;
    ctx.load(self, ctx.instance_slot(self, Index), out);
    if (out.tag() == ResultTag::Empty)
        raise_error(ctx, CoreObject::AttributeErrorType,
                    Index == exception_group_message_slot ? U"message" : U"exceptions", out);
}
void group_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    const auto arguments = roots.root(args[1]);
    if (!exception_class(ctx, cls.get(ctx)) ||
        !is_subtype(ctx, cls.get(ctx), ctx.builtin(CoreObject::BaseExceptionGroupType))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"BaseExceptionGroup.__new__ requires a BaseExceptionGroup subtype", out);
        return;
    }
    const auto &values = ctx.payload<TupleObject>(arguments).items;
    if (values.size() != 2) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"BaseExceptionGroup.__new__() takes exactly 2 arguments (" +
                        decimal(values.size()) + U" given)",
                    out);
        return;
    }
    const auto message = roots.root(values[0]);
    const auto source = roots.root(values[1]);
    if (!ctx.has_layout<StrObject>(message.get(ctx)) ||
        !is_subtype(ctx, ctx.type_of(message), ctx.builtin(CoreObject::StrType))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"BaseExceptionGroup.__new__() argument 1 must be str, not " +
                        type_name(ctx, ctx.type_of(message)),
                    out);
        return;
    }
    if (!sequence(ctx, source.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"second argument (exceptions) must be a sequence", out);
        return;
    }
    ReturnSlot converted(ctx);
    const std::array conversion{source.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::TupleType)), {conversion}, converted);
    if (raised(converted)) {
        out.raised(converted.exception());
        return;
    }
    const auto children = roots.root(converted.value());
    const auto &items = ctx.payload<TupleObject>(children).items;
    if (items.empty()) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"second argument (exceptions) must be a non-empty sequence", out);
        return;
    }
    bool base_children = false;
    ReturnSlot ordinary(ctx);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!ctx.has_layout<ExceptionObject>(items[i]) ||
            !is_subtype(ctx, ctx.type_of(items[i]), ctx.builtin(CoreObject::BaseExceptionType))) {
            raise_error(ctx, CoreObject::ValueErrorType,
                        U"Item " + decimal(i) +
                            U" of second argument (exceptions) is not an exception",
                        out);
            return;
        }
        ordinary_exception(ctx, roots.root(items[i]), ordinary);
        if (raised(ordinary)) {
            out.raised(ordinary.exception());
            return;
        }
        base_children |= ordinary.value() == ctx.builtin(CoreObject::False);
    }
    if (cls.get(ctx) == ctx.builtin(CoreObject::BaseExceptionGroupType)) {
        if (!base_children)
            cls = roots.root(ctx.builtin(CoreObject::ExceptionGroupType));
    } else if (base_children &&
               is_subtype(ctx, cls.get(ctx), ctx.builtin(CoreObject::ExceptionType))) {
        const auto message =
            cls.get(ctx) == ctx.builtin(CoreObject::ExceptionGroupType)
                ? U"Cannot nest BaseExceptions in an ExceptionGroup"
                : U"Cannot nest BaseExceptions in '" + type_name(ctx, cls.get(ctx)) + U"'";
        raise_error(ctx, CoreObject::TypeErrorType, message, out);
        return;
    }
    ctx.allocate_instance<ExceptionObject>(cls, out, arguments.get(ctx));
    const auto self = roots.root(out.value());
    ctx.store(self.get(ctx), ctx.instance_slot(self.get(ctx), exception_group_message_slot),
              message.get(ctx));
    ctx.store(self.get(ctx), ctx.instance_slot(self.get(ctx), exception_group_exceptions_slot),
              children.get(ctx));
}
void group_init(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    const auto arguments = roots.root(args[1]);
    const auto keywords = roots.root(args[2]);
    if (!receiver(ctx, self.get(ctx), out))
        return;
    if (!DictView(ctx, keywords.get(ctx)).entries().empty()) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    type_name(ctx, ctx.type_of(self)) + U"() takes no keyword arguments", out);
        return;
    }
    // Like BaseException_init: explicit reinitialization changes args, not the
    // readonly message/exceptions snapshot initialized by __new__.
    ctx.store(self.get(ctx), ctx.payload<ExceptionObject>(self).args, arguments.get(ctx));
    out.ok(ctx.builtin(CoreObject::None));
}
void group_str(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    ReturnSlot message(ctx), children(ctx), text(ctx);
    field<exception_group_message_slot>(ctx, self.get(ctx), message);
    if (raised(message)) {
        out.raised(message.exception());
        return;
    }
    field<exception_group_exceptions_slot>(ctx, self.get(ctx), children);
    if (raised(children)) {
        out.raised(children.exception());
        return;
    }
    str(ctx, roots.root(message.value()), text);
    if (raised(text)) {
        out.raised(text.exception());
        return;
    }
    const auto count = ctx.payload<TupleObject>(children.value()).items.size();
    make_str(ctx,
             ctx.payload<StrObject>(text.value()).value + U" (" + decimal(count) +
                 (count > 1 ? U" sub-exceptions)" : U" sub-exception)"),
             out);
}
void derive(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    const auto children = roots.root(args[1]);
    ReturnSlot message(ctx);
    field<exception_group_message_slot>(ctx, self.get(ctx), message);
    if (raised(message)) {
        out.raised(message.exception());
        return;
    }
    const std::array values{message.value(), children.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::BaseExceptionGroupType)), {values}, out);
}
void subset(Attachment &ctx, Handle original, std::span<const Value> children, ReturnSlot &out)
{
    if (children.empty()) {
        out.ok(ctx.builtin(CoreObject::None));
        return;
    }
    RootScope roots(ctx);
    ReturnSlot values(ctx), key(ctx), method(ctx), result(ctx), copied(ctx);
    make_list(ctx, children, values);
    make_str(ctx, U"derive", key);
    get_attribute(ctx, original, roots.root(key.value()), method);
    if (raised(method)) {
        out.raised(method.exception());
        return;
    }
    const std::array arguments{values.value()};
    call(ctx, roots.root(method.value()), {arguments}, result);
    if (raised(result)) {
        out.raised(result.exception());
        return;
    }
    if (!group(ctx, result.value())) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"derive must return an instance of BaseExceptionGroup", out);
        return;
    }
    const auto derived = roots.root(result.value());
    // These are the existing rooted exception fields, not descriptor callbacks.
    for (auto member :
         {&ExceptionObject::traceback, &ExceptionObject::context, &ExceptionObject::cause}) {
        ctx.load(original.get(ctx), ctx.payload<ExceptionObject>(original).*member, copied);
        if (member != &ExceptionObject::traceback || copied.tag() == ResultTag::Ok)
            ctx.store(derived.get(ctx), ctx.payload<ExceptionObject>(derived).*member,
                      copied.tag() == ResultTag::Ok ? std::optional<Value>{copied.value()}
                                                    : std::optional<Value>{});
    }
    // PyException_SetCause sets this flag even when the original has no cause.
    ctx.payload<ExceptionObject>(derived).suppress_context.store(true);
    make_str(ctx, U"__notes__", key);
    const auto notes_key = roots.root(key.value());
    get_attribute(ctx, original, notes_key, values);
    if (raised(values)) {
        if (!is_subtype(ctx, ctx.type_of(values.exception()),
                        ctx.builtin(CoreObject::AttributeErrorType))) {
            out.raised(values.exception());
            return;
        }
    } else if (sequence(ctx, values.value())) {
        const std::array source{values.value()};
        call(ctx, roots.root(ctx.builtin(CoreObject::ListType)), {source}, copied);
        if (raised(copied)) {
            out.raised(copied.exception());
            return;
        }
        set_attribute(ctx, derived, notes_key, roots.root(copied.value()), out);
        if (raised(out))
            return;
    }
    out.ok(derived.get(ctx));
}
enum class Matcher { Type, Predicate };
bool matcher(Attachment &ctx, Value condition, Matcher &kind, ReturnSlot &out)
{
    if (ctx.type_of(condition) == ctx.builtin(CoreObject::FunctionType)) {
        kind = Matcher::Predicate;
        return true;
    }
    kind = Matcher::Type;
    if (exception_class(ctx, condition))
        return true;
    if (ctx.type_of(condition) == ctx.builtin(CoreObject::TupleType)) {
        bool valid = true;
        for (auto item : ctx.payload<TupleObject>(condition).items)
            valid &= exception_class(ctx, item);
        if (valid)
            return true;
    }
    raise_error(ctx, CoreObject::TypeErrorType,
                U"expected a function, exception type or tuple of exception types", out);
    return false;
}
struct SplitDepth {
    Attachment &ctx;
    const bool entered;
    explicit SplitDepth(Attachment &ctx) : ctx(ctx), entered(ctx.enter_native_call()) {}
    ~SplitDepth()
    {
        if (entered)
            ctx.leave_native_call();
    }
};
bool split_recursive(Attachment &ctx, Handle exc, Handle condition, Matcher kind,
                     bool construct_rest, ReturnSlot &match, ReturnSlot &rest)
{
    SplitDepth depth(ctx);
    if (!depth.entered) {
        raise_error(ctx, CoreObject::RecursionErrorType,
                    U"maximum recursion depth exceeded in exceptiongroup_split_recursive", match);
        return false;
    }
    RootScope roots(ctx);
    ReturnSlot checked(ctx), children(ctx);
    const auto none = ctx.builtin(CoreObject::None);
    match.ok(none);
    rest.ok(none);
    if (kind == Matcher::Type)
        exception_match(ctx, exc.get(ctx), condition.get(ctx), checked);
    else {
        const std::array values{exc.get(ctx)};
        call(ctx, condition, {values}, checked);
        if (!raised(checked))
            truth(ctx, roots.root(checked.value()), checked);
    }
    if (raised(checked)) {
        match.raised(checked.exception());
        return false;
    }
    if (checked.value() == ctx.builtin(CoreObject::True)) {
        match.ok(exc.get(ctx));
        return true;
    }
    if (!group(ctx, exc.get(ctx))) {
        if (construct_rest)
            rest.ok(exc.get(ctx));
        return true;
    }
    field<exception_group_exceptions_slot>(ctx, exc.get(ctx), children);
    if (raised(children)) {
        match.raised(children.exception());
        return false;
    }
    std::vector<Value> matching, remaining;
    for (auto child : ctx.payload<TupleObject>(children.value()).items) {
        ReturnSlot child_match(ctx), child_rest(ctx);
        if (!split_recursive(ctx, roots.root(child), condition, kind, construct_rest, child_match,
                             child_rest)) {
            match.raised(child_match.exception());
            return false;
        }
        if (child_match.value() != none)
            matching.push_back(roots.root(child_match.value()).get(ctx));
        if (construct_rest && child_rest.value() != none)
            remaining.push_back(roots.root(child_rest.value()).get(ctx));
    }
    subset(ctx, exc, matching, match);
    if (raised(match))
        return false;
    if (construct_rest) {
        subset(ctx, exc, remaining, rest);
        if (raised(rest)) {
            match.raised(rest.exception());
            return false;
        }
    }
    return true;
}
template <bool Rest>
void split(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]), condition = roots.root(args[1]);
    if (!receiver(ctx, self.get(ctx), out))
        return;
    Matcher kind;
    if (!matcher(ctx, condition.get(ctx), kind, out))
        return;
    ReturnSlot match(ctx), rest(ctx);
    if (!split_recursive(ctx, self, condition, kind, Rest, match, rest)) {
        out.raised(match.exception());
        return;
    }
    if constexpr (Rest) {
        const std::array parts{match.value(), rest.value()};
        cxxpython::core::make_tuple(ctx, parts, out);
    } else
        out.ok(match.value());
}
void class_getitem(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    make_generic_alias(ctx, roots.root(args[0]), roots.root(args[1]), out);
}
void wrap(Attachment &ctx, std::span<const Handle> core, Handle owner, const std::u32string &name,
          bool class_method)
{
    RootScope roots(ctx);
    const auto dictionary = roots.root(*TypeView(ctx, owner).dictionary());
    const auto function = roots.root(*name_lookup(ctx, dictionary.get(ctx), name));
    ReturnSlot wrapper(ctx), key(ctx);
    if (class_method)
        ctx.allocate_instance<ClassMethodObject>(
            core[static_cast<std::size_t>(CoreObject::ClassMethodType)], wrapper,
            function.get(ctx));
    else
        ctx.allocate_instance<StaticMethodObject>(
            core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapper,
            function.get(ctx));
    ctx.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                     name);
    name_store(ctx, dictionary.get(ctx), key.value(), wrapper.value());
}
} // namespace
void install_exception_groups(Attachment &ctx, std::span<const Handle> core)
{
    const auto owner = core[static_cast<std::size_t>(CoreObject::BaseExceptionGroupType)];
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(ctx, core, owner, U"__new__", constructor, group_new);
    wrap(ctx, core, owner, U"__new__", false);
    constructor.parameters[0].name = U"self";
    install_native_attribute(ctx, core, owner, U"__init__", constructor, group_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(ctx, core, owner, U"__str__", unary, group_str);
    auto pair = unary;
    pair.parameters.push_back({U"excs", ParameterKind::PositionalOnly});
    install_native_attribute(ctx, core, owner, U"derive", pair, derive);
    pair.parameters[1].name = U"condition";
    install_native_attribute(ctx, core, owner, U"split", pair, split<true>);
    install_native_attribute(ctx, core, owner, U"subgroup", pair, split<false>);
    pair.parameters[0].name = U"cls";
    pair.parameters[1].name = U"item";
    install_native_attribute(ctx, core, owner, U"__class_getitem__", pair, class_getitem);
    wrap(ctx, core, owner, U"__class_getitem__", true);
    install_native_field(ctx, core, owner, U"message", field<exception_group_message_slot>);
    install_native_field(ctx, core, owner, U"exceptions", field<exception_group_exceptions_slot>);
}
} // namespace cxxpython::core::detail
