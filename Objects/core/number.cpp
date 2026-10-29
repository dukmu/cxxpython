#include "cxxpython/core/protocol.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION < 107400
#error Boost 1.74 or newer is required
#endif

namespace cxxpython::core
{
    namespace detail
    {
        struct BigInt { boost::multiprecision::cpp_int value; };
    }
    namespace
    {
        using Integer = boost::multiprecision::cpp_int;
        void type_error(Attachment &context, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, U"integer operation received an incompatible value", out); }
        bool raised(ReturnSlot &out) { return out.result().tag() == ResultTag::Raised; }
        const IntObject &integer(Attachment &context, Value value)
        {
            if (!is_int(context, value)) throw std::invalid_argument("expected an integer value");
            return context.payload<IntObject>(value);
        }
        Integer expanded(const IntObject &value)
        { return value.large ? value.large->value : Integer(value.small); }
        bool fits(const Integer &value)
        {
            return value >= std::numeric_limits<std::int64_t>::min() &&
                   value <= std::numeric_limits<std::int64_t>::max();
        }
        void publish(Attachment &context, Integer value, ReturnSlot &out)
        {
            if (fits(value)) { make_int(context, value.convert_to<std::int64_t>(), out); return; }
            auto storage = std::make_unique<detail::BigInt>(); storage->value = std::move(value);
            RootScope roots(context);
            context.allocate_instance<IntObject>(roots.root(context.builtin(CoreObject::IntType)), out, std::move(storage));
        }
        void boolean(Attachment &context, bool value, ReturnSlot &out)
        { out.ok(context.builtin(value ? CoreObject::True : CoreObject::False)); }
        template <BinaryOp Op, bool Reflected>
        void arithmetic(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            if (!is_int(context, args[1])) { out.ok(context.builtin(CoreObject::NotImplemented)); return; }
            auto &a = integer(context, args[Reflected ? 1 : 0]);
            auto &b = integer(context, args[Reflected ? 0 : 1]);
            if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo)
                if (!b.large && b.small == 0)
                { raise_error(context, CoreObject::ZeroDivisionErrorType, U"integer division or modulo by zero", out); return; }
            if (!a.large && !b.large)
            {
                std::int64_t result = 0; bool overflow = false;
                if constexpr (Op == BinaryOp::Add) overflow = __builtin_add_overflow(a.small, b.small, &result);
                if constexpr (Op == BinaryOp::Subtract) overflow = __builtin_sub_overflow(a.small, b.small, &result);
                if constexpr (Op == BinaryOp::Multiply) overflow = __builtin_mul_overflow(a.small, b.small, &result);
                if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo)
                {
                    overflow = a.small == std::numeric_limits<std::int64_t>::min() && b.small == -1;
                    if (!overflow)
                    {
                        const auto quotient = a.small / b.small; const auto remainder = a.small % b.small;
                        const bool adjust = remainder && ((remainder < 0) != (b.small < 0));
                        if constexpr (Op == BinaryOp::FloorDivide) result = quotient - (adjust ? 1 : 0);
                        else result = adjust ? remainder + b.small : remainder;
                    }
                    else if constexpr (Op == BinaryOp::Modulo) { result = 0; overflow = false; }
                }
                if (!overflow) { make_int(context, result, out); return; }
            }
            auto left = expanded(a); auto right = expanded(b); Integer result;
            if constexpr (Op == BinaryOp::Add) result = left + right;
            if constexpr (Op == BinaryOp::Subtract) result = left - right;
            if constexpr (Op == BinaryOp::Multiply) result = left * right;
            if constexpr (Op == BinaryOp::FloorDivide || Op == BinaryOp::Modulo)
            {
                Integer remainder = left % right;
                const bool adjust = remainder != 0 && ((remainder < 0) != (right < 0));
                if constexpr (Op == BinaryOp::FloorDivide) { result = left / right; if (adjust) --result; }
                else { result = std::move(remainder); if (adjust) result += right; }
            }
            publish(context, std::move(result), out);
        }
        template <CompareOp Op>
        void compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            if (!is_int(context, args[1])) { out.ok(context.builtin(CoreObject::NotImplemented)); return; }
            const auto &a = integer(context, args[0]); const auto &b = integer(context, args[1]);
            int order;
            if (!a.large && !b.large) order = (a.small > b.small) - (a.small < b.small);
            else { auto left = expanded(a); auto right = expanded(b); order = (left > right) - (left < right); }
            if constexpr (Op == CompareOp::Eq) boolean(context, order == 0, out);
            if constexpr (Op == CompareOp::Ne) boolean(context, order != 0, out);
            if constexpr (Op == CompareOp::Lt) boolean(context, order < 0, out);
            if constexpr (Op == CompareOp::Le) boolean(context, order <= 0, out);
            if constexpr (Op == CompareOp::Gt) boolean(context, order > 0, out);
            if constexpr (Op == CompareOp::Ge) boolean(context, order >= 0, out);
        }
        void int_bool(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            boolean(context, int_sign(context, args[0]) != 0, out);
        }
        void int_identity(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            copy_int(context, args[0], out);
        }
        void int_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            make_int(context, detail::int_hash_value(context, args[0]), out);
        }
        void int_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_int(context, args[0])) { type_error(context, out); return; }
            auto text = int_decimal(context, args[0]); make_str(context, {text.begin(), text.end()}, out);
        }
        void bool_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (context.type_of(args[0]) != context.builtin(CoreObject::BoolType)) { type_error(context, out); return; }
            make_str(context, args[0] == context.builtin(CoreObject::True) ? U"True" : U"False", out);
        }
        void int_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!context.has_layout<TypeObject>(args[0]) ||
                !is_subtype(context, args[0], context.builtin(CoreObject::IntType)) ||
                args[0] == context.builtin(CoreObject::BoolType))
            { type_error(context, out); return; }
            const auto &values = context.payload<TupleObject>(args[1]).items;
            if (values.size() > 1) { type_error(context, out); return; }
            RootScope roots(context); auto cls = roots.root(args[0]); ReturnSlot converted(context);
            if (values.empty()) make_int(context, 0, converted);
            else if (context.type_of(values[0]) == context.builtin(CoreObject::IntType)) copy_int(context, values[0], converted);
            else
            {
                auto value = roots.root(values[0]);
                auto slot = resolve_protocol(context, roots.root(context.type_of(value)), Protocol::Int, roots);
                if (slot.state == SlotState::Missing) index(context, value, converted);
                else invoke_protocol(context, value, slot, {}, converted);
                if (raised(converted)) { out.raised(converted.result().exception()); return; }
                if (!is_int(context, converted.result().value())) { type_error(context, out); return; }
            }
            const auto value = converted.result().value();
            if (cls.get(context) == context.builtin(CoreObject::IntType)) { copy_int(context, value, out); return; }
            const auto &source = integer(context, value);
            if (source.large)
            {
                auto storage = std::make_unique<detail::BigInt>(); storage->value = source.large->value;
                context.allocate_instance<IntObject>(cls, out, std::move(storage));
            }
            else context.allocate_instance<IntObject>(cls, out, source.small);
        }
        void bool_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[0] != context.builtin(CoreObject::BoolType)) { type_error(context, out); return; }
            const auto &values = context.payload<TupleObject>(args[1]).items;
            if (values.size() > 1) { type_error(context, out); return; }
            if (values.empty()) boolean(context, false, out);
            else { RootScope roots(context); truth(context, roots.root(values[0]), out); }
        }
    }
    IntObject::IntObject(InitialType head, std::int64_t value) : header(head), small(value) {}
    IntObject::IntObject(InitialType head, std::unique_ptr<detail::BigInt> value)
        : header(head), small(0), large(std::move(value)) {}
    IntObject::~IntObject() noexcept = default;
    std::size_t IntObject::external_bytes() const noexcept
    {
        if (!large) return 0;
        const auto &backend = large->value.backend();
        auto data = reinterpret_cast<std::uintptr_t>(backend.limbs());
        auto start = reinterpret_cast<std::uintptr_t>(large.get());
        const bool internal = data >= start && data - start < sizeof(detail::BigInt);
        return sizeof(detail::BigInt) + (internal ? 0 : backend.capacity() * sizeof(*backend.limbs()));
    }
    bool is_int(Attachment &context, Value value)
    {
        return is_subtype(context, context.type_of(value), context.builtin(CoreObject::IntType)) &&
               context.has_layout<IntObject>(value);
    }
    bool int_fits_signed(Attachment &context, Value value, std::int64_t &result)
    {
        const auto &data = integer(context, value);
        if (!data.large) { result = data.small; return true; }
        if (!fits(data.large->value)) return false;
        result = data.large->value.convert_to<std::int64_t>(); return true;
    }
    int int_sign(Attachment &context, Value value)
    {
        const auto &data = integer(context, value);
        return data.large ? (data.large->value > 0) - (data.large->value < 0) : (data.small > 0) - (data.small < 0);
    }
    std::string int_decimal(Attachment &context, Value value)
    {
        const auto &data = integer(context, value);
        return data.large ? data.large->value.str() : std::to_string(data.small);
    }
    void make_int(Attachment &context, std::int64_t value, ReturnSlot &out)
    {
        out.check_caller(context);
        if (value == 0 || value == 1) { out.ok(context.builtin(value == 0 ? CoreObject::IntZero : CoreObject::IntOne)); return; }
        RootScope roots(context);
        context.allocate_instance<IntObject>(roots.root(context.builtin(CoreObject::IntType)), out, value);
    }
    void make_int_decimal(Attachment &context, std::string_view text, ReturnSlot &out)
    {
        out.check_caller(context);
        bool negative = false;
        if (!text.empty() && (text.front() == '-' || text.front() == '+'))
        { negative = text.front() == '-'; text.remove_prefix(1); }
        if (text.empty()) throw std::invalid_argument("empty integer literal");
        Integer value = 0;
        for (char digit : text)
        {
            if (digit < '0' || digit > '9') throw std::invalid_argument("invalid native decimal integer literal");
            value *= 10; value += digit - '0';
        }
        if (negative) value = -value;
        publish(context, std::move(value), out);
    }
    void copy_int(Attachment &context, Value value, ReturnSlot &out)
    {
        out.check_caller(context); RootScope roots(context); roots.root(value);
        const auto &data = integer(context, value);
        if (context.type_of(value) == context.builtin(CoreObject::IntType)) { out.ok(value); return; }
        if (data.large) publish(context, data.large->value, out);
        else make_int(context, data.small, out);
    }
    namespace detail
    {
        std::int64_t int_hash_value(Attachment &context, Value value)
        {
            constexpr std::uint64_t modulus = (std::uint64_t{1} << (sizeof(std::intptr_t) == 8 ? 61 : 31)) - 1;
            const auto &data = integer(context, value);
            std::int64_t result;
            if (data.large) result = (data.large->value % modulus).convert_to<std::int64_t>();
            else
            {
                const auto magnitude = data.small < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(data.small) :
                                                       static_cast<std::uint64_t>(data.small);
                result = static_cast<std::int64_t>(magnitude % modulus);
                if (data.small < 0) result = -result;
            }
            return result == -1 ? -2 : result;
        }
        void install_numbers(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            SignatureLayout unary; unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
            SignatureLayout pair = unary; pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
            auto install = [&](std::u32string name, NativeEntry entry, bool binary = false) {
                install_native_attribute(context, core, at(CoreObject::IntType), std::move(name), binary ? pair : unary, entry);
            };
            install(U"__add__", arithmetic<BinaryOp::Add, false>, true);
            install(U"__radd__", arithmetic<BinaryOp::Add, true>, true);
            install(U"__sub__", arithmetic<BinaryOp::Subtract, false>, true);
            install(U"__rsub__", arithmetic<BinaryOp::Subtract, true>, true);
            install(U"__mul__", arithmetic<BinaryOp::Multiply, false>, true);
            install(U"__rmul__", arithmetic<BinaryOp::Multiply, true>, true);
            install(U"__floordiv__", arithmetic<BinaryOp::FloorDivide, false>, true);
            install(U"__rfloordiv__", arithmetic<BinaryOp::FloorDivide, true>, true);
            install(U"__mod__", arithmetic<BinaryOp::Modulo, false>, true);
            install(U"__rmod__", arithmetic<BinaryOp::Modulo, true>, true);
            install(U"__eq__", compare<CompareOp::Eq>, true); install(U"__ne__", compare<CompareOp::Ne>, true);
            install(U"__lt__", compare<CompareOp::Lt>, true); install(U"__le__", compare<CompareOp::Le>, true);
            install(U"__gt__", compare<CompareOp::Gt>, true); install(U"__ge__", compare<CompareOp::Ge>, true);
            install(U"__bool__", int_bool); install(U"__int__", int_identity); install(U"__index__", int_identity);
            install(U"__hash__", int_hash);
            install(U"__repr__", int_repr); install(U"__str__", int_repr);
            install_native_attribute(context, core, at(CoreObject::BoolType), U"__repr__", unary, bool_repr);
            install_native_attribute(context, core, at(CoreObject::BoolType), U"__str__", unary, bool_repr);
            for (auto id : {CoreObject::IntType, CoreObject::BoolType})
            {
                SignatureLayout constructor; constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
                constructor.varargs = U"args";
                install_native_attribute(context, core, at(id), U"__new__", constructor,
                                         id == CoreObject::IntType ? int_new : bool_new);
                RootScope roots(context); ReturnSlot out(context);
                const auto dict = *TypeView(context, at(id)).dictionary();
                auto function = *name_lookup(context, dict, U"__new__");
                context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out, function);
                auto wrapper = roots.root(out.result().value());
                context.allocate_instance<StrObject>(at(CoreObject::StrType), out, U"__new__");
                name_store(context, dict, out.result().value(), wrapper.get(context));
            }
        }
    }
}
