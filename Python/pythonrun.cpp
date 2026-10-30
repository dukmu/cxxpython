#include "internal/pythonrun.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/parser.hpp"

#include <iostream>
#include <unordered_set>

namespace cxxpython::core::detail {
int report_exception(Attachment &context, Value exception)
{
    RootScope roots(context);
    const auto error = roots.root(exception);
    ReturnSlot text(context), trace(context), link(context);
    if (is_subtype(context, context.type_of(error), context.builtin(CoreObject::SystemExitType))) {
        make_str(context, U"code", text);
        get_attribute(context, error, roots.root(text.value()), link);
        auto code = roots.root(link.tag() == ResultTag::Ok ? link.value() : error.get(context));
        if (code.get(context) == context.builtin(CoreObject::None))
            return 0;
        if (context.has_layout<IntObject>(code.get(context))) {
            std::int64_t status;
            return int_fits_signed(context, code.get(context), status)
                       ? static_cast<unsigned char>(status)
                       : 255;
        }
        str(context, code, text);
        const auto message = text.tag() == ResultTag::Ok
                                 ? source_utf8(context.payload<StrObject>(text.value()).value)
                                 : "<str() failed>";
        NativeWait wait(context);
        std::cerr << message << '\n';
        return 1;
    }
    enum class Chain { Context, Cause };
    std::vector<std::pair<Handle, Chain>> chain;
    std::unordered_set<ObjectHeader *> seen;
    auto current = error;
    while (seen.insert(current.get(context).object()).second) {
        Chain relation = Chain::Context;
        const auto &data = context.payload<ExceptionObject>(current);
        context.load(current.get(context), data.cause, link);
        if (link.tag() == ResultTag::Ok)
            relation = Chain::Cause;
        else if (!data.suppress_context.load(std::memory_order_acquire))
            context.load(current.get(context), data.context, link);
        chain.push_back({current, relation});
        if (link.tag() == ResultTag::Empty)
            break;
        current = roots.root(link.value());
    }
    std::string output;
    for (auto i = chain.size(); i-- > 0;) {
        const auto [owner, relation] = chain[i];
        if (i + 1 < chain.size())
            output +=
                relation == Chain::Cause
                    ? "\nThe above exception was the direct cause of the following exception:\n\n"
                    : "\nDuring handling of the above exception, another exception occurred:\n\n";
        context.load(owner.get(context), context.payload<ExceptionObject>(owner).traceback, trace);
        if (trace.tag() == ResultTag::Ok)
            output += "Traceback (most recent call last):\n";
        while (trace.tag() == ResultTag::Ok) {
            const auto value = trace.value();
            const auto &node = context.payload<TracebackObject>(value);
            const auto &frame = context.payload<FrameObject>(node.frame);
            const auto &code = context.payload<CodeObject>(frame.code);
            output += "  File \"" + source_utf8(context.payload<StrObject>(code.filename).value) +
                      "\", line " + std::to_string(node.lineno) + ", in " +
                      source_utf8(context.payload<StrObject>(code.name).value) + "\n";
            context.load(value, node.next, trace);
        }
        const auto name = TypeView(context, context.type_of(owner)).name();
        output += source_utf8(context.payload<StrObject>(std::get<Value>(name)).value);
        str(context, owner, text);
        const auto message = text.tag() == ResultTag::Ok
                                 ? source_utf8(context.payload<StrObject>(text.value()).value)
                                 : "<exception str() failed>";
        if (!message.empty())
            output += ": " + message;
        output += '\n';
    }
    NativeWait wait(context);
    std::cerr << output;
    return 1;
}

int report_source_error(const SourceError &error)
{
    const auto kind = error.kind == SourceErrorKind::Indentation  ? "IndentationError"
                      : error.kind == SourceErrorKind::Tab        ? "TabError"
                      : error.kind == SourceErrorKind::Capability ? "UnsupportedSyntax"
                                                                  : "SyntaxError";
    std::cerr << "  File \"" << error.filename << "\", line " << error.position.line << '\n'
              << error.source_line;
    if (!error.source_line.empty() && error.source_line.back() != '\n')
        std::cerr << '\n';
    std::cerr << std::string(error.offset ? error.offset - 1 : 0, ' ') << "^\n"
              << kind << ": " << error.what() << '\n';
    return 1;
}

int run_interactive(Attachment &context, Handle globals, Handle sys, bool prompts, bool banner)
{
    RootScope roots(context);
    globals = roots.root(globals.get(context));
    sys = roots.root(sys.get(context));
    ReturnSlot key(context), value(context), result(context);
    for (auto [name, text] : {std::pair{U"ps1", U">>> "}, std::pair{U"ps2", U"... "}}) {
        make_str(context, name, key);
        get_attribute(context, sys, roots.root(key.value()), value);
        if (value.tag() == ResultTag::Raised &&
            is_subtype(context, context.type_of(value.exception()),
                       context.builtin(CoreObject::AttributeErrorType))) {
            make_str(context, text, value);
            set_attribute(context, sys, roots.root(key.value()), roots.root(value.value()), result);
            if (result.tag() == ResultTag::Raised)
                return report_exception(context, result.exception());
        } else if (value.tag() == ResultTag::Raised)
            return report_exception(context, value.exception());
    }
    if (banner) {
        NativeWait wait(context);
        std::cerr << "cxxpython (experimental Python 3.12)\n";
    }
    std::string pending;
    for (;;) {
        RootScope iteration(context);
        if (prompts) {
            make_str(context, pending.empty() ? U"ps1" : U"ps2", key);
            get_attribute(context, sys, iteration.root(key.value()), value);
            if (value.tag() == ResultTag::Ok)
                str(context, iteration.root(value.value()), result);
            if (value.tag() == ResultTag::Raised || result.tag() == ResultTag::Raised)
                report_exception(context, value.tag() == ResultTag::Raised ? value.exception()
                                                                           : result.exception());
            else {
                const auto prompt = source_utf8(context.payload<StrObject>(result.value()).value);
                NativeWait wait(context);
                std::cerr << prompt << std::flush;
            }
        }
        std::string line;
        bool read;
        {
            NativeWait wait(context);
            read = static_cast<bool>(std::getline(std::cin, line));
        }
        if (!read && pending.empty())
            return 0;
        if (read)
            pending += line + '\n';
        if (read) {
            try {
                syntax::parse(pending, syntax::InputMode::Interactive, true);
            } catch (const syntax::Error &error) {
                if (error.kind == syntax::ErrorKind::Incomplete)
                    continue;
                // The compiler supplies the ordinary source-position diagnostic.
            }
        }
        try {
            SourceUnit source(SourceBuffer(pending, "<stdin>"), InputMode::Single);
            run(context, {&source, globals}, result);
            if (result.tag() == ResultTag::Raised) {
                auto error = iteration.root(result.exception());
                const bool exiting = is_subtype(context, context.type_of(error),
                                                context.builtin(CoreObject::SystemExitType));
                const auto status = report_exception(context, error.get(context));
                if (exiting)
                    return status;
            }
        } catch (const SourceError &error) {
            report_source_error(error);
        }
        pending.clear();
        if (!read)
            return 0;
    }
}
} // namespace cxxpython::core::detail
