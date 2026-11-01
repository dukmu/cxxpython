// AST construction adapted from CPython 3.12.14 Python/Python-ast.c.
#include "internal/astmodule.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
namespace {
struct Raised {};
void check(const ReturnSlot &value, ReturnSlot &out)
{
    if (value.tag() == ResultTag::Raised) {
        out.raised(value.exception());
        throw Raised{};
    }
}
std::u32string text(std::string_view value)
{
    return {value.begin(), value.end()};
}
void get(Attachment &c, Handle object, std::u32string_view name, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot key(c);
    make_str(c, std::u32string(name), key);
    get_attribute(c, object, roots.root(key.value()), out);
}
void set(Attachment &c, Handle object, std::u32string_view name, Handle value, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot key(c);
    make_str(c, std::u32string(name), key);
    set_attribute(c, object, roots.root(key.value()), value, out);
}
std::u32string type_name(Attachment &c, Value value)
{
    const auto name = TypeView(c, c.type_of(value)).name();
    return std::holds_alternative<Value>(name) ? c.payload<StrObject>(std::get<Value>(name)).value
                                               : std::get<std::u32string>(name);
}
void ast_new(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto cls = roots.root(args[0]);
    auto types = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    const auto &classes = c.payload<TupleObject>(types).items;
    if (!c.has_layout<TypeObject>(cls.get(c)) || !is_subtype(c, cls.get(c), classes[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"AST.__new__ requires an AST subtype", out);
        return;
    }
    c.allocate_instance<AstObject>(cls, out, types.get(c));
}
void ast_init(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]), positional = roots.root(args[1]),
         keywords = roots.root(args[2]);
    ReturnSlot field(c), result(c), key(c);
    try {
        get(c, roots.root(c.type_of(self)), U"_fields", field);
        check(field, out);
        auto fields = roots.root(field.value());
        length(c, fields, result);
        check(result, out);
        std::int64_t count = 0;
        if (!int_fits_signed(c, result.value(), count)) {
            raise_error(c, CoreObject::OverflowErrorType, U"AST field sequence is too large", out);
            return;
        }
        const auto &values = c.payload<TupleObject>(positional).items;
        if (values.size() > static_cast<std::uint64_t>(count)) {
            raise_error(c, CoreObject::TypeErrorType,
                        type_name(c, self.get(c)) + U" constructor takes at most " +
                            text(std::to_string(count)) + U" positional argument" +
                            (count == 1 ? U"" : U"s"),
                        out);
            return;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            make_int(c, static_cast<std::int64_t>(i), key);
            get_item(c, fields, roots.root(key.value()), field);
            check(field, out);
            set_attribute(c, self, roots.root(field.value()), roots.root(values[i]), result);
            check(result, out);
        }
        DictView keyword_view(c, keywords.get(c));
        for (const auto &entry : keyword_view.entries()) {
            auto name = roots.root(entry.key), value = roots.root(entry.value);
            for (std::int64_t i = 0; i < count; ++i) {
                make_int(c, i, key);
                get_item(c, fields, roots.root(key.value()), field);
                check(field, out);
                rich_compare(c, roots.root(field.value()), name, CompareOp::Eq, result);
                check(result, out);
                truth(c, roots.root(result.value()), result);
                check(result, out);
                if (result.value() == c.builtin(CoreObject::True)) {
                    if (static_cast<std::uint64_t>(i) < values.size()) {
                        raise_error(c, CoreObject::TypeErrorType,
                                    type_name(c, self.get(c)) +
                                        U" got multiple values for argument '" +
                                        c.payload<StrObject>(name).value + U"'",
                                    out);
                        return;
                    }
                    break;
                }
            }
            set_attribute(c, self, name, value, result);
            check(result, out);
        }
        out.ok(c.builtin(CoreObject::None));
    } catch (const Raised &) {
    }
}
void ast_reduce(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot dictionary(c), empty(c);
    get(c, self, U"__dict__", dictionary);
    if (dictionary.tag() == ResultTag::Raised) {
        out.raised(dictionary.exception());
        return;
    }
    cxxpython::core::make_tuple(c, {}, empty);
    const std::array items{c.type_of(self), empty.value(), dictionary.value()};
    cxxpython::core::make_tuple(c, items, out);
}
bool singleton(ast::Kind kind)
{
    using ast::Kind;
    const auto base = ast::info(kind).base;
    return base == Kind::expr_context || base == Kind::boolop || base == Kind::operator_ ||
           base == Kind::unaryop || base == Kind::cmpop;
}
void state_anchor(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(c.builtin(CoreObject::None));
}
class Materializer {
    Attachment &c;
    const SourceBuffer &source;
    Handle types, markers;
    ReturnSlot &out;
    std::size_t depth = 0;

    void field(const ast::Field &field, ReturnSlot &value)
    {
        RootScope roots(c);
        std::visit(
            [&](const auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    value.ok(c.builtin(CoreObject::None));
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    make_int(c, item, value);
                else if constexpr (std::is_same_v<T, std::u32string>)
                    make_str(c, item, value);
                else if constexpr (std::is_same_v<T, Literal>)
                    materialize_literal(c, item, value);
                else if constexpr (std::is_same_v<T, ast::NodePtr>) {
                    if (item)
                        node(*item, value);
                    else
                        value.ok(c.builtin(CoreObject::None));
                } else {
                    std::vector<Value> items;
                    items.reserve(item.size());
                    ReturnSlot child(c);
                    for (const auto &element : item) {
                        if constexpr (std::is_same_v<T, ast::Strings>)
                            make_str(c, element, child);
                        else if (element)
                            node(*element, child);
                        else
                            child.ok(c.builtin(CoreObject::None));
                        check(child, out);
                        items.push_back(roots.root(child.value()).get(c));
                        if ((items.size() & 255) == 0)
                            c.poll();
                    }
                    c.allocate_instance<ListObject>(roots.root(c.builtin(CoreObject::ListType)),
                                                    value, std::move(items));
                }
            },
            field);
        check(value, out);
    }

  public:
    Materializer(Attachment &context, const SourceBuffer &buffer, Handle classes, Handle singletons,
                 ReturnSlot &result)
        : c(context), source(buffer), types(classes), markers(singletons), out(result)
    {}
    void node(const ast::Node &node, ReturnSlot &result)
    {
        if (depth >= 256) {
            raise_error(c, CoreObject::RecursionErrorType, U"maximum AST depth exceeded", out);
            throw Raised{};
        }
        ++depth;
        struct Exit {
            std::size_t &depth;
            ~Exit()
            {
                --depth;
            }
        } exit{depth};
        const auto &info = ast::info(node.kind);
        if (singleton(node.kind)) {
            result.ok(c.payload<TupleObject>(markers).items[static_cast<std::size_t>(node.kind)]);
            return;
        }
        RootScope roots(c);
        const auto type =
            roots.root(c.payload<TupleObject>(types).items[static_cast<std::size_t>(node.kind)]);
        c.allocate_instance<AstObject>(type, result, types.get(c));
        check(result, out);
        auto instance = roots.root(result.value());
        if (node.fields.size() != info.fields.size())
            throw std::logic_error("ASDL adapter produced an invalid field count");
        ReturnSlot value(c), assigned(c);
        for (std::size_t i = 0; i < node.fields.size(); ++i) {
            field(node.fields[i], value);
            set(c, instance, info.fields[i].name, roots.root(value.value()), assigned);
            check(assigned, out);
        }
        if (!info.attributes.empty()) {
            const auto position = source.position(node.span);
            const std::array<std::int64_t, 4> parts{position.line, position.column,
                                                    position.end_line, position.end_column};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                make_int(c, parts[i], value);
                check(value, out);
                set(c, instance, info.attributes[i].name, roots.root(value.value()), assigned);
                check(assigned, out);
            }
        }
        result.ok(instance.get(c));
    }
};
} // namespace

void initialize_ast(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot value(c), key(c), stored(c);
    std::vector<Value> classes;
    auto publish = [&] {
        for (const auto &info : ast::node_info) {
            make_str(c, text(info.name), key);
            name_store(c, dictionary.get(c), key.value(),
                       classes[static_cast<std::size_t>(info.kind)]);
        }
        for (auto [name, flag] : {std::pair{U"PyCF_ONLY_AST", 0x400},
                                  {U"PyCF_TYPE_COMMENTS", 0x1000},
                                  {U"PyCF_ALLOW_TOP_LEVEL_AWAIT", 0x2000}}) {
            make_int(c, flag, value);
            auto number = roots.root(value.value());
            make_str(c, name, key);
            name_store(c, dictionary.get(c), key.value(), number.get(c));
        }
        out.ok(c.builtin(CoreObject::None));
    };
    if (auto saved = name_lookup(c, dictionary.get(c), U"_ast_state")) {
        auto anchor = roots.root(*saved);
        if (c.has_layout<NativeFunctionObject>(anchor.get(c)) &&
            c.payload<NativeFunctionObject>(anchor).entry == state_anchor) {
            classes =
                c.payload<TupleObject>(c.payload<NativeFunctionObject>(anchor).captures[0]).items;
            publish();
            return;
        }
    }
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto tuple = [&](std::span<const ast::FieldInfo> fields) {
        std::vector<Value> names;
        for (const auto &field : fields) {
            make_str(c, std::u32string(field.name), value);
            names.push_back(roots.root(value.value()).get(c));
        }
        cxxpython::core::make_tuple(c, names, value);
        return roots.root(value.value());
    };
    for (const auto &info : ast::node_info) {
        std::vector<Handle> bases;
        if (info.kind != ast::Kind::AST)
            bases.push_back(roots.root(classes[static_cast<std::size_t>(info.base)]));
        NativeTypeSpec spec{{std::string(info.name), bases}, LayoutDescriptor::of<AstObject>()};
        spec.type.module = U"ast";
        spec.type.slots = info.kind == ast::Kind::AST
                              ? std::vector<std::u32string>{U"__dict__", U"__weakref__"}
                              : std::vector<std::u32string>{};
        create_native_type(c, spec, value);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return;
        }
        auto type = roots.root(value.value());
        classes.push_back(type.get(c));
        auto namespace_ = roots.root(*TypeView(c, type).dictionary());
        auto store = [&](std::u32string name, Handle item) {
            make_str(c, std::move(name), key);
            name_store(c, namespace_.get(c), key.value(), item.get(c));
        };
        auto fields = tuple(info.fields), attributes = tuple(info.attributes);
        store(U"_fields", fields);
        store(U"__match_args__", fields);
        store(U"_attributes", attributes);
        if (!info.documentation.empty()) {
            make_str(c, text(info.documentation), value);
            store(U"__doc__", roots.root(value.value()));
        }
        for (auto group : {info.fields, info.attributes})
            for (const auto &field : group)
                if (field.optional)
                    store(std::u32string(field.name), roots.root(c.builtin(CoreObject::None)));
    }
    cxxpython::core::make_tuple(c, classes, value);
    auto types = roots.root(value.value());
    auto base = roots.root(classes[0]);
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install_native_attribute(c, core, base, U"__new__", variadic, ast_new, {types.get(c)});
    auto base_dictionary = roots.root(*TypeView(c, base).dictionary());
    auto constructor = roots.root(*name_lookup(c, base_dictionary.get(c), U"__new__"));
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            value, constructor.get(c));
    auto wrapper = roots.root(value.value());
    make_str(c, U"__new__", key);
    name_store(c, base_dictionary.get(c), key.value(), wrapper.get(c));
    install_native_attribute(c, core, base, U"__init__", variadic, ast_init);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, base, U"__reduce__", unary, ast_reduce);
    std::vector<Value> operators;
    for (const auto &info : ast::node_info) {
        if (singleton(info.kind))
            c.allocate_instance<AstObject>(roots.root(classes[static_cast<std::size_t>(info.kind)]),
                                           value, types.get(c));
        else
            value.ok(c.builtin(CoreObject::None));
        operators.push_back(roots.root(value.value()).get(c));
    }
    cxxpython::core::make_tuple(c, operators, value);
    auto markers = roots.root(value.value());
    const std::array captures{types.get(c), markers.get(c)};
    make_native_function(c, "_ast_state", {}, state_anchor, captures, value);
    auto anchor = roots.root(value.value());
    make_str(c, U"_ast_state", key);
    name_store(c, dictionary.get(c), key.value(), anchor.get(c));
    publish();
}

bool is_ast(Attachment &c, Value value)
{
    return c.has_layout<AstObject>(value);
}
void materialize_ast(Attachment &c, const ast::Node &node, const SourceBuffer &source,
                     ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot module(c);
    import_module(c, U"_ast", module);
    if (module.tag() == ResultTag::Raised) {
        out.raised(module.exception());
        return;
    }
    auto dictionary = roots.root(module_dict(c, module.value()));
    const auto state = name_lookup(c, dictionary.get(c), U"_ast_state");
    if (!state || !c.has_layout<NativeFunctionObject>(*state) ||
        c.payload<NativeFunctionObject>(*state).entry != state_anchor) {
        raise_error(c, CoreObject::RuntimeErrorType, U"native AST module state is unavailable",
                    out);
        return;
    }
    auto owner = roots.root(*state);
    const auto &captures = c.payload<NativeFunctionObject>(owner).captures;
    auto types = roots.root(captures[0]), markers = roots.root(captures[1]);
    try {
        Materializer(c, source, types, markers, out).node(node, out);
    } catch (const Raised &) {
    }
}
} // namespace cxxpython::core::detail
