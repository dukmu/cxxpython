// GNU readline integration; Python-level semantics follow CPython 3.12.14
// Modules/readline.c (PSF license). GNU readline has process-global state.
#include "internal/readlinemodule.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"
#include "internal/posixmodule.hpp"

#include <readline/history.h>
#include <readline/readline.h>

#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace cxxpython::core::detail {
namespace {
struct ReadlineState {
    ObjectHeader header;
    AtomicGcEdge completer, startup, pre_input;
    HISTORY_STATE history{};
    int history_base_value = 1;
    int history_max = 0;
    std::string delimiters = " \t\n\"\\'`@$><=;|&{(";
    int history_limit = -1;
    int begin = 0, end = 0;
    bool has_completion_indices = false;
    explicit ReadlineState(InitialType type, Value none)
        : header(type), completer(none), startup(none), pre_input(none)
    {}
    ~ReadlineState() noexcept
    {
        if (history.entries) {
            for (int i = 0; i < history.length; ++i)
                free_history_entry(history.entries[i]);
            std::free(history.entries);
        }
    }
    void trace(Tracer &t) const noexcept
    {
        t.visit(completer);
        t.visit(startup);
        t.visit(pre_input);
    }
};

struct GlobalLease {
    std::mutex mutex;
    std::condition_variable ready;
    std::thread::id owner;
    ReadlineState *state = nullptr;
    unsigned depth = 0;
    bool reading = false;
    bool initialized = false;
};
GlobalLease global;

class Lease {
    Attachment &c;
    ReadlineState &state;
    bool acquired = false;
    bool reader;
    HISTORY_STATE *host_history = nullptr;
    int host_base = 0, host_max = 0;
    decltype(rl_completer_word_break_characters) host_delimiters = nullptr;
    bool outer = false;

  public:
    Lease(Attachment &ctx, ReadlineState &s, bool input, ReturnSlot &out)
        : c(ctx), state(s), reader(input)
    {
        for (;;) {
            std::unique_lock lock(global.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                NativeWait wait(c);
                std::unique_lock parked(global.mutex);
                continue;
            }
            auto self = std::this_thread::get_id();
            if (global.depth && global.owner == self) {
                if (global.state != &state || (input && global.reading)) {
                    lock.unlock();
                    raise_error(c, CoreObject::RuntimeErrorType, U"recursive readline input", out);
                    return;
                }
                ++global.depth;
                if (input)
                    global.reading = true;
                acquired = true;
                return;
            }
            if (!global.depth) {
                global.owner = self;
                global.state = &state;
                global.depth = 1;
                global.reading = input;
                acquired = true;
                lock.unlock();
                if (!global.initialized) {
                    {
                        NativeWait wait(c);
                        rl_initialize();
                        rl_variable_bind("enable-bracketed-paste", "off");
                    }
                    global.initialized = true;
                }
                host_history = history_get_history_state();
                host_base = history_base;
                host_max = history_max_entries;
                host_delimiters = rl_completer_word_break_characters;
                if (history_is_stifled())
                    unstifle_history();
                history_set_history_state(&state.history);
                history_base = state.history_base_value;
                history_max_entries = state.history_max;
                rl_completer_word_break_characters = state.delimiters.data();
                outer = true;
                return;
            }
            lock.unlock();
            NativeWait wait(c);
            std::unique_lock parked(global.mutex);
            global.ready.wait(parked, [] { return global.depth == 0; });
        }
    }
    explicit operator bool() const
    {
        return acquired;
    }
    void finish()
    {
        if (!outer)
            return;
        HISTORY_STATE *snapshot = history_get_history_state();
        state.history = *snapshot;
        std::free(snapshot);
        state.history_base_value = history_base;
        state.history_max = history_max_entries;
        if (history_is_stifled())
            unstifle_history();
        history_set_history_state(host_history);
        std::free(host_history);
        host_history = nullptr;
        history_base = host_base;
        history_max_entries = host_max;
        rl_completer_word_break_characters = host_delimiters;
        outer = false;
    }
    ~Lease()
    {
        if (!acquired)
            return;
        for (;;) {
            std::unique_lock lock(global.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                if (reader)
                    global.reading = false;
                if (--global.depth == 0) {
                    global.owner = {};
                    global.state = nullptr;
                    global.ready.notify_all();
                }
                return;
            }
            NativeWait wait(c);
            std::unique_lock parked(global.mutex);
        }
    }
};

bool encode(Attachment &c, Value value, std::string &text, ReturnSlot &out)
{
    if (!c.has_layout<StrObject>(value)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected str", out);
        return false;
    }
    RootScope roots(c);
    auto input = roots.root(value);
    ReturnSlot errors(c), bytes(c);
    make_str(c, U"surrogateescape", errors);
    encode_primitive(c, input, TextEncoding::Utf8, roots.root(errors.value()), bytes);
    if (bytes.tag() == ResultTag::Raised) {
        out.raised(bytes.exception());
        return false;
    }
    const auto encoded = c.payload<TupleObject>(bytes.value()).items[0];
    text = c.payload<BytesObject>(encoded).value;
    if (text.find('\0') != std::string::npos) {
        raise_error(c, CoreObject::ValueErrorType, U"embedded null character", out);
        return false;
    }
    return true;
}
void decode(Attachment &c, const std::string &text, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot bytes(c), errors(c), decoded(c);
    make_bytes(c, text, bytes);
    make_str(c, U"surrogateescape", errors);
    decode_primitive(c, roots.root(bytes.value()), TextEncoding::Utf8, roots.root(errors.value()),
                     true, decoded);
    if (decoded.tag() == ResultTag::Raised)
        out.raised(decoded.exception());
    else
        out.ok(c.payload<TupleObject>(decoded.value()).items[0]);
}
bool number(Attachment &c, Value value, int &result, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot indexed(c);
    index(c, roots.root(value), indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return false;
    }
    std::int64_t n;
    if (!int_fits_signed(c, indexed.value(), n) || n < INT_MIN || n > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    result = static_cast<int>(n);
    return true;
}
Handle state_of(Attachment &c, Value function, RootScope &roots)
{
    return roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
}

enum class Op {
    Add,
    Get,
    Remove,
    Replace,
    Clear,
    CurrentLength,
    GetLength,
    SetLength,
    ReadFile,
    WriteFile,
    AppendFile,
    InitFile,
    Bind,
    SetCompleter,
    GetCompleter,
    SetDelims,
    GetDelims,
    LineBuffer,
    Begidx,
    Endidx,
    Redisplay,
    Insert,
    Startup,
    PreInput
};

template <Op op>
void method(Attachment &c, Value function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto owner = state_of(c, function, roots);
    auto &s = c.payload<ReadlineState>(owner);
    std::string text;
    ReturnSlot path(c);
    int n = 0;
    if constexpr (op == Op::Add || op == Op::Bind || op == Op::SetDelims || op == Op::Insert) {
        if (!encode(c, args[0], text, out))
            return;
    }
    if constexpr (op == Op::Get || op == Op::Remove || op == Op::Replace || op == Op::SetLength ||
                  op == Op::AppendFile) {
        if (!number(c, args[0], n, out))
            return;
    }
    if constexpr (op == Op::Replace) {
        if (!encode(c, args[1], text, out))
            return;
    }
    if constexpr (op == Op::AppendFile) {
        if (n < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"nelements must be positive", out);
            return;
        }
    }
    if constexpr (op == Op::ReadFile || op == Op::WriteFile || op == Op::InitFile ||
                  op == Op::AppendFile) {
        auto filename = args[op == Op::AppendFile ? 1 : 0];
        if (filename != c.builtin(CoreObject::None) &&
            !filesystem_path(c, filename, text, out, path))
            return;
    }
    Lease lease(c, s, false, out);
    if (!lease)
        return;
    try {
        [&] {
            if constexpr (op == Op::Add)
                add_history(text.c_str());
            if constexpr (op == Op::Clear)
                clear_history();
            if constexpr (op == Op::CurrentLength) {
                make_int(c, history_length, out);
                return;
            }
            if constexpr (op == Op::GetLength) {
                make_int(c, s.history_limit, out);
                return;
            }
            if constexpr (op == Op::SetLength)
                s.history_limit = n;
            if constexpr (op == Op::Get) {
                auto entry = history_get(n);
                if (entry)
                    decode(c, entry->line, out);
                else
                    out.ok(c.builtin(CoreObject::None));
                return;
            }
            if constexpr (op == Op::Remove || op == Op::Replace) {
                if (n < 0) {
                    raise_error(c, CoreObject::ValueErrorType, U"History index cannot be negative",
                                out);
                    return;
                }
                HIST_ENTRY *entry = op == Op::Remove
                                        ? remove_history(n)
                                        : replace_history_entry(n, text.c_str(), nullptr);
                if (!entry) {
                    raise_error(c, CoreObject::ValueErrorType, U"No history item at position", out);
                    return;
                }
                free_history_entry(entry);
            }
            if constexpr (op == Op::ReadFile || op == Op::WriteFile || op == Op::AppendFile ||
                          op == Op::InitFile) {
                int error;
                const auto filename = args[op == Op::AppendFile ? 1 : 0];
                const char *path = filename == c.builtin(CoreObject::None) ? nullptr : text.c_str();
                const int limit = s.history_limit;
                {
                    NativeWait wait(c);
                    if constexpr (op == Op::ReadFile)
                        error = read_history(path);
                    if constexpr (op == Op::WriteFile) {
                        error = write_history(path);
                    }
                    if constexpr (op == Op::AppendFile)
                        error = append_history(n, path);
                    if constexpr (op == Op::WriteFile || op == Op::AppendFile) {
                        if (!error && limit >= 0)
                            history_truncate_file(path, limit);
                    }
                    if constexpr (op == Op::InitFile)
                        error = rl_read_init_file(path);
                }
                if (error) {
                    raise_os_error(c, error, out);
                    return;
                }
                if constexpr (op == Op::InitFile)
                    rl_variable_bind("enable-bracketed-paste", "off");
            }
            if constexpr (op == Op::Bind) {
                NativeWait wait(c);
                rl_parse_and_bind(text.data());
            }
            if constexpr (op == Op::SetCompleter || op == Op::Startup || op == Op::PreInput) {
                if (args[0] != c.builtin(CoreObject::None) &&
                    !type_attribute(c, c.type_of(args[0]), U"__call__")) {
                    raise_error(c, CoreObject::TypeErrorType, U"argument not callable", out);
                    return;
                }
                auto &edge = op == Op::SetCompleter ? s.completer
                             : op == Op::Startup    ? s.startup
                                                    : s.pre_input;
                c.store(owner.get(c), edge, args[0]);
            }
            if constexpr (op == Op::GetCompleter) {
                c.load(owner.get(c), s.completer, out);
                return;
            }
            if constexpr (op == Op::SetDelims) {
                s.delimiters = text;
                rl_completer_word_break_characters = s.delimiters.data();
            }
            if constexpr (op == Op::GetDelims) {
                decode(c, s.delimiters, out);
                return;
            }
            if constexpr (op == Op::LineBuffer) {
                decode(c, rl_line_buffer ? rl_line_buffer : "", out);
                return;
            }
            if constexpr (op == Op::Begidx || op == Op::Endidx) {
                if (!s.has_completion_indices)
                    out.ok(c.builtin(CoreObject::None));
                else
                    make_int(c, op == Op::Begidx ? s.begin : s.end, out);
                return;
            }
            if constexpr (op == Op::Redisplay) {
                NativeWait wait(c);
                rl_redisplay();
            }
            if constexpr (op == Op::Insert)
                rl_insert_text(text.c_str());
            out.ok(c.builtin(CoreObject::None));
        }();
    } catch (...) {
        lease.finish();
        throw;
    }
    lease.finish();
}

struct Session {
    Attachment &context;
    Handle owner;
    ReadlineState &state;
    NativeWait *native_call = nullptr;
    std::unique_ptr<char, decltype(&std::free)> line{nullptr, &std::free};
    bool finished = false;
    bool eof = false;
    bool allocation_failure = false;
    bool native_failure = false;
    int input_error = 0;
    bool handler_installed = false;
};
#if defined(__ANDROID__) && (defined(__aarch64__) || defined(__arm__))
// Bionic's ARM executable TLS ABI reserves eight pointer words after the TP.
alignas(8 * sizeof(void *)) thread_local Session *session = nullptr;
#else
thread_local Session *session = nullptr;
#endif

void callback_failure() noexcept
{
    try {
        throw;
    } catch (const std::bad_alloc &) {
        session->allocation_failure = true;
    } catch (...) {
        session->native_failure = true;
    }
    session->finished = true;
}

int invoke_hook(AtomicGcEdge &edge)
{
    auto &run = *session;
    auto &c = run.context;
    RootScope roots(c);
    ReturnSlot callback(c), result(c);
    c.load(run.owner.get(c), edge, callback);
    if (callback.value() == c.builtin(CoreObject::None))
        return 0;
    auto fn = roots.root(callback.value());
    call(c, fn, {}, result);
    if (result.tag() == ResultTag::Raised || result.value() == c.builtin(CoreObject::None))
        return 0;
    int value = 0;
    ReturnSlot converted(c);
    // CPython's on_hook clears Python callback/conversion errors.
    return number(c, result.value(), value, converted) ? value : 0;
}
int startup_hook() noexcept
{
    try {
        NativeCallback active(*session->native_call);
        return invoke_hook(session->state.startup);
    } catch (...) {
        callback_failure();
    }
    return 0;
}
int pre_input_hook() noexcept
{
    try {
        NativeCallback active(*session->native_call);
        return invoke_hook(session->state.pre_input);
    } catch (...) {
        callback_failure();
    }
    return 0;
}
void line_ready(char *line) noexcept
{
    session->finished = true;
    session->eof = line == nullptr;
    session->line.reset(line);
    if (session->handler_installed) {
        session->handler_installed = false;
        rl_callback_handler_remove();
    }
}
char *completion(const char *text, int index_value) noexcept
{
    try {
        auto &run = *session;
        auto &c = run.context;
        RootScope roots(c);
        ReturnSlot callback(c), word(c), number_value(c), result(c);
        c.load(run.owner.get(c), run.state.completer, callback);
        if (callback.value() == c.builtin(CoreObject::None))
            return nullptr;
        auto fn = roots.root(callback.value());
        decode(c, text, word);
        if (word.tag() == ResultTag::Raised)
            return nullptr;
        make_int(c, index_value, number_value);
        Value args[] = {word.value(), number_value.value()};
        call(c, fn, {args, {}}, result);
        if (result.tag() == ResultTag::Raised || result.value() == c.builtin(CoreObject::None))
            return nullptr;
        std::string encoded;
        ReturnSlot error(c);
        if (!encode(c, result.value(), encoded, error))
            return nullptr;
        auto copy = ::strdup(encoded.c_str());
        if (!copy) {
            run.allocation_failure = true;
            run.finished = true;
        }
        return copy;
    } catch (...) {
        callback_failure();
        return nullptr;
    }
}
char **attempt_completion(const char *text, int begin, int end) noexcept
{
    try {
        auto &run = *session;
        auto &c = run.context;
        NativeCallback active(*run.native_call);
        ReturnSlot prefix(c), word(c);
        decode(c, std::string(rl_line_buffer, static_cast<std::size_t>(begin)), prefix);
        decode(c, std::string(rl_line_buffer + begin, static_cast<std::size_t>(end - begin)), word);
        if (prefix.tag() == ResultTag::Ok && word.tag() == ResultTag::Ok) {
            run.state.begin = static_cast<int>(c.payload<StrObject>(prefix.value()).value.size());
            run.state.end =
                run.state.begin + static_cast<int>(c.payload<StrObject>(word.value()).value.size());
            run.state.has_completion_indices = true;
        }
        rl_attempted_completion_over = 1;
        rl_completion_append_character = '\0';
        return rl_completion_matches(text, completion);
    } catch (...) {
        callback_failure();
        return nullptr;
    }
}

int terminal_getc(FILE *input) noexcept
{
    try {
        auto &run = *session;
        int ch;
        int error = 0;
        ch = std::getc(input);
        if (ch == EOF && std::ferror(input))
            error = errno ? errno : EIO;
        if (error) {
            run.input_error = error;
            run.finished = true;
        }
        return ch;
    } catch (...) {
        callback_failure();
        return EOF;
    }
}

class TerminalSession {
    FILE *input;
    FILE *output;
    FILE *old_in = rl_instream, *old_out = rl_outstream;
    rl_hook_func_t *old_startup = rl_startup_hook, *old_pre_input = rl_pre_input_hook;
    rl_completion_func_t *old_completion = rl_attempted_completion_function;
    rl_getc_func_t *old_getc = rl_getc_function;
    int old_signals = rl_catch_signals, old_winch = rl_catch_sigwinch;
    Session *old_session = session;
    Session &run;

  public:
    TerminalSession(FILE *in, FILE *out, Session &current) : input(in), output(out), run(current)
    {
        session = &run;
        rl_instream = input;
        rl_outstream = output;
        rl_startup_hook = startup_hook;
        rl_pre_input_hook = pre_input_hook;
        rl_attempted_completion_function = attempt_completion;
        rl_getc_function = terminal_getc;
        // GNU longjmp-based signal handling must not cross C++/GC scopes.
        rl_catch_signals = 0;
        rl_catch_sigwinch = 0;
    }
    void install(const char *prompt)
    {
        run.handler_installed = true;
        rl_callback_handler_install(prompt, line_ready);
    }
    ~TerminalSession() noexcept
    {
        if (run.handler_installed)
            rl_callback_handler_remove();
        rl_instream = old_in;
        rl_outstream = old_out;
        rl_startup_hook = old_startup;
        rl_pre_input_hook = old_pre_input;
        rl_attempted_completion_function = old_completion;
        rl_getc_function = old_getc;
        rl_catch_signals = old_signals;
        rl_catch_sigwinch = old_winch;
        session = old_session;
        std::fclose(input);
        std::fclose(output);
    }
};

} // namespace

void initialize_readline(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot type(c), state(c), value(c), key(c);
    NativeTypeSpec spec{{"_readline_state"}, LayoutDescriptor::of<ReadlineState>()};
    spec.type.module = U"readline";
    create_native_type(c, spec, type);
    if (type.tag() == ResultTag::Raised) {
        out.raised(type.exception());
        return;
    }
    c.allocate_instance<ReadlineState>(roots.root(type.value()), state,
                                       c.builtin(CoreObject::None));
    if (state.tag() == ResultTag::Raised) {
        out.raised(state.exception());
        return;
    }
    auto owner = roots.root(state.value());
    auto dictionary = roots.root(module_dict(c, module.get(c)));
    make_str(c, U"__readline_state__", key);
    dict_store(c, dictionary, roots.root(key.value()), owner, out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto install = [&](const char *name, NativeEntry entry, int required, int optional = 0) {
        SignatureLayout signature;
        for (int i = 0; i < required + optional; ++i)
            signature.parameters.push_back(
                {U"arg" + std::u32string(1, U'0' + i), ParameterKind::PositionalOnly,
                 i >= required ? std::optional<Value>(c.builtin(CoreObject::None)) : std::nullopt});
        Value capture = owner.get(c);
        make_native_function(c, name, std::move(signature), entry, {&capture, 1}, value,
                             NativeFunctionBinding::Function);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return false;
        }
        auto fn = roots.root(value.value());
        make_str(c, std::u32string(name, name + std::strlen(name)), key);
        dict_store(c, dictionary, roots.root(key.value()), fn, out);
        return out.tag() != ResultTag::Raised;
    };
#define INSTALL(name, op, req, opt)                                                                \
    if (!install(name, method<Op::op>, req, opt))                                                  \
    return
    INSTALL("add_history", Add, 1, 0);
    INSTALL("get_history_item", Get, 1, 0);
    INSTALL("remove_history_item", Remove, 1, 0);
    INSTALL("replace_history_item", Replace, 2, 0);
    INSTALL("clear_history", Clear, 0, 0);
    INSTALL("get_current_history_length", CurrentLength, 0, 0);
    INSTALL("get_history_length", GetLength, 0, 0);
    INSTALL("set_history_length", SetLength, 1, 0);
    INSTALL("read_history_file", ReadFile, 0, 1);
    INSTALL("write_history_file", WriteFile, 0, 1);
    INSTALL("append_history_file", AppendFile, 1, 1);
    INSTALL("read_init_file", InitFile, 0, 1);
    INSTALL("parse_and_bind", Bind, 1, 0);
    INSTALL("set_completer", SetCompleter, 0, 1);
    INSTALL("get_completer", GetCompleter, 0, 0);
    INSTALL("set_completer_delims", SetDelims, 1, 0);
    INSTALL("get_completer_delims", GetDelims, 0, 0);
    INSTALL("get_line_buffer", LineBuffer, 0, 0);
    INSTALL("get_begidx", Begidx, 0, 0);
    INSTALL("get_endidx", Endidx, 0, 0);
    INSTALL("redisplay", Redisplay, 0, 0);
    INSTALL("insert_text", Insert, 1, 0);
    INSTALL("set_startup_hook", Startup, 0, 1);
    INSTALL("set_pre_input_hook", PreInput, 0, 1);
#undef INSTALL
    out.ok(c.builtin(CoreObject::None));
}

void read_terminal_line(Attachment &c, int input_fd, int output_fd, Handle prompt, ReturnSlot &out)
{
    RootScope roots(c);
    std::string prompt_text;
    if (!encode(c, prompt.get(c), prompt_text, out))
        return;
    ReturnSlot module(c);
    import_module(c, U"readline", module);
    if (module.tag() == ResultTag::Raised) {
        out.raised(module.exception());
        return;
    }
    auto mod = roots.root(module.value());
    auto state = name_lookup(c, module_dict(c, mod.get(c)), U"__readline_state__");
    if (!state || !c.has_layout<ReadlineState>(*state)) {
        raise_error(c, CoreObject::RuntimeErrorType, U"invalid readline state", out);
        return;
    }
    auto owner = roots.root(*state);
    auto &s = c.payload<ReadlineState>(owner);
    Lease lease(c, s, true, out);
    if (!lease)
        return;
    try {
        [&] {
            int in_copy = dup(input_fd), out_copy = dup(output_fd);
            FILE *input = in_copy < 0 ? nullptr : fdopen(in_copy, "r");
            FILE *output = out_copy < 0 ? nullptr : fdopen(out_copy, "w");
            if (!input || !output) {
                int error = errno;
                if (input)
                    fclose(input);
                else if (in_copy >= 0)
                    close(in_copy);
                if (output)
                    fclose(output);
                else if (out_copy >= 0)
                    close(out_copy);
                raise_os_error(c, error, out);
                return;
            }
            setvbuf(input, nullptr, _IONBF, 0);
            Session run{c, owner, s};
            {
                NativeWait wait(c);
                run.native_call = &wait;
                TerminalSession terminal(input, output, run);
                terminal.install(prompt_text.c_str());
                while (!run.finished)
                    rl_callback_read_char();
            }
            if (run.allocation_failure) {
                out.raised(c.builtin(CoreObject::MemoryError));
                return;
            }
            if (run.native_failure) {
                raise_error(c, CoreObject::RuntimeErrorType, U"readline native callback failed",
                            out);
                return;
            }
            if (run.input_error) {
                raise_os_error(c, run.input_error, out);
                return;
            }
            if (!run.eof && run.line && *run.line) {
                auto last = history_get(history_base + history_length - 1);
                if (!last || std::strcmp(run.line.get(), last->line) != 0)
                    add_history(run.line.get());
            }
            std::string line = run.line ? run.line.get() : "";
            if (!run.eof)
                line.push_back('\n');
            decode(c, line, out);
        }();
    } catch (const std::bad_alloc &) {
        lease.finish();
        out.raised(c.builtin(CoreObject::MemoryError));
        return;
    } catch (...) {
        lease.finish();
        throw;
    }
    lease.finish();
}
} // namespace cxxpython::core::detail
