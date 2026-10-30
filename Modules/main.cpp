#include "internal/main.hpp"
#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "internal/atexitmodule.hpp"
#include "internal/compiler.hpp"
#include "internal/iomodule.hpp"
#include "internal/pythonrun.hpp"
#include "internal/stdio.hpp"
#include "internal/sysmodule.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cxxpython::core::detail {
namespace {
enum class Mode { Stdin, Command, Module, File };
struct Options {
    Mode mode = Mode::Stdin;
    std::string target;
    std::optional<std::string> expression;
    std::vector<std::string> arguments;
    bool interactive = false;
    bool quiet = false;
    bool explicit_stdin = false;
};

bool stdin_is_tty()
{
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

void usage(std::ostream &out)
{
    out << "usage: cxxpython [-i] [-q] [-c SOURCE | -m MODULE | FILE | -] [args ...]\n"
           "       cxxpython [-h | --help | -V | --version]\n";
}

// --eval is a legacy test diagnostic, including after an execution target.
int parse(int argc, char **argv, Options &options)
{
    bool selected = false, literal = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!literal && arg == "--") {
            literal = true;
            continue;
        }
        if (!literal && arg == "--eval") {
            if (options.expression || ++i == argc) {
                std::cerr << "cxxpython: --eval requires one expression\n";
                return 2;
            }
            options.expression = argv[i];
            continue;
        }
        if (selected) {
            options.arguments.push_back(arg);
            continue;
        }
        if (!literal && (arg == "-h" || arg == "--help")) {
            usage(std::cout);
            return -1;
        }
        if (!literal && (arg == "-V" || arg == "--version")) {
            std::cout << "cxxpython (experimental Python 3.12)\n";
            return -1;
        }
        if (!literal && arg == "-i") {
            options.interactive = true;
            continue;
        }
        if (!literal && arg == "-q") {
            options.quiet = true;
            continue;
        }
        if (!literal && (arg == "-c" || arg == "-m")) {
            if (++i == argc) {
                std::cerr << "cxxpython: " << arg << " requires an argument\n";
                return 2;
            }
            options.mode = arg == "-c" ? Mode::Command : Mode::Module;
            options.target = argv[i];
            selected = true;
            continue;
        }
        if (arg == "-" && !literal) {
            options.explicit_stdin = true;
            selected = true;
            continue;
        }
        if (!literal && arg.starts_with('-')) {
            std::cerr << "cxxpython: unknown option: " << arg << '\n';
            usage(std::cerr);
            return 2;
        }
        options.mode = Mode::File;
        options.target = arg;
        selected = true;
    }
    return 0;
}

std::string read_stream(std::istream &stream)
{
    std::string text(std::istreambuf_iterator<char>(stream), {});
    if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
    return text;
}
} // namespace

int pymain(int argc, char **argv)
{
    try {
        Options options;
        const int parsed = parse(argc, argv, options);
        if (parsed != 0)
            return parsed < 0 ? 0 : parsed;
        const bool tty = stdin_is_tty();
        const bool repl_only = options.mode == Mode::Stdin && !options.explicit_stdin &&
                               (tty || options.interactive) && !options.expression;
        Interpreter interpreter;
        Attachment context(interpreter);
        context.bootstrap_core();
        RootScope roots(context);
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        if (out.tag() == ResultTag::Raised)
            return report_exception(context, out.exception());
        const auto main_module = roots.root(out.value());
        const auto globals = roots.root(module_dict(context, main_module.get(context)));
        std::string path;
        if (options.mode == Mode::File) {
            std::error_code error;
            auto absolute = std::filesystem::canonical(options.target, error);
            if (!error)
                path = absolute.parent_path().string();
        } else if (options.mode == Mode::Module) {
            std::error_code error;
            path = std::filesystem::current_path(error).string();
        }
        import_module(context, U"sys", out);
        if (out.tag() == ResultTag::Raised)
            return report_exception(context, out.exception());
        const auto sys = roots.root(out.value());
        {
            ReturnSlot key(context), paths(context), first(context);
            make_str(context, U"path", key);
            get_attribute(context, sys, roots.root(key.value()), paths);
            if (paths.tag() == ResultTag::Raised)
                return report_exception(context, paths.exception());
            make_str(context, decode_filename(SourceBuffer("", path)), first);
            set_item(context, roots.root(paths.value()),
                     roots.root(context.builtin(CoreObject::IntZero)), roots.root(first.value()),
                     out);
            if (out.tag() == ResultTag::Raised)
                return report_exception(context, out.exception());
        }
        {
            std::error_code error;
            auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
            if (error)
                executable = std::filesystem::absolute(argv[0], error);
            ReturnSlot key(context), value(context);
            make_str(context, U"executable", key);
            make_str(context, decode_filename(SourceBuffer("", executable.string())), value);
            set_attribute(context, sys, roots.root(key.value()), roots.root(value.value()), out);
            if (out.tag() == ResultTag::Raised)
                return report_exception(context, out.exception());
        }
        const std::string argv0 = options.mode == Mode::File      ? options.target
                                  : options.mode == Mode::Command ? "-c"
                                  : options.mode == Mode::Module  ? "-m"
                                  : options.explicit_stdin        ? "-"
                                                                  : "";
        options.arguments.insert(options.arguments.begin(), argv0);
        std::vector<Value> argv_values;
        for (const auto &argument : options.arguments) {
            make_str(context, decode_filename(SourceBuffer("", argument)), out);
            argv_values.push_back(roots.root(out.value()).get(context));
        }
        make_list(context, argv_values, out);
        const auto argv_list = roots.root(out.value());
        make_str(context, U"argv", out);
        set_attribute(context, sys, roots.root(out.value()), argv_list, out);
        if (out.tag() == ResultTag::Raised)
            return report_exception(context, out.exception());
        if (options.mode == Mode::File) {
            ReturnSlot key(context), value(context);
            make_str(context, U"__file__", key);
            make_str(context,
                     decode_filename(
                         SourceBuffer("", std::filesystem::absolute(options.target).string())),
                     value);
            set_attribute(context, main_module, roots.root(key.value()), roots.root(value.value()),
                          out);
            if (out.tag() == ResultTag::Raised)
                return report_exception(context, out.exception());
        }
        {
            ReturnSlot key(context);
            for (const auto name : {U"__spec__", U"__package__", U"__cached__"}) {
                make_str(context, name, key);
                set_attribute(context, main_module, roots.root(key.value()),
                              roots.root(context.builtin(CoreObject::None)), out);
                if (out.tag() == ResultTag::Raised)
                    return report_exception(context, out.exception());
            }
        }

        int status = 0;
        try {
            if (options.mode == Mode::Module) {
                import_module(context, U"runpy", out);
                if (out.tag() != ResultTag::Raised) {
                    const auto runpy = roots.root(out.value());
                    make_str(context, U"_run_module_as_main", out);
                    get_attribute(context, runpy, roots.root(out.value()), out);
                    if (out.tag() != ResultTag::Raised) {
                        const auto function = roots.root(out.value());
                        make_str(context, decode_filename(SourceBuffer("", options.target)), out);
                        const auto name = roots.root(out.value());
                        const std::array args{name.get(context)};
                        call(context, function, {args, {}}, out);
                    }
                }
                if (out.tag() == ResultTag::Raised)
                    status = report_exception(context, out.exception());
            } else if (!repl_only && !(options.expression && options.mode == Mode::Stdin &&
                                       !options.explicit_stdin)) {
                std::string text;
                std::string filename;
                if (options.mode == Mode::Command) {
                    text = options.target;
                    filename = "<string>";
                } else if (options.mode == Mode::File) {
                    filename = options.target;
                    NativeWait wait(context);
                    std::ifstream file(filename, std::ios::binary);
                    if (!file) {
                        std::cerr << "cxxpython: cannot open file '" << filename << "'\n";
                        status = 2;
                    } else
                        text = read_stream(file);
                } else {
                    filename = "<stdin>";
                    ReturnSlot input(context);
                    get_sys_attribute(context, U"stdin", input);
                    if (input.tag() == ResultTag::Raised)
                        status = report_exception(context, input.exception());
                    else if (!io_call(context, roots.root(input.value()), U"read", {}, out))
                        status = report_exception(context, out.exception());
                    else if (context.has_layout<StrObject>(out.value()))
                        text = source_utf8(context.payload<StrObject>(out.value()).value);
                    else {
                        raise_error(context, CoreObject::TypeErrorType,
                                    U"stdin.read() returned non-string", out);
                        status = report_exception(context, out.exception());
                    }
                }
                if (!status) {
                    SourceUnit source(SourceBuffer(std::move(text), filename));
                    run(context, {&source, globals}, out);
                    if (out.tag() == ResultTag::Raised)
                        status = report_exception(context, out.exception());
                }
            }
            if (!status && options.expression) {
                SourceUnit source(SourceBuffer(*options.expression, "<eval>"), InputMode::Eval);
                run(context, {&source, globals}, out);
                if (out.tag() != ResultTag::Raised)
                    repr(context, roots.root(out.value()), out);
                if (out.tag() == ResultTag::Raised)
                    status = report_exception(context, out.exception());
                else {
                    ReturnSlot text(context), output(context);
                    make_str(context, context.payload<StrObject>(out.value()).value + U"\n", text);
                    const auto rendered = roots.root(text.value());
                    get_sys_attribute(context, U"stdout", output);
                    if (output.tag() == ResultTag::Raised)
                        status = report_exception(context, output.exception());
                    else {
                        const Value value = rendered.get(context);
                        if (!io_call(context, roots.root(output.value()), U"write", {&value, 1},
                                     out))
                            status = report_exception(context, out.exception());
                    }
                }
            }
        } catch (const SourceError &error) {
            status = report_source_error(error);
        }
        if (!repl_only && (options.mode == Mode::File || options.mode == Mode::Stdin))
            flush_stdio(context, StdioFlush::AfterExecution);
        if (repl_only || options.interactive)
            status = run_interactive(context, globals, sys, tty || options.interactive,
                                     tty && !options.quiet && repl_only);
        run_exitfuncs(context, out);
        if (out.tag() == ResultTag::Raised)
            return report_exception(context, out.exception());
        if (!flush_stdio(context))
            return 120;
        return status;
    } catch (const SourceError &error) {
        return report_source_error(error);
    } catch (const CodeValidationError &error) {
        std::cerr << "internal bytecode validation error: " << error.what() << '\n';
        return 70;
    } catch (const std::bad_alloc &) {
        std::cerr << "MemoryError\n";
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "internal error: " << error.what() << '\n';
        return 70;
    }
}
} // namespace cxxpython::core::detail
