#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core {
namespace {
constexpr std::array definitions{
    ProtocolDefinition{U"__bool__", 0, ProtocolResult::Boolean},
    ProtocolDefinition{U"__len__", 0, ProtocolResult::Length},
    ProtocolDefinition{U"__int__", 0, ProtocolResult::Integer},
    ProtocolDefinition{U"__index__", 0, ProtocolResult::Integer},
    ProtocolDefinition{U"__repr__", 0, ProtocolResult::String},
    ProtocolDefinition{U"__str__", 0, ProtocolResult::String},
    ProtocolDefinition{U"__hash__", 0, ProtocolResult::Hash, true},
    ProtocolDefinition{U"__eq__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ne__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__lt__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__le__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__gt__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ge__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__add__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__radd__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__iadd__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__sub__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rsub__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__isub__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__mul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rmul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__imul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__floordiv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rfloordiv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ifloordiv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__mod__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rmod__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__imod__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__enter__", 0, ProtocolResult::Any},
    ProtocolDefinition{U"__exit__", 3, ProtocolResult::Any},
    ProtocolDefinition{U"__aenter__", 0, ProtocolResult::Any},
    ProtocolDefinition{U"__aexit__", 3, ProtocolResult::Any},
    ProtocolDefinition{U"__pos__", 0, ProtocolResult::Any},
    ProtocolDefinition{U"__neg__", 0, ProtocolResult::Any},
    ProtocolDefinition{U"__invert__", 0, ProtocolResult::Any},
    ProtocolDefinition{U"__iter__", 0, ProtocolResult::Any, true},
    ProtocolDefinition{U"__next__", 0, ProtocolResult::Any, true},
    ProtocolDefinition{U"__getitem__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__setitem__", 2, ProtocolResult::Any},
    ProtocolDefinition{U"__delitem__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__truediv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rtruediv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__itruediv__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__pow__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rpow__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ipow__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__lshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rlshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ilshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rrshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__irshift__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__and__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rand__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__iand__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__xor__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rxor__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ixor__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__or__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ror__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__ior__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__matmul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__rmatmul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__imatmul__", 1, ProtocolResult::Any},
    ProtocolDefinition{U"__contains__", 1, ProtocolResult::Any},
};
static_assert(definitions.size() == static_cast<std::size_t>(Protocol::Count));
constexpr std::array comparisons{Protocol::Eq, Protocol::Ne, Protocol::Lt,
                                 Protocol::Le, Protocol::Gt, Protocol::Ge};
constexpr std::array reflected_comparisons{Protocol::Eq, Protocol::Ne, Protocol::Gt,
                                           Protocol::Ge, Protocol::Lt, Protocol::Le};
constexpr std::array operations{
    std::array{Protocol::Add, Protocol::RAdd, Protocol::IAdd},
    std::array{Protocol::Sub, Protocol::RSub, Protocol::ISub},
    std::array{Protocol::Mul, Protocol::RMul, Protocol::IMul},
    std::array{Protocol::FloorDiv, Protocol::RFloorDiv, Protocol::IFloorDiv},
    std::array{Protocol::Mod, Protocol::RMod, Protocol::IMod},
    std::array{Protocol::TrueDiv, Protocol::RTrueDiv, Protocol::ITrueDiv},
    std::array{Protocol::Power, Protocol::RPower, Protocol::IPower},
    std::array{Protocol::LeftShift, Protocol::RLeftShift, Protocol::ILeftShift},
    std::array{Protocol::RightShift, Protocol::RRightShift, Protocol::IRightShift},
    std::array{Protocol::BitAnd, Protocol::RBitAnd, Protocol::IBitAnd},
    std::array{Protocol::BitXor, Protocol::RBitXor, Protocol::IBitXor},
    std::array{Protocol::BitOr, Protocol::RBitOr, Protocol::IBitOr},
    std::array{Protocol::MatrixMultiply, Protocol::RMatrixMultiply, Protocol::IMatrixMultiply},
};
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
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool implemented(Attachment &context, ReturnSlot &out)
{
    return raised(out) || out.value() != context.builtin(CoreObject::NotImplemented);
}
void boolean(Attachment &context, bool value, ReturnSlot &out)
{
    out.ok(context.builtin(value ? CoreObject::True : CoreObject::False));
}
ResolvedSlot resolve(Attachment &context, Handle value, Protocol id, RootScope &roots)
{
    return resolve_protocol(context, roots.root(context.type_of(value)), id, roots);
}
enum class SlotPhase { Any, Numeric, Sequence };
bool sequence_slot(Attachment &context, const ResolvedSlot &slot)
{
    if (slot.state != SlotState::Native)
        return false;
    const auto &function = context.payload<NativeFunctionObject>(slot.descriptor);
    return function.sequence_owner &&
           is_subtype(context, slot.owner.get(context), *function.sequence_owner);
}
bool attempt(Attachment &context, Handle receiver, Handle argument, Protocol id, ReturnSlot &out,
             SlotPhase phase = SlotPhase::Any)
{
    RootScope roots(context);
    auto slot = resolve(context, receiver, id, roots);
    if (slot.state == SlotState::Missing)
        return false;
    if (phase != SlotPhase::Any && sequence_slot(context, slot) != (phase == SlotPhase::Sequence))
        return false;
    std::array args{argument.get(context)};
    invoke_protocol(context, receiver, slot, args, out);
    return implemented(context, out);
}
bool reflected_override(Attachment &context, Handle left_type, Handle right_type,
                        Protocol reflected, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot result(context);
    make_str(context, std::u32string(protocol_definition(reflected).name), result);
    auto name = roots.root(result.value());
    auto lookup = [&](Handle type) -> std::optional<Handle> {
        get_attribute(context, type, name, result);
        if (!raised(result))
            return roots.root(result.value());
        if (!is_subtype(context, context.type_of(result.exception()),
                        context.builtin(CoreObject::AttributeErrorType)))
            out.raised(result.exception());
        return {};
    };
    // Class lookup can bind a descriptor or run a metaclass hook. Like
    // Python's overload test, right lookup precedes left, then != has
    // its own identity shortcut and truth conversion.
    auto right = lookup(right_type);
    if (!right)
        return false;
    auto left = lookup(left_type);
    if (!left)
        return out.tag() != ResultTag::Raised;
    if (left->get(context) == right->get(context))
        return false;
    rich_compare(context, *left, *right, CompareOp::Ne, result);
    if (raised(result)) {
        out.raised(result.exception());
        return false;
    }
    ReturnSlot converted(context);
    truth(context, roots.root(result.value()), converted);
    if (raised(converted)) {
        out.raised(converted.exception());
        return false;
    }
    return converted.value() == context.builtin(CoreObject::True);
}
bool binary_numeric(Attachment &context, Handle a, Handle b, BinaryOp op, ReturnSlot &out)
{
    const auto &ids = operations.at(static_cast<std::size_t>(op));
    const auto ltype = context.type_of(a), rtype = context.type_of(b);
    const bool different = ltype != rtype;
    bool right_first = false;
    if (different) {
        RootScope roots(context);
        const auto left = roots.root(ltype), right = roots.root(rtype);
        if (is_subtype(context, right, left)) {
            const auto reflected = resolve(context, b, ids[1], roots);
            const auto normal = resolve(context, b, ids[0], roots);
            if ((reflected.state != SlotState::Missing && !sequence_slot(context, reflected)) ||
                (normal.state != SlotState::Missing && !sequence_slot(context, normal)))
                right_first = reflected_override(context, left, right, ids[1], out);
            if (raised(out))
                return true;
        }
    }
    if (right_first && attempt(context, b, a, ids[1], out, SlotPhase::Numeric))
        return true;
    if (attempt(context, a, b, ids[0], out, SlotPhase::Numeric))
        return true;
    return different && !right_first && attempt(context, b, a, ids[1], out, SlotPhase::Numeric);
}
bool sequence_binary(Attachment &context, Handle a, Handle b, BinaryOp op, bool inplace,
                     ReturnSlot &out)
{
    if (op != BinaryOp::Add && op != BinaryOp::Multiply)
        return false;
    const auto &ids = operations.at(static_cast<std::size_t>(op));
    if (inplace && attempt(context, a, b, ids[2], out, SlotPhase::Sequence))
        return true;
    if (attempt(context, a, b, ids[0], out, SlotPhase::Sequence))
        return true;
    return op == BinaryOp::Multiply && attempt(context, b, a, ids[1], out, SlotPhase::Sequence);
}
void object_eq(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] == args[1])
        boolean(context, true, out);
    else
        out.ok(context.builtin(CoreObject::NotImplemented));
}
void object_ne(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto self = roots.root(args[0]);
    auto slot = resolve(context, self, Protocol::Eq, roots);
    ReturnSlot equal(context);
    invoke_protocol(context, self, slot, args.subspan(1), equal);
    if (raised(equal)) {
        out.raised(equal.exception());
        return;
    }
    if (!implemented(context, equal)) {
        out.ok(equal.value());
        return;
    }
    ReturnSlot converted(context);
    truth(context, roots.root(equal.value()), converted);
    if (raised(converted))
        out.raised(converted.exception());
    else
        boolean(context, converted.value() == context.builtin(CoreObject::False), out);
}
void none_bool(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != context.builtin(CoreObject::None)) {
        type_error(context, U"NoneType.__bool__ requires None", out);
        return;
    }
    boolean(context, false, out);
}
template <class T, CoreObject Type>
void native_len(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!is_subtype(context, context.type_of(args[0]), context.builtin(Type)) ||
        !context.has_layout<T>(args[0])) {
        type_error(context, U"length descriptor received an incompatible value", out);
        return;
    }
    auto &data = context.payload<T>(args[0]);
    std::size_t size;
    if constexpr (std::is_same_v<T, StrObject>)
        size = data.value.size();
    else if constexpr (std::is_same_v<T, TupleObject>)
        size = data.items.size();
    else
        size = detail::dictionary_size(context, args[0]);
    if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::overflow_error("native container size exceeds addressable storage");
    make_int(context, static_cast<std::int64_t>(size), out);
}
} // namespace
const ProtocolDefinition &protocol_definition(Protocol id)
{
    return definitions.at(static_cast<std::size_t>(id));
}
ResolvedSlot resolve_protocol(Attachment &context, Handle type, Protocol id, RootScope &roots)
{
    const auto &definition = protocol_definition(id);
    const auto value = type.get(context);
    if (auto cached = context.cached_protocol(type, id, roots))
        return *cached;
    if (!is_subtype(context, context.type_of(value), context.type_type()) ||
        context.payload<TypeObject>(type).state != TypeState::Ready)
        throw std::invalid_argument("protocol resolution requires a ready type");
    auto rooted_type = roots.root(value);
    for (;;) {
        const auto epoch = context.type_epoch();
        if (!(epoch & 1)) {
            auto attribute =
                detail::find_type_attribute(context, value, std::u32string(definition.name));
            if (context.type_epoch() == epoch) {
                if (!attribute) {
                    ResolvedSlot slot{id, SlotState::Missing, rooted_type, {}, {}, epoch};
                    context.cache_protocol(slot);
                    return slot;
                }
                auto state = definition.none_disables &&
                                     attribute->value == context.builtin(CoreObject::None)
                                 ? SlotState::Disabled
                             : context.type_of(attribute->value) ==
                                     context.builtin(CoreObject::NativeFunctionType)
                                 ? SlotState::Native
                                 : SlotState::PythonDescriptor;
                ResolvedSlot slot{id,
                                  state,
                                  rooted_type,
                                  roots.root(attribute->owner),
                                  roots.root(attribute->value),
                                  epoch};
                context.cache_protocol(slot);
                return slot;
            }
        }
        context.poll();
        std::this_thread::yield();
    }
}
ResultView invoke_protocol(Attachment &context, Handle receiver, const ResolvedSlot &slot,
                           std::span<const Value> args, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(receiver.get(context));
        for (auto value : args)
            roots.root(value);
        if (args.size() != protocol_definition(slot.protocol).arity)
            throw std::invalid_argument("protocol arity mismatch");
        const auto original_type = slot.type.get(context);
        auto current = slot;
        if (slot.epoch != context.type_epoch() || original_type != context.type_of(self))
            current = resolve(context, self, slot.protocol, roots);
        if (current.state != SlotState::Missing)
            roots.root(current.descriptor.get(context));
        out.reset(context);
        if (current.state == SlotState::Missing || current.state == SlotState::Disabled) {
            type_error(context, U"operation is missing or explicitly disabled", out);
            return;
        }
        detail::invoke_special(context, self, current.descriptor, {args, {}}, out);
    });
}
ResultView index(Attachment &context, Handle value, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(value.get(context));
        out.reset(context);
        if (is_int(context, self.get(context))) {
            copy_int(context, self.get(context), out);
            return;
        }
        auto slot = resolve(context, self, Protocol::Index, roots);
        if (slot.state == SlotState::Missing || slot.state == SlotState::Disabled) {
            const auto view = TypeView(context, context.type_of(self));
            const auto name = view.name();
            const auto text = std::holds_alternative<Value>(name)
                                  ? context.payload<StrObject>(std::get<Value>(name)).value
                                  : std::get<std::u32string>(name);
            type_error(context, U"'" + text + U"' object cannot be interpreted as an integer", out);
            return;
        }
        ReturnSlot result(context);
        invoke_protocol(context, self, slot, {}, result);
        if (raised(result)) {
            out.raised(result.exception());
            return;
        }
        if (!is_int(context, result.value())) {
            type_error(context, U"__index__ returned a non-int", out);
            return;
        }
        copy_int(context, result.value(), out);
    });
}
ResultView hash(Attachment &context, Handle value, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(value.get(context));
        out.reset(context);
        auto slot = resolve(context, self, Protocol::Hash, roots);
        ReturnSlot result(context);
        invoke_protocol(context, self, slot, {}, result);
        if (raised(result)) {
            out.raised(result.exception());
            return;
        }
        const auto integer = result.value();
        if (!is_int(context, integer)) {
            type_error(context, U"__hash__ returned a non-int", out);
            return;
        }
        std::int64_t word;
        if (!int_fits_signed(context, integer, word) ||
            word < std::numeric_limits<std::intptr_t>::min() ||
            word > std::numeric_limits<std::intptr_t>::max())
            word = detail::int_hash_value(context, integer);
        make_int(context, word == -1 ? -2 : word, out);
    });
}
namespace {
ResultView string_operation(Attachment &context, Handle value, Protocol id, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(value.get(context));
        out.reset(context);
        if (id == Protocol::Str && context.type_of(self) == context.builtin(CoreObject::StrType)) {
            out.ok(self.get(context));
            return;
        }
        auto slot = resolve(context, self, id, roots);
        ReturnSlot result(context);
        invoke_protocol(context, self, slot, {}, result);
        if (raised(result)) {
            out.raised(result.exception());
            return;
        }
        const auto text = result.value();
        if (!is_subtype(context, context.type_of(text), context.builtin(CoreObject::StrType)) ||
            !context.has_layout<StrObject>(text)) {
            type_error(context, U"string conversion returned a non-str", out);
            return;
        }
        out.ok(text);
    });
}
} // namespace
ResultView repr(Attachment &context, Handle value, ReturnSlot &out)
{
    return string_operation(context, value, Protocol::Repr, out);
}
ResultView str(Attachment &context, Handle value, ReturnSlot &out)
{
    return string_operation(context, value, Protocol::Str, out);
}
ResultView length(Attachment &context, Handle value, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(value.get(context));
        out.reset(context);
        auto slot = resolve(context, self, Protocol::Len, roots);
        if (slot.state == SlotState::Missing) {
            auto name = TypeView(context, context.type_of(self)).name();
            auto text = std::holds_alternative<Value>(name)
                            ? context.payload<StrObject>(std::get<Value>(name)).value
                            : std::get<std::u32string>(name);
            type_error(context, U"object of type '" + text + U"' has no len()", out);
            return;
        }
        ReturnSlot result(context);
        invoke_protocol(context, self, slot, {}, result);
        if (raised(result)) {
            out.raised(result.exception());
            return;
        }
        ReturnSlot converted(context);
        index(context, roots.root(result.value()), converted);
        if (raised(converted)) {
            out.raised(converted.exception());
            return;
        }
        auto integer = converted.value();
        if (int_sign(context, integer) < 0) {
            raise_error(context, CoreObject::ValueErrorType, U"__len__ returned a negative value",
                        out);
            return;
        }
        std::int64_t size;
        if (!int_fits_signed(context, integer, size) ||
            size > std::numeric_limits<std::intptr_t>::max()) {
            raise_error(context, CoreObject::OverflowErrorType, U"length does not fit Py_ssize_t",
                        out);
            return;
        }
        out.ok(integer);
    });
}
ResultView truth(Attachment &context, Handle value, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(value.get(context));
        out.reset(context);
        auto slot = resolve(context, self, Protocol::Bool, roots);
        ReturnSlot result(context);
        if (slot.state != SlotState::Missing) {
            invoke_protocol(context, self, slot, {}, result);
            if (raised(result)) {
                out.raised(result.exception());
                return;
            }
            auto boolean_value = result.value();
            if (context.type_of(boolean_value) != context.builtin(CoreObject::BoolType)) {
                type_error(context, U"__bool__ returned a non-bool", out);
                return;
            }
            out.ok(boolean_value);
            return;
        }
        auto len = resolve(context, self, Protocol::Len, roots);
        if (len.state == SlotState::Missing) {
            boolean(context, true, out);
            return;
        }
        length(context, self, result);
        if (raised(result))
            out.raised(result.exception());
        else
            boolean(context, int_sign(context, result.value()) != 0, out);
    });
}
ResultView unary(Attachment &context, Handle value, UnaryOp op, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        if (op == UnaryOp::Not) {
            ReturnSlot result(context);
            truth(context, self, result);
            if (raised(result))
                out.raised(result.exception());
            else
                boolean(context, result.value() == context.builtin(CoreObject::False), out);
            return;
        }
        constexpr std::array protocols{Protocol::Pos, Protocol::Neg, Protocol::Invert};
        auto slot = resolve(context, self, protocols.at(static_cast<std::size_t>(op)), roots);
        invoke_protocol(context, self, slot, {}, out);
    });
}
ResultView rich_compare(Attachment &context, Handle left, Handle right, CompareOp op,
                        ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto a = roots.root(left.get(context));
        auto b = roots.root(right.get(context));
        out.reset(context);
        if (op == CompareOp::Is || op == CompareOp::IsNot) {
            boolean(context, (a.get(context) == b.get(context)) == (op == CompareOp::Is), out);
            return;
        }
        if (op == CompareOp::In || op == CompareOp::NotIn) {
            ReturnSlot result(context);
            auto slot = resolve(context, b, Protocol::Contains, roots);
            bool found = false;
            if (slot.state != SlotState::Missing) {
                std::array args{a.get(context)};
                invoke_protocol(context, b, slot, args, result);
                if (raised(result)) {
                    out.raised(result.exception());
                    return;
                }
                truth(context, roots.root(result.value()), result);
                if (raised(result)) {
                    out.raised(result.exception());
                    return;
                }
                found = result.value() == context.builtin(CoreObject::True);
            } else {
                get_iter(context, b, result);
                if (raised(result)) {
                    out.raised(result.exception());
                    return;
                }
                auto iterator = roots.root(result.value());
                for (;;) {
                    auto status = iter_next(context, iterator, result);
                    if (status == NextStatus::Raised) {
                        out.raised(result.exception());
                        return;
                    }
                    if (status == NextStatus::Done)
                        break;
                    RootScope iteration(context);
                    auto item = iteration.root(result.value());
                    container_equal(context, item, a, result);
                    if (raised(result)) {
                        out.raised(result.exception());
                        return;
                    }
                    if (result.value() == context.builtin(CoreObject::True)) {
                        found = true;
                        break;
                    }
                }
            }
            boolean(context, found == (op == CompareOp::In), out);
            return;
        }
        auto normal = comparisons.at(static_cast<std::size_t>(op));
        auto reflected = reflected_comparisons.at(static_cast<std::size_t>(op));
        const auto ltype = context.type_of(a);
        const auto rtype = context.type_of(b);
        const bool right_first = ltype != rtype && is_subtype(context, rtype, ltype);
        if (right_first && attempt(context, b, a, reflected, out))
            return;
        if (attempt(context, a, b, normal, out))
            return;
        if (!right_first && attempt(context, b, a, reflected, out))
            return;
        if (op == CompareOp::Eq || op == CompareOp::Ne)
            boolean(context, (a.get(context) == b.get(context)) == (op == CompareOp::Eq), out);
        else
            type_error(context, U"ordering is not supported between these objects", out);
    });
}
ResultView container_equal(Attachment &context, Handle left, Handle right, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto a = roots.root(left.get(context));
        auto b = roots.root(right.get(context));
        out.reset(context);
        if (a.get(context) == b.get(context)) {
            boolean(context, true, out);
            return;
        }
        ReturnSlot result(context);
        rich_compare(context, a, b, CompareOp::Eq, result);
        if (raised(result))
            out.raised(result.exception());
        else
            truth(context, roots.root(result.value()), out);
    });
}
ResultView binary(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto a = roots.root(left.get(context));
        auto b = roots.root(right.get(context));
        out.reset(context);
        if (binary_numeric(context, a, b, op, out) ||
            sequence_binary(context, a, b, op, false, out))
            return;
        type_error(context, U"unsupported operand types for binary operation", out);
    });
}
ResultView inplace(Attachment &context, Handle left, Handle right, BinaryOp op, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto a = roots.root(left.get(context));
        auto b = roots.root(right.get(context));
        out.reset(context);
        if (attempt(context, a, b, operations.at(static_cast<std::size_t>(op))[2], out,
                    SlotPhase::Numeric) ||
            binary_numeric(context, a, b, op, out) || sequence_binary(context, a, b, op, true, out))
            return;
        type_error(context, U"unsupported operand types for inplace operation", out);
    });
}
ResultView power(Attachment &context, Handle base, Handle exponent, Handle modulus, ReturnSlot &out)
{
    if (modulus.get(context) == context.builtin(CoreObject::None))
        return binary(context, base, exponent, BinaryOp::Power, out);
    return guarded(context, out, [&] {
        RootScope roots(context);
        auto self = roots.root(base.get(context));
        auto other = roots.root(exponent.get(context));
        auto mod = roots.root(modulus.get(context));
        out.reset(context);
        // Python 3.12's three-argument power does not call __rpow__.
        if (auto slot = detail::type_attribute(context, context.type_of(self), U"__pow__")) {
            const Value args[] = {other.get(context), mod.get(context)};
            detail::invoke_special(context, self, roots.root(*slot), {args}, out);
            if (raised(out) || out.value() != context.builtin(CoreObject::NotImplemented))
                return;
        }
        type_error(context, U"unsupported operand types for modular power", out);
    });
}
ResultView get_item(Attachment &context, Handle value, Handle key, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        const auto item = roots.root(key.get(context));
        auto slot = resolve(context, self, Protocol::GetItem, roots);
        std::array args{item.get(context)};
        if (slot.state == SlotState::Missing &&
            is_subtype(context, context.type_of(self), context.type_type())) {
            if (self.get(context) == context.type_type()) {
                make_generic_alias(context, self, item, out);
                return;
            }
            ReturnSlot name(context), method(context);
            make_str(context, U"__class_getitem__", name);
            get_attribute(context, self, roots.root(name.value()), method);
            if (method.tag() == ResultTag::Ok &&
                method.value() != context.builtin(CoreObject::None)) {
                call(context, roots.root(method.value()), {args}, out);
                return;
            }
            if (method.tag() == ResultTag::Raised &&
                !is_subtype(context, context.type_of(method.exception()),
                            context.builtin(CoreObject::AttributeErrorType))) {
                out.raised(method.exception());
                return;
            }
        }
        invoke_protocol(context, self, slot, args, out);
    });
}
ResultView divmod(Attachment &context, Handle left, Handle right, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        left = roots.root(left.get(context));
        right = roots.root(right.get(context));
        const auto ltype = context.type_of(left), rtype = context.type_of(right);
        auto lookup = [&](Value type, const char32_t *name) -> std::optional<Handle> {
            auto method = detail::type_attribute(context, type, name);
            return method ? std::optional<Handle>{roots.root(*method)} : std::nullopt;
        };
        auto normal = lookup(ltype, U"__divmod__");
        auto reflected = lookup(rtype, U"__rdivmod__");
        auto inherited = lookup(ltype, U"__rdivmod__");
        const bool first = ltype != rtype && is_subtype(context, rtype, ltype) && reflected &&
                           (!inherited || reflected->get(context) != inherited->get(context));
        auto invoke = [&](Handle self, Handle argument, const std::optional<Handle> &method) {
            if (!method)
                return false;
            const Value values[] = {argument.get(context)};
            detail::invoke_special(context, self, *method, {values}, out);
            return raised(out) || out.value() != context.builtin(CoreObject::NotImplemented);
        };
        if ((first && invoke(right, left, reflected)) || invoke(left, right, normal) ||
            (!first && ltype != rtype && invoke(right, left, reflected)))
            return;
        type_error(context, U"unsupported operand types for divmod()", out);
    });
}
ResultView set_item(Attachment &context, Handle value, Handle key, Handle item, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        const auto index = roots.root(key.get(context));
        const auto stored = roots.root(item.get(context));
        auto slot = resolve(context, self, Protocol::SetItem, roots);
        std::array args{index.get(context), stored.get(context)};
        invoke_protocol(context, self, slot, args, out);
    });
}
ResultView del_item(Attachment &context, Handle value, Handle key, ReturnSlot &out)
{
    return guarded(context, out, [&] {
        RootScope roots(context);
        const auto self = roots.root(value.get(context));
        const auto index = roots.root(key.get(context));
        auto slot = resolve(context, self, Protocol::DelItem, roots);
        std::array args{index.get(context)};
        invoke_protocol(context, self, slot, args, out);
    });
}
namespace detail {
void install_protocols(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
    RootScope roots(context);
    auto object = roots.root(context.object_type());
    install_native_attribute(context, core, object, U"__eq__", pair, object_eq);
    install_native_attribute(context, core, object, U"__ne__", pair, object_ne);
    install_native_attribute(context, core, at(CoreObject::NoneType), U"__bool__", unary,
                             none_bool);
    install_native_attribute(context, core, at(CoreObject::StrType), U"__len__", unary,
                             native_len<StrObject, CoreObject::StrType>);
    install_native_attribute(context, core, at(CoreObject::TupleType), U"__len__", unary,
                             native_len<TupleObject, CoreObject::TupleType>);
    install_native_attribute(context, core, at(CoreObject::DictType), U"__len__", unary,
                             native_len<DictObject, CoreObject::DictType>);
}
} // namespace detail
} // namespace cxxpython::core
