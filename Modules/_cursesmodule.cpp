// Native ncursesw support; Python policy is from CPython 3.12.14 (PSF license).
#include "internal/cursesmodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <curses.h>
#include <cwchar>
#include <mutex>
#include <string>
#include <term.h>

namespace cxxpython::core::detail {
namespace {
std::mutex curses_mutex;
SCREEN *curses_screen = nullptr;
TERMINAL *prepared_terminal = nullptr;
struct Window {
    struct Resource {
        WINDOW *window;
        const bool owned;
    };
    ObjectHeader header;
    AtomicGcEdge error_type;
    const NativeOwner<Resource> resource;
    explicit Window(InitialType type, WINDOW *w, bool own, Value error)
        : header(type), error_type(error), resource(std::make_unique<Resource>(Resource{w, own}))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(error_type);
    }
    std::size_t external_bytes() const noexcept
    {
        return sizeof(Resource);
    }
    static void finalize(Attachment &c, Handle self, ReturnSlot &out)
    {
        auto *resource = c.payload<Window>(self).resource.get();
        {
            NativeWait wait(c);
            std::lock_guard lock(curses_mutex);
            if (resource->owned && resource->window)
                delwin(resource->window);
            resource->window = nullptr;
        }
        out.ok(c.builtin(CoreObject::None));
    }
};

bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
void error(Attachment &c, Handle type, ReturnSlot &out,
           std::u32string_view text = U"curses function returned ERR")
{
    RootScope roots(c);
    ReturnSlot message(c), args(c), exception(c);
    make_str(c, std::u32string(text), message);
    if (failed(message)) {
        out.raised(message.exception());
        return;
    }
    auto msg = roots.root(message.value());
    Value values[] = {msg.get(c)};
    make_tuple(c, values, args);
    if (failed(args)) {
        out.raised(args.exception());
        return;
    }
    c.allocate_instance<ExceptionObject>(type, exception, args.value());
    if (failed(exception))
        out.raised(exception.exception());
    else
        out.raised(exception.value());
}
bool number(Attachment &c, Value v, int &n, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot converted(c);
    index(c, roots.root(v), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    std::int64_t value;
    if (!int_fits_signed(c, converted.value(), value) || value < INT_MIN || value > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return false;
    }
    n = static_cast<int>(value);
    return true;
}
bool text(Attachment &c, Value v, std::string &s, ReturnSlot &out)
{
    if (c.has_layout<BytesObject>(v)) {
        s = c.payload<BytesObject>(v).value;
        if (s.find('\0') != std::string::npos) {
            raise_error(c, CoreObject::ValueErrorType, U"embedded null byte", out);
            return false;
        }
        return true;
    }
    if (!c.has_layout<StrObject>(v)) {
        raise_error(c, CoreObject::TypeErrorType, U"expected str or bytes", out);
        return false;
    }
    RootScope roots(c);
    ReturnSlot errors(c), encoded(c);
    make_str(c, U"strict", errors);
    encode_primitive(c, roots.root(v), TextEncoding::Utf8, roots.root(errors.value()), encoded);
    if (failed(encoded)) {
        out.raised(encoded.exception());
        return false;
    }
    s = c.payload<BytesObject>(c.payload<TupleObject>(encoded.value()).items[0]).value;
    if (s.find('\0') != std::string::npos) {
        raise_error(c, CoreObject::ValueErrorType, U"embedded null character", out);
        return false;
    }
    return true;
}
void status(Attachment &c, Handle error_type, int result, ReturnSlot &out)
{
    if (result == ERR)
        error(c, error_type, out);
    else
        out.ok(c.builtin(CoreObject::None));
}
enum class ModuleOp {
    Init,
    End,
    IsEnd,
    New,
    NoEcho,
    Echo,
    CBreak,
    NoCBreak,
    Raw,
    NoRaw,
    StartColor,
    InitPair,
    ColorPair,
    Setup,
    Str,
    Num,
    Flag,
    HasKey,
    HasColors,
    CanChangeColor,
    DefaultColors,
    Beep,
    Flash,
    Cursor,
    Unget,
    UngetWide,
    PairNumber
};
template <ModuleOp op>
void module_call(Attachment &c, Handle fn, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto error_type = roots.root(c.payload<NativeFunctionObject>(fn).captures[2]);
    int x = 0, y = 0, z = 0, v = 0;
    std::string s;
    if constexpr (op == ModuleOp::New || op == ModuleOp::InitPair) {
        if (!number(c, a[0], x, out) || !number(c, a[1], y, out))
            return;
        if constexpr (op == ModuleOp::New) {
            if (!number(c, a[2], z, out) || !number(c, a[3], v, out))
                return;
        } else if (!number(c, a[2], z, out))
            return;
    }
    if constexpr (op == ModuleOp::ColorPair || op == ModuleOp::HasKey || op == ModuleOp::Cursor ||
                  op == ModuleOp::Unget || op == ModuleOp::PairNumber)
        if (!number(c, a[0], x, out))
            return;
    if constexpr (op == ModuleOp::UngetWide) {
        if (c.has_layout<StrObject>(a[0])) {
            const auto &value = c.payload<StrObject>(a[0]).value;
            if (value.size() != 1) {
                raise_error(c, CoreObject::TypeErrorType, U"expected a character", out);
                return;
            }
            x = static_cast<int>(value[0]);
        } else if (!number(c, a[0], x, out))
            return;
    }
    if constexpr (op == ModuleOp::Setup) {
        if (a[0] != c.builtin(CoreObject::None) && !text(c, a[0], s, out))
            return;
        if (!number(c, a[1], x, out))
            return;
        if (x == -1)
            x = 1;
    }
    if constexpr (op == ModuleOp::Str || op == ModuleOp::Num || op == ModuleOp::Flag)
        if (!text(c, a[0], s, out))
            return;
    WINDOW *w = nullptr;
    int result = OK;
    int rows = 0, cols = 0;
    std::string result_string;
    bool missing = false;
    bool uninitialized = false;
    const bool terminal_given = op == ModuleOp::Setup && a[0] != c.builtin(CoreObject::None);
    {
        NativeWait wait(c);
        std::lock_guard lock(curses_mutex);
        if constexpr (op != ModuleOp::Setup && op != ModuleOp::Init) {
            if constexpr (op == ModuleOp::Str || op == ModuleOp::Num || op == ModuleOp::Flag)
                uninitialized = cur_term == nullptr;
            else
                uninitialized = stdscr == nullptr;
        }
        if (!uninitialized) {
            if constexpr (op == ModuleOp::Init) {
                if (!curses_screen) {
                    auto *prepared = prepared_terminal;
                    // newterm reports failure instead of initscr's process exit.
                    curses_screen = newterm(prepared ? termname() : nullptr, stdout, stdin);
                    if (curses_screen) {
                        if (prepared && prepared != cur_term)
                            del_curterm(prepared);
                        prepared_terminal = nullptr;
                    }
                } else
                    set_term(curses_screen);
                if (curses_screen)
                    w = stdscr;
            }
            if constexpr (op == ModuleOp::Init) {
                if (w) {
                    rows = LINES;
                    cols = COLS;
                }
            }
            if constexpr (op == ModuleOp::New)
                w = newwin(x, y, z, v);
            if constexpr (op == ModuleOp::End)
                result = endwin();
            if constexpr (op == ModuleOp::IsEnd)
                result = isendwin();
            if constexpr (op == ModuleOp::NoEcho)
                result = noecho();
            if constexpr (op == ModuleOp::Echo)
                result = echo();
            if constexpr (op == ModuleOp::CBreak)
                result = cbreak();
            if constexpr (op == ModuleOp::NoCBreak)
                result = nocbreak();
            if constexpr (op == ModuleOp::Raw)
                result = raw();
            if constexpr (op == ModuleOp::NoRaw)
                result = noraw();
            if constexpr (op == ModuleOp::StartColor)
                result = start_color();
            if constexpr (op == ModuleOp::StartColor) {
                if (result != ERR) {
                    rows = COLORS;
                    cols = COLOR_PAIRS;
                }
            }
            if constexpr (op == ModuleOp::InitPair)
                result = init_pair(x, y, z);
            if constexpr (op == ModuleOp::ColorPair)
                result = COLOR_PAIR(x);
            if constexpr (op == ModuleOp::HasKey)
                result = has_key(x);
            if constexpr (op == ModuleOp::Setup) {
                if (!curses_screen && !prepared_terminal) {
                    int err;
                    result = setupterm(terminal_given ? s.data() : nullptr, x, &err);
                    if (result != ERR)
                        prepared_terminal = cur_term;
                }
            }
            if constexpr (op == ModuleOp::Num)
                result = tigetnum(s.data());
            if constexpr (op == ModuleOp::Flag)
                result = tigetflag(s.data());
            if constexpr (op == ModuleOp::HasColors)
                result = has_colors();
            if constexpr (op == ModuleOp::CanChangeColor)
                result = can_change_color();
            if constexpr (op == ModuleOp::DefaultColors)
                result = use_default_colors();
            if constexpr (op == ModuleOp::Beep)
                result = beep();
            if constexpr (op == ModuleOp::Flash)
                result = flash();
            if constexpr (op == ModuleOp::Cursor)
                result = curs_set(x);
            if constexpr (op == ModuleOp::Unget)
                result = ungetch(x);
            if constexpr (op == ModuleOp::UngetWide)
                result = unget_wch(static_cast<wchar_t>(x));
            if constexpr (op == ModuleOp::PairNumber)
                result = PAIR_NUMBER(x);
            if constexpr (op == ModuleOp::Str) {
                char *p = tigetstr(s.data());
                missing = !p || p == reinterpret_cast<char *>(-1);
                if (!missing)
                    result_string = p;
            }
        }
    }
    if (uninitialized) {
        error(c, error_type, out,
              op == ModuleOp::Str || op == ModuleOp::Num || op == ModuleOp::Flag
                  ? U"must call (at least) setupterm() first"
                  : U"must call initscr() first");
        return;
    }
    auto discard_window = [&] {
        if (!w)
            return;
        NativeWait wait(c);
        std::lock_guard lock(curses_mutex);
        if constexpr (op == ModuleOp::New)
            delwin(w);
        else
            endwin();
    };
    if constexpr (op == ModuleOp::Init || op == ModuleOp::StartColor) {
        if ((op == ModuleOp::Init && w) || (op == ModuleOp::StartColor && result != ERR)) {
            try {
                RootScope roots(c);
                auto module = roots.root(c.payload<NativeFunctionObject>(fn).captures[1]);
                auto dict = roots.root(module_dict(c, module.get(c)));
                ReturnSlot key(c), value(c), stored(c);
                auto publish = [&](const char32_t *name, int n) {
                    make_str(c, name, key);
                    auto name_root = roots.root(key.value());
                    make_int(c, n, value);
                    dict_store(c, dict, name_root, roots.root(value.value()), stored);
                    return !failed(stored);
                };
                if (!publish(op == ModuleOp::Init ? U"LINES" : U"COLORS", rows) ||
                    !publish(op == ModuleOp::Init ? U"COLS" : U"COLOR_PAIRS", cols)) {
                    out.raised(stored.exception());
                    if constexpr (op == ModuleOp::Init)
                        discard_window();
                    return;
                }
            } catch (...) {
                if constexpr (op == ModuleOp::Init)
                    discard_window();
                throw;
            }
        }
    }
    if constexpr (op == ModuleOp::Init || op == ModuleOp::New) {
        if (!w) {
            error(c, error_type, out);
            return;
        }
        auto type = roots.root(c.payload<NativeFunctionObject>(fn).captures[0]);
        try {
            c.allocate_instance<Window>(type, out, w, op == ModuleOp::New, error_type.get(c));
        } catch (...) {
            discard_window();
            throw;
        }
    } else if constexpr (op == ModuleOp::IsEnd || op == ModuleOp::HasKey ||
                         op == ModuleOp::HasColors || op == ModuleOp::CanChangeColor)
        out.ok(c.builtin(result ? CoreObject::True : CoreObject::False));
    else if constexpr (op == ModuleOp::ColorPair || op == ModuleOp::Num || op == ModuleOp::Flag ||
                       op == ModuleOp::PairNumber)
        make_int(c, result, out);
    else if constexpr (op == ModuleOp::Cursor) {
        if (result == ERR)
            error(c, error_type, out);
        else
            make_int(c, result, out);
    } else if constexpr (op == ModuleOp::Str) {
        if (missing)
            out.ok(c.builtin(CoreObject::None));
        else
            make_bytes(c, result_string, out);
    } else
        status(c, error_type, result, out);
}

enum class WinOp {
    Add,
    Move,
    Refresh,
    Clear,
    Erase,
    Max,
    YX,
    Beg,
    Keypad,
    Getch,
    Getstr,
    GetWide,
    NoDelay,
    Timeout,
    AttrSet,
    AttrOn,
    AttrOff,
    ClrToEol,
    ClrToBot,
    Touch,
    NoutRefresh
};
template <WinOp op>
void window_call(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(a[0]);
    if (!c.has_layout<Window>(self.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"expected curses window", out);
        return;
    }
    auto &object = c.payload<Window>(self);
    auto *resource = object.resource.get();
    ReturnSlot error_slot(c);
    c.load(self.get(c), object.error_type, error_slot);
    auto error_type = roots.root(error_slot.value());
    int x = 0, y = 0, result = OK;
    std::string s;
    std::wstring wide;
    constexpr bool variadic =
        op == WinOp::Add || op == WinOp::Getch || op == WinOp::Getstr || op == WinOp::GetWide;
    std::span<const Value> args;
    if constexpr (variadic)
        args = c.payload<TupleObject>(a[1]).items;
    else
        args = a.subspan(1);
    bool coordinates = false, attributes = false, wide_text = false;
    int attr = A_NORMAL, limit = 1023;
    if constexpr (op == WinOp::Add) {
        if (args.empty() || args.size() > 4) {
            raise_error(c, CoreObject::TypeErrorType, U"addstr requires 1 to 4 arguments", out);
            return;
        }
        coordinates = args.size() >= 3;
        attributes = args.size() == 2 || args.size() == 4;
        if (coordinates && (!number(c, args[0], y, out) || !number(c, args[1], x, out)))
            return;
        auto value = args[coordinates ? 2 : 0];
        wide_text = c.has_layout<StrObject>(value);
        if (wide_text) {
            for (auto ch : c.payload<StrObject>(value).value) {
                if (!ch) {
                    raise_error(c, CoreObject::ValueErrorType, U"embedded null character", out);
                    return;
                }
                wide.push_back(static_cast<wchar_t>(ch));
            }
        } else if (!text(c, value, s, out))
            return;
        if (attributes && !number(c, args.back(), attr, out))
            return;
    }
    if constexpr (op == WinOp::Getch || op == WinOp::GetWide || op == WinOp::Getstr) {
        if (args.size() > (op == WinOp::Getstr ? 3 : 2) ||
            (op != WinOp::Getstr && args.size() == 1)) {
            raise_error(c, CoreObject::TypeErrorType, U"invalid input coordinates", out);
            return;
        }
        coordinates = args.size() >= 2;
        if (coordinates && (!number(c, args[0], y, out) || !number(c, args[1], x, out)))
            return;
        if constexpr (op == WinOp::Getstr) {
            if ((args.size() == 1 || args.size() == 3) && !number(c, args.back(), limit, out))
                return;
            if (limit < 0) {
                raise_error(c, CoreObject::ValueErrorType, U"'n' must be nonnegative", out);
                return;
            }
            limit = std::min(limit, 1023);
        }
    }
    if constexpr (op == WinOp::Move)
        if (!number(c, args[0], y, out) || !number(c, args[1], x, out))
            return;
    if constexpr (op == WinOp::Keypad || op == WinOp::NoDelay || op == WinOp::Timeout ||
                  op == WinOp::AttrSet || op == WinOp::AttrOn || op == WinOp::AttrOff)
        if (!number(c, args[0], x, out))
            return;
    char buffer[1024]{};
    wint_t input = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(curses_mutex);
        WINDOW *w = resource->window;
        if (!w)
            result = ERR;
        else {
            if (coordinates)
                result = wmove(w, y, x);
            if constexpr (op == WinOp::Add) {
                const auto previous = getattrs(w);
                if (attributes)
                    wattrset(w, attr);
                if (result != ERR)
                    result = wide_text ? waddwstr(w, wide.c_str()) : waddstr(w, s.c_str());
                if (attributes)
                    wattrset(w, previous);
            }
            if constexpr (op == WinOp::Move)
                result = wmove(w, y, x);
            if constexpr (op == WinOp::Refresh)
                result = wrefresh(w);
            if constexpr (op == WinOp::Clear)
                result = wclear(w);
            if constexpr (op == WinOp::Erase)
                result = werase(w);
            if constexpr (op == WinOp::Max) {
                getmaxyx(w, y, x);
            }
            if constexpr (op == WinOp::YX) {
                getyx(w, y, x);
            }
            if constexpr (op == WinOp::Beg) {
                getbegyx(w, y, x);
            }
            if constexpr (op == WinOp::Keypad)
                result = keypad(w, x != 0);
            if constexpr (op == WinOp::NoDelay)
                result = nodelay(w, x != 0);
            if constexpr (op == WinOp::Timeout)
                wtimeout(w, x);
            if constexpr (op == WinOp::AttrSet)
                result = wattrset(w, x);
            if constexpr (op == WinOp::AttrOn)
                result = wattron(w, x);
            if constexpr (op == WinOp::AttrOff)
                result = wattroff(w, x);
            if constexpr (op == WinOp::ClrToEol)
                result = wclrtoeol(w);
            if constexpr (op == WinOp::ClrToBot)
                result = wclrtobot(w);
            if constexpr (op == WinOp::Touch)
                result = touchwin(w);
            if constexpr (op == WinOp::NoutRefresh)
                result = wnoutrefresh(w);
            if constexpr (op == WinOp::Getch) {
                if (result != ERR)
                    result = wgetch(w);
            }
            if constexpr (op == WinOp::GetWide) {
                if (result != ERR)
                    result = wget_wch(w, &input);
            }
            if constexpr (op == WinOp::Getstr) {
                if (result != ERR)
                    result = wgetnstr(w, buffer, limit);
            }
        }
    }
    if (result == ERR && op != WinOp::Getch && op != WinOp::Getstr) {
        error(c, error_type, out);
        return;
    }
    if constexpr (op == WinOp::Max || op == WinOp::YX || op == WinOp::Beg) {
        ReturnSlot first(c), second(c);
        make_int(c, y, first);
        auto one = roots.root(first.value());
        make_int(c, x, second);
        auto two = roots.root(second.value());
        Value items[] = {one.get(c), two.get(c)};
        make_tuple(c, items, out);
    } else if constexpr (op == WinOp::Getch)
        make_int(c, result, out);
    else if constexpr (op == WinOp::GetWide) {
        if (result == KEY_CODE_YES)
            make_int(c, input, out);
        else
            make_str(c, std::u32string(1, static_cast<char32_t>(input)), out);
    } else if constexpr (op == WinOp::Getstr) {
        if (result == ERR)
            buffer[0] = 0;
        make_bytes(c, buffer, out);
    } else
        status(c, error_type, result, out);
}
} // namespace

void initialize_curses(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    ReturnSlot value(c), key(c);
    const Handle bases[] = {roots.root(c.builtin(CoreObject::ExceptionType))};
    NativeTypeSpec error_spec{{"error", bases}, LayoutDescriptor::of<ExceptionObject>()};
    error_spec.type.module = U"_curses";
    create_native_type(c, error_spec, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto error_type = roots.root(value.value());
    NativeTypeSpec spec{{"window"}, LayoutDescriptor::of<Window>()};
    spec.type.module = U"_curses";
    create_native_type(c, spec, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    auto type = roots.root(value.value());
    auto dict = roots.root(module_dict(c, module.get(c)));
    auto store = [&](const char *name, Value v) {
        auto item = roots.root(v);
        make_str(c, std::u32string(name, name + std::strlen(name)), key);
        dict_store(c, dict, roots.root(key.value()), item, out);
        return !failed(out);
    };
    ReturnSlot zero(c), minus_one(c);
    make_int(c, 0, zero);
    auto zero_root = roots.root(zero.value());
    make_int(c, -1, minus_one);
    auto minus_one_root = roots.root(minus_one.value());
    auto install = [&](const char *name, NativeEntry entry, int count, bool setup = false,
                       bool new_window = false) {
        SignatureLayout sig;
        for (int i = 0; i < count; ++i) {
            std::u32string n = setup ? (i ? U"fd" : U"term") : U"arg" + std::u32string(1, U'0' + i);
            std::optional<Value> default_value;
            if (setup)
                default_value = i ? minus_one_root.get(c) : c.builtin(CoreObject::None);
            else if (new_window && i >= 2)
                default_value = zero_root.get(c);
            sig.parameters.push_back(
                {n, setup ? ParameterKind::PositionalOrKeyword : ParameterKind::PositionalOnly,
                 default_value});
        }
        Value captures[] = {type.get(c), module.get(c), error_type.get(c)};
        make_native_function(c, name, std::move(sig), entry, captures, value,
                             NativeFunctionBinding::Function);
        return !failed(value) && store(name, value.value());
    };
    auto method = [&](const char32_t *name, NativeEntry entry, int count) {
        SignatureLayout sig;
        sig.parameters.push_back({U"self", ParameterKind::PositionalOnly});
        if (count < 0)
            sig.varargs = U"args";
        for (int i = 0; i < count; ++i)
            sig.parameters.push_back(
                {U"arg" + std::u32string(1, U'0' + i), ParameterKind::PositionalOnly});
        install_native_attribute(c, core, type, name, sig, entry);
    };
#define W(name, op, n) method(U##name, window_call<WinOp::op>, n)
    W("addstr", Add, -1);
    W("move", Move, 2);
    W("refresh", Refresh, 0);
    W("clear", Clear, 0);
    W("erase", Erase, 0);
    W("getmaxyx", Max, 0);
    W("getyx", YX, 0);
    W("getbegyx", Beg, 0);
    W("keypad", Keypad, 1);
    W("getch", Getch, -1);
    W("getstr", Getstr, -1);
    W("get_wch", GetWide, -1);
    W("nodelay", NoDelay, 1);
    W("timeout", Timeout, 1);
    W("attrset", AttrSet, 1);
    W("attron", AttrOn, 1);
    W("attroff", AttrOff, 1);
    W("clrtoeol", ClrToEol, 0);
    W("clrtobot", ClrToBot, 0);
    W("touchwin", Touch, 0);
    W("noutrefresh", NoutRefresh, 0);
#undef W
#define M(name, op, n)                                                                             \
    if (!install(name, module_call<ModuleOp::op>, n))                                              \
    return
    M("initscr", Init, 0);
    M("endwin", End, 0);
    M("isendwin", IsEnd, 0);
    if (!install("newwin", module_call<ModuleOp::New>, 4, false, true))
        return;
    M("noecho", NoEcho, 0);
    M("echo", Echo, 0);
    M("cbreak", CBreak, 0);
    M("nocbreak", NoCBreak, 0);
    M("raw", Raw, 0);
    M("noraw", NoRaw, 0);
    M("start_color", StartColor, 0);
    M("init_pair", InitPair, 3);
    M("color_pair", ColorPair, 1);
    M("has_key", HasKey, 1);
    M("has_colors", HasColors, 0);
    M("can_change_color", CanChangeColor, 0);
    M("use_default_colors", DefaultColors, 0);
    M("beep", Beep, 0);
    M("flash", Flash, 0);
    M("curs_set", Cursor, 1);
    M("ungetch", Unget, 1);
    M("unget_wch", UngetWide, 1);
    M("pair_number", PairNumber, 1);
    if (!install("setupterm", module_call<ModuleOp::Setup>, 2, true))
        return;
    M("tigetstr", Str, 1);
    M("tigetnum", Num, 1);
    M("tigetflag", Flag, 1);
#undef M
    if (!store("error", error_type.get(c)) || !store("window", type.get(c)))
        return;
    struct Constant {
        const char *name;
        int value;
    };
    const Constant constants[] = {{"A_NORMAL", A_NORMAL},       {"A_BOLD", A_BOLD},
                                  {"A_REVERSE", A_REVERSE},     {"A_UNDERLINE", A_UNDERLINE},
                                  {"COLOR_BLACK", COLOR_BLACK}, {"COLOR_RED", COLOR_RED},
                                  {"COLOR_GREEN", COLOR_GREEN}, {"COLOR_YELLOW", COLOR_YELLOW},
                                  {"COLOR_BLUE", COLOR_BLUE},   {"COLOR_MAGENTA", COLOR_MAGENTA},
                                  {"COLOR_CYAN", COLOR_CYAN},   {"COLOR_WHITE", COLOR_WHITE},
                                  {"KEY_LEFT", KEY_LEFT},       {"KEY_RIGHT", KEY_RIGHT},
                                  {"KEY_UP", KEY_UP},           {"KEY_DOWN", KEY_DOWN}};
    for (const auto &constant : constants) {
        make_int(c, constant.value, value);
        if (!store(constant.name, value.value()))
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
