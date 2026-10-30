// Incremental newline handling follows CPython 3.12.14 Modules/_io/textio.c
// (PSF license). The managed edges and operation claim are runtime-specific.
#include "internal/textio.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cxxpython::core {
namespace {
struct NewlineDecoder {
    ObjectHeader header;
    AtomicGcEdge decoder;
    std::mutex mutex;
    std::condition_variable ready;
    std::thread::id owner;
    bool active = false;
    bool initialized = false;
    bool translate = false;
    bool pending_cr = false;
    unsigned seen = 0;
    NewlineDecoder(InitialType type, Value none) : header(type), decoder(none) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(decoder);
    }
};

class Claim {
    Attachment &c;
    NewlineDecoder &s;
    bool held = false;

  public:
    Claim(Attachment &ctx, NewlineDecoder &state, ReturnSlot &out) : c(ctx), s(state)
    {
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                NativeWait wait(c);
                std::unique_lock parked(s.mutex);
                continue;
            }
            if (!s.active) {
                s.active = true;
                s.owner = std::this_thread::get_id();
                held = true;
                return;
            }
            if (s.owner == std::this_thread::get_id()) {
                lock.unlock();
                raise_error(c, CoreObject::RuntimeErrorType, U"reentrant newline decoder", out);
                return;
            }
            lock.unlock();
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
            s.ready.wait(parked, [&] { return !s.active; });
        }
    }
    ~Claim()
    {
        if (!held)
            return;
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                s.active = false;
                s.owner = {};
                s.ready.notify_one();
                return;
            }
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
        }
    }
    explicit operator bool() const
    {
        return held;
    }
};

bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<NewlineDecoder>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected IncrementalNewlineDecoder", out);
    return false;
}
Handle decoder(Attachment &c, Handle self, NewlineDecoder &s, RootScope &roots)
{
    ReturnSlot value(c);
    c.load(self.get(c), s.decoder, value);
    return roots.root(value.value());
}
void create(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<NewlineDecoder>(roots.root(a[0]), out, c.builtin(CoreObject::None));
}
void init(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto underlying = roots.root(a[1]);
    ReturnSlot t(c);
    truth(c, roots.root(a[2]), t);
    if (t.tag() == ResultTag::Raised) {
        out.raised(t.exception());
        return;
    }
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    c.store(self, s.decoder, underlying);
    s.translate = t.value() != c.builtin(CoreObject::False);
    s.pending_cr = false;
    s.seen = 0;
    s.initialized = true;
    out.ok(c.builtin(CoreObject::None));
}

void decode(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto input = roots.root(a[1]);
    auto final_arg = roots.root(a[2]);
    ReturnSlot final_truth(c);
    truth(c, final_arg, final_truth);
    if (final_truth.tag() == ResultTag::Raised) {
        out.raised(final_truth.exception());
        return;
    }
    bool final = final_truth.value() != c.builtin(CoreObject::False);
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    auto inner = decoder(c, self, s, roots);
    ReturnSlot decoded(c);
    if (inner.get(c) == c.builtin(CoreObject::None))
        decoded.ok(input.get(c));
    else {
        std::array<Value, 2> args{input.get(c), final_arg.get(c)};
        if (!detail::io_call(c, inner, U"decode", args, decoded)) {
            out.raised(decoded.exception());
            return;
        }
    }
    auto text_root = roots.root(decoded.value());
    if (!c.has_layout<StrObject>(text_root.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"decoder should return a string", out);
        return;
    }
    std::u32string text = c.payload<StrObject>(text_root).value;
    if (s.pending_cr && (!text.empty() || final)) {
        text.insert(text.begin(), U'\r');
        s.pending_cr = false;
    }
    if (!final && !text.empty() && text.back() == U'\r') {
        text.pop_back();
        s.pending_cr = true;
    }
    std::u32string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == U'\r') {
            bool pair = i + 1 < text.size() && text[i + 1] == U'\n';
            s.seen |= pair ? 4 : 1;
            if (s.translate)
                result.push_back(U'\n');
            else {
                result.push_back(U'\r');
                if (pair)
                    result.push_back(U'\n');
            }
            if (pair)
                ++i;
        } else {
            if (text[i] == U'\n')
                s.seen |= 2;
            result.push_back(text[i]);
        }
    }
    make_str(c, std::move(result), out);
}
void reset(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    auto inner = decoder(c, self, s, roots);
    if (inner.get(c) != c.builtin(CoreObject::None)) {
        ReturnSlot result(c);
        if (!detail::io_call(c, inner, U"reset", {}, result)) {
            out.raised(result.exception());
            return;
        }
    }
    s.pending_cr = false;
    s.seen = 0;
    out.ok(c.builtin(CoreObject::None));
}
void getstate(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    auto inner = decoder(c, self, s, roots);
    ReturnSlot state(c), empty(c);
    Handle buffer = roots.root(c.builtin(CoreObject::None));
    Handle flag = roots.root(c.builtin(CoreObject::IntZero));
    if (inner.get(c) != c.builtin(CoreObject::None)) {
        if (!detail::io_call(c, inner, U"getstate", {}, state)) {
            out.raised(state.exception());
            return;
        }
        auto tuple = roots.root(state.value());
        if (!c.has_layout<TupleObject>(tuple.get(c)) ||
            c.payload<TupleObject>(tuple).items.size() != 2) {
            raise_error(c, CoreObject::TypeErrorType, U"decoder getstate() must return a 2-tuple",
                        out);
            return;
        }
        buffer = roots.root(c.payload<TupleObject>(tuple).items[0]);
        flag = roots.root(c.payload<TupleObject>(tuple).items[1]);
    } else {
        make_bytes(c, "", empty);
        buffer = roots.root(empty.value());
    }
    ReturnSlot one(c), shifted(c), pending(c), combined(c);
    make_int(c, 1, one);
    auto one_root = roots.root(one.value());
    binary(c, flag, one_root, BinaryOp::LeftShift, shifted);
    if (shifted.tag() == ResultTag::Raised) {
        out.raised(shifted.exception());
        return;
    }
    auto shifted_root = roots.root(shifted.value());
    make_int(c, s.pending_cr ? 1 : 0, pending);
    binary(c, shifted_root, roots.root(pending.value()), BinaryOp::BitOr, combined);
    if (combined.tag() == ResultTag::Raised) {
        out.raised(combined.exception());
        return;
    }
    std::array<Value, 2> items{buffer.get(c), combined.value()};
    make_tuple(c, std::span<const Value>(items), out);
}
void setstate(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(a[0]);
    auto state = roots.root(a[1]);
    if (!c.has_layout<TupleObject>(state.get(c)) ||
        c.payload<TupleObject>(state).items.size() != 2) {
        raise_error(c, CoreObject::TypeErrorType, U"state must be a 2-tuple", out);
        return;
    }
    auto buffer = roots.root(c.payload<TupleObject>(state).items[0]);
    auto flags = roots.root(c.payload<TupleObject>(state).items[1]);
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    ReturnSlot one(c), bit(c), shifted(c);
    make_int(c, 1, one);
    auto one_root = roots.root(one.value());
    binary(c, flags, one_root, BinaryOp::BitAnd, bit);
    if (bit.tag() == ResultTag::Raised) {
        out.raised(bit.exception());
        return;
    }
    auto bit_root = roots.root(bit.value());
    binary(c, flags, one_root, BinaryOp::RightShift, shifted);
    if (shifted.tag() == ResultTag::Raised) {
        out.raised(shifted.exception());
        return;
    }
    auto shifted_root = roots.root(shifted.value());
    auto inner = decoder(c, self, s, roots);
    if (inner.get(c) != c.builtin(CoreObject::None)) {
        std::array<Value, 2> items{buffer.get(c), shifted_root.get(c)};
        ReturnSlot tuple(c), result(c);
        make_tuple(c, std::span<const Value>(items), tuple);
        auto tuple_root = roots.root(tuple.value());
        Value argument = tuple_root.get(c);
        if (!detail::io_call(c, inner, U"setstate", {&argument, 1}, result)) {
            out.raised(result.exception());
            return;
        }
    }
    std::int64_t pending;
    if (!detail::io_index(c, bit_root, pending, out))
        return;
    s.pending_cr = pending != 0;
    out.ok(c.builtin(CoreObject::None));
}
void newlines(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope roots(c);
    auto self = roots.root(value);
    auto &s = c.payload<NewlineDecoder>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    if (!s.seen) {
        out.ok(c.builtin(CoreObject::None));
        return;
    }
    std::vector<Handle> values;
    for (auto [bit, text] : {std::pair{1u, U"\r"}, std::pair{2u, U"\n"}, std::pair{4u, U"\r\n"}}) {
        if (s.seen & bit) {
            ReturnSlot v(c);
            make_str(c, text, v);
            values.push_back(roots.root(v.value()));
        }
    }
    if (values.size() == 1) {
        out.ok(values[0].get(c));
        return;
    }
    std::vector<Value> items;
    for (auto h : values)
        items.push_back(h.get(c));
    make_tuple(c, std::span<const Value>(items), out);
}
} // namespace

namespace detail {
void initialize_textio(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    NativeTypeSpec spec{{"IncrementalNewlineDecoder"}, LayoutDescriptor::of<NewlineDecoder>()};
    spec.type.module = U"_io";
    ReturnSlot type_slot(c);
    create_native_type(c, spec, type_slot);
    if (type_slot.tag() == ResultTag::Raised) {
        out.raised(type_slot.exception());
        return;
    }
    auto type = roots.root(type_slot.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto ctor = unary;
    ctor.varargs = U"args";
    ctor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", ctor, create);
    auto dict = *TypeView(c, type).dictionary();
    auto function = roots.root(*name_lookup(c, dict, U"__new__"));
    ReturnSlot wrapper(c), key(c);
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                            function.get(c));
    make_str(c, U"__new__", key);
    name_store(c, dict, key.value(), wrapper.value());
    auto init_sig = unary;
    init_sig.parameters.push_back({U"decoder", ParameterKind::PositionalOrKeyword});
    init_sig.parameters.push_back({U"translate", ParameterKind::PositionalOrKeyword});
    ReturnSlot strict(c);
    make_str(c, U"strict", strict);
    auto strict_root = roots.root(strict.value());
    init_sig.parameters.push_back(
        {U"errors", ParameterKind::PositionalOrKeyword, strict_root.get(c)});
    install_native_attribute(c, core, type, U"__init__", init_sig, init);
    auto decode_sig = unary;
    decode_sig.parameters.push_back({U"input", ParameterKind::PositionalOnly});
    decode_sig.parameters.push_back(
        {U"final", ParameterKind::PositionalOnly, at(CoreObject::False).get(c)});
    install_native_attribute(c, core, type, U"decode", decode_sig, decode);
    install_native_attribute(c, core, type, U"reset", unary, reset);
    install_native_attribute(c, core, type, U"getstate", unary, getstate);
    auto setstate_sig = unary;
    setstate_sig.parameters.push_back({U"state", ParameterKind::PositionalOnly});
    install_native_attribute(c, core, type, U"setstate", setstate_sig, setstate);
    install_native_field(c, core, type, U"newlines", newlines);
    make_str(c, U"IncrementalNewlineDecoder", key);
    name_store(c, module_dict(c, module.get(c)), key.value(), type.get(c));
    out.ok(c.builtin(CoreObject::None));
}
} // namespace detail
} // namespace cxxpython::core

// Text streams use only the public buffer and codec protocols.
#include "cxxpython/import.hpp"
#include "internal/codecregistry.hpp"

namespace cxxpython::core {
namespace text_wrapper {
struct Stream {
    ObjectHeader header;
    AtomicGcEdge buffer, decoder, encoder, encoding, errors, snapshot;
    std::mutex mutex;
    std::condition_variable ready;
    std::thread::id owner;
    bool active = false, initialized = false, detached = false, eof = false;
    bool line_buffering = false, write_through = false;
    bool can_read = false, can_write = false;
    bool seekable = false, have_snapshot = false, snapshot_eof = false, has_read1 = false;
    std::int64_t snapshot_pos = 0;
    std::int64_t snapshot_feed = 0;
    int newline = 0;
    std::u32string pending;
    std::size_t used = 0;
    Stream(InitialType type, Value none)
        : header(type), buffer(none), decoder(none), encoder(none), encoding(none), errors(none),
          snapshot(none)
    {}
    void trace(Tracer &t) const noexcept
    {
        t.visit(buffer);
        t.visit(decoder);
        t.visit(encoder);
        t.visit(encoding);
        t.visit(errors);
        t.visit(snapshot);
    }
};

class Claim {
    Attachment &c;
    Stream &s;
    bool held = false;

  public:
    Claim(Attachment &ctx, Stream &stream, ReturnSlot &out) : c(ctx), s(stream)
    {
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                NativeWait wait(c);
                std::unique_lock parked(s.mutex);
                continue;
            }
            if (!s.active) {
                s.active = true;
                s.owner = std::this_thread::get_id();
                held = true;
                return;
            }
            if (s.owner == std::this_thread::get_id()) {
                lock.unlock();
                raise_error(c, CoreObject::RuntimeErrorType, U"reentrant text stream operation",
                            out);
                return;
            }
            lock.unlock();
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
            s.ready.wait(parked, [&] { return !s.active; });
        }
    }
    ~Claim()
    {
        if (!held)
            return;
        for (;;) {
            std::unique_lock lock(s.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                s.active = false;
                s.owner = {};
                s.ready.notify_one();
                return;
            }
            NativeWait wait(c);
            std::unique_lock parked(s.mutex);
        }
    }
    explicit operator bool() const
    {
        return held;
    }
};
bool receiver(Attachment &c, Value v, ReturnSlot &out)
{
    if (c.has_layout<Stream>(v))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected TextIOWrapper", out);
    return false;
}
Handle edge(Attachment &c, Handle self, AtomicGcEdge &e, RootScope &r)
{
    ReturnSlot value(c);
    c.load(self.get(c), e, value);
    return r.root(value.value());
}
bool attribute(Attachment &c, Handle obj, const char32_t *name, ReturnSlot &out)
{
    RootScope r(c);
    ReturnSlot key(c);
    make_str(c, name, key);
    get_attribute(c, obj, r.root(key.value()), out);
    return out.tag() != ResultTag::Raised;
}
bool method(Attachment &c, Handle obj, const char32_t *name, std::span<const Value> args,
            ReturnSlot &out)
{
    return detail::io_call(c, obj, name, args, out);
}
bool usable(Attachment &c, Stream &s, ReturnSlot &out)
{
    if (!s.initialized || s.detached) {
        raise_error(c, CoreObject::ValueErrorType, U"underlying buffer has been detached", out);
        return false;
    }
    return true;
}
bool is_closed(Attachment &c, Handle buffer, ReturnSlot &out)
{
    RootScope r(c);
    ReturnSlot value(c), b(c);
    if (!attribute(c, buffer, U"closed", value)) {
        out.raised(value.exception());
        return true;
    }
    truth(c, r.root(value.value()), b);
    if (b.tag() == ResultTag::Raised) {
        out.raised(b.exception());
        return true;
    }
    if (b.value() != c.builtin(CoreObject::False)) {
        detail::io_closed_error(c, out);
        return true;
    }
    return false;
}
bool flag(Attachment &c, Value v, bool &result, ReturnSlot &out)
{
    RootScope r(c);
    ReturnSlot b(c);
    truth(c, r.root(v), b);
    if (b.tag() == ResultTag::Raised) {
        out.raised(b.exception());
        return false;
    }
    result = b.value() != c.builtin(CoreObject::False);
    return true;
}
void create(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    RootScope r(c);
    c.allocate_instance<Stream>(r.root(a[0]), out, c.builtin(CoreObject::None));
}
void init(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto buffer = r.root(a[1]);
    ReturnSlot default_encoding(c), default_errors(c);
    make_str(c, U"utf-8", default_encoding);
    make_str(c, U"strict", default_errors);
    auto encoding = r.root(a[2] == c.builtin(CoreObject::None) ? default_encoding.value() : a[2]);
    auto errors = r.root(a[3] == c.builtin(CoreObject::None) ? default_errors.value() : a[3]);
    if (!c.has_layout<StrObject>(encoding.get(c)) || !c.has_layout<StrObject>(errors.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"encoding and errors must be strings", out);
        return;
    }
    int nl = 0;
    if (a[4] != c.builtin(CoreObject::None)) {
        if (!c.has_layout<StrObject>(a[4])) {
            raise_error(c, CoreObject::TypeErrorType, U"newline must be str or None", out);
            return;
        }
        auto text = c.payload<StrObject>(a[4]).value;
        if (text.empty())
            nl = 1;
        else if (text == U"\n")
            nl = 2;
        else if (text == U"\r")
            nl = 3;
        else if (text == U"\r\n")
            nl = 4;
        else {
            raise_error(c, CoreObject::ValueErrorType, U"illegal newline value", out);
            return;
        }
    }
    bool line, through;
    if (!flag(c, a[5], line, out) || !flag(c, a[6], through, out))
        return;
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    ReturnSlot readable(c), writable(c);
    if (!method(c, buffer, U"readable", {}, readable)) {
        out.raised(readable.exception());
        return;
    }
    if (!method(c, buffer, U"writable", {}, writable)) {
        out.raised(writable.exception());
        return;
    }
    bool can_read, can_write;
    if (!flag(c, readable.value(), can_read, out) || !flag(c, writable.value(), can_write, out))
        return;
    ReturnSlot info(c);
    detail::lookup_codec(c, encoding, info);
    if (info.tag() == ResultTag::Raised) {
        out.raised(info.exception());
        return;
    }
    auto codec = r.root(info.value());
    ReturnSlot dec_type(c), enc_type(c), dec(c), enc(c);
    auto decoder = r.root(c.builtin(CoreObject::None));
    auto encoder = r.root(c.builtin(CoreObject::None));
    Value arg = errors.get(c);
    if (can_read) {
        if (!attribute(c, codec, U"incrementaldecoder", dec_type)) {
            out.raised(dec_type.exception());
            return;
        }
        if (dec_type.value() == c.builtin(CoreObject::None)) {
            raise_error(c, CoreObject::LookupErrorType, U"codec has no incremental decoder", out);
            return;
        }
        call(c, r.root(dec_type.value()), {{&arg, 1}}, dec);
        if (dec.tag() == ResultTag::Raised) {
            out.raised(dec.exception());
            return;
        }
        decoder = r.root(dec.value());
    }
    if (can_write) {
        if (!attribute(c, codec, U"incrementalencoder", enc_type)) {
            out.raised(enc_type.exception());
            return;
        }
        if (enc_type.value() == c.builtin(CoreObject::None)) {
            raise_error(c, CoreObject::LookupErrorType, U"codec has no incremental encoder", out);
            return;
        }
        call(c, r.root(enc_type.value()), {{&arg, 1}}, enc);
        if (enc.tag() == ResultTag::Raised) {
            out.raised(enc.exception());
            return;
        }
        encoder = r.root(enc.value());
    }
    if (can_read && nl <= 1) {
        ReturnSlot mod(c), klass(c), wrapped(c);
        import_module(c, U"_io", mod);
        if (mod.tag() == ResultTag::Raised) {
            out.raised(mod.exception());
            return;
        }
        if (!attribute(c, r.root(mod.value()), U"IncrementalNewlineDecoder", klass)) {
            out.raised(klass.exception());
            return;
        }
        std::array<Value, 2> args{decoder.get(c),
                                  c.builtin(nl == 0 ? CoreObject::True : CoreObject::False)};
        call(c, r.root(klass.value()), {args}, wrapped);
        if (wrapped.tag() == ResultTag::Raised) {
            out.raised(wrapped.exception());
            return;
        }
        decoder = r.root(wrapped.value());
    }
    ReturnSlot seekable_result(c);
    if (!method(c, buffer, U"seekable", {}, seekable_result)) {
        out.raised(seekable_result.exception());
        return;
    }
    bool can_seek;
    if (!flag(c, seekable_result.value(), can_seek, out))
        return;
    if (can_seek && can_write) {
        ReturnSlot position(c), equal(c), equal_truth(c), state_result(c);
        if (!method(c, buffer, U"tell", {}, position)) {
            out.raised(position.exception());
            return;
        }
        auto zero = r.root(c.builtin(CoreObject::IntZero));
        rich_compare(c, r.root(position.value()), zero, CompareOp::Eq, equal);
        if (equal.tag() == ResultTag::Raised) {
            out.raised(equal.exception());
            return;
        }
        truth(c, r.root(equal.value()), equal_truth);
        if (equal_truth.tag() == ResultTag::Raised) {
            out.raised(equal_truth.exception());
            return;
        }
        if (equal_truth.value() == c.builtin(CoreObject::False)) {
            Value state = zero.get(c);
            if (!method(c, encoder, U"setstate", {&state, 1}, state_result)) {
                out.raised(state_result.exception());
                return;
            }
        }
    }
    ReturnSlot read1(c);
    bool has_read1 = attribute(c, buffer, U"read1", read1);
    if (!has_read1 &&
        !is_subtype(c, c.type_of(read1.exception()), c.builtin(CoreObject::AttributeErrorType))) {
        out.raised(read1.exception());
        return;
    }
    c.store(self, s.buffer, buffer);
    c.store(self, s.decoder, decoder);
    c.store(self, s.encoder, encoder);
    c.store(self, s.encoding, encoding);
    c.store(self, s.errors, errors);
    s.newline = nl;
    s.line_buffering = line;
    s.write_through = through;
    s.pending.clear();
    s.used = 0;
    s.eof = false;
    s.detached = false;
    s.initialized = true;
    s.seekable = can_seek;
    s.has_read1 = has_read1;
    s.can_read = can_read;
    s.can_write = can_write;
    s.have_snapshot = false;
    out.ok(c.builtin(CoreObject::None));
}

// Retain the decoder state preceding each byte chunk for cookie reconstruction.
bool fill(Attachment &c, Handle self, Stream &s, ReturnSlot &out)
{
    RootScope r(c);
    auto buffer = edge(c, self, s.buffer, r);
    auto decoder = edge(c, self, s.decoder, r);
    if (s.seekable) {
        ReturnSlot pos(c), state(c);
        if (!method(c, buffer, U"tell", {}, pos)) {
            out.raised(pos.exception());
            return false;
        }
        if (!detail::io_index(c, r.root(pos.value()), s.snapshot_pos, out))
            return false;
        if (!method(c, decoder, U"getstate", {}, state)) {
            out.raised(state.exception());
            return false;
        }
        c.store(self, s.snapshot, r.root(state.value()));
        s.have_snapshot = true;
    }
    ReturnSlot size(c), data(c), result(c);
    make_int(c, 8192, size);
    Value n = size.value();
    if (!method(c, buffer, s.has_read1 ? U"read1" : U"read", {&n, 1}, data)) {
        out.raised(data.exception());
        return false;
    }
    auto bytes = r.root(data.value());
    if (!is_bytes(c, bytes.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"underlying read() should return bytes", out);
        return false;
    }
    auto count = c.payload<BytesObject>(bytes).value.size();
    bool eof = count == 0;
    std::array<Value, 2> args{bytes.get(c), c.builtin(eof ? CoreObject::True : CoreObject::False)};
    if (!method(c, decoder, U"decode", args, result)) {
        out.raised(result.exception());
        return false;
    }
    auto text = r.root(result.value());
    if (!c.has_layout<StrObject>(text.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"decoder should return a string", out);
        return false;
    }
    s.pending = c.payload<StrObject>(text).value;
    s.used = 0;
    s.eof = eof;
    s.snapshot_eof = eof;
    s.snapshot_feed = static_cast<std::int64_t>(count);
    c.account_external(self, s.pending.capacity() * sizeof(char32_t));
    return true;
}
template <bool line> void read(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    std::int64_t limit = -1;
    if (a[1] != c.builtin(CoreObject::None) && !detail::io_index(c, r.root(a[1]), limit, out))
        return;
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.can_read) {
        detail::io_unsupported(c, U"not readable", out);
        return;
    }
    auto buffer = edge(c, self, s.buffer, r);
    if (is_closed(c, buffer, out))
        return;
    std::u32string result;
    while (limit < 0 || result.size() < static_cast<std::uint64_t>(limit)) {
        if (s.used == s.pending.size()) {
            if (s.eof)
                break;
            if (!fill(c, self, s, out))
                return;
            if (s.pending.empty())
                continue;
        }
        char32_t ch = s.pending[s.used++];
        result.push_back(ch);
        if constexpr (line) {
            if (s.newline == 0 || s.newline == 2) {
                if (ch == U'\n')
                    break;
            } else if (s.newline == 3) {
                if (ch == U'\r')
                    break;
            } else if (s.newline == 4) {
                if (ch == U'\n' && result.size() >= 2 && result[result.size() - 2] == U'\r')
                    break;
            } else {
                if (ch == U'\n')
                    break;
                if (ch == U'\r') {
                    if ((limit < 0 || result.size() < static_cast<std::uint64_t>(limit))) {
                        while (s.used == s.pending.size() && !s.eof)
                            if (!fill(c, self, s, out))
                                return;
                        if (s.used < s.pending.size() && s.pending[s.used] == U'\n') {
                            result.push_back(U'\n');
                            ++s.used;
                        }
                    }
                    break;
                }
            }
        }
    }
    make_str(c, std::move(result), out);
}
void write(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto input = r.root(a[1]);
    if (!c.has_layout<StrObject>(input.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"write() argument must be str", out);
        return;
    }
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.can_write) {
        detail::io_unsupported(c, U"not writable", out);
        return;
    }
    auto buffer = edge(c, self, s.buffer, r);
    if (is_closed(c, buffer, out))
        return;
    auto text = c.payload<StrObject>(input).value;
    auto length = text.size();
    bool flush_line = s.line_buffering && (text.find(U'\n') != std::u32string::npos ||
                                           text.find(U'\r') != std::u32string::npos);
    if (s.newline == 3 || s.newline == 4) {
        std::u32string translated;
        for (auto ch : text) {
            if (ch == U'\n') {
                translated += U'\r';
                if (s.newline == 4)
                    translated += U'\n';
            } else
                translated += ch;
        }
        text.swap(translated);
    }
    ReturnSlot source(c), encoded(c), result(c);
    make_str(c, std::move(text), source);
    auto source_root = r.root(source.value());
    Value arg = source_root.get(c);
    auto encoder = edge(c, self, s.encoder, r);
    if (!method(c, encoder, U"encode", {&arg, 1}, encoded)) {
        out.raised(encoded.exception());
        return;
    }
    auto bytes = r.root(encoded.value());
    if (!is_bytes(c, bytes.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"encoder should return bytes", out);
        return;
    }
    arg = bytes.get(c);
    if (!method(c, buffer, U"write", {&arg, 1}, result)) {
        out.raised(result.exception());
        return;
    }
    if (flush_line && !method(c, buffer, U"flush", {}, result)) {
        out.raised(result.exception());
        return;
    }
    auto decoder = edge(c, self, s.decoder, r);
    if (s.can_read && !method(c, decoder, U"reset", {}, result)) {
        out.raised(result.exception());
        return;
    }
    s.pending.clear();
    s.used = 0;
    s.eof = false;
    s.have_snapshot = false;
    make_int(c, static_cast<std::int64_t>(length), out);
}
template <int operation>
void action(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    auto buffer = edge(c, self, s.buffer, r);
    ReturnSlot result(c);
    if constexpr (operation == 0) {
        if (!method(c, buffer, U"flush", {}, result)) {
            out.raised(result.exception());
            return;
        }
        out.ok(c.builtin(CoreObject::None));
    } else if constexpr (operation == 1) {
        ReturnSlot closed(c), is_true(c), flushed(c);
        if (!attribute(c, buffer, U"closed", closed)) {
            out.raised(closed.exception());
            return;
        }
        truth(c, r.root(closed.value()), is_true);
        if (is_true.tag() == ResultTag::Raised) {
            out.raised(is_true.exception());
            return;
        }
        if (is_true.value() != c.builtin(CoreObject::False)) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
        bool flush_ok = method(c, buffer, U"flush", {}, flushed);
        bool close_ok = method(c, buffer, U"close", {}, result);
        if (!flush_ok) {
            out.raised(flushed.exception());
            return;
        }
        if (!close_ok) {
            out.raised(result.exception());
            return;
        }
        out.ok(c.builtin(CoreObject::None));
    } else {
        if (!method(c, buffer, U"flush", {}, result)) {
            out.raised(result.exception());
            return;
        }
        s.detached = true;
        c.store(self.get(c), s.buffer, c.builtin(CoreObject::None));
        out.ok(buffer.get(c));
    }
}
template <int which> void property(Attachment &c, Value value, ReturnSlot &out)
{
    if (!receiver(c, value, out))
        return;
    RootScope r(c);
    auto self = r.root(value);
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim)
        return;
    if constexpr (which == 1 || which == 2) {
        if (!s.initialized) {
            raise_error(c, CoreObject::ValueErrorType, U"I/O operation on uninitialized object",
                        out);
            return;
        }
    } else if (!usable(c, s, out))
        return;
    if constexpr (which == 0)
        out.ok(edge(c, self, s.buffer, r).get(c));
    if constexpr (which == 1)
        out.ok(edge(c, self, s.encoding, r).get(c));
    if constexpr (which == 2)
        out.ok(edge(c, self, s.errors, r).get(c));
    if constexpr (which == 3)
        out.ok(c.builtin(s.line_buffering ? CoreObject::True : CoreObject::False));
    if constexpr (which == 4)
        out.ok(c.builtin(s.write_through ? CoreObject::True : CoreObject::False));
    if constexpr (which == 5) {
        auto b = edge(c, self, s.buffer, r);
        attribute(c, b, U"closed", out);
    }
    if constexpr (which == 6) {
        if (!s.can_read || s.newline > 1)
            out.ok(c.builtin(CoreObject::None));
        else {
            auto d = edge(c, self, s.decoder, r);
            attribute(c, d, U"newlines", out);
        }
    }
    if constexpr (which == 7) {
        auto b = edge(c, self, s.buffer, r);
        attribute(c, b, U"name", out);
    }
}
template <int which> void delegate(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    auto buffer = edge(c, self, s.buffer, r);
    constexpr const char32_t *name = which == 0   ? U"readable"
                                     : which == 1 ? U"writable"
                                     : which == 2 ? U"seekable"
                                     : which == 3 ? U"fileno"
                                                  : U"isatty";
    method(c, buffer, name, {}, out);
}
bool calc(Attachment &c, RootScope &r, Handle left, Handle right, BinaryOp op, Handle &result,
          ReturnSlot &out)
{
    ReturnSlot slot(c);
    binary(c, left, right, op, slot);
    if (slot.tag() == ResultTag::Raised) {
        out.raised(slot.exception());
        return false;
    }
    result = r.root(slot.value());
    return true;
}
Handle integer(Attachment &c, RootScope &r, std::int64_t n)
{
    ReturnSlot value(c);
    make_int(c, n, value);
    return r.root(value.value());
}
void tell(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.seekable) {
        detail::io_unsupported(c, U"underlying stream is not seekable", out);
        return;
    }
    auto buffer = edge(c, self, s.buffer, r);
    if (is_closed(c, buffer, out))
        return;
    ReturnSlot flush(c);
    if (!method(c, buffer, U"flush", {}, flush)) {
        out.raised(flush.exception());
        return;
    }
    if (!s.can_read) {
        method(c, buffer, U"tell", {}, out);
        return;
    }
    auto decoder = edge(c, self, s.decoder, r);
    Handle state = r.root(c.builtin(CoreObject::None));
    std::int64_t pos = 0;
    std::int64_t feed = 0, skip = 0, eof = 0;
    if (s.have_snapshot) {
        state = edge(c, self, s.snapshot, r);
        pos = s.snapshot_pos;
        feed = s.snapshot_feed;
        skip = static_cast<std::int64_t>(s.used);
        eof = s.snapshot_eof;
    } else {
        ReturnSlot st(c), position(c);
        if (!method(c, decoder, U"getstate", {}, st)) {
            out.raised(st.exception());
            return;
        }
        state = r.root(st.value());
        if (!method(c, buffer, U"tell", {}, position)) {
            out.raised(position.exception());
            return;
        }
        if (!detail::io_index(c, r.root(position.value()), pos, out))
            return;
    }
    if (!c.has_layout<TupleObject>(state.get(c)) ||
        c.payload<TupleObject>(state).items.size() != 2) {
        raise_error(c, CoreObject::TypeErrorType, U"decoder getstate() must return a 2-tuple", out);
        return;
    }
    auto bytes = r.root(c.payload<TupleObject>(state).items[0]);
    auto flags = r.root(c.payload<TupleObject>(state).items[1]);
    if (!is_bytes(c, bytes.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"decoder state buffer must be bytes", out);
        return;
    }
    auto count = c.payload<BytesObject>(bytes).value.size();
    if (pos < 0 || count > static_cast<std::uint64_t>(pos) ||
        count > static_cast<std::uint64_t>(0xffffffffll - feed) || skip > 0xffffffffll) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"decoder state cannot be represented in a cookie", out);
        return;
    }
    pos -= static_cast<std::int64_t>(count);
    feed += static_cast<std::int64_t>(count);
    auto result = integer(c, r, pos);
    Handle part = flags;
    if (!calc(c, r, part, integer(c, r, 64), BinaryOp::LeftShift, part, out) ||
        !calc(c, r, result, part, BinaryOp::BitOr, result, out))
        return;
    for (auto [value, shift] : {std::pair{feed, 96}, std::pair{skip, 128}, std::pair{eof, 160}}) {
        part = integer(c, r, value);
        if (!calc(c, r, part, integer(c, r, shift), BinaryOp::LeftShift, part, out) ||
            !calc(c, r, result, part, BinaryOp::BitOr, result, out))
            return;
    }
    out.ok(result.get(c));
}
bool cookie_field(Attachment &c, RootScope &r, Handle cookie, int shift, int bits,
                  std::int64_t &value, ReturnSlot &out)
{
    Handle shifted = cookie, mask = integer(c, r, 1);
    if (shift && !calc(c, r, cookie, integer(c, r, shift), BinaryOp::RightShift, shifted, out))
        return false;
    if (!calc(c, r, mask, integer(c, r, bits), BinaryOp::LeftShift, mask, out) ||
        !calc(c, r, mask, integer(c, r, 1), BinaryOp::Subtract, mask, out) ||
        !calc(c, r, shifted, mask, BinaryOp::BitAnd, shifted, out))
        return false;
    return detail::io_index(c, shifted, value, out);
}
void seek(Attachment &c, Handle, std::span<const Value> a, ReturnSlot &out)
{
    if (!receiver(c, a[0], out))
        return;
    RootScope r(c);
    auto self = r.root(a[0]);
    auto cookie = r.root(a[1]);
    std::int64_t whence;
    if (!detail::io_index(c, r.root(a[2]), whence, out))
        return;
    if (whence == 1) {
        std::int64_t offset;
        if (!detail::io_index(c, cookie, offset, out))
            return;
        if (offset != 0) {
            detail::io_unsupported(c, U"can't do nonzero cur-relative seeks", out);
            return;
        }
        std::array<Value, 1> self_arg{self.get(c)};
        tell(c, self, self_arg, out);
        return;
    }
    auto &s = c.payload<Stream>(self);
    Claim claim(c, s, out);
    if (!claim || !usable(c, s, out))
        return;
    if (!s.seekable) {
        detail::io_unsupported(c, U"underlying stream is not seekable", out);
        return;
    }
    auto buffer = edge(c, self, s.buffer, r);
    if (is_closed(c, buffer, out))
        return;
    if (whence != 0 && whence != 2) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid whence", out);
        return;
    }
    if (whence == 0) {
        ReturnSlot negative(c), is_negative(c);
        rich_compare(c, cookie, integer(c, r, 0), CompareOp::Lt, negative);
        if (negative.tag() == ResultTag::Raised) {
            out.raised(negative.exception());
            return;
        }
        truth(c, r.root(negative.value()), is_negative);
        if (is_negative.tag() == ResultTag::Raised) {
            out.raised(is_negative.exception());
            return;
        }
        if (is_negative.value() != c.builtin(CoreObject::False)) {
            raise_error(c, CoreObject::ValueErrorType, U"negative seek position", out);
            return;
        }
    }
    std::int64_t pos, flags, feed, skip, eof;
    if (!cookie_field(c, r, cookie, 0, 64, pos, out) ||
        !cookie_field(c, r, cookie, 64, 32, flags, out) ||
        !cookie_field(c, r, cookie, 96, 32, feed, out) ||
        !cookie_field(c, r, cookie, 128, 32, skip, out) ||
        !cookie_field(c, r, cookie, 160, 8, eof, out))
        return;
    if (whence != 0 &&
        !(whence == 2 && pos == 0 && flags == 0 && feed == 0 && skip == 0 && eof == 0)) {
        detail::io_unsupported(c, U"can't do nonzero cur-relative or end-relative seeks", out);
        return;
    }
    ReturnSlot result(c);
    if (!method(c, buffer, U"flush", {}, result)) {
        out.raised(result.exception());
        return;
    }
    auto offset = integer(c, r, pos);
    auto origin = integer(c, r, whence);
    std::array<Value, 2> args{offset.get(c), origin.get(c)};
    if (!method(c, buffer, U"seek", args, result)) {
        out.raised(result.exception());
        return;
    }
    auto actual_pos = r.root(result.value());
    auto decoder = edge(c, self, s.decoder, r);
    ReturnSlot empty(c), tuple(c), reset(c);
    make_bytes(c, "", empty);
    auto empty_root = r.root(empty.value());
    auto flag_root = integer(c, r, flags);
    std::array<Value, 2> state{empty_root.get(c), flag_root.get(c)};
    make_tuple(c, std::span<const Value>(state), tuple);
    auto tuple_root = r.root(tuple.value());
    Value arg = tuple_root.get(c);
    if (s.can_read && !method(c, decoder, U"setstate", {&arg, 1}, reset)) {
        out.raised(reset.exception());
        return;
    }
    auto encoder = edge(c, self, s.encoder, r);
    bool start = whence == 0 && pos == 0 && flags == 0;
    if (!s.can_write) {
        // Read-only streams do not own an encoder.
    } else if (start) {
        if (!method(c, encoder, U"reset", {}, reset)) {
            out.raised(reset.exception());
            return;
        }
    } else {
        auto zero = integer(c, r, 0);
        arg = zero.get(c);
        if (!method(c, encoder, U"setstate", {&arg, 1}, reset)) {
            out.raised(reset.exception());
            return;
        }
    }
    s.pending.clear();
    s.used = 0;
    s.eof = false;
    s.have_snapshot = false;
    if ((feed || eof) && s.can_read) {
        auto n = integer(c, r, feed);
        arg = n.get(c);
        ReturnSlot bytes(c), decoded(c);
        if (!method(c, buffer, U"read", {&arg, 1}, bytes)) {
            out.raised(bytes.exception());
            return;
        }
        auto b = r.root(bytes.value());
        if (!is_bytes(c, b.get(c)) ||
            c.payload<BytesObject>(b).value.size() != static_cast<std::size_t>(feed)) {
            raise_error(c, CoreObject::OSErrorType, U"can't restore logical file position", out);
            return;
        }
        std::array<Value, 2> decode_args{b.get(c),
                                         c.builtin(eof ? CoreObject::True : CoreObject::False)};
        if (!method(c, decoder, U"decode", decode_args, decoded)) {
            out.raised(decoded.exception());
            return;
        }
        auto text = r.root(decoded.value());
        if (!c.has_layout<StrObject>(text.get(c)) ||
            c.payload<StrObject>(text).value.size() < static_cast<std::size_t>(skip)) {
            raise_error(c, CoreObject::OSErrorType, U"can't restore logical file position", out);
            return;
        }
        s.pending = c.payload<StrObject>(text).value;
        c.account_external(self, s.pending.capacity() * sizeof(char32_t));
        s.used = static_cast<std::size_t>(skip);
        s.eof = eof != 0;
        s.snapshot_pos = pos;
        s.snapshot_feed = feed;
        s.snapshot_eof = eof != 0;
        s.have_snapshot = true;
        c.store(self, s.snapshot, tuple_root);
    }
    out.ok(whence == 2 ? actual_pos.get(c) : cookie.get(c));
}
} // namespace text_wrapper

namespace detail {
void initialize_textio_wrapper(Attachment &c, Handle module, ReturnSlot &out)
{
    using namespace text_wrapper;
    RootScope r(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(r.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    std::array<Handle, 1> bases{at(CoreObject::TextIOBaseType)};
    NativeTypeSpec spec{{"TextIOWrapper", bases}, LayoutDescriptor::of<Stream>()};
    spec.type.module = U"_io";
    ReturnSlot slot(c);
    create_native_type(c, spec, slot);
    if (slot.tag() == ResultTag::Raised) {
        out.raised(slot.exception());
        return;
    }
    auto type = r.root(slot.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto ctor = unary;
    ctor.varargs = U"args";
    ctor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", ctor, text_wrapper::create);
    auto dict = *TypeView(c, type).dictionary();
    auto fn = r.root(*name_lookup(c, dict, U"__new__"));
    ReturnSlot wrapper(c), key(c);
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper, fn.get(c));
    make_str(c, U"__new__", key);
    name_store(c, dict, key.value(), wrapper.value());
    auto sig = unary;
    sig.parameters.push_back({U"buffer", ParameterKind::PositionalOrKeyword});
    sig.parameters.push_back(
        {U"encoding", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    sig.parameters.push_back(
        {U"errors", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    sig.parameters.push_back(
        {U"newline", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    sig.parameters.push_back(
        {U"line_buffering", ParameterKind::PositionalOrKeyword, at(CoreObject::False).get(c)});
    sig.parameters.push_back(
        {U"write_through", ParameterKind::PositionalOrKeyword, at(CoreObject::False).get(c)});
    install_native_attribute(c, core, type, U"__init__", sig, text_wrapper::init);
    auto optional = unary;
    optional.parameters.push_back(
        {U"size", ParameterKind::PositionalOnly, at(CoreObject::None).get(c)});
    auto binary = unary;
    binary.parameters.push_back({U"text", ParameterKind::PositionalOnly});
    install_native_attribute(c, core, type, U"read", optional, text_wrapper::read<false>);
    install_native_attribute(c, core, type, U"readline", optional, text_wrapper::read<true>);
    install_native_attribute(c, core, type, U"write", binary, text_wrapper::write);
    install_native_attribute(c, core, type, U"flush", unary, action<0>);
    install_native_attribute(c, core, type, U"close", unary, action<1>);
    install_native_attribute(c, core, type, U"detach", unary, action<2>);
    install_native_attribute(c, core, type, U"tell", unary, tell);
    auto seek_sig = unary;
    seek_sig.parameters.push_back({U"cookie", ParameterKind::PositionalOnly});
    seek_sig.parameters.push_back(
        {U"whence", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    install_native_attribute(c, core, type, U"seek", seek_sig, seek);
    install_native_attribute(c, core, type, U"readable", unary, delegate<0>);
    install_native_attribute(c, core, type, U"writable", unary, delegate<1>);
    install_native_attribute(c, core, type, U"seekable", unary, delegate<2>);
    install_native_attribute(c, core, type, U"fileno", unary, delegate<3>);
    install_native_attribute(c, core, type, U"isatty", unary, delegate<4>);
    install_native_field(c, core, type, U"buffer", property<0>);
    install_native_field(c, core, type, U"encoding", property<1>);
    install_native_field(c, core, type, U"errors", property<2>);
    install_native_field(c, core, type, U"line_buffering", property<3>);
    install_native_field(c, core, type, U"write_through", property<4>);
    install_native_field(c, core, type, U"closed", property<5>);
    install_native_field(c, core, type, U"newlines", property<6>);
    install_native_field(c, core, type, U"name", property<7>);
    make_str(c, U"TextIOWrapper", key);
    name_store(c, module_dict(c, module.get(c)), key.value(), type.get(c));
    out.ok(c.builtin(CoreObject::None));
}
} // namespace detail
} // namespace cxxpython::core
