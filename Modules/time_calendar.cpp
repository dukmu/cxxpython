// Calendar conversion follows CPython 3.12.14 Modules/timemodule.c and
// Objects/structseq.c. PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/timemodule.hpp"

#include "cxxpython/floatobject.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/codecs.hpp"
#include "internal/compiler.hpp"
#include "internal/errors.hpp"
#include "internal/posixmodule.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <time.h>

namespace cxxpython::core::detail {
namespace {
constexpr const char32_t *fields[] = {U"tm_year",  U"tm_mon",  U"tm_mday",  U"tm_hour",
                                      U"tm_min",   U"tm_sec",  U"tm_wday",  U"tm_yday",
                                      U"tm_isdst", U"tm_zone", U"tm_gmtoff"};
struct NativeTime {
    tm value{};
    std::string zone;
    bool has_zone = false;
};
std::size_t extra_slot(Attachment &c, Value self, std::size_t field)
{
    const auto &names = c.payload<TypeObject>(c.type_of(self)).slot_names;
    const auto found = std::find(names.begin(), names.end(), fields[field]);
    if (found == names.end())
        throw std::logic_error("struct_time field missing from native layout");
    return static_cast<std::size_t>(found - names.begin());
}
bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
Handle calendar_type(Attachment &c, Handle function, RootScope &roots)
{
    return roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
}
bool timestamp(Attachment &c, Handle input, time_t &seconds, ReturnSlot &out)
{
    if (input.get(c) == c.builtin(CoreObject::None)) {
        seconds = ::time(nullptr);
        return true;
    }
    if (is_float(c, input.get(c))) {
        const double value = c.payload<FloatObject>(input).value;
        if (std::isnan(value)) {
            raise_error(c, CoreObject::ValueErrorType, U"Invalid value NaN (not a number)", out);
            return false;
        }
        const long double rounded = std::floor(static_cast<long double>(value));
        if (!std::isfinite(value) || rounded < std::numeric_limits<time_t>::min() ||
            rounded > std::numeric_limits<time_t>::max()) {
            raise_error(c, CoreObject::OverflowErrorType,
                        U"timestamp out of range for platform time_t", out);
            return false;
        }
        seconds = static_cast<time_t>(rounded);
        return true;
    }
    index(c, input, out);
    if (failed(out))
        return false;
    std::int64_t value;
    if (!int_fits_signed(c, out.value(), value) || value < std::numeric_limits<time_t>::min() ||
        value > std::numeric_limits<time_t>::max()) {
        raise_error(c, CoreObject::OverflowErrorType, U"timestamp out of range for platform time_t",
                    out);
        return false;
    }
    seconds = static_cast<time_t>(value);
    return true;
}
bool native_time(Attachment &c, time_t seconds, bool utc, NativeTime &result, ReturnSlot &out)
{
    int error = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(process_environment_lock());
        if (!(utc ? ::gmtime_r(&seconds, &result.value) : ::localtime_r(&seconds, &result.value)))
            error = errno ? errno : EINVAL;
        else if (result.value.tm_zone) {
            result.zone = result.value.tm_zone;
            result.has_zone = true;
        }
    }
    if (!error)
        return true;
    raise_os_error(c, error, out);
    return false;
}
template <std::size_t I> void record_field(Attachment &c, Value self, ReturnSlot &out)
{
    if constexpr (I < 9)
        out.ok(c.payload<TupleObject>(self).items[I]);
    else {
        c.load(self, c.instance_slot(self, extra_slot(c, self, I)), out);
        if (out.tag() == ResultTag::Empty)
            out.ok(c.builtin(CoreObject::None));
    }
}
void make_record(Attachment &c, Handle type, const NativeTime &native, ReturnSlot &out)
{
    const auto &t = native.value;
    const std::int64_t numbers[] = {std::int64_t(t.tm_year) + 1900,
                                    t.tm_mon + 1,
                                    t.tm_mday,
                                    t.tm_hour,
                                    t.tm_min,
                                    t.tm_sec,
                                    (t.tm_wday + 6) % 7,
                                    t.tm_yday + 1,
                                    t.tm_isdst};
    RootScope roots(c);
    std::vector<Value> items;
    ReturnSlot value(c);
    for (const auto number : numbers) {
        make_int(c, number, value);
        items.push_back(roots.root(value.value()).get(c));
    }
    c.allocate_instance<TupleObject>(type, out, std::move(items));
    const auto record = roots.root(out.value());
    make_str(c, decode_filename(SourceBuffer("", native.zone)), value);
    c.store(record.get(c), c.instance_slot(record.get(c), extra_slot(c, record.get(c), 9)),
            value.value());
    make_int(c, t.tm_gmtoff, value);
    c.store(record.get(c), c.instance_slot(record.get(c), extra_slot(c, record.get(c), 10)),
            value.value());
}
void record_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto type = calendar_type(c, function, roots), sequence = roots.root(args[1]),
               dictionary = roots.root(args[2]);
    if (args[0] != type.get(c)) {
        raise_error(c, CoreObject::TypeErrorType, U"struct_time requires its own type", out);
        return;
    }
    ReturnSlot converted(c);
    std::vector<Value> items;
    if (c.type_of(sequence) == c.builtin(CoreObject::TupleType)) {
        for (auto item : c.payload<TupleObject>(sequence).items)
            items.push_back(roots.root(item).get(c));
    } else if (c.type_of(sequence) == c.builtin(CoreObject::ListType))
        list_snapshot(c, sequence, roots, items);
    else {
        get_iter(c, sequence, converted);
        if (failed(converted)) {
            if (is_subtype(c, c.type_of(converted.exception()),
                           c.builtin(CoreObject::TypeErrorType)))
                raise_error(c, CoreObject::TypeErrorType, U"constructor requires a sequence", out);
            else
                out.raised(converted.exception());
            return;
        }
        const auto iterator = roots.root(converted.value());
        const std::array argument{iterator.get(c)};
        call(c, roots.root(c.builtin(CoreObject::ListType)), {argument, {}}, converted);
        if (failed(converted)) {
            out.raised(converted.exception());
            return;
        }
        list_snapshot(c, roots.root(converted.value()), roots, items);
    }
    if (!c.has_layout<DictObject>(dictionary.get(c)) ||
        !is_subtype(c, c.type_of(dictionary), c.builtin(CoreObject::DictType))) {
        raise_error(c, CoreObject::TypeErrorType,
                    U"time.struct_time() takes a dict as second arg, if any", out);
        return;
    }
    if (items.size() < 9 || items.size() > 11) {
        const auto count = std::to_string(items.size());
        raise_error(c, CoreObject::TypeErrorType,
                    std::u32string(U"time.struct_time() takes an at ") +
                        (items.size() < 9 ? U"least 9-sequence (" : U"most 11-sequence (") +
                        std::u32string(count.begin(), count.end()) + U"-sequence given)",
                    out);
        return;
    }
    c.allocate_instance<TupleObject>(type, out,
                                     std::vector<Value>(items.begin(), items.begin() + 9));
    const auto record = roots.root(out.value());
    for (std::size_t i = 9; i < 11; ++i) {
        if (i < items.size())
            converted.ok(items[i]);
        else if (const auto found = name_lookup(c, dictionary.get(c), fields[i]))
            converted.ok(*found);
        else
            converted.ok(c.builtin(CoreObject::None));
        c.store(record.get(c), c.instance_slot(record.get(c), extra_slot(c, record.get(c), i)),
                converted.value());
    }
    out.ok(record.get(c));
}
bool record_receiver(Attachment &c, Handle function, Handle self, ReturnSlot &out)
{
    if (c.type_of(self) == c.payload<NativeFunctionObject>(function).captures[0])
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a time.struct_time", out);
    return false;
}
void record_repr(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    if (!record_receiver(c, function, self, out))
        return;
    std::u32string text = U"time.struct_time(";
    for (std::size_t i = 0; i < 9; ++i) {
        if (i)
            text += U", ";
        text += fields[i];
        text += U"=";
        repr(c, roots.root(c.payload<TupleObject>(self).items[i]), out);
        if (failed(out))
            return;
        text += c.payload<StrObject>(out.value()).value;
    }
    make_str(c, text + U")", out);
}
void record_reduce(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    if (!record_receiver(c, function, self, out))
        return;
    ReturnSlot sequence(c), dictionary(c), key(c), value(c);
    cxxpython::core::make_tuple(c, c.payload<TupleObject>(self).items, sequence);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), dictionary);
    for (std::size_t i = 0; i < 2; ++i) {
        c.load(self.get(c), c.instance_slot(self.get(c), extra_slot(c, self.get(c), i + 9)), value);
        const auto item =
            roots.root(value.tag() == ResultTag::Ok ? value.value() : c.builtin(CoreObject::None));
        make_str(c, fields[i + 9], key);
        dict_store(c, roots.root(dictionary.value()), roots.root(key.value()), item, out);
        if (failed(out))
            return;
    }
    const std::array arguments{sequence.value(), dictionary.value()};
    cxxpython::core::make_tuple(c, arguments, value);
    const std::array result{c.type_of(self), value.value()};
    cxxpython::core::make_tuple(c, result, out);
}
bool c_integer(Attachment &c, Handle input, int &result, ReturnSlot &out)
{
    index(c, input, out);
    if (failed(out))
        return false;
    std::int64_t number;
    if (!int_fits_signed(c, out.value(), number)) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C long",
                    out);
        return false;
    }
    if (number > INT_MAX || number < INT_MIN) {
        raise_error(c, CoreObject::OverflowErrorType,
                    number > INT_MAX ? U"signed integer is greater than maximum"
                                     : U"signed integer is less than minimum",
                    out);
        return false;
    }
    result = static_cast<int>(number);
    return true;
}
bool parse_tuple(Attachment &c, Handle input, Handle type, std::u32string_view method, bool checked,
                 NativeTime &result, ReturnSlot &out)
{
    if (!c.has_layout<TupleObject>(input.get(c)) ||
        !is_subtype(c, c.type_of(input), c.builtin(CoreObject::TupleType))) {
        raise_error(c, CoreObject::TypeErrorType, U"Tuple or struct_time argument required", out);
        return false;
    }
    const auto &items = c.payload<TupleObject>(input).items;
    if (items.size() != 9) {
        raise_error(c, CoreObject::TypeErrorType,
                    std::u32string(method) + U"(): illegal time tuple argument", out);
        return false;
    }
    RootScope roots(c);
    std::array<int, 9> n;
    for (std::size_t i = 0; i < n.size(); ++i)
        if (!c_integer(c, roots.root(items[i]), n[i], out))
            return false;
    auto invalid = [&](const char32_t *message) {
        raise_error(c, CoreObject::ValueErrorType, message, out);
        return false;
    };
    if (n[0] < INT_MIN + 1900) {
        raise_error(c, CoreObject::OverflowErrorType, U"year out of range", out);
        return false;
    }
    if (checked) {
        if (n[1] < 0 || n[1] > 12)
            return invalid(U"month out of range");
        if (n[2] < 0 || n[2] > 31)
            return invalid(U"day of month out of range");
        if (n[3] < 0 || n[3] > 23)
            return invalid(U"hour out of range");
        if (n[4] < 0 || n[4] > 59)
            return invalid(U"minute out of range");
        if (n[5] < 0 || n[5] > 61)
            return invalid(U"seconds out of range");
        if (n[7] < 0 || n[7] > 366)
            return invalid(U"day of year out of range");
        n[1] = std::max(n[1], 1);
        n[2] = std::max(n[2], 1);
        n[7] = std::max(n[7], 1);
    }
    if (n[1] == INT_MIN || n[7] == INT_MIN) {
        raise_error(c, CoreObject::OverflowErrorType, U"time tuple field out of range", out);
        return false;
    }
    auto &t = result.value;
    t.tm_year = n[0] - 1900;
    t.tm_mon = n[1] - 1;
    t.tm_mday = n[2];
    t.tm_hour = n[3];
    t.tm_min = n[4];
    t.tm_sec = n[5];
    t.tm_wday = static_cast<int>((std::int64_t(n[6]) + 1) % 7);
    t.tm_yday = n[7] - 1;
    t.tm_isdst = n[8];
    if (checked && t.tm_wday < 0)
        return invalid(U"day of week out of range");
    if (c.type_of(input) == type.get(c)) {
        ReturnSlot extra(c), encoded(c), errors(c);
        c.load(input.get(c), c.instance_slot(input.get(c), extra_slot(c, input.get(c), 9)), extra);
        if (extra.tag() == ResultTag::Ok && extra.value() != c.builtin(CoreObject::None)) {
            const auto zone = roots.root(extra.value());
            if (!c.has_layout<StrObject>(zone.get(c)) ||
                !is_subtype(c, c.type_of(zone), c.builtin(CoreObject::StrType))) {
                raise_error(c, CoreObject::TypeErrorType,
                            U"bad argument type for built-in operation", out);
                return false;
            }
            make_str(c, U"strict", errors);
            encode_primitive(c, zone, TextEncoding::Utf8, roots.root(errors.value()), encoded);
            if (failed(encoded)) {
                out.raised(encoded.exception());
                return false;
            }
            const auto bytes = roots.root(c.payload<TupleObject>(encoded.value()).items[0]);
            result.zone = c.payload<BytesObject>(bytes).value;
            result.has_zone = true;
        }
        c.load(input.get(c), c.instance_slot(input.get(c), extra_slot(c, input.get(c), 10)), extra);
        if (extra.tag() == ResultTag::Ok && extra.value() != c.builtin(CoreObject::None)) {
            index(c, roots.root(extra.value()), encoded);
            if (failed(encoded)) {
                out.raised(encoded.exception());
                return false;
            }
            std::int64_t offset;
            if (!int_fits_signed(c, encoded.value(), offset) || offset < LONG_MIN ||
                offset > LONG_MAX) {
                raise_error(c, CoreObject::OverflowErrorType,
                            U"Python int too large to convert to C long", out);
                return false;
            }
            t.tm_gmtoff = static_cast<long>(offset);
        }
    }
    return true;
}
template <bool Utc>
void convert_time(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    time_t seconds;
    NativeTime native;
    if (timestamp(c, roots.root(args[0]), seconds, out) &&
        native_time(c, seconds, Utc, native, out))
        make_record(c, calendar_type(c, function, roots), native, out);
}
bool tuple_or_current(Attachment &c, Handle function, Value input, std::u32string_view method,
                      NativeTime &native, ReturnSlot &out)
{
    RootScope roots(c);
    const auto &captures = c.payload<NativeFunctionObject>(function).captures;
    if (input == captures[1])
        return native_time(c, ::time(nullptr), false, native, out);
    return parse_tuple(c, roots.root(input), roots.root(captures[0]), method, true, native, out);
}
void format_time(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto format = roots.root(args[0]);
    if (!c.has_layout<StrObject>(format.get(c)) ||
        !is_subtype(c, c.type_of(format), c.builtin(CoreObject::StrType))) {
        const auto name = format.get(c) == c.builtin(CoreObject::None)
                              ? U"None"
                              : diagnostic_type_name(c, roots.root(c.type_of(format)));
        raise_error(c, CoreObject::TypeErrorType,
                    U"strftime() argument 1 must be str, not " + std::u32string(name), out);
        return;
    }
    NativeTime native;
    if (!tuple_or_current(c, function, args[1], U"strftime", native, out))
        return;
    native.value.tm_isdst = std::clamp(native.value.tm_isdst, -1, 1);
    const auto input = c.payload<StrObject>(format).value;
    std::u32string result;
    std::size_t i = 0;
    while (i < input.size()) {
        std::string part;
        for (; i < input.size() && input[i] && input[i] < 128; ++i)
            part += static_cast<char>(input[i]);
        if (!part.empty()) {
            std::string rendered;
            {
                NativeWait wait(c);
                std::lock_guard lock(process_environment_lock());
                native.value.tm_zone = native.has_zone ? native.zone.c_str() : nullptr;
                std::vector<char> buffer(1024);
                for (;;) {
                    const auto size =
                        ::strftime(buffer.data(), buffer.size(), part.c_str(), &native.value);
                    if (size || buffer.size() / 256 >= part.size()) {
                        rendered.assign(buffer.data(), size);
                        break;
                    }
                    if (buffer.size() > buffer.max_size() / 2)
                        throw std::bad_alloc();
                    buffer.resize(buffer.size() * 2);
                }
            }
            result += decode_filename(SourceBuffer("", rendered));
        }
        const auto start = i;
        for (; i < input.size() && input[i] != U'%'; ++i) {
        }
        result.append(input, start, i - start);
        c.poll();
    }
    make_str(c, std::move(result), out);
}
void ascii_time(Attachment &c, const NativeTime &native, ReturnSlot &out)
{
    constexpr const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto &t = native.value;
    std::array<char, 96> buffer;
    const auto size = std::snprintf(buffer.data(), buffer.size(), "%s %s%3d %02d:%02d:%02d %lld",
                                    days[t.tm_wday], months[t.tm_mon], t.tm_mday, t.tm_hour,
                                    t.tm_min, t.tm_sec, static_cast<long long>(t.tm_year) + 1900);
    make_str(c,
             decode_filename(
                 SourceBuffer("", std::string(buffer.data(), static_cast<std::size_t>(size)))),
             out);
}
void asctime_entry(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    NativeTime native;
    if (tuple_or_current(c, function, args[0], U"asctime", native, out))
        ascii_time(c, native, out);
}
void ctime_entry(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    time_t seconds;
    NativeTime native;
    if (timestamp(c, roots.root(args[0]), seconds, out) &&
        native_time(c, seconds, false, native, out))
        ascii_time(c, native, out);
}
void mktime_entry(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    NativeTime native;
    if (!parse_tuple(c, roots.root(args[0]), calendar_type(c, function, roots), U"mktime", false,
                     native, out))
        return;
    native.value.tm_wday = -1;
    time_t seconds;
    {
        NativeWait wait(c);
        std::lock_guard lock(process_environment_lock());
        seconds = ::mktime(&native.value);
    }
    if (seconds == time_t(-1) && native.value.tm_wday == -1)
        raise_error(c, CoreObject::OverflowErrorType, U"mktime argument out of range", out);
    else
        make_float(c, static_cast<double>(seconds), out);
}
void timezone_fields(Attachment &c, Handle module, ReturnSlot &out)
{
    std::array<std::string, 2> names;
    std::array<long, 2> offsets{};
    int error = 0;
    {
        NativeWait wait(c);
        std::lock_guard lock(process_environment_lock());
        ::tzset();
        constexpr time_t year = (365 * 24 + 6) * 3600;
        auto seconds = (::time(nullptr) / year) * year;
        for (std::size_t i = 0; i < 2; ++i, seconds += year / 2) {
            tm value{};
            if (!::localtime_r(&seconds, &value)) {
                error = errno ? errno : EINVAL;
                break;
            }
            offsets[i] = -value.tm_gmtoff;
            if (value.tm_zone)
                names[i] = value.tm_zone;
        }
    }
    if (error) {
        raise_os_error(c, error, out);
        return;
    }
    if (offsets[0] < offsets[1]) {
        std::swap(offsets[0], offsets[1]);
        std::swap(names[0], names[1]);
    }
    RootScope roots(c);
    const auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c), value(c);
    auto put = [&](const char32_t *name) {
        const auto item = roots.root(value.value());
        make_str(c, name, key);
        dict_store(c, dictionary, roots.root(key.value()), item, out);
        return !failed(out);
    };
    make_int(c, offsets[0], value);
    if (!put(U"timezone"))
        return;
    make_int(c, offsets[1], value);
    if (!put(U"altzone"))
        return;
    make_int(c, offsets[0] != offsets[1], value);
    if (!put(U"daylight"))
        return;
    std::vector<Value> items;
    for (const auto &name : names) {
        make_str(c, decode_filename(SourceBuffer("", name)), value);
        items.push_back(roots.root(value.value()).get(c));
    }
    cxxpython::core::make_tuple(c, items, value);
    if (put(U"tzname"))
        out.ok(c.builtin(CoreObject::None));
}
void tzset_entry(Attachment &c, Handle function, std::span<const Value>, ReturnSlot &out)
{
    RootScope roots(c);
    timezone_fields(c, roots.root(c.payload<NativeFunctionObject>(function).captures[0]), out);
}

void strptime_entry(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    const auto text = roots.root(args[0]);
    const auto format = roots.root(args[1]);
    import_module(c, U"_strptime", out);
    if (failed(out))
        return;
    const auto module = roots.root(out.value());
    make_str(c, U"_strptime_time", out);
    get_attribute(c, module, roots.root(out.value()), out);
    if (failed(out))
        return;
    const std::array values{text.get(c), format.get(c)};
    call(c, roots.root(out.value()), {values, {}}, out);
}
} // namespace

void initialize_time_calendar(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dictionary = roots.root(module_dict(c, module.get(c)));
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    const Handle bases[] = {roots.root(c.builtin(CoreObject::TupleType))};
    TypeSpec spec{U"struct_time", bases};
    spec.module = U"time";
    spec.slots = std::vector<std::u32string>{U"tm_zone", U"tm_gmtoff"};
    spec.subclassable = false;
    ReturnSlot value(c), key(c);
    create_native_type(c, NativeTypeSpec{spec, LayoutDescriptor::of<TupleObject>()}, value);
    if (failed(value)) {
        out.raised(value.exception());
        return;
    }
    const auto type = roots.root(value.value());
    const NativeGetter getters[] = {record_field<0>, record_field<1>, record_field<2>,
                                    record_field<3>, record_field<4>, record_field<5>,
                                    record_field<6>, record_field<7>, record_field<8>,
                                    record_field<9>, record_field<10>};
    for (std::size_t i = 0; i < 11; ++i)
        install_native_member(c, core, type, fields[i], getters[i]);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), value);
    const auto defaults = roots.root(value.value());
    SignatureLayout constructor;
    constructor.parameters = {{U"cls", ParameterKind::PositionalOnly},
                              {U"sequence"},
                              {U"dict", ParameterKind::PositionalOrKeyword, defaults.get(c)}};
    install_native_attribute(c, core, type, U"__new__", constructor, record_new, {type.get(c)});
    auto attributes = roots.root(*TypeView(c, type).dictionary());
    const auto function = roots.root(*name_lookup(c, attributes.get(c), U"__new__"));
    c.allocate_instance<StaticMethodObject>(roots.root(c.builtin(CoreObject::StaticMethodType)),
                                            value, function.get(c));
    make_str(c, U"__new__", key);
    name_store(c, attributes.get(c), key.value(), value.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, type, U"__repr__", unary, record_repr, {type.get(c)});
    install_native_attribute(c, core, type, U"__reduce__", unary, record_reduce, {type.get(c)});
    for (const auto &[name, number] :
         {std::pair{U"n_fields", 11}, {U"n_sequence_fields", 9}, {U"n_unnamed_fields", 0}}) {
        make_int(c, number, value);
        const auto item = roots.root(value.value());
        make_str(c, name, key);
        name_store(c, attributes.get(c), key.value(), item.get(c));
    }
    std::vector<Value> match;
    for (std::size_t i = 0; i < 9; ++i) {
        make_str(c, fields[i], value);
        match.push_back(roots.root(value.value()).get(c));
    }
    cxxpython::core::make_tuple(c, match, value);
    const auto match_args = roots.root(value.value());
    make_str(c, U"__match_args__", key);
    name_store(c, attributes.get(c), key.value(), match_args.get(c));
    make_str(c, U"struct_time", key);
    dict_store(c, dictionary, roots.root(key.value()), type, out);
    if (failed(out))
        return;
    make_list(c, {}, value);
    const auto missing = roots.root(value.value());
    const std::array captures{type.get(c), missing.get(c)};
    auto install = [&](const char *name, const char32_t *wide, SignatureLayout signature,
                       NativeEntry entry, std::span<const Value> captured) {
        make_native_function(c, name, signature, entry, captured, value,
                             NativeFunctionBinding::Function);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        const auto item = roots.root(value.value());
        make_str(c, wide, key);
        dict_store(c, dictionary, roots.root(key.value()), item, out);
        return !failed(out);
    };
    SignatureLayout seconds;
    seconds.parameters = {{U"seconds", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)}};
    if (!install("gmtime", U"gmtime", seconds, convert_time<true>, captures) ||
        !install("localtime", U"localtime", seconds, convert_time<false>, captures) ||
        !install("ctime", U"ctime", seconds, ctime_entry, captures))
        return;
    SignatureLayout tuple;
    tuple.parameters = {{U"tuple", ParameterKind::PositionalOnly, missing.get(c)}};
    if (!install("asctime", U"asctime", tuple, asctime_entry, captures))
        return;
    tuple.parameters[0].default_value.reset();
    if (!install("mktime", U"mktime", tuple, mktime_entry, captures))
        return;
    SignatureLayout format;
    format.parameters = {{U"format", ParameterKind::PositionalOnly},
                         {U"tuple", ParameterKind::PositionalOnly, missing.get(c)}};
    if (!install("strftime", U"strftime", format, format_time, captures))
        return;
    make_str(c, U"%a %b %d %H:%M:%S %Y", value);
    const auto default_format = roots.root(value.value());
    SignatureLayout parse;
    parse.parameters = {{U"string", ParameterKind::PositionalOnly},
                        {U"format", ParameterKind::PositionalOnly, default_format.get(c)}};
    if (!install("strptime", U"strptime", parse, strptime_entry, {}))
        return;
    const std::array timezone_capture{module.get(c)};
    if (!install("tzset", U"tzset", {}, tzset_entry, timezone_capture))
        return;
    make_int(c, 11, value);
    const auto count = roots.root(value.value());
    make_str(c, U"_STRUCT_TM_ITEMS", key);
    dict_store(c, dictionary, roots.root(key.value()), count, out);
    if (!failed(out))
        timezone_fields(c, module, out);
}
} // namespace cxxpython::core::detail
