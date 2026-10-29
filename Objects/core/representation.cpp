#include "cxxpython/core/protocol.hpp"

#include <algorithm>
#include <bit>
#include <charconv>

namespace cxxpython::core
{
    namespace
    {
        #include "cxxpython/core/unicode_nonprintable.inc"

        bool raised(ReturnSlot &out) { return out.result().tag() == ResultTag::Raised; }
        void type_error(Attachment &context, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, U"descriptor received an incompatible value", out); }
        template <class T, CoreObject Type> bool instance_of(Attachment &context, Value value)
        {
            return is_subtype(context, context.type_of(value), context.builtin(Type)) && context.has_layout<T>(value);
        }
        bool string_value(Attachment &context, Value value)
        { return instance_of<StrObject, CoreObject::StrType>(context, value); }
        std::int64_t signed_hash(std::uintptr_t word)
        {
            const auto result = static_cast<std::int64_t>(std::bit_cast<std::intptr_t>(word));
            return result == -1 ? -2 : result;
        }
        std::u32string pointer_text(Value value)
        {
            char buffer[2 * sizeof(std::uintptr_t)];
            auto converted = std::to_chars(std::begin(buffer), std::end(buffer), reinterpret_cast<std::uintptr_t>(value.object()), 16);
            return U"0x" + std::u32string(buffer, converted.ptr);
        }
        std::u32string type_name(Attachment &context, Value type, bool qualified)
        {
            const auto &data = context.payload<TypeObject>(type);
            return context.payload<StrObject>(std::get<Value>(qualified ? data.qualname : data.name)).value;
        }
        std::u32string display_type(Attachment &context, Value type)
        {
            const auto &data = context.payload<TypeObject>(type);
            auto module = data.dictionary ? detail::name_lookup(context, *data.dictionary, U"__module__") : std::optional<Value>{};
            if (module && string_value(context, *module))
            {
                const auto &name = context.payload<StrObject>(*module).value;
                if (name != U"builtins") return name + U"." + type_name(context, type, true);
            }
            return type_name(context, type, false);
        }
        void object_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { make_int(context, signed_hash(std::rotr(reinterpret_cast<std::uintptr_t>(args[0].object()), 4)), out); }
        void object_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { make_str(context, U"<" + display_type(context, context.type_of(args[0])) + U" object at " + pointer_text(args[0]) + U">", out); }
        void object_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        { RootScope roots(context); repr(context, roots.root(args[0]), out); }
        void type_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!context.has_layout<TypeObject>(args[0]) || !is_subtype(context, context.type_of(args[0]), context.type_type()))
            { type_error(context, out); return; }
            make_str(context, U"<class '" + display_type(context, args[0]) + U"'>", out);
        }
        void singleton_repr(Attachment &context, Handle fn, std::span<const Value> args, ReturnSlot &out)
        {
            const auto &captures = context.payload<NativeFunctionObject>(fn).captures;
            if (args[0] != captures[0]) { type_error(context, out); return; }
            out.ok(captures[1]);
        }
        void none_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[0] != context.builtin(CoreObject::None)) { type_error(context, out); return; }
            make_int(context, static_cast<std::intptr_t>(0xfca86420U), out);
        }
        void string_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!string_value(context, args[0])) { type_error(context, out); return; }
            const auto &text = context.payload<StrObject>(args[0]).value;
            auto word = text.empty() ? 0 : static_cast<std::uintptr_t>(context.keyed_hash(std::as_bytes(std::span(text.data(), text.size()))));
            make_int(context, signed_hash(word), out);
        }
        void string_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!string_value(context, args[0])) { type_error(context, out); return; }
            if (context.type_of(args[0]) == context.builtin(CoreObject::StrType)) out.ok(args[0]);
            else make_str(context, context.payload<StrObject>(args[0]).value, out);
        }
        bool printable(char32_t point)
        {
            if (point == U' ') return true;
            auto next = std::upper_bound(std::begin(non_printable), std::end(non_printable), point,
                                        [](auto value, auto range) { return value < range.first; });
            return next == std::begin(non_printable) || point > (next - 1)->second;
        }
        void string_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!string_value(context, args[0])) { type_error(context, out); return; }
            const auto &text = context.payload<StrObject>(args[0]).value;
            const auto quote = text.find(U'\'') != std::u32string::npos && text.find(U'"') == std::u32string::npos ? U'"' : U'\'';
            std::u32string result(1, quote);
            constexpr char32_t digits[] = U"0123456789abcdef";
            for (auto point : text)
            {
                if (point == quote || point == U'\\') { result += U'\\'; result += point; }
                else if (point == U'\n') result += U"\\n";
                else if (point == U'\r') result += U"\\r";
                else if (point == U'\t') result += U"\\t";
                else if (printable(point)) result += point;
                else
                {
                    const unsigned count = point <= 0xff ? 2 : point <= 0xffff ? 4 : 8;
                    result += count == 2 ? U"\\x" : count == 4 ? U"\\u" : U"\\U";
                    for (auto remaining = count; remaining; --remaining) result += digits[(point >> ((remaining - 1) * 4)) & 15];
                }
            }
            result += quote; make_str(context, std::move(result), out);
        }
        template <CompareOp Op> void order_result(Attachment &context, int order, ReturnSlot &out)
        {
            bool result;
            if constexpr (Op == CompareOp::Eq) result = order == 0;
            if constexpr (Op == CompareOp::Ne) result = order != 0;
            if constexpr (Op == CompareOp::Lt) result = order < 0;
            if constexpr (Op == CompareOp::Le) result = order <= 0;
            if constexpr (Op == CompareOp::Gt) result = order > 0;
            if constexpr (Op == CompareOp::Ge) result = order >= 0;
            out.ok(context.builtin(result ? CoreObject::True : CoreObject::False));
        }
        template <CompareOp Op>
        void string_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!string_value(context, args[0])) { type_error(context, out); return; }
            if (!string_value(context, args[1])) { out.ok(context.builtin(CoreObject::NotImplemented)); return; }
            order_result<Op>(context, context.payload<StrObject>(args[0]).value.compare(context.payload<StrObject>(args[1]).value), out);
        }
        struct ReprGuard
        {
            Attachment &context;
            Value object;
            bool entered;
            ReprGuard(Attachment &c, Handle value) : context(c), object(value.get(c)), entered(c.enter_repr(value)) {}
            ~ReprGuard() { if (entered) context.leave_repr(object); }
        };
        void tuple_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) { type_error(context, out); return; }
            RootScope roots(context); ReprGuard recursion(context, roots.root(args[0]));
            if (!recursion.entered) { make_str(context, U"(...)", out); return; }
            const auto &items = context.payload<TupleObject>(args[0]).items;
            std::u32string result = U"(";
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                RootScope item_roots(context); ReturnSlot text(context); repr(context, item_roots.root(items[i]), text);
                if (raised(text)) { out.raised(text.result().exception()); return; }
                if (i) result += U", ";
                result += context.payload<StrObject>(text.result().value()).value;
            }
            if (items.size() == 1) result += U',';
            result += U')'; make_str(context, std::move(result), out);
        }
        void tuple_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) { type_error(context, out); return; }
            constexpr std::uintptr_t prime1 = sizeof(std::uintptr_t) == 8 ? 11400714785074694791ULL : 2654435761UL;
            constexpr std::uintptr_t prime2 = sizeof(std::uintptr_t) == 8 ? 14029467366897019727ULL : 2246822519UL;
            constexpr std::uintptr_t prime5 = sizeof(std::uintptr_t) == 8 ? 2870177450012600261ULL : 374761393UL;
            auto accumulator = prime5;
            const auto &items = context.payload<TupleObject>(args[0]).items;
            for (auto value : items)
            {
                RootScope roots(context); ReturnSlot hashed(context); cxxpython::core::hash(context, roots.root(value), hashed);
                if (raised(hashed)) { out.raised(hashed.result().exception()); return; }
                std::int64_t lane = 0; (void)int_fits_signed(context, hashed.result().value(), lane);
                accumulator += static_cast<std::uintptr_t>(lane) * prime2;
                accumulator = std::rotl(accumulator, sizeof(std::uintptr_t) == 8 ? 31 : 13); accumulator *= prime1;
            }
            accumulator += items.size() ^ (prime5 ^ 3527539UL);
            make_int(context, accumulator == std::numeric_limits<std::uintptr_t>::max() ? 1546275796 : signed_hash(accumulator), out);
        }
        template <CompareOp Op>
        void tuple_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[0])) { type_error(context, out); return; }
            if (!instance_of<TupleObject, CoreObject::TupleType>(context, args[1]))
            { out.ok(context.builtin(CoreObject::NotImplemented)); return; }
            const auto &left = context.payload<TupleObject>(args[0]).items;
            const auto &right = context.payload<TupleObject>(args[1]).items;
            for (std::size_t i = 0; i < std::min(left.size(), right.size()); ++i)
            {
                RootScope roots(context); auto a = roots.root(left[i]); auto b = roots.root(right[i]); ReturnSlot equal(context);
                container_equal(context, a, b, equal);
                if (raised(equal)) { out.raised(equal.result().exception()); return; }
                if (equal.result().value() == context.builtin(CoreObject::True)) continue;
                if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne) order_result<Op>(context, 1, out);
                else rich_compare(context, a, b, Op, out);
                return;
            }
            order_result<Op>(context, (left.size() > right.size()) - (left.size() < right.size()), out);
        }
        template <CompareOp Op>
        void cell_compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<CellObject, CoreObject::CellType>(context, args[0])) { type_error(context, out); return; }
            if (!instance_of<CellObject, CoreObject::CellType>(context, args[1]))
            { out.ok(context.builtin(CoreObject::NotImplemented)); return; }
            const auto left = context.payload<CellObject>(args[0]).contents;
            const auto right = context.payload<CellObject>(args[1]).contents;
            if (left && right)
            { RootScope roots(context); rich_compare(context, roots.root(*left), roots.root(*right), Op, out); }
            else order_result<Op>(context, static_cast<int>(left.has_value()) - static_cast<int>(right.has_value()), out);
        }
        void cell_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<CellObject, CoreObject::CellType>(context, args[0])) { type_error(context, out); return; }
            const auto contents = context.payload<CellObject>(args[0]).contents;
            auto result = U"<cell at " + pointer_text(args[0]) + U": ";
            result += contents ? type_name(context, context.type_of(*contents), false).substr(0, 80) + U" object at " + pointer_text(*contents) : U"empty";
            result += U'>'; make_str(context, std::move(result), out);
        }
        void proxy_hash(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<MappingProxyObject, CoreObject::MappingProxyType>(context, args[0])) { type_error(context, out); return; }
            RootScope roots(context); cxxpython::core::hash(context, roots.root(context.payload<MappingProxyObject>(args[0]).mapping), out);
        }
        void exception_repr(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<ExceptionObject, CoreObject::BaseExceptionType>(context, args[0])) { type_error(context, out); return; }
            RootScope roots(context); auto arguments = roots.root(context.payload<ExceptionObject>(args[0]).args);
            const auto &values = context.payload<TupleObject>(arguments).items;
            auto name = type_name(context, context.type_of(args[0]), false); ReturnSlot text(context);
            if (values.size() == 1)
            {
                repr(context, roots.root(values[0]), text);
                if (!raised(text)) name += U"(" + context.payload<StrObject>(text.result().value()).value + U")";
            }
            else
            {
                repr(context, arguments, text);
                if (!raised(text)) name += context.payload<StrObject>(text.result().value()).value;
            }
            if (raised(text)) { out.raised(text.result().exception()); return; }
            make_str(context, std::move(name), out);
        }
        void exception_str(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!instance_of<ExceptionObject, CoreObject::BaseExceptionType>(context, args[0])) { type_error(context, out); return; }
            RootScope roots(context); auto arguments = roots.root(context.payload<ExceptionObject>(args[0]).args);
            const auto &values = context.payload<TupleObject>(arguments).items;
            if (values.empty()) make_str(context, U"", out);
            else str(context, values.size() == 1 ? roots.root(values[0]) : arguments, out);
        }
    }
    namespace detail
    {
        void install_value_protocols(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            SignatureLayout unary; unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
            SignatureLayout pair = unary; pair.parameters.push_back({U"other", ParameterKind::PositionalOnly});
            auto install = [&](Handle owner, std::u32string name, NativeEntry entry) {
                install_native_attribute(context, core, owner, std::move(name), unary, entry);
            };
            RootScope roots(context); auto object = roots.root(context.object_type());
            install(object, U"__hash__", object_hash); install(object, U"__repr__", object_repr); install(object, U"__str__", object_str);
            install(roots.root(context.type_type()), U"__repr__", type_repr);
            install(at(CoreObject::StrType), U"__hash__", string_hash); install(at(CoreObject::StrType), U"__str__", string_str);
            install(at(CoreObject::StrType), U"__repr__", string_repr);
            install(at(CoreObject::TupleType), U"__hash__", tuple_hash); install(at(CoreObject::TupleType), U"__repr__", tuple_repr);
            install(at(CoreObject::CellType), U"__repr__", cell_repr); install(at(CoreObject::NoneType), U"__hash__", none_hash);
            install(at(CoreObject::MappingProxyType), U"__hash__", proxy_hash);
            install(at(CoreObject::BaseExceptionType), U"__repr__", exception_repr);
            install(at(CoreObject::BaseExceptionType), U"__str__", exception_str);
            for (auto [id, label] : {std::pair{CoreObject::None, U"None"}, {CoreObject::NotImplemented, U"NotImplemented"}})
            {
                ReturnSlot text(context); context.allocate_instance<StrObject>(at(CoreObject::StrType), text, label);
                auto name = roots.root(text.result().value());
                install_native_attribute(context, core, roots.root(context.type_of(at(id))), U"__repr__", unary, singleton_repr,
                                         {at(id).get(context), name.get(context)});
            }
            for (auto id : {CoreObject::CellType, CoreObject::DictType})
            {
                ReturnSlot text(context); context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__hash__");
                name_store(context, *context.payload<TypeObject>(at(id)).dictionary, text.result().value(), at(CoreObject::None).get(context));
            }
            const std::array<std::u32string, 6> names{U"__eq__", U"__ne__", U"__lt__", U"__le__", U"__gt__", U"__ge__"};
            const std::array string_entries{string_compare<CompareOp::Eq>, string_compare<CompareOp::Ne>, string_compare<CompareOp::Lt>,
                string_compare<CompareOp::Le>, string_compare<CompareOp::Gt>, string_compare<CompareOp::Ge>};
            const std::array tuple_entries{tuple_compare<CompareOp::Eq>, tuple_compare<CompareOp::Ne>, tuple_compare<CompareOp::Lt>,
                tuple_compare<CompareOp::Le>, tuple_compare<CompareOp::Gt>, tuple_compare<CompareOp::Ge>};
            const std::array cell_entries{cell_compare<CompareOp::Eq>, cell_compare<CompareOp::Ne>, cell_compare<CompareOp::Lt>,
                cell_compare<CompareOp::Le>, cell_compare<CompareOp::Gt>, cell_compare<CompareOp::Ge>};
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                install_native_attribute(context, core, at(CoreObject::StrType), names[i], pair, string_entries[i]);
                install_native_attribute(context, core, at(CoreObject::TupleType), names[i], pair, tuple_entries[i]);
                install_native_attribute(context, core, at(CoreObject::CellType), names[i], pair, cell_entries[i]);
            }
        }
    }
}
