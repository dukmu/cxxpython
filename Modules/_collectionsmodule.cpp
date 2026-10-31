// Native deque implementation, based on CPython 3.12.14's _collectionsmodule.c.
#include "internal/collectionsmodule.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <algorithm>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <mutex>

namespace cxxpython::core::detail {
namespace {
struct DequeObject {
    struct Storage {
        std::mutex mutex;
        std::list<Value> items;
        std::uint64_t version = 0;
        std::int64_t maxlen = -1;
    };
    ObjectHeader header;
    const std::unique_ptr<Storage> contents;
    explicit DequeObject(InitialType type) : header(type), contents(std::make_unique<Storage>()) {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto value : contents->items)
            tracer.visit(value);
    }
    std::size_t external_bytes() const noexcept
    {
        return sizeof(Storage) + contents->items.size() * (sizeof(Value) + 2 * sizeof(void *));
    }
};

struct DequeIterator {
    ObjectHeader header;
    AtomicGcEdge owner;
    std::size_t remaining = 0;
    std::list<Value>::iterator cursor;
    std::uint64_t version = 0;
    bool reverse = false;
    DequeIterator(InitialType type, Value value, bool reversed)
        : header(type), owner(value), reverse(reversed)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
    }
};

struct TupleGetter {
    ObjectHeader header;
    const std::int64_t index;
    AtomicGcEdge doc;
    TupleGetter(InitialType type, std::int64_t offset, Value documentation)
        : header(type), index(offset), doc(documentation)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(doc);
    }
};

template <class F> auto locked(Attachment &c, DequeObject::Storage &storage, F &&f)
{
    for (;;) {
        std::unique_lock lock(storage.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint guard(c);
            return f();
        }
        NativeWait wait(c);
        storage.mutex.lock();
        storage.mutex.unlock();
    }
}

bool check(Attachment &c, Value self, ReturnSlot &out)
{
    if (c.has_layout<DequeObject>(self))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected a deque", out);
    return false;
}

bool as_index(Attachment &c, Handle value, std::int64_t &number, ReturnSlot &out)
{
    ReturnSlot result(c);
    index(c, value, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return false;
    }
    if (!int_fits_signed(c, result.value(), number)) {
        raise_error(c, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return false;
    }
    return true;
}

void create(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto base = c.payload<NativeFunctionObject>(function).captures[0];
    if (!c.has_layout<TypeObject>(args[0]) || !is_subtype(c, args[0], base)) {
        raise_error(c, CoreObject::TypeErrorType, U"deque.__new__ requires a deque subtype", out);
        return;
    }
    c.allocate_instance<DequeObject>(roots.root(args[0]), out);
}

void append_item(Attachment &c, Handle self, Value value, bool left)
{
    auto &s = *c.payload<DequeObject>(self).contents;
    std::list<Value> prepared{value}, discarded;
    locked(c, s, [&] {
        if (left)
            s.items.splice(s.items.begin(), prepared);
        else
            s.items.splice(s.items.end(), prepared);
        ++s.version;
        if (s.maxlen >= 0 && s.items.size() > static_cast<std::size_t>(s.maxlen)) {
            if (left)
                discarded.splice(discarded.end(), s.items, std::prev(s.items.end()));
            else
                discarded.splice(discarded.end(), s.items, s.items.begin());
        }
        c.account_external(self, sizeof(DequeObject::Storage) +
                                     s.items.size() * (sizeof(Value) + 2 * sizeof(void *)));
    });
}

template <bool Left>
void append(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    roots.root(args[1]);
    append_item(c, self, args[1], Left);
    out.ok(c.builtin(CoreObject::None));
}

template <bool Left> void pop(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    auto &s = *c.payload<DequeObject>(args[0]).contents;
    std::list<Value> discarded;
    bool empty = locked(c, s, [&] {
        if (s.items.empty())
            return true;
        out.ok(Left ? s.items.front() : s.items.back());
        if (Left)
            discarded.splice(discarded.end(), s.items, s.items.begin());
        else
            discarded.splice(discarded.end(), s.items, std::prev(s.items.end()));
        ++s.version;
        c.account_external(args[0], sizeof(DequeObject::Storage) +
                                        s.items.size() * (sizeof(Value) + 2 * sizeof(void *)));
        return false;
    });
    if (empty)
        raise_error(c, CoreObject::IndexErrorType, U"pop from an empty deque", out);
}

void clear(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    auto &s = *c.payload<DequeObject>(args[0]).contents;
    std::list<Value> discarded;
    locked(c, s, [&] {
        discarded.splice(discarded.end(), s.items);
        ++s.version;
        c.account_external(args[0], sizeof(DequeObject::Storage));
    });
    out.ok(c.builtin(CoreObject::None));
}

void size(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    auto &s = *c.payload<DequeObject>(args[0]).contents;
    auto n = locked(c, s, [&] { return s.items.size(); });
    make_int(c, static_cast<std::int64_t>(n), out);
}

void maxlen(Attachment &c, Value self, ReturnSlot &out)
{
    auto &s = *c.payload<DequeObject>(self).contents;
    auto n = locked(c, s, [&] { return s.maxlen; });
    if (n < 0)
        out.ok(c.builtin(CoreObject::None));
    else
        make_int(c, n, out);
}

template <bool Left> bool extend_from(Attachment &c, Handle self, Handle input, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot iterator(c), item(c);
    // CPython extends from itself using a list copy.
    if (self.get(c) == input.get(c)) {
        auto &s = *c.payload<DequeObject>(self).contents;
        std::deque<ReturnSlot> slots;
        std::size_t n = 0;
        for (;;) {
            auto needed = locked(c, s, [&] { return s.items.size(); });
            while (slots.size() < needed)
                slots.emplace_back(c);
            if (locked(c, s, [&] {
                    if (s.items.size() > slots.size())
                        return false;
                    n = s.items.size();
                    std::size_t i = 0;
                    for (auto value : s.items)
                        slots[i++].ok(value);
                    return true;
                }))
                break;
        }
        for (std::size_t i = 0; i < n; ++i)
            append_item(c, self, slots[i].value(), Left);
        return true;
    }
    get_iter(c, input, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return false;
    }
    auto it = roots.root(iterator.value());
    for (;;) {
        auto status = iter_next(c, it, item);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return false;
        }
        append_item(c, self, item.value(), Left);
    }
}

template <bool Left>
void extend(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    if (extend_from<Left>(c, roots.root(args[0]), roots.root(args[1]), out))
        out.ok(c.builtin(CoreObject::None));
}

void deque_copy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot limit(c);
    maxlen(c, self.get(c), limit);
    if (limit.tag() == ResultTag::Raised) {
        out.raised(limit.exception());
        return;
    }
    const Value arguments[] = {self.get(c), limit.value()};
    call(c, roots.root(c.type_of(self)), {arguments}, out);
}

void deque_inplace_add(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), input = roots.root(args[1]);
    if (extend_from<false>(c, self, input, out))
        out.ok(self.get(c));
}

void initialize(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto input = roots.root(args[1]);
    std::int64_t limit = -1;
    if (args[2] != c.builtin(CoreObject::None)) {
        if (!as_index(c, roots.root(args[2]), limit, out))
            return;
        if (limit < 0) {
            raise_error(c, CoreObject::ValueErrorType, U"maxlen must be non-negative", out);
            return;
        }
    }
    auto &s = *c.payload<DequeObject>(self).contents;
    std::list<Value> discarded;
    locked(c, s, [&] {
        discarded.splice(discarded.end(), s.items);
        s.maxlen = limit;
        ++s.version;
        c.account_external(self, sizeof(DequeObject::Storage));
    });
    if (extend_from<false>(c, self, input, out))
        out.ok(c.builtin(CoreObject::None));
}

enum class ItemOp { Get, Set, Delete, Insert };
std::list<Value>::iterator position(DequeObject::Storage &s, std::size_t at)
{
    if (at <= s.items.size() / 2)
        return std::next(s.items.begin(), static_cast<std::ptrdiff_t>(at));
    return std::prev(s.items.end(), static_cast<std::ptrdiff_t>(s.items.size() - at));
}
template <ItemOp Op> void item(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    std::int64_t n;
    if (!as_index(c, roots.root(args[1]), n, out))
        return;
    if constexpr (Op == ItemOp::Set || Op == ItemOp::Insert)
        roots.root(args[2]);
    auto &s = *c.payload<DequeObject>(self).contents;
    std::list<Value> prepared, discarded;
    if constexpr (Op == ItemOp::Insert)
        prepared.push_back(args[2]);
    auto result = locked(c, s, [&] {
        auto length = static_cast<std::int64_t>(s.items.size());
        auto at = n;
        if (at < 0)
            at = at < -length ? 0 : at + length;
        if constexpr (Op == ItemOp::Insert) {
            if (s.maxlen >= 0 && length >= s.maxlen)
                return 2;
            at = std::min(at, length);
            s.items.splice(position(s, static_cast<std::size_t>(at)), prepared);
            ++s.version;
            c.account_external(self, sizeof(DequeObject::Storage) +
                                         s.items.size() * (sizeof(Value) + 2 * sizeof(void *)));
        } else {
            if ((n < 0 && n < -length) || at >= length)
                return 1;
            auto node = position(s, static_cast<std::size_t>(at));
            if constexpr (Op == ItemOp::Get)
                out.ok(*node);
            if constexpr (Op == ItemOp::Set)
                *node = args[2];
            if constexpr (Op == ItemOp::Delete) {
                discarded.splice(discarded.end(), s.items, node);
                ++s.version;
                c.account_external(self, sizeof(DequeObject::Storage) +
                                             s.items.size() * (sizeof(Value) + 2 * sizeof(void *)));
            }
        }
        return 0;
    });
    if (result == 1)
        raise_error(c, CoreObject::IndexErrorType, U"deque index out of range", out);
    else if (result == 2)
        raise_error(c, CoreObject::IndexErrorType, U"deque already at its maximum size", out);
    else if constexpr (Op != ItemOp::Get)
        out.ok(c.builtin(CoreObject::None));
}

void rotate(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    std::int64_t n;
    if (!as_index(c, roots.root(args[1]), n, out))
        return;
    auto &s = *c.payload<DequeObject>(args[0]).contents;
    locked(c, s, [&] {
        if (s.items.size() > 1) {
            auto length = static_cast<std::int64_t>(s.items.size());
            n %= length;
            if (n > length / 2)
                n -= length;
            if (n < -length / 2)
                n += length;
            while (n > 0) {
                s.items.splice(s.items.begin(), s.items, std::prev(s.items.end()));
                --n;
            }
            while (n < 0) {
                s.items.splice(s.items.end(), s.items, s.items.begin());
                ++n;
            }
        }
        ++s.version;
    });
    out.ok(c.builtin(CoreObject::None));
}

void reverse(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    auto &s = *c.payload<DequeObject>(args[0]).contents;
    locked(c, s, [&] { std::reverse(s.items.begin(), s.items.end()); });
    out.ok(c.builtin(CoreObject::None));
}

template <bool Reverse>
void iterator(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto type = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    c.allocate_instance<DequeIterator>(type, out, self.get(c), Reverse);
    if (out.tag() == ResultTag::Raised)
        return;
    auto &it = c.payload<DequeIterator>(out.value());
    auto &s = *c.payload<DequeObject>(self).contents;
    locked(c, s, [&] {
        it.version = s.version;
        it.remaining = s.items.size();
        it.cursor = Reverse ? s.items.end() : s.items.begin();
    });
}

void iter_self(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}

void next(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<DequeIterator>(args[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"expected deque iterator", out);
        return;
    }
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &it = c.payload<DequeIterator>(self);
    ReturnSlot owner(c);
    c.load(self.get(c), it.owner, owner);
    if (owner.tag() == ResultTag::Empty) {
        stop_iteration(c, out);
        return;
    }
    auto deque = roots.root(owner.value());
    auto &s = *c.payload<DequeObject>(deque).contents;
    auto status = locked(c, s, [&] {
        if (it.version != s.version) {
            it.remaining = 0;
            return 2;
        }
        if (!it.remaining)
            return 1;
        if (it.reverse)
            --it.cursor;
        out.ok(*it.cursor);
        if (!it.reverse)
            ++it.cursor;
        --it.remaining;
        return 0;
    });
    if (status == 1)
        stop_iteration(c, out);
    else if (status == 2)
        raise_error(c, CoreObject::RuntimeErrorType, U"deque mutated during iteration", out);
}

enum class Search { Count, Index, Remove, Contains };
template <Search Mode>
void search(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto needle = roots.root(args[1]);
    std::int64_t start = 0, stop = std::numeric_limits<std::int64_t>::max();
    if constexpr (Mode == Search::Index) {
        if (!as_index(c, roots.root(args[2]), start, out) ||
            !as_index(c, roots.root(args[3]), stop, out))
            return;
    }
    auto &s = *c.payload<DequeObject>(self).contents;
    std::uint64_t version;
    std::size_t length;
    locked(c, s, [&] {
        version = s.version;
        length = s.items.size();
    });
    auto len = static_cast<std::int64_t>(length);
    if (start < 0)
        start = start < -len ? 0 : start + len;
    if (stop < 0)
        stop = stop < -len ? 0 : stop + len;
    stop = std::min(stop, len);
    std::int64_t matches = 0;
    ReturnSlot value(c), comparison(c), boolean(c);
    std::list<Value>::iterator cursor;
    std::list<Value> discarded;
    for (auto i = start; i < stop; ++i) {
        bool changed = locked(c, s, [&] {
            if (version != s.version)
                return true;
            if (i == start)
                cursor = position(s, static_cast<std::size_t>(start));
            value.ok(*cursor);
            return false;
        });
        if (changed) {
            raise_error(c, CoreObject::RuntimeErrorType, U"deque mutated during iteration", out);
            return;
        }
        container_equal(c, roots.root(value.value()), needle, comparison);
        if (comparison.tag() == ResultTag::Raised) {
            out.raised(comparison.exception());
            return;
        }
        truth(c, roots.root(comparison.value()), boolean);
        if (boolean.tag() == ResultTag::Raised) {
            out.raised(boolean.exception());
            return;
        }
        bool equal = boolean.value() == c.builtin(CoreObject::True);
        changed = locked(c, s, [&] {
            if (version != s.version)
                return true;
            if constexpr (Mode == Search::Remove) {
                if (equal) {
                    discarded.splice(discarded.end(), s.items, cursor);
                    ++s.version;
                    c.account_external(self,
                                       sizeof(DequeObject::Storage) +
                                           s.items.size() * (sizeof(Value) + 2 * sizeof(void *)));
                    return false;
                }
            }
            ++cursor;
            return false;
        });
        if (changed) {
            raise_error(c, CoreObject::RuntimeErrorType, U"deque mutated during iteration", out);
            return;
        }
        if (!equal)
            continue;
        if constexpr (Mode == Search::Count)
            ++matches;
        if constexpr (Mode == Search::Index) {
            make_int(c, i, out);
            return;
        }
        if constexpr (Mode == Search::Contains) {
            out.ok(c.builtin(CoreObject::True));
            return;
        }
        if constexpr (Mode == Search::Remove) {
            out.ok(c.builtin(CoreObject::None));
            return;
        }
    }
    if constexpr (Mode == Search::Count)
        make_int(c, matches, out);
    if constexpr (Mode == Search::Contains)
        out.ok(c.builtin(CoreObject::False));
    if constexpr (Mode == Search::Remove)
        raise_error(c, CoreObject::ValueErrorType, U"deque.remove(x): x not in deque", out);
    if constexpr (Mode == Search::Index) {
        repr(c, needle, value);
        if (value.tag() == ResultTag::Raised) {
            out.raised(value.exception());
            return;
        }
        auto message = c.payload<StrObject>(value.value()).value + U" is not in deque";
        raise_error(c, CoreObject::ValueErrorType, std::move(message), out);
    }
}

void representation(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!c.enter_repr(self)) {
        make_str(c, U"[...]", out);
        return;
    }
    struct Guard {
        Attachment &context;
        Value self;
        ~Guard()
        {
            context.leave_repr(self);
        }
    } guard{c, self.get(c)};
    auto &s = *c.payload<DequeObject>(self).contents;
    std::deque<ReturnSlot> slots;
    std::size_t count = 0;
    std::int64_t limit = -1;
    std::uint64_t version = 0;
    for (;;) {
        auto needed = locked(c, s, [&] { return s.items.size(); });
        while (slots.size() < needed)
            slots.emplace_back(c);
        if (locked(c, s, [&] {
                if (s.items.size() > slots.size())
                    return false;
                count = s.items.size();
                limit = s.maxlen;
                version = s.version;
                std::size_t i = 0;
                for (auto value : s.items)
                    slots[i++].ok(value);
                return true;
            }))
            break;
    }
    auto type_name = TypeView(c, c.type_of(self)).name();
    std::u32string text = std::holds_alternative<Value>(type_name)
                              ? c.payload<StrObject>(std::get<Value>(type_name)).value
                              : std::get<std::u32string>(type_name);
    text += U"([";
    ReturnSlot result(c);
    for (std::size_t i = 0; i < count; ++i) {
        if (i)
            text += U", ";
        repr(c, roots.root(slots[i].value()), result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
        if (locked(c, s, [&] { return s.version != version; })) {
            raise_error(c, CoreObject::RuntimeErrorType, U"deque mutated during iteration", out);
            return;
        }
        text += c.payload<StrObject>(result.value()).value;
    }
    text += U"]";
    if (limit >= 0) {
        text += U", maxlen=";
        auto number = std::to_string(limit);
        text.append(number.begin(), number.end());
    }
    text += U")";
    make_str(c, std::move(text), out);
}

template <CompareOp Op>
void compare(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check(c, args[0], out))
        return;
    if (!c.has_layout<DequeObject>(args[1])) {
        out.ok(c.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(c);
    auto left = roots.root(args[0]), right = roots.root(args[1]);
    auto &a = *c.payload<DequeObject>(left).contents;
    auto &b = *c.payload<DequeObject>(right).contents;
    std::deque<ReturnSlot> first, second;
    std::uint64_t version_a = 0, version_b = 0;
    auto snapshot = [&](DequeObject::Storage &s, std::deque<ReturnSlot> &slots,
                        std::uint64_t &version) {
        for (;;) {
            auto count = locked(c, s, [&] { return s.items.size(); });
            while (slots.size() < count)
                slots.emplace_back(c);
            if (locked(c, s, [&] {
                    if (s.items.size() > slots.size())
                        return false;
                    std::size_t i = 0;
                    for (auto value : s.items)
                        slots[i++].ok(value);
                    count = s.items.size();
                    version = s.version;
                    return true;
                })) {
                while (slots.size() > count)
                    slots.pop_back();
                return;
            }
        }
    };
    snapshot(a, first, version_a);
    snapshot(b, second, version_b);
    if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne) {
        if (first.size() != second.size()) {
            out.ok(c.builtin(Op == CompareOp::Eq ? CoreObject::False : CoreObject::True));
            return;
        }
    }
    ReturnSlot result(c), boolean(c);
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i) {
        auto x = roots.root(first[i].value()), y = roots.root(second[i].value());
        container_equal(c, x, y, result);
        if (result.tag() == ResultTag::Raised) {
            out.raised(result.exception());
            return;
        }
        truth(c, roots.root(result.value()), boolean);
        if (boolean.tag() == ResultTag::Raised) {
            out.raised(boolean.exception());
            return;
        }
        if (locked(c, a, [&] { return a.version != version_a; }) ||
            locked(c, b, [&] { return b.version != version_b; })) {
            raise_error(c, CoreObject::RuntimeErrorType, U"deque mutated during iteration", out);
            return;
        }
        if (boolean.value() == c.builtin(CoreObject::True))
            continue;
        if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne)
            out.ok(c.builtin(Op == CompareOp::Ne ? CoreObject::True : CoreObject::False));
        else
            rich_compare(c, x, y, Op, out);
        return;
    }
    bool result_value = false;
    if constexpr (Op == CompareOp::Eq)
        result_value = first.size() == second.size();
    if constexpr (Op == CompareOp::Ne)
        result_value = first.size() != second.size();
    if constexpr (Op == CompareOp::Lt)
        result_value = first.size() < second.size();
    if constexpr (Op == CompareOp::Le)
        result_value = first.size() <= second.size();
    if constexpr (Op == CompareOp::Gt)
        result_value = first.size() > second.size();
    if constexpr (Op == CompareOp::Ge)
        result_value = first.size() >= second.size();
    out.ok(c.builtin(result_value ? CoreObject::True : CoreObject::False));
}

void tuplegetter_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto type = c.payload<NativeFunctionObject>(function).captures[0];
    if (args[0] != type) {
        raise_error(c, CoreObject::TypeErrorType, U"_tuplegetter is not an acceptable base type",
                    out);
        return;
    }
    std::int64_t offset;
    if (!as_index(c, roots.root(args[1]), offset, out))
        return;
    roots.root(args[2]);
    c.allocate_instance<TupleGetter>(roots.root(type), out, offset, args[2]);
}

void tuplegetter_get(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<TupleGetter>(args[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"expected _tuplegetter", out);
        return;
    }
    if (args[1] == c.builtin(CoreObject::None)) {
        out.ok(args[0]);
        return;
    }
    if (!c.has_layout<TupleObject>(args[1])) {
        raise_error(c, CoreObject::TypeErrorType, U"descriptor requires a tuple", out);
        return;
    }
    auto offset = c.payload<TupleGetter>(args[0]).index;
    const auto &items = c.payload<TupleObject>(args[1]).items;
    if (offset < 0 || static_cast<std::uint64_t>(offset) >= items.size()) {
        raise_error(c, CoreObject::IndexErrorType, U"tuple index out of range", out);
        return;
    }
    out.ok(items[static_cast<std::size_t>(offset)]);
}

template <bool Delete>
void tuplegetter_set(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    raise_error(c, CoreObject::AttributeErrorType,
                Delete ? U"can't delete attribute" : U"can't set attribute", out);
}

void tuplegetter_doc_get(Attachment &c, Value self, ReturnSlot &out)
{
    c.load(self, c.payload<TupleGetter>(self).doc, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(CoreObject::None));
}

void tuplegetter_doc_set(Attachment &c, Value self, std::optional<Value> value, ReturnSlot &out)
{
    c.store(self, c.payload<TupleGetter>(self).doc, value);
    out.ok(c.builtin(CoreObject::None));
}

void tuplegetter_repr(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &getter = c.payload<TupleGetter>(self);
    ReturnSlot doc(c), rendered(c);
    c.load(self.get(c), getter.doc, doc);
    if (doc.tag() == ResultTag::Empty)
        doc.ok(c.builtin(CoreObject::None));
    repr(c, roots.root(doc.value()), rendered);
    if (rendered.tag() == ResultTag::Raised) {
        out.raised(rendered.exception());
        return;
    }
    auto number = std::to_string(getter.index);
    std::u32string text = U"_tuplegetter(";
    text.append(number.begin(), number.end());
    text += U", ";
    text += c.payload<StrObject>(rendered.value()).value;
    text += U")";
    make_str(c, std::move(text), out);
}

void tuplegetter_reduce(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto &getter = c.payload<TupleGetter>(self);
    ReturnSlot doc(c), number(c), packed(c);
    c.load(self.get(c), getter.doc, doc);
    if (doc.tag() == ResultTag::Empty)
        doc.ok(c.builtin(CoreObject::None));
    make_int(c, getter.index, number);
    if (number.tag() == ResultTag::Raised) {
        out.raised(number.exception());
        return;
    }
    const Value constructor_args[] = {number.value(), doc.value()};
    make_tuple(c, constructor_args, packed);
    if (packed.tag() == ResultTag::Raised) {
        out.raised(packed.exception());
        return;
    }
    const Value result[] = {c.type_of(self), packed.value()};
    make_tuple(c, result, out);
}

void count_elements(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto mapping = roots.root(args[0]), source = roots.root(args[1]);
    ReturnSlot iterator(c), item(c), get(c), result(c), number(c), h(c), key(c);
    get_iter(c, source, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return;
    }
    auto it = roots.root(iterator.value());
    auto dict_type = c.builtin(CoreObject::DictType);
    auto mapping_type = c.type_of(mapping);
    bool fast = c.has_layout<DictObject>(mapping.get(c)) &&
                type_attribute(c, mapping_type, U"get") == type_attribute(c, dict_type, U"get") &&
                type_attribute(c, mapping_type, U"__setitem__") ==
                    type_attribute(c, dict_type, U"__setitem__");
    if (!fast) {
        make_str(c, U"get", key);
        get_attribute(c, mapping, roots.root(key.value()), get);
        if (get.tag() == ResultTag::Raised) {
            out.raised(get.exception());
            return;
        }
    }
    for (;;) {
        auto status = iter_next(c, it, item);
        if (status == NextStatus::Done)
            break;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        RootScope step(c);
        auto element = step.root(item.value());
        std::int64_t hash_value = 0;
        if (fast) {
            hash(c, element, h);
            if (h.tag() == ResultTag::Raised) {
                out.raised(h.exception());
                return;
            }
            if (!int_fits_signed(c, h.value(), hash_value))
                throw std::logic_error("invalid hash result");
            dict_lookup_hash(c, mapping, element, hash_value, result);
            if (result.tag() == ResultTag::Raised) {
                out.raised(result.exception());
                return;
            }
            if (result.tag() == ResultTag::Empty) {
                dict_store_hash(c, mapping, element, step.root(c.builtin(CoreObject::IntOne)),
                                hash_value, out);
                if (out.tag() == ResultTag::Raised)
                    return;
                continue;
            }
        } else {
            const Value arguments[] = {element.get(c), c.builtin(CoreObject::IntZero)};
            call(c, step.root(get.value()), {arguments}, result);
            if (result.tag() == ResultTag::Raised) {
                out.raised(result.exception());
                return;
            }
        }
        binary(c, step.root(result.value()), step.root(c.builtin(CoreObject::IntOne)),
               BinaryOp::Add, number);
        if (number.tag() == ResultTag::Raised) {
            out.raised(number.exception());
            return;
        }
        if (fast)
            dict_store_hash(c, mapping, element, step.root(number.value()), hash_value, out);
        else
            set_item(c, mapping, element, step.root(number.value()), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}

bool defaultdict_receiver(Attachment &c, Handle function, Value self, ReturnSlot &out)
{
    auto type = c.payload<NativeFunctionObject>(function).captures[0];
    if (c.has_layout<DictObject>(self) && is_subtype(c, c.type_of(self), type))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected defaultdict", out);
    return false;
}

void defaultdict_init(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!defaultdict_receiver(c, function, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto positional = roots.root(args[1]);
    auto keywords = roots.root(args[2]);
    const auto &values = c.payload<TupleObject>(positional).items;
    Value factory = c.builtin(CoreObject::None);
    if (!values.empty()) {
        factory = values[0];
        if (factory != c.builtin(CoreObject::None) &&
            !type_attribute(c, c.type_of(factory), U"__call__")) {
            raise_error(c, CoreObject::TypeErrorType, U"first argument must be callable or None",
                        out);
            return;
        }
    }
    roots.root(factory);
    ReturnSlot rest(c);
    auto remaining = std::span<const Value>(values).subspan(values.empty() ? 0 : 1);
    make_tuple(c, remaining, rest);
    if (rest.tag() == ResultTag::Raised) {
        out.raised(rest.exception());
        return;
    }
    auto input = roots.root(rest.value());
    c.store(self.get(c), c.instance_slot(self.get(c), 0), factory);
    auto dict_init = roots.root(c.payload<NativeFunctionObject>(function).captures[1]);
    const Value bound[] = {self.get(c), input.get(c), keywords.get(c)};
    auto entry = c.payload<NativeFunctionObject>(dict_init).entry;
    entry(c, dict_init, std::span<const Value>(bound, 3), out);
}

void defaultdict_missing(Attachment &c, Handle function, std::span<const Value> args,
                         ReturnSlot &out)
{
    if (!defaultdict_receiver(c, function, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]), key = roots.root(args[1]);
    ReturnSlot factory(c), result(c);
    c.load(self.get(c), c.instance_slot(self.get(c), 0), factory);
    if (factory.tag() == ResultTag::Empty || factory.value() == c.builtin(CoreObject::None)) {
        const Value arguments[] = {key.get(c)};
        call(c, roots.root(c.builtin(CoreObject::KeyErrorType)), {arguments}, result);
        if (result.tag() == ResultTag::Raised)
            out.raised(result.exception());
        else
            out.raised(result.value());
        return;
    }
    call(c, roots.root(factory.value()), {}, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    auto value = roots.root(result.value());
    dict_store(c, self, key, value, out);
    if (out.tag() != ResultTag::Raised)
        out.ok(value.get(c));
}

void defaultdict_repr(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!defaultdict_receiver(c, function, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    auto dict_repr = roots.root(c.payload<NativeFunctionObject>(function).captures[1]);
    ReturnSlot result(c), factory(c), factory_repr(c);
    const Value arguments[] = {self.get(c)};
    call(c, dict_repr, {arguments}, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    auto dictionary_text = roots.root(result.value());
    c.load(self.get(c), c.instance_slot(self.get(c), 0), factory);
    if (factory.tag() == ResultTag::Empty)
        factory.ok(c.builtin(CoreObject::None));
    auto factory_handle = roots.root(factory.value());
    if (c.enter_repr(factory_handle)) {
        struct ReprGuard {
            Attachment &context;
            Value object;
            ~ReprGuard()
            {
                context.leave_repr(object);
            }
        } guard{c, factory.value()};
        repr(c, factory_handle, factory_repr);
    } else {
        make_str(c, U"...", factory_repr);
    }
    if (factory_repr.tag() == ResultTag::Raised) {
        out.raised(factory_repr.exception());
        return;
    }
    auto name = TypeView(c, c.type_of(self)).name();
    std::u32string text = std::holds_alternative<Value>(name)
                              ? c.payload<StrObject>(std::get<Value>(name)).value
                              : std::get<std::u32string>(name);
    text += U"(";
    text += c.payload<StrObject>(factory_repr.value()).value;
    text += U", ";
    text += c.payload<StrObject>(dictionary_text).value;
    text += U")";
    make_str(c, std::move(text), out);
}

void defaultdict_copy(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!defaultdict_receiver(c, function, args[0], out))
        return;
    RootScope roots(c);
    auto self = roots.root(args[0]);
    ReturnSlot factory(c);
    c.load(self.get(c), c.instance_slot(self.get(c), 0), factory);
    if (factory.tag() == ResultTag::Empty)
        factory.ok(c.builtin(CoreObject::None));
    const Value arguments[] = {factory.value(), self.get(c)};
    call(c, roots.root(c.type_of(self)), {arguments}, out);
}
void state_anchor(Attachment &c, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(c.builtin(CoreObject::None));
}
} // namespace

void initialize_collections(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto module_dictionary = roots.root(module_dict(c, module.get(c)));
    const char32_t *names[] = {U"deque",       U"_deque_iterator", U"_deque_reverse_iterator",
                               U"defaultdict", U"_tuplegetter",    U"_count_elements"};
    auto publish = [&](std::span<const Value> exports) {
        ReturnSlot key(c);
        for (std::size_t i = 0; i < exports.size(); ++i) {
            const auto value = roots.root(exports[i]);
            make_str(c, names[i], key);
            dict_store(c, module_dictionary, roots.root(key.value()), value, out);
            if (out.tag() == ResultTag::Raised)
                return;
        }
        out.ok(c.builtin(CoreObject::None));
    };
    if (auto anchor = name_lookup(c, module_dictionary.get(c), U"_collections_state")) {
        const auto saved = roots.root(*anchor);
        if (c.has_layout<NativeFunctionObject>(saved.get(c)) &&
            c.payload<NativeFunctionObject>(saved).entry == state_anchor) {
            publish(c.payload<NativeFunctionObject>(saved).captures);
            return;
        }
    }
    std::vector<Handle> core;
    core.reserve(core_count);
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    auto at = [&](CoreObject object) { return core[static_cast<std::size_t>(object)]; };
    ReturnSlot deque_slot(c), iter_slot(c), reverse_slot(c), default_slot(c), getter_slot(c),
        temp(c);
    NativeTypeSpec deque_spec{{"deque"}, LayoutDescriptor::of<DequeObject>()};
    deque_spec.type.module = U"collections";
    deque_spec.type.slots = std::vector<std::u32string>{U"__weakref__"};
    create_native_type(c, deque_spec, deque_slot);
    if (deque_slot.tag() == ResultTag::Raised) {
        out.raised(deque_slot.exception());
        return;
    }
    auto deque_type = roots.root(deque_slot.value());
    c.payload<TypeObject>(deque_type)
        .collection.store(CollectionKind::Sequence, std::memory_order_relaxed);
    NativeTypeSpec iter_spec{{"_deque_iterator"}, LayoutDescriptor::of<DequeIterator>()};
    iter_spec.type.module = U"collections";
    iter_spec.type.slots = std::vector<std::u32string>{};
    create_native_type(c, iter_spec, iter_slot);
    if (iter_slot.tag() == ResultTag::Raised) {
        out.raised(iter_slot.exception());
        return;
    }
    auto iter_type = roots.root(iter_slot.value());
    NativeTypeSpec reverse_spec{{"_deque_reverse_iterator"}, LayoutDescriptor::of<DequeIterator>()};
    reverse_spec.type.module = U"collections";
    reverse_spec.type.slots = std::vector<std::u32string>{};
    create_native_type(c, reverse_spec, reverse_slot);
    if (reverse_slot.tag() == ResultTag::Raised) {
        out.raised(reverse_slot.exception());
        return;
    }
    auto reverse_type = roots.root(reverse_slot.value());
    const Handle dict_bases[] = {at(CoreObject::DictType)};
    NativeTypeSpec default_spec{{"defaultdict", dict_bases}, LayoutDescriptor::of<DictObject>()};
    default_spec.type.module = U"collections";
    default_spec.type.slots = std::vector<std::u32string>{U"default_factory"};
    create_native_type(c, default_spec, default_slot);
    if (default_slot.tag() == ResultTag::Raised) {
        out.raised(default_slot.exception());
        return;
    }
    auto default_type = roots.root(default_slot.value());
    NativeTypeSpec getter_spec{{"_tuplegetter"}, LayoutDescriptor::of<TupleGetter>()};
    getter_spec.type.module = U"collections";
    getter_spec.type.slots = std::vector<std::u32string>{};
    create_native_type(c, getter_spec, getter_slot);
    if (getter_slot.tag() == ResultTag::Raised) {
        out.raised(getter_slot.exception());
        return;
    }
    auto getter_type = roots.root(getter_slot.value());
    c.payload<TypeObject>(getter_type).subclassable = false;
    auto install = [&](Handle type, std::u32string name, SignatureLayout signature,
                       NativeEntry entry, std::vector<Value> captures = {}) {
        install_native_attribute(c, core, type, std::move(name), std::move(signature), entry,
                                 std::move(captures));
    };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto triple = pair;
    triple.parameters.push_back({U"item", ParameterKind::PositionalOnly});
    auto variadic = unary;
    variadic.varargs = U"args";
    variadic.varkwargs = U"kwargs";
    install(deque_type, U"__new__", variadic, create, {deque_type.get(c)});
    auto dictionary = *TypeView(c, deque_type).dictionary();
    auto constructor = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temp,
                                            constructor.get(c));
    auto wrapper = roots.root(temp.value());
    make_str(c, U"__new__", temp);
    name_store(c, dictionary, temp.value(), wrapper.get(c));
    make_tuple(c, {}, temp);
    auto empty = roots.root(temp.value());
    auto init = unary;
    init.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword, empty.get(c)});
    init.parameters.push_back(
        {U"maxlen", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(c)});
    install(deque_type, U"__init__", init, initialize);
    install(deque_type, U"append", pair, append<false>);
    install(deque_type, U"appendleft", pair, append<true>);
    install(deque_type, U"pop", unary, pop<false>);
    install(deque_type, U"popleft", unary, pop<true>);
    install(deque_type, U"extend", pair, extend<false>);
    install(deque_type, U"extendleft", pair, extend<true>);
    install(deque_type, U"copy", unary, deque_copy);
    install(deque_type, U"__copy__", unary, deque_copy);
    install(deque_type, U"__iadd__", pair, deque_inplace_add);
    install(deque_type, U"clear", unary, clear);
    install(deque_type, U"__len__", unary, size);
    install(deque_type, U"__getitem__", pair, item<ItemOp::Get>);
    install(deque_type, U"__setitem__", triple, item<ItemOp::Set>);
    install(deque_type, U"__delitem__", pair, item<ItemOp::Delete>);
    install(deque_type, U"insert", triple, item<ItemOp::Insert>);
    auto rotate_signature = unary;
    rotate_signature.parameters.push_back(
        {U"n", ParameterKind::PositionalOnly, at(CoreObject::IntOne).get(c)});
    install(deque_type, U"rotate", rotate_signature, rotate);
    install(deque_type, U"reverse", unary, reverse);
    install(deque_type, U"count", pair, search<Search::Count>);
    install(deque_type, U"remove", pair, search<Search::Remove>);
    install(deque_type, U"__contains__", pair, search<Search::Contains>);
    make_int(c, std::numeric_limits<std::int64_t>::max(), temp);
    auto last = roots.root(temp.value());
    auto index_signature = pair;
    index_signature.parameters.push_back(
        {U"start", ParameterKind::PositionalOnly, at(CoreObject::IntZero).get(c)});
    index_signature.parameters.push_back({U"stop", ParameterKind::PositionalOnly, last.get(c)});
    install(deque_type, U"index", index_signature, search<Search::Index>);
    install(deque_type, U"__iter__", unary, iterator<false>, {iter_type.get(c)});
    install(deque_type, U"__reversed__", unary, iterator<true>, {reverse_type.get(c)});
    install(deque_type, U"__repr__", unary, representation);
    install(deque_type, U"__eq__", pair, compare<CompareOp::Eq>);
    install(deque_type, U"__ne__", pair, compare<CompareOp::Ne>);
    install(deque_type, U"__lt__", pair, compare<CompareOp::Lt>);
    install(deque_type, U"__le__", pair, compare<CompareOp::Le>);
    install(deque_type, U"__gt__", pair, compare<CompareOp::Gt>);
    install(deque_type, U"__ge__", pair, compare<CompareOp::Ge>);
    make_str(c, U"__hash__", temp);
    name_store(c, dictionary, temp.value(), at(CoreObject::None).get(c));
    install_native_field(c, core, deque_type, U"maxlen", maxlen);
    for (auto type : {iter_type, reverse_type}) {
        install(type, U"__iter__", unary, iter_self);
        install(type, U"__next__", unary, next);
    }
    auto dict_init = roots.root(*type_attribute(c, at(CoreObject::DictType).get(c), U"__init__"));
    auto dict_repr = roots.root(*type_attribute(c, at(CoreObject::DictType).get(c), U"__repr__"));
    const std::vector<Value> default_captures{default_type.get(c), dict_init.get(c)};
    install(default_type, U"__init__", variadic, defaultdict_init, default_captures);
    install(default_type, U"__missing__", pair, defaultdict_missing, default_captures);
    install(default_type, U"copy", unary, defaultdict_copy, default_captures);
    install(default_type, U"__copy__", unary, defaultdict_copy, default_captures);
    install(default_type, U"__repr__", unary, defaultdict_repr,
            {default_type.get(c), dict_repr.get(c)});
    SignatureLayout getter_new;
    getter_new.parameters = {{U"cls", ParameterKind::PositionalOnly},
                             {U"index", ParameterKind::PositionalOnly},
                             {U"doc", ParameterKind::PositionalOnly}};
    install(getter_type, U"__new__", getter_new, tuplegetter_new, {getter_type.get(c)});
    auto getter_dictionary = *TypeView(c, getter_type).dictionary();
    auto getter_constructor = roots.root(*name_lookup(c, getter_dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), temp,
                                            getter_constructor.get(c));
    auto getter_wrapper = roots.root(temp.value());
    make_str(c, U"__new__", temp);
    name_store(c, getter_dictionary, temp.value(), getter_wrapper.get(c));
    auto getter_get = triple;
    getter_get.parameters[2].default_value = at(CoreObject::None).get(c);
    install(getter_type, U"__get__", getter_get, tuplegetter_get);
    install(getter_type, U"__set__", triple, tuplegetter_set<false>);
    install(getter_type, U"__delete__", pair, tuplegetter_set<true>);
    install(getter_type, U"__repr__", unary, tuplegetter_repr);
    install(getter_type, U"__reduce__", unary, tuplegetter_reduce);
    install_native_field(c, core, getter_type, U"__doc__", tuplegetter_doc_get,
                         tuplegetter_doc_set);
    SignatureLayout count_signature;
    count_signature.parameters = {{U"mapping", ParameterKind::PositionalOnly},
                                  {U"iterable", ParameterKind::PositionalOnly}};
    make_native_function(c, "_count_elements", count_signature, count_elements, {}, temp,
                         NativeFunctionBinding::Function);
    auto count = roots.root(temp.value());
    const std::vector<Value> exports{deque_type.get(c),   iter_type.get(c),   reverse_type.get(c),
                                     default_type.get(c), getter_type.get(c), count.get(c)};
    make_native_function(c, "_collections_state", {}, state_anchor, exports, temp,
                         NativeFunctionBinding::Function);
    auto state = roots.root(temp.value());
    make_str(c, U"_collections_state", temp);
    dict_store(c, module_dictionary, roots.root(temp.value()), state, out);
    if (out.tag() == ResultTag::Raised)
        return;
    publish(exports);
}
} // namespace cxxpython::core::detail
