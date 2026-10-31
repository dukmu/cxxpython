// Managed AST conversion follows CPython 3.12.14 Python/Python-ast.c and ast.c.
#include "internal/astmodule.hpp"

#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"

#include <algorithm>
#include <limits>

namespace cxxpython::core::detail {
namespace {
struct Raised {};
std::u32string text(std::string_view value)
{
    return {value.begin(), value.end()};
}
class Snapshot {
    Attachment &c;
    ReturnSlot &out;
    std::size_t depth = 0;
    [[noreturn]] void fail(CoreObject category, std::u32string message)
    {
        raise_error(c, category, std::move(message), out);
        throw Raised{};
    }
    void check(const ReturnSlot &result)
    {
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            throw Raised{};
        }
    }
    std::u32string type_name(Handle object)
    {
        const auto name = TypeView(c, c.type_of(object)).name();
        return std::holds_alternative<Value>(name)
                   ? c.payload<StrObject>(std::get<Value>(name)).value
                   : std::get<std::u32string>(name);
    }
    bool none(Handle value)
    {
        return value.get(c) == c.builtin(CoreObject::None);
    }
    bool family(ast::Kind kind, ast::Kind expected)
    {
        for (;;) {
            if (kind == expected)
                return true;
            if (kind == ast::Kind::AST)
                return false;
            kind = ast::info(kind).base;
        }
    }
    ast::Kind node_kind(Handle owner)
    {
        RootScope roots(c);
        auto classes = roots.root(c.payload<AstObject>(owner).types);
        const auto &types = c.payload<TupleObject>(classes).items;
        TypeView view(c, c.type_of(owner));
        const auto &mro =
            c.payload<TupleObject>(c.payload<TypeSnapshot>(*view.snapshot()).mro).items;
        for (auto parent : mro)
            for (std::size_t i = 0; i < types.size(); ++i)
                if (types[i] == parent)
                    return static_cast<ast::Kind>(i);
        fail(CoreObject::TypeErrorType, U"AST instance has no matching native AST class");
    }
    bool read(Handle owner, std::u32string_view name, ReturnSlot &value)
    {
        RootScope roots(c);
        ReturnSlot key(c);
        make_str(c, std::u32string(name), key);
        get_attribute(c, owner, roots.root(key.value()), value);
        if (value.tag() == ResultTag::Raised &&
            is_subtype(c, c.type_of(value.exception()),
                       c.builtin(CoreObject::AttributeErrorType))) {
            value.reset(c);
            return false;
        }
        check(value);
        return true;
    }
    std::int64_t integer(Handle value)
    {
        if (!c.has_layout<IntObject>(value.get(c)) ||
            !is_subtype(c, c.type_of(value), c.builtin(CoreObject::IntType)))
            fail(CoreObject::ValueErrorType, U"invalid integer value: " + type_name(value));
        std::int64_t result = 0;
        if (!int_fits_signed(c, value.get(c), result) || result < std::numeric_limits<int>::min() ||
            result > std::numeric_limits<int>::max())
            fail(CoreObject::OverflowErrorType, U"Python int too large to convert to C int");
        return result;
    }
    Literal constant(Handle value, std::size_t nesting = 0)
    {
        if (nesting >= 256)
            fail(CoreObject::RecursionErrorType,
                 U"maximum recursion depth exceeded during compilation");
        const auto type = c.type_of(value);
        if (none(value))
            return {Literal::Kind::None, {}};
        if (value.get(c) == c.builtin(CoreObject::True))
            return {Literal::Kind::True, {}};
        if (value.get(c) == c.builtin(CoreObject::False))
            return {Literal::Kind::False, {}};
        if (value.get(c) == c.builtin(CoreObject::Ellipsis))
            return {Literal::Kind::Ellipsis, {}};
        if (type == c.builtin(CoreObject::IntType))
            return {Literal::Kind::Integer, text(int_decimal(c, value.get(c)))};
        if (type == c.builtin(CoreObject::FloatType))
            return {Literal::Kind::Float, {}, c.payload<FloatObject>(value).value};
        if (type == c.builtin(CoreObject::ComplexType)) {
            const auto &number = c.payload<ComplexObject>(value);
            return {Literal::Kind::Complex, {}, number.real, number.imag};
        }
        if (type == c.builtin(CoreObject::StrType))
            return {Literal::Kind::String, c.payload<StrObject>(value).value};
        if (type == c.builtin(CoreObject::BytesType)) {
            std::u32string data;
            for (const unsigned char byte : c.payload<BytesObject>(value).value)
                data.push_back(byte);
            return {Literal::Kind::Bytes, std::move(data)};
        }
        if (type == c.builtin(CoreObject::TupleType) ||
            type == c.builtin(CoreObject::FrozenSetType)) {
            RootScope roots(c);
            std::vector<Literal> elements;
            if (type == c.builtin(CoreObject::TupleType)) {
                for (auto item : c.payload<TupleObject>(value).items) {
                    elements.push_back(constant(roots.root(item), nesting + 1));
                    if ((elements.size() & 255) == 0)
                        c.poll();
                }
            } else {
                DictView view(c, c.payload<SetObject>(value).table);
                for (const auto &entry : view.entries()) {
                    elements.push_back(constant(roots.root(entry.key), nesting + 1));
                    if ((elements.size() & 255) == 0)
                        c.poll();
                }
            }
            return {type == c.builtin(CoreObject::TupleType) ? Literal::Kind::Tuple
                                                             : Literal::Kind::FrozenSet,
                    {},
                    0.0,
                    0.0,
                    std::move(elements)};
        }
        fail(CoreObject::TypeErrorType, U"got an invalid type in Constant: " + type_name(value));
    }
    ast::Field scalar(Handle value, const ast::FieldInfo &field, bool sequence_element = false)
    {
        using ast::FieldType;
        if (field.type == FieldType::constant)
            return constant(value);
        if (none(value) && (field.optional || sequence_element))
            return std::monostate{};
        if (field.type == FieldType::int_)
            return integer(value);
        if (field.type == FieldType::identifier || field.type == FieldType::string) {
            if (!c.has_layout<StrObject>(value.get(c)))
                fail(CoreObject::TypeErrorType, U"AST string must be of type str");
            return c.payload<StrObject>(value).value;
        }
        const auto category = *ast::node_category(field.type);
        return node(value, category);
    }
    ast::Field field(Handle owner, const ast::FieldInfo &spec, const ast::NodeInfo &info)
    {
        RootScope roots(c);
        ReturnSlot result(c);
        if (!read(owner, spec.name, result)) {
            if (spec.optional)
                return std::monostate{};
            fail(CoreObject::TypeErrorType, U"required field '" + std::u32string(spec.name) +
                                                U"' missing from " + text(info.name));
        }
        auto value = roots.root(result.value());
        if (!spec.sequence)
            return scalar(value, spec);
        if (!c.has_layout<ListObject>(value.get(c)) ||
            !is_subtype(c, c.type_of(value), c.builtin(CoreObject::ListType)))
            fail(CoreObject::TypeErrorType, text(info.name) + U" field \"" +
                                                std::u32string(spec.name) +
                                                U"\" must be a list, not a " + type_name(value));
        std::vector<Value> items;
        list_snapshot(c, value, roots, items);
        if (spec.type == ast::FieldType::identifier || spec.type == ast::FieldType::string) {
            ast::Strings strings;
            for (auto item : items)
                strings.push_back(std::get<std::u32string>(scalar(roots.root(item), spec)));
            return strings;
        }
        ast::Nodes nodes;
        for (auto item : items) {
            auto element = roots.root(item);
            if (none(element))
                nodes.push_back({});
            else
                nodes.push_back(node(element, *ast::node_category(spec.type)));
            if ((nodes.size() & 255) == 0)
                c.poll();
        }
        return nodes;
    }
    void location(Handle owner, ast::Node &node, const ast::NodeInfo &info)
    {
        if (info.attributes.empty())
            return;
        RootScope roots(c);
        ReturnSlot value(c);
        std::array<std::int64_t, 4> numbers{};
        for (std::size_t i = 0; i < 4; ++i) {
            const auto &spec = info.attributes[i];
            if (!read(owner, spec.name, value) || value.value() == c.builtin(CoreObject::None)) {
                if (i < 2)
                    fail(CoreObject::TypeErrorType,
                         U"required field '" + std::u32string(spec.name) + U"' missing from " +
                             text(ast::info(info.base).name));
                numbers[i] = numbers[i - 2];
            } else
                numbers[i] = integer(roots.root(value.value()));
        }
        const auto [line, col, end_line, end_col] = numbers;
        if (line <= 0 || end_line <= 0)
            fail(CoreObject::ValueErrorType, U"AST line numbers must be positive in this VM");
        auto num = [&](std::int64_t n) { return text(std::to_string(n)); };
        if (line > end_line)
            fail(CoreObject::ValueErrorType,
                 U"AST node line range (" + num(line) + U", " + num(end_line) + U") is not valid");
        if ((line < 0 && end_line != line) || (col < 0 && col != end_col))
            fail(CoreObject::ValueErrorType, U"AST node column range (" + num(col) + U", " +
                                                 num(end_col) + U") for line range (" + num(line) +
                                                 U", " + num(end_line) + U") is not valid");
        if (line == end_line && col > end_col)
            fail(CoreObject::ValueErrorType, U"line " + num(line) + U", column " + num(col) + U"-" +
                                                 num(end_col) + U" is not a valid range");
        constexpr auto unknown = std::numeric_limits<std::uint32_t>::max();
        node.span.coordinates =
            SourcePosition{static_cast<std::uint32_t>(std::max<std::int64_t>(0, line)),
                           col < 0 ? unknown : static_cast<std::uint32_t>(col),
                           static_cast<std::uint32_t>(std::max<std::int64_t>(0, end_line)),
                           end_col < 0 ? unknown : static_cast<std::uint32_t>(end_col)};
    }

  public:
    Snapshot(Attachment &context, ReturnSlot &result) : c(context), out(result) {}
    ast::NodePtr node(Handle owner, ast::Kind expected)
    {
        const auto kind = c.has_layout<AstObject>(owner.get(c)) ? node_kind(owner) : ast::Kind::AST;
        if (ast::info(kind).abstract || !family(kind, expected)) {
            if (expected == ast::Kind::Module || expected == ast::Kind::Expression ||
                expected == ast::Kind::Interactive)
                fail(CoreObject::TypeErrorType, U"expected " + text(ast::info(expected).name) +
                                                    U" node, got " + type_name(owner));
            fail(CoreObject::TypeErrorType, U"expected some sort of " +
                                                text(ast::info(expected).name) + U", but got " +
                                                type_name(owner));
        }
        if (depth >= 256)
            fail(CoreObject::RecursionErrorType,
                 U"maximum recursion depth exceeded during compilation");
        ++depth;
        struct Exit {
            std::size_t &depth;
            ~Exit()
            {
                --depth;
            }
        } exit{depth};
        const auto &info = ast::info(kind);
        auto result = ast::make(kind, Span{});
        location(owner, *result, info);
        for (const auto &spec : info.fields)
            result->fields.push_back(field(owner, spec, info));
        if (info.attributes.empty()) {
            // ASDL products have no public locations; use an actual child for IR diagnostics.
            for (const auto &value : result->fields) {
                const ast::Node *child = nullptr;
                if (const auto *item = std::get_if<ast::NodePtr>(&value))
                    child = item->get();
                else if (const auto *items = std::get_if<ast::Nodes>(&value))
                    if (!items->empty())
                        child = items->front().get();
                if (child && child->span.coordinates) {
                    result->span = child->span;
                    break;
                }
            }
        }
        return result;
    }
};
} // namespace

ast::NodePtr snapshot_ast(Attachment &context, Handle tree, InputMode mode, ReturnSlot &out)
{
    try {
        return Snapshot(context, out)
            .node(tree, mode == InputMode::Exec   ? ast::Kind::Module
                        : mode == InputMode::Eval ? ast::Kind::Expression
                                                  : ast::Kind::Interactive);
    } catch (const Raised &) {
        return {};
    }
}
} // namespace cxxpython::core::detail
