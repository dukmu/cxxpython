#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "internal/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unordered_set>

namespace {
using namespace cxxpython::core;
int report(Attachment &context, Value exception)
{
    RootScope roots(context);
    const auto error = roots.root(exception);
    ReturnSlot text(context), trace(context), link(context);
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
} // namespace

int main(int argc, char **argv)
{
    using namespace cxxpython::core;
    try {
        std::optional<std::string> command, filename, expression;
        std::vector<std::string> arguments;
        bool trailing = false;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (trailing)
                arguments.push_back(option);
            else if (option == "--" && (command || filename))
                trailing = true;
            else if (((option == "-c" && !command && !filename) || option == "--eval") &&
                     i + 1 < argc) {
                auto &target = option == "-c" ? command : expression;
                if (target)
                    throw std::invalid_argument("duplicate execution option");
                target = argv[++i];
            } else if (!option.starts_with('-') && !filename && !command)
                filename = option;
            else if (command || filename) {
                trailing = true;
                arguments.push_back(option);
            } else
                throw std::invalid_argument(
                    "usage: cxxpython [-c SOURCE | FILE] [--eval EXPRESSION]");
        }
        if ((command && filename) || (!command && !filename && !expression))
            throw std::invalid_argument("usage: cxxpython [-c SOURCE | FILE] [--eval EXPRESSION]");
        std::optional<SourceUnit> executable, evaluated;
        if (filename) {
            std::ifstream file(*filename, std::ios::binary);
            if (!file) {
                std::cerr << "cannot open " << *filename << '\n';
                return 2;
            }
            executable.emplace(
                SourceBuffer(std::string(std::istreambuf_iterator<char>(file), {}), *filename));
        } else if (command)
            executable.emplace(SourceBuffer(*command, "<string>"));
        if (expression)
            evaluated.emplace(SourceBuffer(*expression, "<eval>"), InputMode::Eval);
        const auto script =
            filename ? std::optional{std::filesystem::absolute(*filename)} : std::nullopt;
        const auto search_path =
            filename ? std::filesystem::canonical(*filename).parent_path().string() : std::string{};
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap_core();
        RootScope roots(context);
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        if (out.tag() == ResultTag::Raised)
            return report(context, out.exception());
        const auto main_module = roots.root(out.value());
        const auto globals = roots.root(module_dict(context, main_module.get(context)));
        const std::array paths{search_path};
        set_import_path(context, paths, out);
        if (out.tag() == ResultTag::Raised)
            return report(context, out.exception());
        import_module(context, U"sys", out);
        if (out.tag() == ResultTag::Raised)
            return report(context, out.exception());
        const auto sys = roots.root(out.value());
        arguments.insert(arguments.begin(), filename ? *filename : command ? "-c" : "");
        std::vector<Value> argv_values;
        for (const auto &argument : arguments) {
            make_str(context, detail::decode_filename(SourceBuffer("", argument)), out);
            argv_values.push_back(roots.root(out.value()).get(context));
        }
        make_list(context, argv_values, out);
        const auto argv_list = roots.root(out.value());
        make_str(context, U"argv", out);
        set_attribute(context, sys, roots.root(out.value()), argv_list, out);
        if (out.tag() == ResultTag::Raised)
            return report(context, out.exception());
        if (script) {
            ReturnSlot key(context), value(context);
            make_str(context, U"__file__", key);
            make_str(context, script->u32string(), value);
            set_attribute(context, main_module, roots.root(key.value()), roots.root(value.value()),
                          out);
            if (out.tag() == ResultTag::Raised)
                return report(context, out.exception());
        }
        if (executable) {
            run(context, {&*executable, globals}, out);
            if (out.result().tag() == ResultTag::Raised)
                return report(context, out.result().exception());
        }
        if (evaluated) {
            run(context, {&*evaluated, globals}, out);
            if (out.result().tag() == ResultTag::Raised)
                return report(context, out.result().exception());
            repr(context, roots.root(out.result().value()), out);
            if (out.result().tag() == ResultTag::Raised)
                return report(context, out.result().exception());
            const auto text = source_utf8(context.payload<StrObject>(out.result().value()).value);
            NativeWait wait(context);
            std::cout << text << '\n';
        }
        return 0;
    } catch (const SourceError &error) {
        const auto kind = error.kind == SourceErrorKind::Indentation  ? "IndentationError"
                          : error.kind == SourceErrorKind::Tab        ? "TabError"
                          : error.kind == SourceErrorKind::Capability ? "UnsupportedSyntax"
                                                                      : "SyntaxError";
        std::cerr << "  File \"" << error.filename << "\", line " << error.position.line << '\n'
                  << error.source_line;
        if (!error.source_line.empty() && error.source_line.back() != '\n')
            std::cerr << '\n';
        std::cerr << std::string(error.offset - 1, ' ') << "^\n"
                  << kind << ": " << error.what() << '\n';
        return 1;
    } catch (const CodeValidationError &error) {
        std::cerr << "internal bytecode validation error: " << error.what() << '\n';
        return 70;
    } catch (const std::bad_alloc &) {
        std::cerr << "MemoryError\n";
        return 1;
    } catch (const std::invalid_argument &error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "internal error: " << error.what() << '\n';
        return 70;
    }
}
