#include "cxxpython/execution.hpp"
#include "cxxpython/floatobject.hpp"
#include "internal/listsort.hpp"

#include <cmath>
#include <limits>

namespace cxxpython::core {
namespace {
bool type_object(Attachment &context, Value value)
{
    return context.has_layout<TypeObject>(value) &&
           is_subtype(context, context.type_of(value), context.type_type());
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void builtin_abs(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto value = roots.root(args[0]);
    auto method = detail::type_attribute(context, context.type_of(value), U"__abs__");
    if (!method) {
        raise_error(context, CoreObject::TypeErrorType, U"bad operand type for abs()", out);
        return;
    }
    detail::invoke_special(context, value, roots.root(*method), {{}, {}}, out);
}
void builtin_chr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot converted(context);
    index(context, roots.root(args[0]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    const auto &value = context.payload<IntObject>(converted.value());
    if (value.large || value.small < std::numeric_limits<int>::min() ||
        value.small > std::numeric_limits<int>::max()) {
        raise_error(context, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C int", out);
        return;
    }
    if (value.small < 0 || value.small > 0x10ffff) {
        raise_error(context, CoreObject::ValueErrorType, U"chr() arg not in range(0x110000)", out);
        return;
    }
    make_str(context, std::u32string(1, static_cast<char32_t>(value.small)), out);
}
void builtin_callable(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    auto type = context.type_of(args[0]);
    bool result = type == context.builtin(CoreObject::NativeFunctionType) ||
                  type == context.builtin(CoreObject::FunctionType) ||
                  type == context.builtin(CoreObject::BoundMethodType) ||
                  type == context.builtin(CoreObject::MethodDescriptorType) ||
                  detail::type_attribute(context, type, U"__call__").has_value();
    out.ok(context.builtin(result ? CoreObject::True : CoreObject::False));
}
void builtin_sorted(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot list(context), reverse(context), sorted(context);
    call(context, roots.root(context.builtin(CoreObject::ListType)), {args.first(1), {}}, list);
    if (failed(list)) {
        out.raised(list.exception());
        return;
    }
    truth(context, roots.root(args[2]), reverse);
    if (failed(reverse)) {
        out.raised(reverse.exception());
        return;
    }
    const auto result = roots.root(list.value());
    detail::sort_list(context, result, roots.root(args[1]),
                      reverse.value() == context.builtin(CoreObject::True), sorted);
    if (failed(sorted))
        out.raised(sorted.exception());
    else
        out.ok(result.get(context));
}
template <bool Minimum>
void builtin_min_max(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto positional = roots.root(args[0]);
    const auto &values = context.payload<TupleObject>(positional.get(context)).items;
    DictView keywords(context, args[1]);
    std::optional<Handle> key, fallback;
    for (auto entry : keywords.entries()) {
        const auto &name = context.payload<StrObject>(entry.key).value;
        if (name == U"key")
            key = roots.root(entry.value);
        else if (name == U"default")
            fallback = roots.root(entry.value);
        else {
            raise_error(context, CoreObject::TypeErrorType, U"unexpected keyword argument", out);
            return;
        }
    }
    if (values.empty() || (values.size() > 1 && fallback)) {
        raise_error(context, CoreObject::TypeErrorType, U"invalid arguments to min/max", out);
        return;
    }
    ReturnSlot iterator_result(context), best(context), best_key(context);
    get_iter(context, values.size() == 1 ? roots.root(values[0]) : positional, iterator_result);
    if (failed(iterator_result)) {
        out.raised(iterator_result.exception());
        return;
    }
    auto iterator = roots.root(iterator_result.value());
    for (;;) {
        RootScope iteration(context);
        ReturnSlot item(context), item_key(context), comparison(context), truth_result(context);
        auto status = iter_next(context, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done)
            break;
        auto item_handle = iteration.root(item.value());
        if (key && key->get(context) != context.builtin(CoreObject::None)) {
            const std::array call_args{item_handle.get(context)};
            call(context, *key, {call_args, {}}, item_key);
            if (failed(item_key)) {
                out.raised(item_key.exception());
                return;
            }
        } else
            item_key.ok(item_handle.get(context));
        bool replace = best.tag() == ResultTag::Empty;
        if (!replace) {
            rich_compare(context, iteration.root(item_key.value()),
                         iteration.root(best_key.value()), Minimum ? CompareOp::Lt : CompareOp::Gt,
                         comparison);
            if (failed(comparison)) {
                out.raised(comparison.exception());
                return;
            }
            truth(context, iteration.root(comparison.value()), truth_result);
            if (failed(truth_result)) {
                out.raised(truth_result.exception());
                return;
            }
            replace = truth_result.value() == context.builtin(CoreObject::True);
        }
        if (replace) {
            best.ok(item_handle.get(context));
            best_key.ok(item_key.value());
        }
    }
    if (best.tag() == ResultTag::Empty) {
        if (fallback)
            out.ok(fallback->get(context));
        else
            raise_error(context, CoreObject::ValueErrorType,
                        U"min/max() iterable argument is empty", out);
    } else
        out.ok(best.value());
}
void builtin_divmod(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto left = roots.root(args[0]);
    auto right = roots.root(args[1]);
    auto left_type = context.type_of(left);
    auto right_type = context.type_of(right);
    auto lookup = [&](Value type, const char32_t *name) -> std::optional<Handle> {
        auto method = detail::type_attribute(context, type, name);
        return method ? std::optional<Handle>{roots.root(*method)} : std::nullopt;
    };
    auto normal = lookup(left_type, U"__divmod__");
    auto reflected = lookup(right_type, U"__rdivmod__");
    auto inherited = lookup(left_type, U"__rdivmod__");
    bool first_reflected = left_type != right_type && is_subtype(context, right_type, left_type) &&
                           reflected &&
                           (!inherited || reflected->get(context) != inherited->get(context));
    auto invoke = [&](Handle receiver, Handle argument, const std::optional<Handle> &method) {
        if (!method)
            return false;
        const std::array values{argument.get(context)};
        detail::invoke_special(context, receiver, *method, {values, {}}, out);
        return failed(out) || out.value() != context.builtin(CoreObject::NotImplemented);
    };
    if (first_reflected && invoke(right, left, reflected))
        return;
    if (invoke(left, right, normal))
        return;
    if (!first_reflected && left_type != right_type && invoke(right, left, reflected))
        return;
    raise_error(context, CoreObject::TypeErrorType, U"unsupported operand types for divmod()", out);
}
void builtin_sum(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot iterator_result(context), result(context);
    get_iter(context, roots.root(args[0]), iterator_result);
    if (failed(iterator_result)) {
        out.raised(iterator_result.exception());
        return;
    }
    auto iterator = roots.root(iterator_result.value());
    auto start = args[1];
    if ((context.has_layout<StrObject>(start) &&
         is_subtype(context, context.type_of(start), context.builtin(CoreObject::StrType))) ||
        is_bytes(context, start)) {
        raise_error(context, CoreObject::TypeErrorType, U"sum() cannot sum strings or bytes", out);
        return;
    }
    result.ok(start);
    enum class Mode { Integer, Float, General };
    Mode mode =
        context.type_of(start) == context.builtin(CoreObject::IntType) &&
                !context.payload<IntObject>(start).large
            ? Mode::Integer
            : (context.type_of(start) == context.builtin(CoreObject::FloatType) ? Mode::Float
                                                                                : Mode::General);
    double sum = mode == Mode::Float ? context.payload<FloatObject>(start).value : 0.0;
    double correction = 0.0;
    for (;;) {
        RootScope iteration(context);
        ReturnSlot item(context), next(context);
        auto status = iter_next(context, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done) {
            if (mode == Mode::Float) {
                if (correction && std::isfinite(correction))
                    sum += correction;
                make_float(context, sum, out);
            } else
                out.ok(result.value());
            return;
        }
        auto value = iteration.root(item.value());
        if (mode == Mode::Integer &&
            (context.type_of(value) == context.builtin(CoreObject::IntType) ||
             context.type_of(value) == context.builtin(CoreObject::BoolType))) {
            const auto &a = context.payload<IntObject>(result.value());
            const auto &b = context.payload<IntObject>(value);
            std::int64_t total;
            if (!b.large && !__builtin_add_overflow(a.small, b.small, &total)) {
                make_int(context, total, result);
                continue;
            }
        }
        if (mode == Mode::Float) {
            if (context.type_of(value) == context.builtin(CoreObject::FloatType)) {
                double x = context.payload<FloatObject>(value).value;
                double t = sum + x;
                correction += std::fabs(sum) >= std::fabs(x) ? (sum - t) + x : (x - t) + sum;
                sum = t;
                continue;
            }
            if (is_int(context, value.get(context)) && !context.payload<IntObject>(value).large) {
                sum += static_cast<double>(context.payload<IntObject>(value).small);
                continue;
            }
            if (correction && std::isfinite(correction))
                sum += correction;
            make_float(context, sum, result);
        }
        auto previous_mode = mode;
        binary(context, iteration.root(result.value()), value, BinaryOp::Add, next);
        if (failed(next)) {
            out.raised(next.exception());
            return;
        }
        result.ok(next.value());
        if (previous_mode == Mode::Integer &&
            context.type_of(result.value()) == context.builtin(CoreObject::FloatType)) {
            mode = Mode::Float;
            sum = context.payload<FloatObject>(result.value()).value;
            correction = 0.0;
        } else
            mode = Mode::General;
    }
}
void builtin_ord(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    const auto value = args[0];
    if (context.has_layout<StrObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType))) {
        const auto &text = context.payload<StrObject>(value).value;
        if (text.size() == 1) {
            make_int(context, static_cast<std::int64_t>(text[0]), out);
            return;
        }
    } else if (is_bytes(context, value)) {
        const auto &bytes = context.payload<BytesObject>(value).value;
        if (bytes.size() == 1) {
            make_int(context, static_cast<unsigned char>(bytes[0]), out);
            return;
        }
    }
    raise_error(context, CoreObject::TypeErrorType, U"ord() expected a character", out);
}
template <bool Any>
void builtin_all_any(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot iterator_result(context);
    get_iter(context, roots.root(args[0]), iterator_result);
    if (failed(iterator_result)) {
        out.raised(iterator_result.exception());
        return;
    }
    auto iterator = roots.root(iterator_result.value());
    for (;;) {
        RootScope iteration(context);
        ReturnSlot item(context), result(context);
        auto status = iter_next(context, iterator, item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done) {
            out.ok(context.builtin(Any ? CoreObject::False : CoreObject::True));
            return;
        }
        truth(context, iteration.root(item.value()), result);
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        if ((result.value() == context.builtin(CoreObject::True)) == Any) {
            out.ok(context.builtin(Any ? CoreObject::True : CoreObject::False));
            return;
        }
    }
}
void class_check(Attachment &context, Handle subject, Handle candidate, bool instance,
                 ReturnSlot &out, unsigned depth = 0)
{
    RootScope roots(context);
    if (depth > 1000) {
        raise_error(context, CoreObject::RecursionErrorType, U"recursive class check", out);
        return;
    }
    const auto cls = candidate.get(context);
    if (instance && context.type_of(subject) == cls) {
        out.ok(context.builtin(CoreObject::True));
        return;
    }
    if (context.has_layout<TupleObject>(cls) &&
        is_subtype(context, context.type_of(cls), context.builtin(CoreObject::TupleType))) {
        for (auto item : context.payload<TupleObject>(cls).items) {
            class_check(context, subject, roots.root(item), instance, out, depth + 1);
            if (failed(out) || out.value() == context.builtin(CoreObject::True))
                return;
        }
        out.ok(context.builtin(CoreObject::False));
        return;
    }
    if (context.type_of(cls) != context.type_type()) {
        auto method = detail::type_attribute(
            context, context.type_of(cls), instance ? U"__instancecheck__" : U"__subclasscheck__");
        if (method) {
            const std::array args{subject.get(context)};
            ReturnSlot result(context);
            detail::invoke_special(context, candidate, roots.root(*method), {args, {}}, result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            truth(context, roots.root(result.value()), out);
            return;
        }
    }
    if (!type_object(context, cls)) {
        raise_error(context, CoreObject::TypeErrorType,
                    instance ? U"isinstance() arg 2 must be a type or tuple of types"
                             : U"issubclass() arg 2 must be a class or tuple of classes",
                    out);
        return;
    }
    if (!instance) {
        if (!type_object(context, subject.get(context))) {
            raise_error(context, CoreObject::TypeErrorType, U"issubclass() arg 1 must be a class",
                        out);
            return;
        }
        out.ok(context.builtin(is_subtype(context, subject, candidate) ? CoreObject::True
                                                                       : CoreObject::False));
        return;
    }
    if (is_subtype(context, context.type_of(subject), cls)) {
        out.ok(context.builtin(CoreObject::True));
        return;
    }
    ReturnSlot name(context), actual(context);
    make_str(context, U"__class__", name);
    get_attribute(context, subject, roots.root(name.value()), actual);
    if (failed(actual)) {
        if (!is_subtype(context, context.type_of(actual.exception()),
                        context.builtin(CoreObject::AttributeErrorType))) {
            out.raised(actual.exception());
            return;
        }
        out.ok(context.builtin(CoreObject::False));
        return;
    }
    const auto actual_class = roots.root(actual.value());
    out.ok(context.builtin(type_object(context, actual_class.get(context)) &&
                                   is_subtype(context, actual_class, candidate)
                               ? CoreObject::True
                               : CoreObject::False));
}
void builtin_isinstance(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    class_check(context, roots.root(args[0]), roots.root(args[1]), true, out);
}
void builtin_issubclass(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    class_check(context, roots.root(args[0]), roots.root(args[1]), false, out);
}
void attribute_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto &defaults = context.payload<TupleObject>(args[2]).items;
    if (defaults.size() > 1) {
        raise_error(context, CoreObject::TypeErrorType, U"getattr expected at most 3 arguments",
                    out);
        return;
    }
    get_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
    if (failed(out) && !defaults.empty() &&
        is_subtype(context, context.type_of(out.exception()),
                   context.builtin(CoreObject::AttributeErrorType)))
        out.ok(defaults[0]);
}
void attribute_has(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    get_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
    if (failed(out)) {
        if (!is_subtype(context, context.type_of(out.exception()),
                        context.builtin(CoreObject::AttributeErrorType)))
            return;
        out.ok(context.builtin(CoreObject::False));
    } else
        out.ok(context.builtin(CoreObject::True));
}
void attribute_set(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    set_attribute(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[2]), out);
}
void attribute_delete(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    del_attribute(context, roots.root(args[0]), roots.root(args[1]), out);
}
void builtin_format(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    format(context, roots.root(args[0]), roots.root(args[1]), out);
}
void builtin_ascii(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    ascii(context, roots.root(args[0]), out);
}
void mapping_store(Attachment &context, Handle mapping, std::u32string name, Value value,
                   ReturnSlot &out)
{
    RootScope roots(context);
    const auto item = roots.root(value);
    ReturnSlot key(context);
    make_str(context, std::move(name), key);
    if (context.type_of(mapping) == context.builtin(CoreObject::DictType)) {
        detail::name_store(context, mapping.get(context), key.value(), item.get(context));
        out.ok(context.builtin(CoreObject::None));
    } else
        set_item(context, mapping, roots.root(key.value()), item, out);
}
void build_class(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (context.type_of(args[0]) != context.builtin(CoreObject::FunctionType) ||
        !context.has_layout<FunctionObject>(args[0]) || !context.has_layout<StrObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::StrType))) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"__build_class__ requires a function and string name", out);
        return;
    }
    RootScope roots(context);
    const auto original_bases = roots.root(args[2]);
    ReturnSlot temporary(context), callback(context);
    std::vector<Value> bases;
    bool changed = false;
    for (auto base : context.payload<TupleObject>(original_bases).items) {
        const auto value = roots.root(base);
        if (!type_object(context, base)) {
            make_str(context, U"__mro_entries__", temporary);
            get_attribute(context, value, roots.root(temporary.value()), callback);
            if (failed(callback) && !is_subtype(context, context.type_of(callback.exception()),
                                                context.builtin(CoreObject::AttributeErrorType))) {
                out.raised(callback.exception());
                return;
            }
            if (!failed(callback)) {
                const std::array values{original_bases.get(context)};
                call(context, roots.root(callback.value()), {values, {}}, temporary);
                if (failed(temporary)) {
                    out.raised(temporary.exception());
                    return;
                }
                if (!context.has_layout<TupleObject>(temporary.value()) ||
                    !is_subtype(context, context.type_of(temporary.value()),
                                context.builtin(CoreObject::TupleType))) {
                    raise_error(context, CoreObject::TypeErrorType,
                                U"__mro_entries__ must return a tuple", out);
                    return;
                }
                for (auto item : context.payload<TupleObject>(temporary.value()).items)
                    bases.push_back(roots.root(item).get(context));
                changed = true;
                continue;
            }
        }
        bases.push_back(base);
    }
    Handle bases_tuple = original_bases;
    if (changed) {
        cxxpython::core::make_tuple(context, bases, temporary);
        bases_tuple = roots.root(temporary.value());
    }
    std::vector<KeywordArg> named;
    std::optional<Handle> explicit_meta;
    DictView keyword_args(context, args[3]);
    for (auto entry : keyword_args.entries()) {
        roots.root(entry.key);
        const auto value = roots.root(entry.value);
        if (context.payload<StrObject>(entry.key).value == U"metaclass")
            explicit_meta = value;
        else
            named.push_back({entry.key, value.get(context)});
    }
    auto meta = explicit_meta.value_or(
        roots.root(bases.empty() ? context.type_type() : context.type_of(bases.front())));
    if (type_object(context, meta.get(context))) {
        for (auto base : bases) {
            const auto candidate = roots.root(context.type_of(base));
            if (is_subtype(context, meta, candidate))
                continue;
            if (!is_subtype(context, candidate, meta)) {
                raise_error(context, CoreObject::TypeErrorType, U"metaclass conflict", out);
                return;
            }
            meta = candidate;
        }
    }
    make_str(context, U"__prepare__", temporary);
    get_attribute(context, meta, roots.root(temporary.value()), callback);
    if (failed(callback)) {
        if (!is_subtype(context, context.type_of(callback.exception()),
                        context.builtin(CoreObject::AttributeErrorType))) {
            out.raised(callback.exception());
            return;
        }
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              temporary);
    } else {
        const std::array values{args[1], bases_tuple.get(context)};
        call(context, roots.root(callback.value()), {values, named}, temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return;
        }
    }
    const auto mapping = roots.root(temporary.value());
    if (context.type_of(mapping) != context.builtin(CoreObject::DictType)) {
        const auto item = resolve_protocol(context, roots.root(context.type_of(mapping)),
                                           Protocol::GetItem, roots);
        if (item.state == SlotState::Missing) {
            raise_error(context, CoreObject::TypeErrorType, U"__prepare__ must return a mapping",
                        out);
            return;
        }
    }
    ReturnSlot body(context);
    detail::execute_class_body(context, roots.root(args[0]), mapping, body);
    if (failed(body)) {
        out.raised(body.exception());
        return;
    }
    const auto returned = roots.root(body.value());
    if (changed) {
        mapping_store(context, mapping, U"__orig_bases__", original_bases.get(context), temporary);
        if (failed(temporary)) {
            out.raised(temporary.exception());
            return;
        }
    }
    const std::array values{args[1], bases_tuple.get(context), mapping.get(context)};
    call(context, meta, {values, named}, out);
    if (failed(out))
        return;
    if (type_object(context, out.value()) &&
        context.has_layout<CellObject>(returned.get(context))) {
        ReturnSlot cell(context);
        if (!read_cell(context, returned.get(context), cell))
            raise_error(context, CoreObject::RuntimeErrorType,
                        U"__class__ not set; was __classcell__ propagated to type.__new__?", out);
        else if (cell.value() != out.value())
            raise_error(context, CoreObject::TypeErrorType,
                        U"__class__ cell does not contain the created class", out);
    }
}
} // namespace
namespace detail {
void install_class_builtins(Attachment &context, std::span<const Handle> core, Value mapping)
{
    RootScope roots(context);
    ReturnSlot function(context), name(context);
    SignatureLayout signature;
    signature.parameters = {{U"func", ParameterKind::PositionalOnly},
                            {U"name", ParameterKind::PositionalOnly}};
    signature.varargs = U"bases";
    signature.varkwargs = U"keywords";
    context.allocate_instance<NativeFunctionObject>(
        core[static_cast<std::size_t>(CoreObject::NativeFunctionType)], function, "__build_class__",
        signature, build_class, std::vector<Value>{});
    context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], name,
                                         U"__build_class__");
    name_store(context, mapping, name.value(), function.value());
    auto install = [&](const char *label, std::u32string key, SignatureLayout layout,
                       NativeEntry entry) {
        context.allocate_instance<NativeFunctionObject>(
            core[static_cast<std::size_t>(CoreObject::NativeFunctionType)], function, label,
            std::move(layout), entry, std::vector<Value>{});
        context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)],
                                             name, std::move(key));
        name_store(context, mapping, name.value(), function.value());
    };
    SignatureLayout attributes;
    attributes.parameters = {{U"object", ParameterKind::PositionalOnly},
                             {U"name", ParameterKind::PositionalOnly}};
    install("hasattr", U"hasattr", attributes, attribute_has);
    install("delattr", U"delattr", attributes, attribute_delete);
    auto get = attributes;
    get.varargs = U"default";
    install("getattr", U"getattr", get, attribute_get);
    attributes.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    install("setattr", U"setattr", attributes, attribute_set);
    SignatureLayout class_signature;
    class_signature.parameters = {{U"object", ParameterKind::PositionalOnly},
                                  {U"classinfo", ParameterKind::PositionalOnly}};
    install("isinstance", U"isinstance", class_signature, builtin_isinstance);
    install("issubclass", U"issubclass", class_signature, builtin_issubclass);
    SignatureLayout ascii_signature;
    ascii_signature.parameters = {{U"object", ParameterKind::PositionalOnly}};
    install("ascii", U"ascii", ascii_signature, builtin_ascii);
    install("abs", U"abs", ascii_signature, builtin_abs);
    install("chr", U"chr", ascii_signature, builtin_chr);
    install("ord", U"ord", ascii_signature, builtin_ord);
    install("callable", U"callable", ascii_signature, builtin_callable);
    install("all", U"all", ascii_signature, builtin_all_any<false>);
    install("any", U"any", ascii_signature, builtin_all_any<true>);
    SignatureLayout pair_signature;
    pair_signature.parameters = {{U"x", ParameterKind::PositionalOnly},
                                 {U"y", ParameterKind::PositionalOnly}};
    install("divmod", U"divmod", pair_signature, builtin_divmod);
    SignatureLayout min_max_signature;
    min_max_signature.varargs = U"args";
    min_max_signature.varkwargs = U"kwargs";
    install("min", U"min", min_max_signature, builtin_min_max<true>);
    install("max", U"max", min_max_signature, builtin_min_max<false>);
    ReturnSlot zero(context);
    zero.ok(core[static_cast<std::size_t>(CoreObject::IntZero)].get(context));
    SignatureLayout sum_signature;
    sum_signature.parameters = {{U"iterable", ParameterKind::PositionalOnly},
                                {U"start", ParameterKind::PositionalOrKeyword, zero.value()}};
    install("sum", U"sum", sum_signature, builtin_sum);
    SignatureLayout sorted_signature;
    sorted_signature.parameters = {
        {U"iterable", ParameterKind::PositionalOnly},
        {U"key", ParameterKind::KeywordOnly,
         core[static_cast<std::size_t>(CoreObject::None)].get(context)},
        {U"reverse", ParameterKind::KeywordOnly,
         core[static_cast<std::size_t>(CoreObject::False)].get(context)}};
    install("sorted", U"sorted", sorted_signature, builtin_sorted);
    context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], name,
                                         U"");
    const auto empty_spec = roots.root(name.value());
    SignatureLayout format_signature;
    format_signature.parameters = {
        {U"value", ParameterKind::PositionalOnly},
        {U"format_spec", ParameterKind::PositionalOnly, empty_spec.get(context)}};
    install("format", U"format", format_signature, builtin_format);
}
} // namespace detail
} // namespace cxxpython::core
