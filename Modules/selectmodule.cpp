// Adapted from CPython 3.12.14 Modules/selectmodule.c and Objects/fileobject.c.
// PSF license: doc/licenses/python-3.12.14.txt.
#include "internal/selectmodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/errors.hpp"
#include "internal/pytime.hpp"
#include "internal/signalmodule.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <limits>
#include <sys/select.h>

namespace cxxpython::core::detail {
namespace {
bool file_descriptor(Attachment &c, Handle object, int &fd, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot value(c), name(c), method(c);
    value.ok(object.get(c));
    if (!is_int(c, value.value())) {
        make_str(c, U"fileno", name);
        get_attribute(c, object, roots.root(name.value()), method);
        if (method.tag() == ResultTag::Raised) {
            if (is_subtype(c, c.type_of(method.exception()),
                           c.builtin(CoreObject::AttributeErrorType)))
                raise_error(c, CoreObject::TypeErrorType,
                            U"argument must be an int, or have a fileno() method.", out);
            else
                out.raised(method.exception());
            return false;
        }
        call(c, roots.root(method.value()), {}, value);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return false;
        }
        if (!is_int(c, value.value())) {
            raise_error(c, CoreObject::TypeErrorType, U"fileno() returned a non-integer", out);
            return false;
        }
    }
    std::int64_t number;
    if (!int_fits_signed(c, value.value(), number) || number < INT_MIN || number > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    if (number < 0) {
        auto text = std::to_string(number);
        raise_error(c, CoreObject::ValueErrorType,
                    U"file descriptor cannot be a negative integer (" +
                        std::u32string(text.begin(), text.end()) + U")",
                    out);
        return false;
    }
    fd = static_cast<int>(number);
    return true;
}

bool timeout_value(Attachment &c, Handle value, std::int64_t &nanos, ReturnSlot &out)
{
    ReturnSlot converted(c);
    if (!seconds_to_nanoseconds(c, value, nanos, converted)) {
        if (is_subtype(c, c.type_of(converted.exception()), c.builtin(CoreObject::TypeErrorType)))
            raise_error(c, CoreObject::TypeErrorType, U"timeout must be a float or None", out);
        else
            out.raised(converted.exception());
        return false;
    }
    if (nanos < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"timeout must be non-negative", out);
        return false;
    }
    const auto micros = nanos / 1000 + (nanos % 1000 != 0);
    if (micros / 1000000 > std::numeric_limits<time_t>::max()) {
        raise_error(c, CoreObject::OverflowErrorType, U"timestamp out of range for platform time_t",
                    out);
        return false;
    }
    return true;
}

void select_entry(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::int64_t timeout = 0;
    const bool finite = args[3] != c.builtin(CoreObject::None);
    if (finite && !timeout_value(c, roots.root(args[3]), timeout, out))
        return;
    std::array<fd_set, 3> requested{}, ready{};
    std::array<std::vector<int>, 3> descriptors;
    std::array<std::vector<Value>, 3> objects;
    int maximum = -1;
    for (std::size_t which = 0; which < 3; ++which) {
        FD_ZERO(&requested[which]);
        ReturnSlot sequence(c);
        const std::array input{args[which]};
        call(c, roots.root(c.builtin(CoreObject::TupleType)), {input, {}}, sequence);
        if (sequence.tag() == ResultTag::Raised) {
            out.raised(sequence.exception());
            return;
        }
        const auto snapshot = roots.root(sequence.value());
        objects[which] = c.payload<TupleObject>(snapshot).items;
        for (Value value : objects[which]) {
            int fd;
            if (!file_descriptor(c, roots.root(value), fd, out))
                return;
            if (fd >= FD_SETSIZE) {
                raise_error(c, CoreObject::ValueErrorType,
                            U"filedescriptor out of range in select()", out);
                return;
            }
            if (descriptors[which].size() >= FD_SETSIZE) {
                raise_error(c, CoreObject::ValueErrorType, U"too many file descriptors in select()",
                            out);
                return;
            }
            FD_SET(fd, &requested[which]);
            descriptors[which].push_back(fd);
            maximum = std::max(maximum, fd);
        }
    }
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        timeval interval{};
        if (finite) {
            // Recompute after signals; never extend the original monotonic deadline.
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            const auto left = std::max<std::int64_t>(timeout - elapsed, 0);
            const auto micros = left / 1000 + (left % 1000 != 0);
            interval.tv_sec = static_cast<time_t>(micros / 1000000);
            interval.tv_usec = static_cast<suseconds_t>(micros % 1000000);
        }
        ready = requested;
        int result, error;
        {
            NativeWait wait(c);
            result = ::select(maximum + 1, &ready[0], &ready[1], &ready[2],
                              finite ? &interval : nullptr);
            error = result < 0 ? errno : 0;
        }
        if (!error)
            break;
        if (error != EINTR) {
            raise_os_error(c, error, out);
            return;
        }
        if (check_signals(c, out))
            return;
    }
    std::vector<Value> lists;
    for (std::size_t which = 0; which < 3; ++which) {
        std::vector<Value> selected;
        for (std::size_t i = 0; i < descriptors[which].size(); ++i)
            if (FD_ISSET(descriptors[which][i], &ready[which]))
                selected.push_back(objects[which][i]);
        make_list(c, selected, out);
        if (out.tag() == ResultTag::Raised)
            return;
        lists.push_back(roots.root(out.value()).get(c));
    }
    c.allocate_instance<TupleObject>(roots.root(c.builtin(CoreObject::TupleType)), out,
                                     std::move(lists));
}
} // namespace

void initialize_select(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    const auto dictionary = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c), function(c);
    SignatureLayout signature;
    signature.parameters = {
        {U"rlist", ParameterKind::PositionalOnly},
        {U"wlist", ParameterKind::PositionalOnly},
        {U"xlist", ParameterKind::PositionalOnly},
        {U"timeout", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)}};
    make_native_function(c, "select", std::move(signature), select_entry, {}, function,
                         NativeFunctionBinding::Function);
    if (function.tag() == ResultTag::Raised) {
        out.raised(function.exception());
        return;
    }
    make_str(c, U"select", key);
    dict_store(c, dictionary, roots.root(key.value()), roots.root(function.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    make_str(c, U"error", key);
    dict_store(c, dictionary, roots.root(key.value()),
               roots.root(c.builtin(CoreObject::OSErrorType)), out);
}
} // namespace cxxpython::core::detail
