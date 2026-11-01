// Public iterator semantics adapted from CPython 3.12.14 Python/Python-tokenize.c.
#include "internal/tokenizemodule.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/parser.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"

namespace cxxpython::core::detail {
namespace {
struct TokenizerIterator {
    ObjectHeader header;
    const Value reader;
    const std::optional<Value> encoding;
    const Value strict, replace;
    const bool extra;
    AtomicGcEdge line;
    std::size_t line_revision = 0;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    syntax::TokenStream stream;
    TokenizerIterator(InitialType head, Value callback, std::optional<Value> codec,
                      Value encode_errors, Value decode_errors, bool extended)
        : header(head), reader(callback), encoding(codec), strict(encode_errors),
          replace(decode_errors), extra(extended), stream(extended)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(reader);
        if (encoding)
            tracer.visit(*encoding);
        tracer.visit(strict);
        tracer.visit(replace);
        tracer.visit(line);
    }
    std::size_t external_bytes() const noexcept
    {
        return stream.external_bytes();
    }
};
struct Raised {};
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void propagate(ReturnSlot &from, ReturnSlot &out)
{
    if (failed(from)) {
        out.raised(from.exception());
        throw Raised{};
    }
}
void attribute(Attachment &c, Handle owner, const char32_t *name, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot key(c);
    make_str(c, name, key);
    get_attribute(c, owner, roots.root(key.value()), out);
}
void syntax_error(Attachment &c, const syntax::LexicalDiagnostic &error, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot value(c), arguments(c), result(c);
    std::vector<Value> location;
    auto text = [&](std::u32string s) {
        make_str(c, std::move(s), value);
        location.push_back(roots.root(value.value()).get(c));
    };
    auto number = [&](int n) {
        if (n < 0)
            value.ok(c.builtin(CoreObject::None));
        else
            make_int(c, n, value);
        location.push_back(roots.root(value.value()).get(c));
    };
    text(U"<string>");
    number(error.lineno);
    number(error.offset);
    if (error.has_text)
        text(error.line);
    else
        location.push_back(c.builtin(CoreObject::None));
    number(error.end_lineno);
    number(error.end_offset);
    cxxpython::core::make_tuple(c, location, value);
    auto details = roots.root(value.value());
    make_str(c, std::u32string(error.message.begin(), error.message.end()), value);
    auto message = roots.root(value.value());
    const auto kind = error.kind == syntax::ErrorKind::Tab ? CoreObject::TabErrorType
                      : error.kind == syntax::ErrorKind::Indentation
                          ? CoreObject::IndentationErrorType
                          : CoreObject::SyntaxErrorType;
    const std::array args{message.get(c), details.get(c)};
    call(c, roots.root(c.builtin(kind)), {args, {}}, result);
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    auto exception = roots.root(result.value());
    if (!error.location_in_args) {
        const std::array only_message{message.get(c)};
        cxxpython::core::make_tuple(c, only_message, arguments);
        c.store(exception.get(c), c.payload<ExceptionObject>(exception).args, arguments.value());
    }
    out.raised(exception.get(c));
}
void position(Attachment &c, int row, int column, ReturnSlot &out)
{
    ReturnSlot first(c), second(c);
    make_int(c, row, first);
    make_int(c, column, second);
    const std::array pair{first.value(), second.value()};
    cxxpython::core::make_tuple(c, pair, out);
}
void tokenizer_new(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto cls = roots.root(args[0]);
    const auto &positional = c.payload<TupleObject>(args[1]).items;
    if (cls.get(c) != c.payload<NativeFunctionObject>(function).captures[0]) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid tokenizer type", out);
        return;
    }
    if (positional.size() != 1) {
        const auto count = std::to_string(positional.size());
        raise_error(c, CoreObject::TypeErrorType,
                    U"tokenizeriter() takes exactly 1 positional argument (" +
                        std::u32string(count.begin(), count.end()) + U" given)",
                    out);
        return;
    }
    auto reader = roots.root(positional[0]);
    std::optional<Handle> extra, encoding;
    DictView keywords(c, args[2]);
    for (const auto &entry : keywords.entries()) {
        const auto &name = c.payload<StrObject>(entry.key).value;
        if (name == U"extra_tokens")
            extra = roots.root(entry.value);
        else if (name == U"encoding")
            encoding = roots.root(entry.value);
        else {
            raise_error(c, CoreObject::TypeErrorType,
                        U"'" + name + U"' is an invalid keyword argument for tokenizeriter()", out);
            return;
        }
    }
    if (!extra) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"tokenizeriter() missing required argument 'extra_tokens' (pos 2)", out);
        return;
    }
    ReturnSlot flag(c);
    truth(c, *extra, flag);
    if (failed(flag)) {
        out.raised(flag.exception());
        return;
    }
    if (encoding && !c.has_layout<StrObject>(encoding->get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"encoding must be str", out);
        return;
    }
    ReturnSlot errors(c);
    make_str(c, U"strict", errors);
    auto strict = roots.root(errors.value());
    make_str(c, U"replace", errors);
    auto replace = roots.root(errors.value());
    c.allocate_instance<TokenizerIterator>(
        cls, out, reader.get(c), encoding ? std::optional<Value>{encoding->get(c)} : std::nullopt,
        strict.get(c), replace.get(c), flag.value() == c.builtin(CoreObject::True));
}
void iterator_self(Attachment &, Value, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void iterator_next(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &iterator = c.payload<TokenizerIterator>(self);
    if (iterator.active.test_and_set(std::memory_order_acquire)) {
        raise_error(c, CoreObject::ValueErrorType, U"tokenizer already executing", out);
        return;
    }
    struct Active {
        std::atomic_flag &flag;
        ~Active()
        {
            flag.clear(std::memory_order_release);
        }
    } active{iterator.active};
    auto reader = roots.root(iterator.reader);
    ReturnSlot read(c), text(c), bytes(c), temporary(c);
    auto strict = roots.root(iterator.strict), replace = roots.root(iterator.replace);
    std::optional<Handle> encoding;
    if (iterator.encoding)
        encoding = roots.root(*iterator.encoding);
    try {
        const auto line = [&]() -> std::string {
            RootScope callback(c);
            call(c, reader, {}, read);
            if (failed(read) && is_subtype(c, c.type_of(read.exception()),
                                           c.builtin(CoreObject::StopIterationType)))
                return {};
            propagate(read, out);
            auto input = callback.root(read.value());
            if (iterator.encoding) {
                if (!is_bytes(c, input.get(c))) {
                    raise_error(c, CoreObject::TypeErrorType,
                                U"readline() returned a non-bytes object", out);
                    throw Raised{};
                }
                decode_bytes(c, input, *encoding, replace, text);
                propagate(text, out);
            } else if (!c.has_layout<StrObject>(input.get(c))) {
                raise_error(c, CoreObject::TypeErrorType,
                            U"readline() returned a non-string object", out);
                throw Raised{};
            } else
                text.ok(input.get(c));
            encode_primitive(c, callback.root(text.value()), TextEncoding::Utf8, strict, bytes);
            propagate(bytes, out);
            const auto encoded = c.payload<TupleObject>(bytes.value()).items[0];
            return c.payload<BytesObject>(encoded).value;
        };
        const auto warning = [&](const syntax::LexicalDiagnostic &diagnostic) {
            ReturnSlot module(c), method(c), message(c), filename(c), lineno(c);
            import_module(c, U"warnings", module);
            propagate(module, out);
            attribute(c, roots.root(module.value()), U"warn_explicit", method);
            propagate(method, out);
            make_str(c, std::u32string(diagnostic.message.begin(), diagnostic.message.end()),
                     message);
            make_str(c, U"<string>", filename);
            make_int(c, diagnostic.lineno, lineno);
            const std::array values{message.value(), c.builtin(CoreObject::SyntaxWarningType),
                                    filename.value(), lineno.value()};
            call(c, roots.root(method.value()), {values, {}}, temporary);
            if (failed(temporary) && is_subtype(c, c.type_of(temporary.exception()),
                                                c.builtin(CoreObject::SyntaxWarningType)))
                throw syntax::Error(diagnostic.span, diagnostic.message);
            propagate(temporary, out);
        };
        auto token = iterator.stream.next(
            line, [&] { c.poll(); }, warning);
        c.account_external(self, iterator.external_bytes());
        if (!token) {
            raise_error(c, CoreObject::StopIterationType, U"EOF", out);
            return;
        }
        auto kind = token->token.kind;
        if (iterator.extra) {
            if (token->trailing) {
                token->lineno = token->end_lineno = token->lineno + 1;
                token->column = token->end_column = 0;
            }
            if (kind > syntax::TokenKind::DEDENT && kind < syntax::TokenKind::OP)
                kind = syntax::TokenKind::OP;
            else if (kind == syntax::TokenKind::ASYNC || kind == syntax::TokenKind::AWAIT)
                kind = syntax::TokenKind::NAME;
            else if (kind == syntax::TokenKind::NEWLINE) {
                if (!token->implicit_newline)
                    token->token.text = !token->token.text.empty() && token->token.text[0] == U'\r'
                                            ? U"\r\n"
                                            : U"\n";
                ++token->end_column;
            } else if (kind == syntax::TokenKind::NL && token->implicit_newline)
                token->token.text.clear();
        }
        std::vector<Value> fields;
        auto keep = [&] { fields.push_back(roots.root(temporary.value()).get(c)); };
        make_int(c, static_cast<int>(kind), temporary);
        keep();
        make_str(c, std::move(token->token.text), temporary);
        keep();
        position(c, token->lineno, token->column, temporary);
        keep();
        position(c, token->end_lineno, token->end_column, temporary);
        keep();
        if (iterator.line_revision != token->line_revision) {
            make_str(c, *token->line, temporary);
            c.store(self.get(c), iterator.line, temporary.value());
            iterator.line_revision = token->line_revision;
        } else
            c.load(self.get(c), iterator.line, temporary);
        keep();
        cxxpython::core::make_tuple(c, fields, out);
    } catch (const Raised &) {
        c.account_external(self, iterator.external_bytes());
    } catch (const syntax::Error &) {
        c.account_external(self, iterator.external_bytes());
        syntax_error(c, iterator.stream.diagnostic(), out);
    } catch (...) {
        c.account_external(self, iterator.external_bytes());
        throw;
    }
}
} // namespace

void initialize_tokenize(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"TokenizerIter"}, LayoutDescriptor::of<TokenizerIterator>()};
    spec.type.module = U"_tokenize";
    spec.type.slots = std::vector<std::u32string>{};
    spec.type.immutable = true;
    spec.type.subclassable = false;
    ReturnSlot value(c), key(c);
    create_native_type(c, spec, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto type = roots.root(value.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, type, U"__iter__", unary, iterator_self);
    install_native_attribute(c, core, type, U"__next__", unary, iterator_next);
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}};
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", constructor, tokenizer_new, {type.get(c)});
    auto dict = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dict, U"__new__"));
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            value, function.get(c));
    make_str(c, U"__new__", key);
    name_store(c, dict, key.value(), value.value());
    make_str(c, U"TokenizerIter", key);
    dict_store(c, roots.root(module_dict(c, module.get(c))), roots.root(key.value()), type, out);
}
} // namespace cxxpython::core::detail
