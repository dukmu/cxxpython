// Clock and sleep behavior follows CPython 3.12.14 Modules/timemodule.c
// and Python/pytime.c, Copyright Python Software Foundation (PSF license).
#include "internal/timemodule.hpp"
#include "internal/signalmodule.hpp"

#include "cxxpython/compiler.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/iomodule.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <limits>
#include <time.h>

namespace cxxpython::core::detail {
namespace {
constexpr std::int64_t billion = 1000000000;

template <clockid_t Clock, bool Nanoseconds>
void clock_entry(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    timespec value{};
    if (clock_gettime(Clock, &value) != 0) {
        io_os_error(ctx, errno, out);
        return;
    }
    if constexpr (Nanoseconds) {
        if (value.tv_sec > std::numeric_limits<std::int64_t>::max() / billion ||
            value.tv_sec < std::numeric_limits<std::int64_t>::min() / billion) {
            raise_error(ctx, CoreObject::OverflowErrorType, U"timestamp too large", out);
            return;
        }
        const auto seconds = static_cast<std::int64_t>(value.tv_sec) * billion;
        if (seconds > std::numeric_limits<std::int64_t>::max() - value.tv_nsec) {
            raise_error(ctx, CoreObject::OverflowErrorType, U"timestamp too large", out);
            return;
        }
        make_int(ctx, seconds + value.tv_nsec, out);
    } else {
        make_float(ctx, static_cast<double>(value.tv_sec) + value.tv_nsec * 1e-9, out);
    }
}

void sleep_entry(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::int64_t nanos;
    if (is_float(ctx, args[0])) {
        const double value = ctx.payload<FloatObject>(args[0]).value;
        if (std::isnan(value)) {
            raise_error(ctx, CoreObject::ValueErrorType, U"Invalid value NaN (not a number)", out);
            return;
        }
        const double scaled = value * billion;
        const long double rounded = value < 0 ? std::floor(scaled) : std::ceil(scaled);
        if (!std::isfinite(value) ||
            rounded > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
            rounded < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
            raise_error(ctx, CoreObject::OverflowErrorType,
                        U"timestamp out of range for platform time_t", out);
            return;
        }
        nanos = static_cast<std::int64_t>(rounded);
    } else {
        ReturnSlot converted(ctx);
        index(ctx, roots.root(args[0]), converted);
        if (converted.tag() == ResultTag::Raised) {
            out.raised(converted.exception());
            return;
        }
        std::int64_t seconds;
        if (!int_fits_signed(ctx, converted.value(), seconds) ||
            seconds > std::numeric_limits<std::int64_t>::max() / billion ||
            seconds < std::numeric_limits<std::int64_t>::min() / billion) {
            raise_error(ctx, CoreObject::OverflowErrorType,
                        U"timestamp out of range for platform time_t", out);
            return;
        }
        nanos = seconds * billion;
    }
    if (nanos < 0) {
        raise_error(ctx, CoreObject::ValueErrorType, U"sleep length must be non-negative", out);
        return;
    }
    timespec deadline{};
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        io_os_error(ctx, errno, out);
        return;
    }
    deadline.tv_sec += static_cast<time_t>(nanos / billion);
    deadline.tv_nsec += static_cast<long>(nanos % billion);
    if (deadline.tv_nsec >= billion) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= billion;
    }
    for (;;) {
        int error;
        {
            NativeWait wait(ctx);
            error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
        }
        if (!error)
            break;
        if (error != EINTR) {
            io_os_error(ctx, error, out);
            return;
        }
        if (check_signals(ctx, out))
            return;
    }
    out.ok(ctx.builtin(CoreObject::None));
}

void clock_info(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!ctx.has_layout<StrObject>(args[0])) {
        RootScope roots(ctx);
        const auto argument = roots.root(args[0]);
        const auto type = roots.root(ctx.type_of(argument));
        TypeView view(ctx, type);
        const auto stored = view.name();
        auto name = argument.get(ctx) == ctx.builtin(CoreObject::None) ? std::u32string(U"None")
                    : std::holds_alternative<std::u32string>(stored)
                        ? std::get<std::u32string>(stored)
                        : ctx.payload<StrObject>(std::get<Value>(stored)).value;
        std::size_t length = 0, bytes = 0;
        for (char32_t ch : name) {
            const std::size_t width = ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
            if (bytes + width > 50)
                break;
            bytes += width;
            ++length;
        }
        if (length < name.size() && bytes < 50) {
            // CPython's %.50s can cut UTF-8 in the middle of a character.
            auto message = std::string("get_clock_info() argument 1 must be str, not ") +
                           source_utf8(name).substr(0, 50);
            ReturnSlot text(ctx), method(ctx);
            make_bytes(ctx, std::move(message), text);
            const auto data = roots.root(text.value());
            make_str(ctx, U"decode", text);
            get_attribute(ctx, data, roots.root(text.value()), method);
            if (method.tag() == ResultTag::Raised) {
                out.raised(method.exception());
                return;
            }
            call(ctx, roots.root(method.value()), {}, out);
            return;
        }
        name.resize(length);
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"get_clock_info() argument 1 must be str, not " + name, out);
        return;
    }
    const auto name = ctx.payload<StrObject>(args[0]).value;
    if (name.find(U'\0') != std::u32string::npos) {
        raise_error(ctx, CoreObject::ValueErrorType, U"embedded null character", out);
        return;
    }
    clockid_t clock;
    std::u32string implementation;
    bool monotonic = true, adjustable = false;
    if (name == U"time") {
        clock = CLOCK_REALTIME;
        implementation = U"clock_gettime(CLOCK_REALTIME)";
        monotonic = false;
        adjustable = true;
    } else if (name == U"monotonic" || name == U"perf_counter") {
        clock = CLOCK_MONOTONIC;
        implementation = U"clock_gettime(CLOCK_MONOTONIC)";
    } else if (name == U"process_time") {
        clock = CLOCK_PROCESS_CPUTIME_ID;
        implementation = U"clock_gettime(CLOCK_PROCESS_CPUTIME_ID)";
    } else if (name == U"thread_time") {
        clock = CLOCK_THREAD_CPUTIME_ID;
        implementation = U"clock_gettime(CLOCK_THREAD_CPUTIME_ID)";
    } else {
        raise_error(ctx, CoreObject::ValueErrorType, U"unknown clock", out);
        return;
    }
    timespec resolution{};
    if (clock_getres(clock, &resolution) != 0) {
        io_os_error(ctx, errno, out);
        return;
    }
    RootScope roots(ctx);
    ReturnSlot temporary(ctx), key(ctx);
    call(ctx, roots.root(ctx.builtin(CoreObject::SimpleNamespaceType)), {}, temporary);
    if (temporary.tag() == ResultTag::Raised) {
        out.raised(temporary.exception());
        return;
    }
    const auto result = roots.root(temporary.value());
    ctx.load(result.get(ctx), *ctx.instance_dictionary(result.get(ctx)), temporary);
    const auto dictionary = roots.root(temporary.value());
    auto store = [&](std::u32string key_name) {
        const auto value = roots.root(temporary.value());
        make_str(ctx, std::move(key_name), key);
        dict_store(ctx, dictionary, roots.root(key.value()), value, out);
        return out.tag() != ResultTag::Raised;
    };
    make_str(ctx, std::move(implementation), temporary);
    if (!store(U"implementation"))
        return;
    temporary.ok(ctx.builtin(monotonic ? CoreObject::True : CoreObject::False));
    if (!store(U"monotonic"))
        return;
    temporary.ok(ctx.builtin(adjustable ? CoreObject::True : CoreObject::False));
    if (!store(U"adjustable"))
        return;
    make_float(ctx, static_cast<double>(resolution.tv_sec) + resolution.tv_nsec * 1e-9, temporary);
    if (!store(U"resolution"))
        return;
    out.ok(result.get(ctx));
}
} // namespace

void initialize_time(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot key(ctx), function(ctx);
    SignatureLayout unary;
    unary.parameters = {{U"arg", ParameterKind::PositionalOnly}};
    auto add = [&](const char *name, std::u32string wide, auto entry, bool takes_arg) {
        make_native_function(ctx, name, takes_arg ? unary : SignatureLayout{}, entry, {}, function,
                             NativeFunctionBinding::Function);
        if (function.tag() == ResultTag::Raised) {
            out.raised(function.exception());
            return false;
        }
        const auto value = roots.root(function.value());
        make_str(ctx, std::move(wide), key);
        dict_store(ctx, dictionary, roots.root(key.value()), value, out);
        return out.tag() != ResultTag::Raised;
    };
#define ADD_CLOCK(name, id)                                                                        \
    if (!add(#name, U## #name, clock_entry<id, false>, false) ||                                   \
        !add(#name "_ns", U## #name "_ns", clock_entry<id, true>, false))                          \
    return
    ADD_CLOCK(time, CLOCK_REALTIME);
    ADD_CLOCK(monotonic, CLOCK_MONOTONIC);
    ADD_CLOCK(perf_counter, CLOCK_MONOTONIC);
    ADD_CLOCK(process_time, CLOCK_PROCESS_CPUTIME_ID);
    ADD_CLOCK(thread_time, CLOCK_THREAD_CPUTIME_ID);
#undef ADD_CLOCK
    if (!add("sleep", U"sleep", sleep_entry, true))
        return;
    if (add("get_clock_info", U"get_clock_info", clock_info, true))
        initialize_time_calendar(ctx, module, out);
}
} // namespace cxxpython::core::detail
