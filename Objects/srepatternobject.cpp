// Adapted from CPython 3.12.14 Modules/_sre/sre.c (PSF/CNRI license).
#include "../Modules/_sre/engine.h"
#include "../Modules/_sre/sre_constants.h"
#include "cxxpython/calliterobject.hpp"
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/sreobject.hpp"
#include "internal/errors.hpp"
#include "internal/memoryformat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <exception>
#include <limits>

namespace cxxpython::core {
namespace {
template <class Read>
decltype(auto) storage_read(Attachment &c, ByteArrayObject::Storage &s, Read read)
{
    for (;;) {
        std::unique_lock lock(s.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return read();
        }
        NativeWait wait(c);
        s.mutex.lock();
        s.mutex.unlock();
    }
}
bool integer(Attachment &c, Handle value, std::ptrdiff_t &result, ReturnSlot &out)
{
    ReturnSlot number(c);
    index(c, value, number);
    if (number.tag() == ResultTag::Raised) {
        out.raised(number.exception());
        return false;
    }
    std::int64_t n;
    if (!int_fits_signed(c, number.value(), n) || n < std::numeric_limits<std::ptrdiff_t>::min() ||
        n > std::numeric_limits<std::ptrdiff_t>::max()) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return false;
    }
    result = n;
    return true;
}
struct Subject {
    std::vector<std::uint32_t> text;
    bool bytes = false;
    detail::BufferExport buffer;
    std::optional<std::size_t> version;
    explicit Subject(Attachment &c) : buffer(c) {}
};
void subject_error(Attachment &c, Handle source, ReturnSlot &out)
{
    RootScope roots(c);
    const auto name = detail::diagnostic_type_name(c, roots.root(c.type_of(source))).substr(0, 200);
    raise_error(c, CoreObject::TypeErrorType,
                U"expected string or bytes-like object, got '" + name + U"'", out);
}
void snapshot_subject(Attachment &c, Subject &subject)
{
    const auto &buffer = subject.buffer;
    auto copy = [&](const std::string &data) {
        for (std::size_t i = 0; i < buffer.length; ++i)
            subject.text[i] = static_cast<unsigned char>(data[buffer.offset + i]);
    };
    if (auto storage = buffer.lease->storage) {
        subject.version = storage_read(c, *storage, [&] {
            if (subject.version != storage->version)
                copy(storage->bytes);
            return storage->version;
        });
    } else
        copy(c.payload<BytesObject>(buffer.owner.value()).value);
}
bool read_subject(Attachment &c, Handle source, Subject &subject, ReturnSlot &out)
{
    RootScope roots(c);
    source = roots.root(source.get(c));
    if (c.has_layout<StrObject>(source.get(c)) &&
        is_subtype(c, c.type_of(source), c.builtin(CoreObject::StrType))) {
        const auto &text = c.payload<StrObject>(source).value;
        subject.text.assign(text.begin(), text.end());
        return true;
    }
    subject.bytes = true;
    if (!detail::export_buffer(c, source, subject.buffer, out) ||
        (subject.buffer.count != 1 &&
         subject.buffer.stride !=
             static_cast<std::int64_t>(detail::memory_format_size(subject.buffer.format.back())))) {
        subject_error(c, source, out);
        return false;
    }
    subject.text.resize(subject.buffer.length);
    snapshot_subject(c, subject);
    return true;
}
void refresh_subject(Attachment &c, Subject &subject)
{
    // The held direct export survives release of the original subject wrapper.
    if (subject.buffer.lease && subject.buffer.lease->storage)
        snapshot_subject(c, subject);
}
bool compatible(Attachment &c, Handle pattern, const Subject &subject, ReturnSlot &out)
{
    const auto kind = c.payload<SrePatternObject>(pattern).kind;
    if (kind == SreStringKind::Any || (kind == SreStringKind::Bytes) == subject.bytes)
        return true;
    raise_error(c, CoreObject::TypeErrorType,
                subject.bytes ? U"cannot use a string pattern on a bytes-like object"
                              : U"cannot use a bytes pattern on a string-like object",
                out);
    return false;
}
struct PollContext {
    Attachment &context;
    std::exception_ptr failure;
};
int poll(void *opaque) noexcept
{
    auto &state = *static_cast<PollContext *>(opaque);
    try {
        state.context.poll();
        return 0;
    } catch (...) {
        state.failure = std::current_exception();
        return 1;
    }
}
struct Captures {
    std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> marks;
    int lastindex = -1;
};
bool execute(Attachment &c, Handle pattern, const Subject &subject, std::ptrdiff_t pos,
             std::ptrdiff_t end, bool search, bool full, bool advance, Captures &result,
             ReturnSlot &out)
{
    const auto &p = c.payload<SrePatternObject>(pattern);
    std::vector<std::ptrdiff_t> marks(2 * (p.groups + 1));
    PollContext state{c, {}};
    const int status = cxx_sre_run(p.code.data(), p.code.size(), subject.text.data(),
                                   subject.text.size(), p.groups, pos, end, search, full, advance,
                                   marks.data(), &result.lastindex, poll, &state);
    if (state.failure)
        std::rethrow_exception(state.failure);
    if (status < 0) {
        if (status == -9)
            throw std::bad_alloc();
        raise_error(c, CoreObject::RuntimeErrorType, U"internal error in regular expression engine",
                    out);
        return false;
    }
    if (!status) {
        out.ok(c.builtin(CoreObject::None));
        return false;
    }
    for (std::size_t i = 0; i < marks.size(); i += 2) {
        if (marks[i] >= 0 && (marks[i + 1] < marks[i] ||
                              static_cast<std::size_t>(marks[i + 1]) > subject.text.size())) {
            raise_error(c, CoreObject::SystemErrorType, U"invalid capturing group span", out);
            return false;
        }
        result.marks.emplace_back(marks[i], marks[i + 1]);
    }
    return true;
}
void match_object(Attachment &c, Handle pattern, Handle source, std::ptrdiff_t pos,
                  std::ptrdiff_t end, Captures captures, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<SreMatchObject>(roots.root(c.builtin(CoreObject::SreMatchType)), out,
                                        pattern.get(c), source.get(c), pos, end, captures.lastindex,
                                        std::move(captures.marks));
}
void slice(Attachment &c, const Subject &subject, std::ptrdiff_t begin, std::ptrdiff_t end,
           ReturnSlot &out)
{
    if (subject.bytes) {
        std::string text;
        text.reserve(end - begin);
        for (auto i = begin; i < end; ++i)
            text += static_cast<char>(subject.text[i]);
        make_bytes(c, std::move(text), out);
    } else {
        std::u32string text(subject.text.begin() + begin, subject.text.begin() + end);
        make_str(c, std::move(text), out);
    }
}
bool bounds(Attachment &c, std::span<const Value> args, Subject &subject, std::ptrdiff_t &pos,
            std::ptrdiff_t &end, ReturnSlot &out)
{
    RootScope roots(c);
    if (!integer(c, roots.root(args[2]), pos, out) || !integer(c, roots.root(args[3]), end, out) ||
        !read_subject(c, roots.root(args[1]), subject, out) ||
        !compatible(c, roots.root(args[0]), subject, out))
        return false;
    const auto length = static_cast<std::ptrdiff_t>(subject.text.size());
    pos = std::clamp(pos, std::ptrdiff_t{0}, length);
    end = std::clamp(end, std::ptrdiff_t{0}, length);
    return true;
}
template <int Mode>
void pattern_match(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto pattern = roots.root(args[0]), source = roots.root(args[1]);
    Subject subject(c);
    std::ptrdiff_t pos, end;
    if (!bounds(c, args, subject, pos, end, out))
        return;
    Captures captures;
    if (execute(c, pattern, subject, pos, end, Mode == 1, Mode == 2, false, captures, out))
        match_object(c, pattern, source, pos, end, std::move(captures), out);
}
void make_scanner(Attachment &c, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto pattern = roots.root(args[0]), source = roots.root(args[1]);
    Subject subject(c);
    std::ptrdiff_t pos, end;
    if (!bounds(c, args, subject, pos, end, out))
        return;
    c.allocate_instance<SreScannerObject>(
        roots.root(c.builtin(CoreObject::SreScannerType)), out, pattern.get(c), source.get(c), pos,
        end, subject.buffer.lease,
        subject.bytes ? subject.buffer.owner.value() : c.builtin(CoreObject::None),
        subject.bytes ? subject.buffer.exporter.value() : c.builtin(CoreObject::None),
        subject.buffer.offset, std::move(subject.text), subject.version);
}
template <bool Iterator>
void scanner(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    make_scanner(c, args, out);
    if constexpr (Iterator) {
        if (out.tag() == ResultTag::Raised)
            return;
        auto self = roots.root(out.value());
        ReturnSlot name(c);
        make_str(c, U"search", name);
        get_attribute(c, self, roots.root(name.value()), out);
        if (out.tag() == ResultTag::Ok)
            detail::make_call_iterator(c, roots.root(out.value()),
                                       roots.root(c.builtin(CoreObject::None)), out);
    }
}
template <bool Search>
void scanner_next(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &scanner = c.payload<SreScannerObject>(self);
    if (scanner.running.test_and_set(std::memory_order_acquire)) {
        raise_error(c, CoreObject::ValueErrorType, U"scanner already executing", out);
        return;
    }
    struct Release {
        SreScannerObject &scanner;
        ~Release()
        {
            scanner.running.clear(std::memory_order_release);
        }
    } release{scanner};
    if (scanner.exhausted) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    auto pattern = roots.root(scanner.pattern), source = roots.root(scanner.string);
    Subject subject(c);
    subject.text = std::move(scanner.text);
    subject.bytes = scanner.lease != nullptr;
    subject.buffer.owner.ok(scanner.buffer_owner);
    subject.buffer.exporter.ok(scanner.buffer_exporter);
    subject.buffer.lease = scanner.lease;
    subject.buffer.offset = scanner.offset;
    subject.buffer.length = subject.text.size();
    subject.version = scanner.version;
    struct Restore {
        SreScannerObject &scanner;
        Subject &subject;
        ~Restore()
        {
            scanner.text = std::move(subject.text);
            scanner.version = subject.version;
        }
    } restore{scanner, subject};
    refresh_subject(c, subject);
    Captures captures;
    if (!execute(c, pattern, subject, scanner.cursor, scanner.endpos, Search, false,
                 scanner.must_advance, captures, out)) {
        if (out.tag() == ResultTag::Ok)
            scanner.exhausted = true;
        return;
    }
    auto mark = captures.marks[0];
    scanner.cursor = mark.second;
    scanner.must_advance = mark.first == mark.second;
    match_object(c, pattern, source, scanner.pos, scanner.endpos, std::move(captures), out);
}
template <bool Split>
void scan_list(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto pattern = roots.root(args[0]), source = roots.root(args[1]);
    Subject subject(c);
    std::ptrdiff_t pos = 0, end, limit = 0;
    if constexpr (Split) {
        if (!integer(c, roots.root(args[2]), limit, out) ||
            !read_subject(c, source, subject, out) || !compatible(c, pattern, subject, out))
            return;
        end = static_cast<std::ptrdiff_t>(subject.text.size());
    } else if (!bounds(c, args, subject, pos, end, out))
        return;
    std::vector<Value> values;
    bool advance = false;
    std::ptrdiff_t previous = pos, count = 0;
    while (pos <= end && (!Split || !limit || count < limit)) {
        Captures captures;
        if (!execute(c, pattern, subject, pos, end, true, false, advance, captures, out)) {
            if (out.tag() == ResultTag::Raised)
                return;
            break;
        }
        const auto mark = captures.marks[0];
        auto append = [&](Value value) { values.push_back(roots.root(value).get(c)); };
        if constexpr (Split) {
            slice(c, subject, previous, mark.first, out);
            append(out.value());
        }
        const auto groups = captures.marks.size() - 1;
        if constexpr (!Split) {
            if (!groups) {
                slice(c, subject, mark.first, mark.second, out);
                append(out.value());
            }
        }
        std::vector<Value> group_values;
        for (std::size_t i = 1; i < captures.marks.size(); ++i) {
            auto group = captures.marks[i];
            if (group.first < 0 && Split)
                out.ok(c.builtin(CoreObject::None));
            else
                slice(c, subject, std::max(group.first, std::ptrdiff_t{0}),
                      std::max(group.second, std::ptrdiff_t{0}), out);
            auto value = roots.root(out.value()).get(c);
            if constexpr (Split)
                append(value);
            else
                group_values.push_back(value);
        }
        if constexpr (!Split) {
            if (groups == 1)
                append(group_values[0]);
            else if (groups) {
                cxxpython::core::make_tuple(c, group_values, out);
                append(out.value());
            }
        }
        previous = pos = mark.second;
        advance = mark.first == mark.second;
        ++count;
        c.poll();
    }
    if constexpr (Split) {
        slice(c, subject, previous, end, out);
        values.push_back(roots.root(out.value()).get(c));
    }
    make_list(c, values, out);
}
void join(Attachment &c, bool bytes, std::span<const Value> pieces, ReturnSlot &out)
{
    RootScope roots(c);
    for (auto value : pieces)
        roots.root(value);
    ReturnSlot list(c), name(c), empty(c);
    make_list(c, pieces, list);
    if (bytes)
        make_bytes(c, {}, empty);
    else
        make_str(c, U"", empty);
    make_str(c, U"join", name);
    get_attribute(c, roots.root(empty.value()), roots.root(name.value()), out);
    if (out.tag() == ResultTag::Ok) {
        const std::array args{list.value()};
        call(c, roots.root(out.value()), {args, {}}, out);
    }
}
void compile_template(Attachment &c, Handle pattern, Handle replacement, ReturnSlot &out)
{
    RootScope roots(c);
    pattern = roots.root(pattern.get(c));
    replacement = roots.root(replacement.get(c));
    import_module(c, U"re", out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto module = roots.root(out.value());
    ReturnSlot key(c);
    make_str(c, U"_compile_template", key);
    get_attribute(c, module, roots.root(key.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto function = roots.root(out.value());
    const std::array args{pattern.get(c), replacement.get(c)};
    call(c, function, {args, {}}, out);
    if (out.tag() == ResultTag::Raised &&
        is_subtype(c, c.type_of(out.exception()), c.builtin(CoreObject::TypeErrorType))) {
        ReturnSlot converted(c);
        if (c.has_layout<StrObject>(replacement.get(c)) &&
            c.type_of(replacement) != c.builtin(CoreObject::StrType))
            make_str(c, c.payload<StrObject>(replacement).value, converted);
        else if (!is_bytes(c, replacement.get(c))) {
            std::string bytes;
            ReturnSlot error(c);
            if (detail::buffer_bytes(c, replacement, bytes, error))
                make_bytes(c, std::move(bytes), converted);
        }
        if (converted.tag() == ResultTag::Ok) {
            const std::array retry{pattern.get(c), converted.value()};
            call(c, function, {retry, {}}, out);
        }
    }
    if (out.tag() == ResultTag::Ok &&
        c.type_of(out.value()) != c.builtin(CoreObject::SreTemplateType))
        raise_error(c, CoreObject::RuntimeErrorType,
                    U"invalid result from replacement template compiler", out);
}
void expand_compiled(Attachment &c, Handle match, Handle compiled, ReturnSlot &out)
{
    RootScope roots(c);
    match = roots.root(match.get(c));
    compiled = roots.root(compiled.get(c));
    auto tuple = roots.root(c.payload<SreTemplateObject>(compiled).items);
    const auto &items = c.payload<TupleObject>(tuple).items;
    std::vector<Value> pieces{items[0]};
    for (std::size_t i = 1; i < items.size(); i += 2) {
        std::ptrdiff_t group;
        if (!integer(c, roots.root(items[i]), group, out))
            return;
        if (group < 0 ||
            static_cast<std::size_t>(group) >= c.payload<SreMatchObject>(match).marks.size()) {
            raise_error(c, CoreObject::IndexErrorType, U"no such group", out);
            return;
        }
        detail::sre_group_slice(c, match, group, roots.root(c.builtin(CoreObject::None)), out);
        if (out.tag() == ResultTag::Raised)
            return;
        if (out.value() != c.builtin(CoreObject::None))
            pieces.push_back(roots.root(out.value()).get(c));
        pieces.push_back(items[i + 1]);
    }
    join(c, !c.has_layout<StrObject>(items[0]), pieces, out);
}
template <bool Count>
void substitute(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto pattern = roots.root(args[0]), replacement = roots.root(args[1]),
         source = roots.root(args[2]);
    std::ptrdiff_t limit;
    if (!integer(c, roots.root(args[3]), limit, out))
        return;
    const auto type = c.type_of(replacement);
    const bool callable = type == c.builtin(CoreObject::FunctionType) ||
                          type == c.builtin(CoreObject::NativeFunctionType) ||
                          type == c.builtin(CoreObject::BoundMethodType) ||
                          type == c.builtin(CoreObject::MethodDescriptorType) ||
                          detail::type_attribute(c, type, U"__call__").has_value();
    bool literal = false;
    if (!callable) {
        if (c.has_layout<StrObject>(replacement.get(c)))
            literal = c.payload<StrObject>(replacement).value.find(U'\\') == std::u32string::npos;
        else {
            std::string bytes;
            ReturnSlot ignored(c);
            literal = detail::buffer_bytes(c, replacement, bytes, ignored) &&
                      bytes.find('\\') == std::string::npos;
        }
        if (!literal) {
            compile_template(c, pattern, replacement, out);
            if (out.tag() == ResultTag::Raised)
                return;
            replacement = roots.root(out.value());
        }
    }
    Subject subject(c);
    if (!read_subject(c, source, subject, out) || !compatible(c, pattern, subject, out))
        return;
    const auto end = static_cast<std::ptrdiff_t>(subject.text.size());
    std::ptrdiff_t pos = 0, previous = 0, count = 0;
    bool advance = false;
    std::vector<Value> pieces;
    while (pos <= end && (!limit || count < limit)) {
        Captures captures;
        if (!execute(c, pattern, subject, pos, end, true, false, advance, captures, out)) {
            if (out.tag() == ResultTag::Raised)
                return;
            break;
        }
        const auto mark = captures.marks[0];
        if (previous < mark.first) {
            slice(c, subject, previous, mark.first, out);
            pieces.push_back(roots.root(out.value()).get(c));
        }
        if (literal)
            out.ok(replacement.get(c));
        else {
            match_object(c, pattern, source, 0, end, std::move(captures), out);
            auto match = roots.root(out.value());
            if (callable) {
                const std::array values{match.get(c)};
                call(c, replacement, {values, {}}, out);
            } else
                expand_compiled(c, match, replacement, out);
        }
        if (out.tag() == ResultTag::Raised)
            return;
        if (out.value() != c.builtin(CoreObject::None))
            pieces.push_back(roots.root(out.value()).get(c));
        previous = pos = mark.second;
        advance = mark.first == mark.second;
        ++count;
        refresh_subject(c, subject);
        c.poll();
    }
    if (previous < end) {
        slice(c, subject, previous, end, out);
        pieces.push_back(roots.root(out.value()).get(c));
    }
    join(c, subject.bytes, pieces, out);
    if constexpr (Count) {
        if (out.tag() == ResultTag::Raised)
            return;
        auto value = roots.root(out.value());
        make_int(c, count, out);
        const std::array pair{value.get(c), out.value()};
        cxxpython::core::make_tuple(c, pair, out);
    }
}
void copy(Attachment &, Value, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
void class_getitem(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    make_generic_alias(c, roots.root(args[0]), roots.root(args[1]), out);
}
void no_constructor(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, U"cannot create regex instances directly", out);
}
template <int Field> void pattern_field(Attachment &c, Value self, ReturnSlot &out)
{
    const auto &p = c.payload<SrePatternObject>(self);
    if constexpr (Field == 0)
        out.ok(p.pattern);
    if constexpr (Field == 1)
        make_int(c, p.flags, out);
    if constexpr (Field == 2)
        make_int(c, p.groups, out);
    if constexpr (Field == 3) {
        RootScope roots(c);
        auto owner = roots.root(self);
        if (DictView(c, c.payload<SrePatternObject>(owner).groupindex).entries().empty())
            c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), out);
        else
            c.allocate_instance<MappingProxyObject>(
                roots.root(c.builtin(CoreObject::MappingProxyType)), out, p.groupindex);
    }
}
void scanner_pattern(Attachment &c, Value self, ReturnSlot &out)
{
    out.ok(c.payload<SreScannerObject>(self).pattern);
}
void pattern_hash(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    const auto &p = c.payload<SrePatternObject>(self);
    hash(c, roots.root(p.pattern), out);
    if (out.tag() == ResultTag::Raised)
        return;
    std::int64_t h;
    if (!int_fits_signed(c, out.value(), h))
        throw std::logic_error("hash result outside signed range");
    const auto bits =
        std::bit_cast<std::uint64_t>(h) ^ c.keyed_hash(std::as_bytes(std::span(p.code))) ^
        static_cast<std::uint64_t>(p.flags) ^ static_cast<std::uint64_t>(p.kind) ^ p.code.size();
    h = std::bit_cast<std::int64_t>(bits);
    make_int(c, h == -1 ? -2 : h, out);
}
template <bool Negate>
void pattern_compare(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (c.type_of(args[1]) != c.builtin(CoreObject::SrePatternType)) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto self = roots.root(args[0]), other = roots.root(args[1]);
    const auto &a = c.payload<SrePatternObject>(self), &b = c.payload<SrePatternObject>(other);
    if (a.flags != b.flags || a.kind != b.kind || a.code != b.code)
        out.ok(c.builtin(Negate ? CoreObject::True : CoreObject::False));
    else {
        container_equal(c, roots.root(a.pattern), roots.root(b.pattern), out);
        if constexpr (Negate)
            if (out.tag() == ResultTag::Ok)
                out.ok(c.builtin(out.value() == c.builtin(CoreObject::True) ? CoreObject::False
                                                                            : CoreObject::True));
    }
}
void pattern_repr(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    const auto &p = c.payload<SrePatternObject>(self);
    repr(c, roots.root(p.pattern), out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto text = c.payload<StrObject>(out.value()).value;
    if (text.size() > 200)
        text.resize(200);
    std::u32string result = U"re.compile(" + text;
    int flags = p.flags;
    if (p.kind == SreStringKind::Unicode &&
        (flags & (SRE_FLAG_LOCALE | SRE_FLAG_UNICODE | SRE_FLAG_ASCII)) == SRE_FLAG_UNICODE)
        flags &= ~SRE_FLAG_UNICODE;
    bool first = true;
    for (auto [flag, label] : {std::pair{SRE_FLAG_TEMPLATE, U"re.TEMPLATE"},
                               {SRE_FLAG_IGNORECASE, U"re.IGNORECASE"},
                               {SRE_FLAG_LOCALE, U"re.LOCALE"},
                               {SRE_FLAG_MULTILINE, U"re.MULTILINE"},
                               {SRE_FLAG_DOTALL, U"re.DOTALL"},
                               {SRE_FLAG_UNICODE, U"re.UNICODE"},
                               {SRE_FLAG_VERBOSE, U"re.VERBOSE"},
                               {SRE_FLAG_DEBUG, U"re.DEBUG"},
                               {SRE_FLAG_ASCII, U"re.ASCII"}})
        if (flags & flag) {
            result += first ? U", " : U"|";
            result += label;
            flags &= ~flag;
            first = false;
        }
    if (flags) {
        char buffer[32];
        auto converted =
            std::to_chars(buffer, buffer + sizeof(buffer), static_cast<unsigned>(flags), 16);
        result += first ? U", 0x" : U"|0x";
        result.append(buffer, converted.ptr);
    }
    make_str(c, result + U")", out);
}
} // namespace

namespace detail {
void sre_slice(Attachment &c, Handle source, std::ptrdiff_t begin, std::ptrdiff_t end,
               ReturnSlot &out)
{
    RootScope roots(c);
    source = roots.root(source.get(c));
    if (c.has_layout<StrObject>(source.get(c))) {
        const auto &text = c.payload<StrObject>(source).value;
        begin = std::min(begin, static_cast<std::ptrdiff_t>(text.size()));
        end = std::min(end, static_cast<std::ptrdiff_t>(text.size()));
        make_str(c, text.substr(begin, end - begin), out);
    } else if (is_bytes(c, source.get(c))) {
        const auto &bytes = c.payload<BytesObject>(source).value;
        begin = std::min(begin, static_cast<std::ptrdiff_t>(bytes.size()));
        end = std::min(end, static_cast<std::ptrdiff_t>(bytes.size()));
        if (begin == 0 && static_cast<std::size_t>(end) == bytes.size() &&
            c.type_of(source) == c.builtin(CoreObject::BytesType))
            out.ok(source.get(c));
        else
            make_bytes(c, bytes.substr(begin, end - begin), out);
    } else if (is_bytearray(c, source.get(c))) {
        auto storage = c.payload<ByteArrayObject>(source).contents;
        auto bytes = storage_read(c, *storage, [&] {
            auto a = std::min(begin, static_cast<std::ptrdiff_t>(storage->bytes.size()));
            auto b = std::min(end, static_cast<std::ptrdiff_t>(storage->bytes.size()));
            return storage->bytes.substr(a, b - a);
        });
        make_bytes(c, std::move(bytes), out);
    } else {
        std::string bytes;
        if (!buffer_bytes(c, source, bytes, out)) {
            subject_error(c, source, out);
            return;
        }
        begin = std::min(begin, static_cast<std::ptrdiff_t>(bytes.size()));
        end = std::min(end, static_cast<std::ptrdiff_t>(bytes.size()));
        make_bytes(c, bytes.substr(begin, end - begin), out);
    }
}
void sre_expand(Attachment &c, Handle match, Handle replacement, ReturnSlot &out)
{
    RootScope roots(c);
    match = roots.root(match.get(c));
    compile_template(c, roots.root(c.payload<SreMatchObject>(match).pattern), replacement, out);
    if (out.tag() == ResultTag::Ok)
        expand_compiled(c, match, roots.root(out.value()), out);
}
void install_sre(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto pattern = at(CoreObject::SrePatternType), scan = at(CoreObject::SreScannerType);
    SignatureLayout unary, binary, search;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    binary = unary;
    binary.parameters.push_back({U"arg", ParameterKind::PositionalOnly});
    search = unary;
    search.parameters.push_back({U"string", ParameterKind::PositionalOrKeyword});
    search.parameters.push_back(
        {U"pos", ParameterKind::PositionalOrKeyword, at(CoreObject::IntZero).get(c)});
    RootScope roots(c);
    ReturnSlot maximum(c), temporary(c);
    c.allocate_instance<IntObject>(at(CoreObject::IntType), maximum,
                                   std::numeric_limits<std::ptrdiff_t>::max());
    search.parameters.push_back({U"endpos", ParameterKind::PositionalOrKeyword, maximum.value()});
    auto install = [&](Handle owner, std::u32string name, const SignatureLayout &sig,
                       NativeEntry entry) {
        install_native_attribute(c, core, owner, std::move(name), sig, entry);
    };
    install(pattern, U"match", search, pattern_match<0>);
    install(pattern, U"search", search, pattern_match<1>);
    install(pattern, U"fullmatch", search, pattern_match<2>);
    install(pattern, U"findall", search, scan_list<false>);
    install(pattern, U"scanner", search, scanner<false>);
    install(pattern, U"finditer", search, scanner<true>);
    auto split = unary;
    split.parameters.push_back({U"string", ParameterKind::PositionalOrKeyword});
    split.parameters.push_back(
        {U"maxsplit", ParameterKind::PositionalOrKeyword, at(CoreObject::IntZero).get(c)});
    install(pattern, U"split", split, scan_list<true>);
    auto sub = unary;
    sub.parameters.push_back({U"repl", ParameterKind::PositionalOrKeyword});
    sub.parameters.push_back({U"string", ParameterKind::PositionalOrKeyword});
    sub.parameters.push_back(
        {U"count", ParameterKind::PositionalOrKeyword, at(CoreObject::IntZero).get(c)});
    install(pattern, U"sub", sub, substitute<false>);
    install(pattern, U"subn", sub, substitute<true>);
    install(pattern, U"__repr__", unary, pattern_repr);
    install(pattern, U"__hash__", unary, pattern_hash);
    install(pattern, U"__eq__", binary, pattern_compare<false>);
    install(pattern, U"__ne__", binary, pattern_compare<true>);
    install(pattern, U"__copy__", unary, copy);
    install(pattern, U"__deepcopy__", binary, copy);
    install(pattern, U"__class_getitem__", binary, class_getitem);
    auto dictionary = *TypeView(c, pattern).dictionary();
    auto method = roots.root(*name_lookup(c, dictionary, U"__class_getitem__"));
    c.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), temporary,
                                           method.get(c));
    auto wrapper = roots.root(temporary.value());
    c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__class_getitem__");
    name_store(c, dictionary, temporary.value(), wrapper.get(c));
    install_native_field(c, core, pattern, U"pattern", pattern_field<0>);
    install_native_field(c, core, pattern, U"flags", pattern_field<1>);
    install_native_field(c, core, pattern, U"groups", pattern_field<2>);
    install_native_field(c, core, pattern, U"groupindex", pattern_field<3>);
    install(scan, U"match", unary, scanner_next<false>);
    install(scan, U"search", unary, scanner_next<true>);
    install_native_field(c, core, scan, U"pattern", scanner_pattern);
    auto construction = unary;
    construction.varargs = U"args";
    construction.varkwargs = U"kwargs";
    for (auto id : {CoreObject::SrePatternType, CoreObject::SreMatchType,
                    CoreObject::SreScannerType, CoreObject::SreTemplateType}) {
        auto type = at(id);
        install(type, U"__new__", construction, no_constructor);
        auto dict = *TypeView(c, type).dictionary();
        auto function = roots.root(*name_lookup(c, dict, U"__new__"));
        c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temporary,
                                                function.get(c));
        auto value = roots.root(temporary.value());
        c.allocate_instance<StrObject>(at(CoreObject::StrType), temporary, U"__new__");
        name_store(c, dict, temporary.value(), value.get(c));
    }
    install_sre_match(c, core);
}
} // namespace detail
} // namespace cxxpython::core
