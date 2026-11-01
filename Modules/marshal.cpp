// Wire format based on CPython 3.12.14 Python/marshal.c (PSF license).
#include "internal/marshal.hpp"

#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "internal/codecs.hpp"
#include "internal/long.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>

namespace cxxpython::core::detail {
namespace {
constexpr unsigned flag_ref = 0x80;
constexpr int max_depth = 2000;

bool failed(const ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void method(Attachment &ctx, Handle object, std::u32string name, std::span<const Value> args,
            ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx), callable(ctx);
    make_str(ctx, std::move(name), key);
    get_attribute(ctx, object, roots.root(key.value()), callable);
    if (failed(callable)) {
        out.raised(callable.exception());
        return;
    }
    call(ctx, roots.root(callable.value()), {args, {}}, out);
}

class Writer {
  public:
    Writer(Attachment &ctx, int version, ReturnSlot &out)
        : ctx_(ctx), roots_(ctx), version_(version), out_(out)
    {}

    bool write(Handle value, int depth = 0)
    {
        if (depth > max_depth)
            return error(CoreObject::ValueErrorType, U"object too deeply nested to marshal");
        Value v = value.get(ctx_);
        if (v == ctx_.builtin(CoreObject::None))
            return byte('N');
        if (v == ctx_.builtin(CoreObject::False))
            return byte('F');
        if (v == ctx_.builtin(CoreObject::True))
            return byte('T');
        if (v == ctx_.builtin(CoreObject::Ellipsis))
            return byte('.');
        if (v == ctx_.builtin(CoreObject::StopIterationType))
            return byte('S');

        unsigned flag = 0;
        if (version_ >= 3) {
            auto found = refs_.find(v.object());
            if (found != refs_.end()) {
                byte('r');
                return number(found->second);
            }
            if (refs_.size() >= static_cast<std::size_t>(INT32_MAX))
                return error(CoreObject::ValueErrorType, U"object too large to marshal");
            refs_.emplace(v.object(), static_cast<std::int32_t>(refs_.size()));
            roots_.root(v);
            flag = flag_ref;
        }
        auto type = [&](char code) { return byte(static_cast<unsigned char>(code) | flag); };
        auto object_type = ctx_.type_of(value);
        if (object_type == ctx_.builtin(CoreObject::IntType)) {
            Integer n = integer_value(ctx_.payload<IntObject>(v));
            if (n >= INT32_MIN && n <= INT32_MAX) {
                type('i');
                return number(n.convert_to<std::int32_t>());
            }
            bool negative = n < 0;
            if (negative)
                n = -n;
            std::vector<std::uint16_t> digits;
            while (n != 0) {
                digits.push_back(static_cast<std::uint16_t>((n & 0x7fff).convert_to<unsigned>()));
                n >>= 15;
            }
            if (digits.size() > static_cast<std::size_t>(INT32_MAX))
                return error(CoreObject::ValueErrorType, U"object too large to marshal");
            type('l');
            auto count = static_cast<std::int32_t>(digits.size());
            number(negative ? -count : count);
            for (auto digit : digits) {
                byte(digit & 255);
                byte(digit >> 8);
            }
            return true;
        }
        if (object_type == ctx_.builtin(CoreObject::FloatType)) {
            type(version_ >= 2 ? 'g' : 'f');
            return version_ >= 2 ? real(ctx_.payload<FloatObject>(v).value)
                                 : text_real(ctx_.payload<FloatObject>(v).value);
        }
        if (object_type == ctx_.builtin(CoreObject::ComplexType)) {
            auto &z = ctx_.payload<ComplexObject>(v);
            type(version_ >= 2 ? 'y' : 'x');
            if (version_ >= 2) {
                real(z.real);
                return real(z.imag);
            }
            return text_real(z.real) && text_real(z.imag);
        }
        if (object_type == ctx_.builtin(CoreObject::StrType)) {
            ReturnSlot errors(ctx_), encoded(ctx_);
            make_str(ctx_, U"surrogatepass", errors);
            encode_primitive(ctx_, value, TextEncoding::Utf8, roots_.root(errors.value()), encoded);
            if (failed(encoded)) {
                out_.raised(encoded.exception());
                return false;
            }
            auto bytes = ctx_.payload<TupleObject>(encoded.value()).items[0];
            const auto &s = ctx_.payload<BytesObject>(bytes).value;
            bool ascii = true;
            for (unsigned char ch : s)
                if (ch >= 128) {
                    ascii = false;
                    break;
                }
            if (version_ >= 4 && ascii && s.size() < 256) {
                type('z');
                byte(static_cast<unsigned>(s.size()));
                data_ += s;
                return true;
            }
            type(version_ >= 4 && ascii ? 'a' : 'u');
            return sized(s);
        }
        if (object_type == ctx_.builtin(CoreObject::BytesType)) {
            type('s');
            return sized(ctx_.payload<BytesObject>(v).value);
        }
        if (object_type == ctx_.builtin(CoreObject::CodeType)) {
            type('c');
            return write_code(value, depth);
        }
        if (object_type == ctx_.builtin(CoreObject::TupleType) ||
            object_type == ctx_.builtin(CoreObject::ListType)) {
            std::vector<Value> items;
            bool tuple = object_type == ctx_.builtin(CoreObject::TupleType);
            if (tuple)
                items = ctx_.payload<TupleObject>(v).items;
            else
                list_snapshot(ctx_, value, roots_, items);
            if (items.size() > static_cast<std::size_t>(INT32_MAX))
                return error(CoreObject::ValueErrorType, U"object too large to marshal");
            if (tuple && version_ >= 4 && items.size() < 256) {
                type(')');
                byte(static_cast<unsigned>(items.size()));
            } else {
                type(tuple ? '(' : '[');
                number(static_cast<std::int32_t>(items.size()));
            }
            for (Value item : items)
                if (!write(roots_.root(item), depth + 1))
                    return false;
            return true;
        }
        if (object_type == ctx_.builtin(CoreObject::DictType)) {
            DictView view(ctx_, v);
            type('{');
            for (const auto &entry : view.entries()) {
                if (!write(roots_.root(entry.key), depth + 1) ||
                    !write(roots_.root(entry.value), depth + 1))
                    return false;
            }
            return byte('0');
        }
        if (object_type == ctx_.builtin(CoreObject::SetType) ||
            object_type == ctx_.builtin(CoreObject::FrozenSetType)) {
            DictView view(ctx_, ctx_.payload<SetObject>(v).table);
            if (view.entries().size() > static_cast<std::size_t>(INT32_MAX))
                return error(CoreObject::ValueErrorType, U"object too large to marshal");
            type(object_type == ctx_.builtin(CoreObject::FrozenSetType) ? '>' : '<');
            number(static_cast<std::int32_t>(view.entries().size()));
            for (const auto &entry : view.entries())
                if (!write(roots_.root(entry.key), depth + 1))
                    return false;
            return true;
        }
        std::string buffer;
        ReturnSlot buffer_error(ctx_);
        if (buffer_bytes(ctx_, value, buffer, buffer_error)) {
            type('s');
            return sized(buffer);
        }
        return error(CoreObject::ValueErrorType, U"unmarshallable object");
    }
    const std::string &data() const
    {
        return data_;
    }

  private:
    bool write_code(Handle code, int depth)
    {
        constexpr std::array<const char32_t *, 16> names = {
            U"co_argcount",      U"co_posonlyargcount", U"co_kwonlyargcount",
            U"co_stacksize",     U"co_flags",           U"co_code",
            U"co_consts",        U"co_names",           U"co_varnames",
            U"co_cellvars",      U"co_freevars",        U"co_filename",
            U"co_name",          U"co_qualname",        U"co_linetable",
            U"co_exceptiontable"};
        std::vector<Handle> fields;
        for (auto name : names) {
            ReturnSlot key(ctx_), field(ctx_);
            make_str(ctx_, name, key);
            get_attribute(ctx_, code, roots_.root(key.value()), field);
            if (failed(field)) {
                out_.raised(field.exception());
                return false;
            }
            fields.push_back(roots_.root(field.value()));
        }
        ReturnSlot key(ctx_), first_line(ctx_);
        make_str(ctx_, U"co_firstlineno", key);
        get_attribute(ctx_, code, roots_.root(key.value()), first_line);
        if (failed(first_line)) {
            out_.raised(first_line.exception());
            return false;
        }
        auto first = roots_.root(first_line.value());
        auto write_number = [&](Handle field) {
            std::int64_t n;
            if (!is_int(ctx_, field.get(ctx_)) || !int_fits_signed(ctx_, field.get(ctx_), n) ||
                n < 0 || n > INT32_MAX)
                return error(CoreObject::ValueErrorType, U"bad code object integer field");
            return number(static_cast<std::int32_t>(n));
        };
        for (int i = 0; i < 5; ++i)
            if (!write_number(fields[i]))
                return false;

        for (int i : {8, 9, 10})
            if (!ctx_.has_layout<TupleObject>(fields[i].get(ctx_)))
                return error(CoreObject::TypeErrorType, U"bad code object locals tuple");
        const auto &vars = ctx_.payload<TupleObject>(fields[8]).items;
        const auto &cells = ctx_.payload<TupleObject>(fields[9]).items;
        const auto &free = ctx_.payload<TupleObject>(fields[10]).items;
        std::vector<Value> locals(vars.begin(), vars.end());
        std::string kinds(vars.size(), static_cast<char>(0x20));
        for (auto item : locals)
            if (!ctx_.has_layout<StrObject>(item))
                return error(CoreObject::TypeErrorType, U"bad code object local name");
        for (auto cell : cells) {
            if (!ctx_.has_layout<StrObject>(cell))
                return error(CoreObject::TypeErrorType, U"bad code object cell name");
            const auto &name = ctx_.payload<StrObject>(cell).value;
            std::size_t i = 0;
            while (i < locals.size() && ctx_.payload<StrObject>(locals[i]).value != name)
                ++i;
            if (i == locals.size()) {
                locals.push_back(cell);
                kinds.push_back(static_cast<char>(0x40));
            } else {
                kinds[i] = static_cast<char>(static_cast<unsigned char>(kinds[i]) | 0x40);
            }
        }
        for (auto item : free) {
            if (!ctx_.has_layout<StrObject>(item))
                return error(CoreObject::TypeErrorType, U"bad code object free name");
            locals.push_back(item);
            kinds.push_back(static_cast<char>(0x80));
        }
        ReturnSlot local_names(ctx_), local_kinds(ctx_);
        cxxpython::core::make_tuple(ctx_, locals, local_names);
        auto names_handle = roots_.root(local_names.value());
        make_bytes(ctx_, std::move(kinds), local_kinds);
        auto kinds_handle = roots_.root(local_kinds.value());
        for (int i : {5, 6, 7})
            if (!write(fields[i], depth + 1))
                return false;
        if (!write(names_handle, depth + 1) || !write(kinds_handle, depth + 1))
            return false;
        for (int i : {11, 12, 13})
            if (!write(fields[i], depth + 1))
                return false;
        return write_number(first) && write(fields[14], depth + 1) && write(fields[15], depth + 1);
    }
    bool error(CoreObject type, std::u32string message)
    {
        raise_error(ctx_, type, std::move(message), out_);
        return false;
    }
    bool byte(unsigned c)
    {
        data_.push_back(static_cast<char>(c));
        return true;
    }
    bool number(std::int32_t n)
    {
        auto u = static_cast<std::uint32_t>(n);
        for (int i = 0; i < 4; ++i)
            byte((u >> (8 * i)) & 255);
        return true;
    }
    bool real(double d)
    {
        auto u = std::bit_cast<std::uint64_t>(d);
        for (int i = 0; i < 8; ++i)
            byte(static_cast<unsigned>((u >> (8 * i)) & 255));
        return true;
    }
    bool text_real(double d)
    {
        char buffer[64];
        auto converted =
            std::to_chars(buffer, buffer + sizeof(buffer), d, std::chars_format::general,
                          std::numeric_limits<double>::max_digits10);
        if (converted.ec != std::errc{})
            return error(CoreObject::ValueErrorType, U"cannot marshal float");
        auto size = static_cast<std::size_t>(converted.ptr - buffer);
        byte(static_cast<unsigned>(size));
        data_.append(buffer, size);
        return true;
    }
    bool sized(const std::string &s)
    {
        if (s.size() > static_cast<std::size_t>(INT32_MAX))
            return error(CoreObject::ValueErrorType, U"object too large to marshal");
        number(static_cast<std::int32_t>(s.size()));
        data_ += s;
        return true;
    }
    Attachment &ctx_;
    RootScope roots_;
    int version_;
    ReturnSlot &out_;
    std::string data_;
    std::unordered_map<ObjectHeader *, std::int32_t> refs_;
};

class Reader {
  public:
    Reader(Attachment &ctx, std::string data, std::optional<Handle> file, ReturnSlot &out)
        : ctx_(ctx), roots_(ctx), data_(std::move(data)), file_(file), out_(out)
    {}

    bool read(ReturnSlot &result, int depth = 0, bool allow_null = false,
              bool allow_pending = false)
    {
        if (depth > max_depth)
            return error(CoreObject::ValueErrorType, U"recursion limit exceeded");
        unsigned raw;
        if (!byte(raw))
            return false;
        bool flagged = raw & flag_ref;
        unsigned code = raw & ~flag_ref;
        if (code == '0') {
            if (allow_null) {
                result.reset();
                return true;
            }
            return error(CoreObject::TypeErrorType, U"NULL object in marshal data");
        }
        if (code == 'r') {
            std::int32_t index;
            if (!number(index))
                return false;
            if (index < 0 || static_cast<std::size_t>(index) >= refs_.size())
                return error(CoreObject::ValueErrorType, U"bad marshal data (invalid reference)");
            if (!refs_[index]) {
                if (!allow_pending)
                    return error(CoreObject::ValueErrorType,
                                 U"bad marshal data (invalid reference)");
                pending_ = static_cast<std::size_t>(index);
                result.ok(ctx_.builtin(CoreObject::None));
                return true;
            }
            result.ok(refs_[index]->get(ctx_));
            return true;
        }
        std::optional<std::size_t> index;
        if (flagged) {
            index = refs_.size();
            refs_.push_back({});
        }
        auto publish = [&] {
            if (index)
                refs_[*index] = roots_.root(result.value());
            return true;
        };
        switch (code) {
        case 'N':
            result.ok(ctx_.builtin(CoreObject::None));
            return publish();
        case 'F':
            result.ok(ctx_.builtin(CoreObject::False));
            return publish();
        case 'T':
            result.ok(ctx_.builtin(CoreObject::True));
            return publish();
        case '.':
            result.ok(ctx_.builtin(CoreObject::Ellipsis));
            return publish();
        case 'S':
            result.ok(ctx_.builtin(CoreObject::StopIterationType));
            return publish();
        case 'c':
            if (!read_code(result, depth))
                return false;
            return publish();
        case 'i': {
            std::int32_t n;
            if (!number(n))
                return false;
            make_int(ctx_, n, result);
            return publish();
        }
        case 'I': {
            std::uint64_t n;
            if (!uint64(n))
                return false;
            publish_integer(ctx_, Integer(std::bit_cast<std::int64_t>(n)), result);
            return publish();
        }
        case 'l': {
            std::int32_t count;
            if (!number(count))
                return false;
            if (count == INT32_MIN)
                return error(CoreObject::ValueErrorType,
                             U"bad marshal data (long size out of range)");
            int n = count < 0 ? -count : count;
            Integer value = 0;
            for (int i = 0; i < n; ++i) {
                unsigned lo, hi;
                if (!byte(lo) || !byte(hi))
                    return false;
                unsigned digit = lo | (hi << 8);
                if (digit >= 32768 || (i == n - 1 && digit == 0))
                    return error(CoreObject::ValueErrorType, U"bad marshal data (invalid long)");
                value += Integer(digit) << (15 * static_cast<std::size_t>(i));
            }
            if (count < 0)
                value = -value;
            publish_integer(ctx_, std::move(value), result);
            return publish();
        }
        case 'g':
        case 'y':
        case 'f':
        case 'x': {
            double a, b = 0;
            if (!(code == 'g' || code == 'y' ? real(a) : text_real(a)))
                return false;
            if ((code == 'y' || code == 'x') && !(code == 'y' ? real(b) : text_real(b)))
                return false;
            if (code == 'g' || code == 'f')
                make_float(ctx_, a, result);
            else
                make_complex(ctx_, a, b, result);
            return publish();
        }
        case 's':
        case 'u':
        case 't':
        case 'a':
        case 'A':
        case 'z':
        case 'Z': {
            std::int32_t size;
            if (code == 'z' || code == 'Z') {
                unsigned n;
                if (!byte(n))
                    return false;
                size = n;
            } else if (!number(size))
                return false;
            if (size < 0)
                return error(CoreObject::ValueErrorType, U"bad marshal data (negative size)");
            std::string bytes;
            if (!take(static_cast<std::size_t>(size), bytes))
                return false;
            if (code == 's')
                make_bytes(ctx_, std::move(bytes), result);
            else {
                ReturnSlot source(ctx_), errors(ctx_);
                make_bytes(ctx_, std::move(bytes), source);
                make_str(ctx_, U"surrogatepass", errors);
                decode_primitive(ctx_, roots_.root(source.value()), TextEncoding::Utf8,
                                 roots_.root(errors.value()), true, result);
                if (failed(result)) {
                    out_.raised(result.exception());
                    return false;
                }
                result.ok(ctx_.payload<TupleObject>(result.value()).items[0]);
            }
            return publish();
        }
        case '(':
        case ')':
        case '[':
        case '<':
        case '>': {
            std::int32_t size;
            if (code == ')') {
                unsigned n;
                if (!byte(n))
                    return false;
                size = n;
            } else if (!number(size))
                return false;
            if (size < 0)
                return error(CoreObject::ValueErrorType, U"bad marshal data (negative size)");
            bool list = code == '[', set = code == '<';
            if (list)
                make_list(ctx_, {}, result);
            if (set)
                make_set(ctx_, {}, result);
            std::optional<Handle> container;
            if (list || set) {
                container = roots_.root(result.value());
                publish();
            }
            std::vector<Value> items;
            for (std::int32_t i = 0; i < size; ++i) {
                ReturnSlot item(ctx_);
                if (!read(item, depth + 1, false, list))
                    return false;
                if (list && pending_) {
                    fixups_.push_back({*pending_, *container, i});
                    pending_.reset();
                }
                auto h = roots_.root(item.value());
                if (list)
                    list_append(ctx_, *container, h, result);
                else if (set)
                    set_add(ctx_, *container, h, result);
                else
                    items.push_back(h.get(ctx_));
                if (failed(result)) {
                    out_.raised(result.exception());
                    return false;
                }
            }
            if (container) {
                result.ok(container->get(ctx_));
                return true;
            }
            if (code == '>') {
                ReturnSlot tuple(ctx_);
                cxxpython::core::make_tuple(ctx_, items, tuple);
                Value arg = tuple.value();
                call(ctx_, roots_.root(ctx_.builtin(CoreObject::FrozenSetType)), {{&arg, 1}, {}},
                     result);
                if (failed(result)) {
                    out_.raised(result.exception());
                    return false;
                }
            } else
                cxxpython::core::make_tuple(ctx_, items, result);
            return publish();
        }
        case '{': {
            call(ctx_, roots_.root(ctx_.builtin(CoreObject::DictType)), {}, result);
            if (failed(result)) {
                out_.raised(result.exception());
                return false;
            }
            auto dict = roots_.root(result.value());
            publish();
            for (;;) {
                ReturnSlot key(ctx_), value(ctx_);
                if (!read(key, depth + 1, true))
                    return false;
                if (key.tag() == ResultTag::Empty)
                    break;
                auto k = roots_.root(key.value());
                if (!read(value, depth + 1))
                    return false;
                dict_store(ctx_, dict, k, roots_.root(value.value()), result);
                if (failed(result)) {
                    out_.raised(result.exception());
                    return false;
                }
            }
            result.ok(dict.get(ctx_));
            return true;
        }
        default:
            return error(CoreObject::ValueErrorType, U"bad marshal data (unknown type code)");
        }
    }

    bool finish()
    {
        for (const auto &fixup : fixups_) {
            if (fixup.reference >= refs_.size() || !refs_[fixup.reference])
                return error(CoreObject::ValueErrorType, U"bad marshal data (invalid reference)");
            ReturnSlot index(ctx_), result(ctx_);
            make_int(ctx_, fixup.index, index);
            set_item(ctx_, fixup.list, roots_.root(index.value()), *refs_[fixup.reference], result);
            if (failed(result)) {
                out_.raised(result.exception());
                return false;
            }
        }
        return true;
    }

  private:
    struct Fixup {
        std::size_t reference;
        Handle list;
        std::int32_t index;
    };
    bool read_code(ReturnSlot &result, int depth)
    {
        std::array<std::int32_t, 5> numbers;
        for (auto &n : numbers)
            if (!number(n))
                return false;
        std::vector<Handle> fields;
        for (int i = 0; i < 8; ++i) {
            ReturnSlot field(ctx_);
            if (!read(field, depth + 1))
                return false;
            fields.push_back(roots_.root(field.value()));
        }
        std::int32_t first_line;
        if (!number(first_line))
            return false;
        for (int i = 0; i < 2; ++i) {
            ReturnSlot field(ctx_);
            if (!read(field, depth + 1))
                return false;
            fields.push_back(roots_.root(field.value()));
        }
        if (!ctx_.has_layout<TupleObject>(fields[3].get(ctx_)) ||
            !ctx_.has_layout<BytesObject>(fields[4].get(ctx_)))
            return error(CoreObject::ValueErrorType, U"bad marshal data (invalid code locals)");
        const auto &names = ctx_.payload<TupleObject>(fields[3]).items;
        const auto &kinds = ctx_.payload<BytesObject>(fields[4]).value;
        if (names.size() != kinds.size() || names.size() > static_cast<std::size_t>(INT32_MAX))
            return error(CoreObject::ValueErrorType, U"bad marshal data (invalid code locals)");
        std::vector<Value> vars, cells, free;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (!ctx_.has_layout<StrObject>(names[i]))
                return error(CoreObject::ValueErrorType, U"bad marshal data (invalid local name)");
            auto kind = static_cast<unsigned char>(kinds[i]);
            if (!(kind & 0xe0))
                return error(CoreObject::ValueErrorType, U"bad marshal data (invalid local kind)");
            if (kind & 0x20)
                vars.push_back(names[i]);
            if (kind & 0x40)
                cells.push_back(names[i]);
            if (kind & 0x80)
                free.push_back(names[i]);
        }
        ReturnSlot var_tuple(ctx_), cell_tuple(ctx_), free_tuple(ctx_);
        cxxpython::core::make_tuple(ctx_, vars, var_tuple);
        cxxpython::core::make_tuple(ctx_, cells, cell_tuple);
        cxxpython::core::make_tuple(ctx_, free, free_tuple);
        std::vector<Value> args;
        auto add_number = [&](std::int64_t n) {
            ReturnSlot value(ctx_);
            make_int(ctx_, n, value);
            args.push_back(roots_.root(value.value()).get(ctx_));
        };
        for (int i = 0; i < 3; ++i)
            add_number(numbers[i]);
        add_number(static_cast<std::int64_t>(vars.size()));
        add_number(numbers[3]);
        add_number(numbers[4]);
        for (int i : {0, 1, 2})
            args.push_back(fields[i].get(ctx_));
        args.push_back(var_tuple.value());
        for (int i : {5, 6, 7})
            args.push_back(fields[i].get(ctx_));
        add_number(first_line);
        args.push_back(fields[8].get(ctx_));
        args.push_back(fields[9].get(ctx_));
        args.push_back(free_tuple.value());
        args.push_back(cell_tuple.value());
        call(ctx_, roots_.root(ctx_.builtin(CoreObject::CodeType)), {args, {}}, result);
        if (failed(result)) {
            out_.raised(result.exception());
            return false;
        }
        return true;
    }
    bool error(CoreObject type, std::u32string message)
    {
        raise_error(ctx_, type, std::move(message), out_);
        return false;
    }
    bool take(std::size_t n, std::string &out)
    {
        if (file_) {
            ReturnSlot count(ctx_), result(ctx_);
            make_int(ctx_, static_cast<std::int64_t>(n), count);
            Value arg = count.value();
            method(ctx_, *file_, U"read", {&arg, 1}, result);
            if (failed(result)) {
                out_.raised(result.exception());
                return false;
            }
            if (!ctx_.has_layout<BytesObject>(result.value()))
                return error(CoreObject::TypeErrorType, U"file.read() returned non-bytes");
            out = ctx_.payload<BytesObject>(result.value()).value;
            if (out.size() != n)
                return error(CoreObject::EOFErrorType, U"EOF read where object expected");
            return true;
        }
        if (n > data_.size() - pos_)
            return error(CoreObject::EOFErrorType, U"marshal data too short");
        out.assign(data_, pos_, n);
        pos_ += n;
        return true;
    }
    bool byte(unsigned &out)
    {
        std::string s;
        if (!take(1, s))
            return false;
        out = static_cast<unsigned char>(s[0]);
        return true;
    }
    bool number(std::int32_t &out)
    {
        std::string s;
        if (!take(4, s))
            return false;
        std::uint32_t n = 0;
        for (int i = 0; i < 4; ++i)
            n |= std::uint32_t(static_cast<unsigned char>(s[i])) << (8 * i);
        out = std::bit_cast<std::int32_t>(n);
        return true;
    }
    bool uint64(std::uint64_t &out)
    {
        std::string s;
        if (!take(8, s))
            return false;
        out = 0;
        for (int i = 0; i < 8; ++i)
            out |= std::uint64_t(static_cast<unsigned char>(s[i])) << (8 * i);
        return true;
    }
    bool real(double &out)
    {
        std::uint64_t n;
        if (!uint64(n))
            return false;
        out = std::bit_cast<double>(n);
        return true;
    }
    bool text_real(double &out)
    {
        unsigned n;
        if (!byte(n))
            return false;
        std::string s;
        if (!take(n, s))
            return false;
        char *end = nullptr;
        out = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size() || end == s.c_str())
            return error(CoreObject::ValueErrorType, U"bad marshal data (invalid float)");
        return true;
    }
    Attachment &ctx_;
    RootScope roots_;
    std::string data_;
    std::size_t pos_ = 0;
    std::optional<Handle> file_;
    ReturnSlot &out_;
    std::vector<std::optional<Handle>> refs_;
    std::optional<std::size_t> pending_;
    std::vector<Fixup> fixups_;
};

bool version(Attachment &ctx, Handle value, int &result, ReturnSlot &out)
{
    std::int64_t n;
    if (!is_int(ctx, value.get(ctx)) || !int_fits_signed(ctx, value.get(ctx), n)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"version must be an integer", out);
        return false;
    }
    result = n < 0 ? 0 : n > 4 ? 4 : static_cast<int>(n);
    return true;
}
void dumps(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    int ver;
    if (!version(ctx, roots.root(args[1]), ver, out))
        return;
    Writer writer(ctx, ver, out);
    if (writer.write(roots.root(args[0])))
        make_bytes(ctx, writer.data(), out);
}
void dump(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto value = roots.root(args[0]), file = roots.root(args[1]);
    int ver;
    if (!version(ctx, roots.root(args[2]), ver, out))
        return;
    Writer writer(ctx, ver, out);
    if (!writer.write(value))
        return;
    ReturnSlot bytes(ctx), result(ctx);
    make_bytes(ctx, writer.data(), bytes);
    Value arg = bytes.value();
    method(ctx, file, U"write", {&arg, 1}, result);
    if (failed(result))
        out.raised(result.exception());
    else
        out.ok(ctx.builtin(CoreObject::None));
}
void loads(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    std::string data;
    ReturnSlot result(ctx);
    if (!buffer_bytes(ctx, roots.root(args[0]), data, result)) {
        if (failed(result))
            out.raised(result.exception());
        else
            raise_error(ctx, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    Reader reader(ctx, std::move(data), {}, out);
    if (reader.read(out))
        reader.finish();
}
void load(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    Reader reader(ctx, {}, roots.root(args[0]), out);
    if (reader.read(out))
        reader.finish();
}
} // namespace

void initialize_marshal(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto dict = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot key(ctx), value(ctx), result(ctx), four(ctx);
    make_int(ctx, 4, four);
    auto store = [&](std::u32string name, Handle item) {
        make_str(ctx, std::move(name), key);
        dict_store(ctx, dict, roots.root(key.value()), item, result);
        if (failed(result)) {
            out.raised(result.exception());
            return false;
        }
        return true;
    };
    if (!store(U"version", roots.root(four.value())))
        return;
    auto add = [&](const char *name, std::u32string key_name, SignatureLayout sig, NativeEntry fn) {
        make_native_function(ctx, name, std::move(sig), fn, {}, value,
                             NativeFunctionBinding::Function);
        if (failed(value)) {
            out.raised(value.exception());
            return false;
        }
        return store(std::move(key_name), roots.root(value.value()));
    };
    SignatureLayout unary;
    unary.parameters = {{U"value", ParameterKind::PositionalOnly}};
    auto write = unary;
    write.parameters.push_back({U"version", ParameterKind::PositionalOnly, four.value()});
    auto write_file = unary;
    write_file.parameters.push_back({U"file", ParameterKind::PositionalOnly});
    write_file.parameters.push_back({U"version", ParameterKind::PositionalOnly, four.value()});
    if (!add("dumps", U"dumps", write, dumps) || !add("dump", U"dump", write_file, dump) ||
        !add("loads", U"loads", unary, loads) || !add("load", U"load", unary, load))
        return;
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
