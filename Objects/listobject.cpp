#include "cxxpython/protocol.hpp"
#include "internal/listsort.hpp"

#include <algorithm>
#include <deque>
#include <limits>

namespace cxxpython::core {
namespace {
template <class F> decltype(auto) locked(Attachment &context, ListObject::Storage &data, F &&body)
{
    for (;;) {
        std::unique_lock lock(data.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint commit(context);
            return body();
        }
        NativeWait parked(context);
        data.mutex.lock();
        data.mutex.unlock();
    }
}
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
bool receiver(Attachment &context, Value value, ReturnSlot &out)
{
    if (context.has_layout<ListObject>(value) &&
        is_subtype(context, context.type_of(value), context.builtin(CoreObject::ListType)))
        return true;
    raise_error(context, CoreObject::TypeErrorType, U"list descriptor requires a list", out);
    return false;
}
bool integer(Attachment &context, Handle value, std::int64_t &number, CoreObject overflow,
             ReturnSlot &out, bool clip = false)
{
    ReturnSlot converted(context);
    index(context, value, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (int_fits_signed(context, converted.value(), number))
        return true;
    if (clip) {
        number = int_sign(context, converted.value()) < 0
                     ? std::numeric_limits<std::int64_t>::min()
                     : std::numeric_limits<std::int64_t>::max();
        return true;
    }
    raise_error(context, overflow, U"list index or count is too large", out);
    return false;
}
void reserve_extra(Attachment &context, Handle self, std::size_t extra)
{
    auto &data = *context.payload<ListObject>(self).contents;
    std::vector<Value> prepared;
    for (;;) {
        std::size_t capacity = 0;
        if (locked(context, data, [&] {
                auto &items = data.items;
                if (extra > items.max_size() - items.size())
                    throw std::bad_alloc();
                const auto needed = items.size() + extra;
                if (needed > items.capacity()) {
                    if (prepared.capacity() < needed) {
                        capacity = std::max(
                            needed, items.capacity() + std::min(items.max_size() - items.capacity(),
                                                                items.capacity() / 2 + 4));
                        return false;
                    }
                    prepared.assign(items.begin(), items.end());
                    items.swap(prepared);
                }
                context.account_external(self, sizeof(ListObject::Storage) +
                                                   items.capacity() * sizeof(Value));
                return true;
            }))
            return;
        prepared.reserve(capacity);
        context.poll();
    }
}
void insert_values(Attachment &context, Handle self, std::span<const Value> values,
                   std::optional<std::int64_t> position = {})
{
    if (values.empty())
        return;
    auto &data = *context.payload<ListObject>(self).contents;
    for (;;) {
        if (locked(context, data, [&] {
                auto &items = data.items;
                if (values.size() > items.capacity() - items.size())
                    return false;
                auto at = position.value_or(static_cast<std::int64_t>(items.size()));
                if (at < 0)
                    at = std::max<std::int64_t>(0, at + static_cast<std::int64_t>(items.size()));
                const auto offset = std::min<std::size_t>(at, items.size());
                items.insert(items.begin() + offset, values.begin(), values.end());
                ++data.mutation_epoch;
                return true;
            }))
            return;
        reserve_extra(context, self, values.size());
    }
}
void clear_list(Attachment &context, Handle self)
{
    std::vector<Value> removed;
    auto &data = *context.payload<ListObject>(self).contents;
    locked(context, data, [&] {
        data.items.swap(removed);
        ++data.mutation_epoch;
        context.account_external(self, sizeof(ListObject::Storage));
    });
}
void list_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    if (!context.has_layout<TypeObject>(args[0]) ||
        !is_subtype(context, args[0], context.builtin(CoreObject::ListType))) {
        raise_error(context, CoreObject::TypeErrorType, U"list.__new__ requires a list subtype",
                    out);
        return;
    }
    context.allocate_instance<ListObject>(roots.root(args[0]), out);
}
bool extend_list(Attachment &context, Handle self, Handle source, ReturnSlot &out)
{
    RootScope roots(context);
    const auto type = context.type_of(source);
    if (type == context.builtin(CoreObject::ListType)) {
        std::vector<Value> values;
        detail::list_snapshot(context, source, roots, values);
        insert_values(context, self, values);
    } else if (type == context.builtin(CoreObject::TupleType)) {
        insert_values(context, self, context.payload<TupleObject>(source).items);
    } else {
        ReturnSlot iterator(context), value(context);
        get_iter(context, source, iterator);
        if (failed(iterator)) {
            out.raised(iterator.exception());
            return false;
        }
        const auto it = roots.root(iterator.value());
        const auto hint = detail::length_hint(context, source, out);
        if (!hint)
            return false;
        if (self.get(context) == source.get(context)) {
            std::vector<Value> values;
            if (*hint > values.max_size())
                throw std::bad_alloc();
            values.reserve(*hint);
            for (;;) {
                const auto status = iter_next(context, it, value);
                if (status == NextStatus::Done)
                    break;
                if (status == NextStatus::Raised) {
                    out.raised(value.exception());
                    return false;
                }
                values.push_back(roots.root(value.value()).get(context));
            }
            insert_values(context, self, values);
            out.ok(context.builtin(CoreObject::None));
            return true;
        }
        reserve_extra(context, self, *hint);
        for (;;) {
            const auto status = iter_next(context, it, value);
            if (status == NextStatus::Done)
                break;
            if (status == NextStatus::Raised) {
                out.raised(value.exception());
                return false;
            }
            const std::array values{value.value()};
            insert_values(context, self, values);
        }
    }
    out.ok(context.builtin(CoreObject::None));
    return true;
}
void list_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]), source = roots.root(args[1]);
    clear_list(context, self);
    extend_list(context, self, source, out);
}
void append(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    insert_values(context, roots.root(args[0]), args.subspan(1));
    out.ok(context.builtin(CoreObject::None));
}
void extend(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    extend_list(context, roots.root(args[0]), roots.root(args[1]), out);
}
void insert(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    std::int64_t at;
    if (!integer(context, roots.root(args[1]), at, CoreObject::OverflowErrorType, out))
        return;
    insert_values(context, roots.root(args[0]), args.subspan(2), at);
    out.ok(context.builtin(CoreObject::None));
}
enum class ItemOperation { Get, Set, Delete, Pop };
void prepare_roots(Attachment &context, std::deque<ReturnSlot> &slots, std::size_t count)
{
    while (slots.size() < count)
        slots.emplace_back(context);
}
void sliced_items(Attachment &context, Handle self, const detail::SliceBounds &bounds,
                  ReturnSlot &out)
{
    auto &data = *context.payload<ListObject>(self).contents;
    std::deque<ReturnSlot> slots;
    std::size_t wanted = 0, count = 0;
    for (;;) {
        if (locked(context, data, [&] {
                const auto selected = bounds.adjust(data.items.size());
                wanted = static_cast<std::size_t>(selected.length);
                if (wanted > slots.size())
                    return false;
                auto position = selected.start;
                for (std::size_t i = 0; i < wanted; ++i) {
                    slots[i].ok(data.items[position]);
                    if (i + 1 < wanted)
                        position += selected.step;
                }
                count = wanted;
                return true;
            }))
            break;
        prepare_roots(context, slots, wanted);
    }
    std::vector<Value> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(slots[i].value());
    make_list(context, values, out);
}
bool replacement_items(Attachment &context, Handle self, Handle source, RootScope &roots,
                       std::vector<Value> &values, ReturnSlot &out)
{
    const auto type = context.type_of(source);
    if (self.get(context) == source.get(context) || type == context.builtin(CoreObject::ListType)) {
        detail::list_snapshot(context, source, roots, values);
        return true;
    }
    if (type == context.builtin(CoreObject::TupleType)) {
        const auto &items = context.payload<TupleObject>(source).items;
        values.assign(items.begin(), items.end());
        return true;
    }
    ReturnSlot iterator(context), value(context);
    get_iter(context, source, iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return false;
    }
    const auto input = roots.root(iterator.value());
    // PySequence_Fast materializes the iterator through the normal list path.
    get_iter(context, input, iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return false;
    }
    const auto it = roots.root(iterator.value());
    const auto hint = detail::length_hint(context, input, out);
    if (!hint)
        return false;
    if (*hint > values.max_size())
        throw std::bad_alloc();
    values.reserve(*hint);
    for (;;) {
        const auto status = iter_next(context, it, value);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(value.exception());
            return false;
        }
        values.push_back(roots.root(value.value()).get(context));
    }
}
template <ItemOperation Operation>
void slice_item(Attachment &context, Handle self, Handle key, std::optional<Handle> source,
                ReturnSlot &out)
{
    detail::SliceBounds bounds;
    if (!detail::unpack_slice(context, key, bounds, out))
        return;
    if constexpr (Operation == ItemOperation::Get) {
        sliced_items(context, self, bounds, out);
        return;
    }
    RootScope roots(context);
    std::vector<Value> replacement, prepared;
    if constexpr (Operation == ItemOperation::Set)
        if (!replacement_items(context, self, *source, roots, replacement, out))
            return;
    auto &data = *context.payload<ListObject>(self).contents;
    enum class Commit { Done, Grow, Mismatch };
    std::size_t needed = 0, selected_size = 0;
    for (;;) {
        const auto result = locked(context, data, [&] {
            auto &items = data.items;
            const auto selected = bounds.adjust(items.size());
            selected_size = static_cast<std::size_t>(selected.length);
            if constexpr (Operation == ItemOperation::Set) {
                if (selected.step != 1) {
                    if (replacement.size() != selected_size)
                        return Commit::Mismatch;
                    auto position = selected.start;
                    for (std::size_t i = 0; i < replacement.size(); ++i) {
                        items[position] = replacement[i];
                        if (i + 1 < replacement.size())
                            position += selected.step;
                    }
                    if (!replacement.empty())
                        ++data.mutation_epoch;
                    return Commit::Done;
                }
                const auto remaining = items.size() - selected_size;
                if (replacement.size() > items.max_size() - remaining)
                    throw std::bad_alloc();
                needed = remaining + replacement.size();
                if (needed > items.capacity()) {
                    if (needed > prepared.capacity()) {
                        needed = std::max(needed, items.capacity() +
                                                      std::min(items.max_size() - items.capacity(),
                                                               items.capacity() / 2 + 4));
                        return Commit::Grow;
                    }
                    prepared.assign(items.begin(), items.end());
                    items.swap(prepared);
                }
                const auto first = items.begin() + selected.start;
                items.erase(first, first + selected.length);
                items.insert(items.begin() + selected.start, replacement.begin(),
                             replacement.end());
                if (selected.length || !replacement.empty())
                    ++data.mutation_epoch;
                context.account_external(self, sizeof(ListObject::Storage) +
                                                   items.capacity() * sizeof(Value));
                return Commit::Done;
            } else {
                if (!selected.length)
                    return Commit::Done;
                ++data.mutation_epoch;
                if (selected.step == 1) {
                    items.erase(items.begin() + selected.start, items.begin() + selected.stop);
                    return Commit::Done;
                }
                auto position = selected.step > 0
                                    ? selected.start
                                    : selected.start + (selected.length - 1) * selected.step;
                const auto stride = selected.step > 0 ? selected.step : -selected.step;
                auto left = selected.length;
                std::size_t write = 0;
                for (std::size_t read = 0; read < items.size(); ++read) {
                    if (left && read == static_cast<std::size_t>(position)) {
                        if (--left)
                            position += stride;
                    } else
                        items[write++] = items[read];
                }
                items.erase(items.begin() + write, items.end());
                return Commit::Done;
            }
        });
        if (result == Commit::Mismatch) {
            const auto a = std::to_string(replacement.size()), b = std::to_string(selected_size);
            raise_error(context, CoreObject::ValueErrorType,
                        U"attempt to assign sequence of size " +
                            std::u32string(a.begin(), a.end()) + U" to extended slice of size " +
                            std::u32string(b.begin(), b.end()),
                        out);
            return;
        }
        if (result == Commit::Done) {
            out.ok(context.builtin(CoreObject::None));
            return;
        }
        prepared.reserve(needed);
        context.poll();
    }
}
template <ItemOperation Operation>
void item(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    if constexpr (Operation != ItemOperation::Pop) {
        if (context.type_of(args[1]) == context.builtin(CoreObject::SliceType)) {
            std::optional<Handle> source;
            if constexpr (Operation == ItemOperation::Set)
                source = roots.root(args[2]);
            slice_item<Operation>(context, self, roots.root(args[1]), source, out);
            return;
        }
    }
    std::int64_t at;
    if (!integer(context, roots.root(args[1]), at,
                 Operation == ItemOperation::Pop ? CoreObject::OverflowErrorType
                                                 : CoreObject::IndexErrorType,
                 out))
        return;
    auto &data = *context.payload<ListObject>(self).contents;
    const bool found = locked(context, data, [&] {
        const auto size = static_cast<std::int64_t>(data.items.size());
        if (at < 0)
            at += size;
        if (at < 0 || at >= size)
            return false;
        if constexpr (Operation == ItemOperation::Set) {
            data.items[at] = args[2];
            ++data.mutation_epoch;
            out.ok(context.builtin(CoreObject::None));
        } else {
            out.ok(data.items[at]);
            if constexpr (Operation != ItemOperation::Get) {
                data.items.erase(data.items.begin() + at);
                ++data.mutation_epoch;
            }
        }
        return true;
    });
    if (!found)
        raise_error(context, CoreObject::IndexErrorType, U"list index out of range", out);
    else if constexpr (Operation == ItemOperation::Delete)
        out.ok(context.builtin(CoreObject::None));
}
void size(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    make_int(context, detail::list_size(context, roots.root(args[0])), out);
}
void clear(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    clear_list(context, roots.root(args[0]));
    out.ok(context.builtin(CoreObject::None));
}
void copy(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    std::vector<Value> values;
    detail::list_snapshot(context, roots.root(args[0]), roots, values);
    make_list(context, values, out);
}
void reverse(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    auto &data = *context.payload<ListObject>(args[0]).contents;
    locked(context, data, [&] {
        std::reverse(data.items.begin(), data.items.end());
        ++data.mutation_epoch;
    });
    out.ok(context.builtin(CoreObject::None));
}
void sort(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]), key = roots.root(args[1]);
    ReturnSlot converted(context);
    truth(context, roots.root(args[2]), converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return;
    }
    detail::sort_list(context, self, key, converted.value() == context.builtin(CoreObject::True),
                      out);
}
void iterator(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    context.allocate_instance<SequenceIteratorObject>(
        roots.root(context.builtin(CoreObject::ListIteratorType)), out, args[0]);
}
void iterator_self(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!context.has_layout<SequenceIteratorObject>(args[0]) ||
        context.type_of(args[0]) != context.builtin(CoreObject::ListIteratorType)) {
        raise_error(context, CoreObject::TypeErrorType, U"list iterator required", out);
        return;
    }
    out.ok(args[0]);
}
void next(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    iterator_self(context, {}, args, out);
    if (failed(out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    auto &state = context.payload<SequenceIteratorObject>(self);
    ReturnSlot owner(context);
    context.load(self.get(context), state.owner, owner);
    if (owner.tag() == ResultTag::Empty) {
        detail::stop_iteration(context, out);
        return;
    }
    const auto list = roots.root(owner.value());
    auto &data = *context.payload<ListObject>(list).contents;
    const bool found = locked(context, data, [&] {
        context.load(self.get(context), state.owner, owner);
        if (owner.tag() == ResultTag::Empty)
            return false;
        const auto at = state.index.load(std::memory_order_relaxed);
        if (at >= data.items.size()) {
            context.store(self.get(context), state.owner, {});
            return false;
        }
        out.ok(data.items[at]);
        state.index.store(at + 1, std::memory_order_relaxed);
        return true;
    });
    if (!found)
        detail::stop_iteration(context, out);
}
template <CompareOp Op>
void compare(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    if (!context.has_layout<ListObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::ListType))) {
        out.ok(context.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(context);
    const auto left = roots.root(args[0]), right = roots.root(args[1]);
    auto finish = [&](std::size_t a, std::size_t b) {
        const bool result = Op == CompareOp::Eq   ? a == b
                            : Op == CompareOp::Ne ? a != b
                            : Op == CompareOp::Lt ? a < b
                            : Op == CompareOp::Le ? a <= b
                            : Op == CompareOp::Gt ? a > b
                                                  : a >= b;
        out.ok(context.builtin(result ? CoreObject::True : CoreObject::False));
    };
    if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne) {
        const auto a = detail::list_size(context, left), b = detail::list_size(context, right);
        if (a != b) {
            finish(a, b);
            return;
        }
    }
    ReturnSlot a(context), b(context), equal(context);
    for (std::size_t i = 0;; ++i) {
        detail::list_item(context, left, i, a);
        detail::list_item(context, right, i, b);
        if (a.tag() == ResultTag::Empty || b.tag() == ResultTag::Empty)
            break;
        RootScope pair(context);
        const auto av = pair.root(a.value()), bv = pair.root(b.value());
        container_equal(context, av, bv, equal);
        if (failed(equal)) {
            out.raised(equal.exception());
            return;
        }
        if (equal.value() == context.builtin(CoreObject::True))
            continue;
        if constexpr (Op == CompareOp::Eq || Op == CompareOp::Ne)
            finish(0, 1);
        else
            rich_compare(context, av, bv, Op, out);
        return;
    }
    finish(detail::list_size(context, left), detail::list_size(context, right));
}
enum class Search { Count, Index, Remove, Contains };
template <Search Mode>
void search(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]), needle = roots.root(args[1]);
    std::int64_t start = 0, stop = std::numeric_limits<std::int64_t>::max();
    if constexpr (Mode == Search::Index) {
        const auto &bounds = context.payload<TupleObject>(args[2]).items;
        if (bounds.size() > 2) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"index expected at most three arguments", out);
            return;
        }
        if (!bounds.empty() && !integer(context, roots.root(bounds[0]), start,
                                        CoreObject::OverflowErrorType, out, true))
            return;
        if (bounds.size() == 2 && !integer(context, roots.root(bounds[1]), stop,
                                           CoreObject::OverflowErrorType, out, true))
            return;
        const auto size = static_cast<std::int64_t>(detail::list_size(context, self));
        if (start < 0)
            start = std::max<std::int64_t>(0, start + size);
        if (stop < 0)
            stop = std::max<std::int64_t>(0, stop + size);
    }
    std::size_t count = 0;
    ReturnSlot value(context), equal(context);
    for (auto i = start; i < stop; ++i) {
        detail::list_item(context, self, i, value);
        if (value.tag() == ResultTag::Empty)
            break;
        RootScope iteration(context);
        container_equal(context, iteration.root(value.value()), needle, equal);
        if (failed(equal)) {
            out.raised(equal.exception());
            return;
        }
        if (equal.value() != context.builtin(CoreObject::True))
            continue;
        if constexpr (Mode == Search::Count)
            ++count;
        else if constexpr (Mode == Search::Contains) {
            out.ok(context.builtin(CoreObject::True));
            return;
        } else if constexpr (Mode == Search::Index) {
            make_int(context, i, out);
            return;
        } else {
            auto &data = *context.payload<ListObject>(self).contents;
            locked(context, data, [&] {
                if (static_cast<std::size_t>(i) < data.items.size()) {
                    data.items.erase(data.items.begin() + i);
                    ++data.mutation_epoch;
                }
            });
            out.ok(context.builtin(CoreObject::None));
            return;
        }
    }
    if constexpr (Mode == Search::Count)
        make_int(context, count, out);
    else if constexpr (Mode == Search::Contains)
        out.ok(context.builtin(CoreObject::False));
    else if constexpr (Mode == Search::Remove)
        raise_error(context, CoreObject::ValueErrorType, U"list.remove(x): x not in list", out);
    else {
        repr(context, needle, value);
        if (failed(value))
            out.raised(value.exception());
        else
            raise_error(context, CoreObject::ValueErrorType,
                        context.payload<StrObject>(value.value()).value + U" is not in list", out);
    }
}
void add(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    if (!context.has_layout<ListObject>(args[1]) ||
        !is_subtype(context, context.type_of(args[1]), context.builtin(CoreObject::ListType))) {
        raise_error(context, CoreObject::TypeErrorType, U"can only concatenate a list to a list",
                    out);
        return;
    }
    RootScope roots(context);
    std::vector<Value> a, b;
    detail::list_snapshot(context, roots.root(args[0]), roots, a);
    detail::list_snapshot(context, roots.root(args[1]), roots, b);
    if (b.size() > a.max_size() - a.size())
        throw std::bad_alloc();
    a.insert(a.end(), b.begin(), b.end());
    make_list(context, a, out);
}
void inplace_add(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    if (extend_list(context, roots.root(args[0]), roots.root(args[1]), out))
        out.ok(args[0]);
}
template <bool Inplace>
void repeat(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(context, args[0], out))
        return;
    RootScope roots(context);
    const auto self = roots.root(args[0]);
    std::int64_t count;
    if (!integer(context, roots.root(args[1]), count, CoreObject::OverflowErrorType, out))
        return;
    const auto repeats = static_cast<std::size_t>(std::max<std::int64_t>(0, count));
    std::vector<Value> prepared;
    if constexpr (!Inplace) {
        std::vector<Value> values;
        detail::list_snapshot(context, self, roots, values);
        if (!values.empty() && repeats > prepared.max_size() / values.size())
            throw std::bad_alloc();
        const auto total = values.size() * repeats;
        prepared.reserve(total);
        for (std::size_t i = 0; i < total; ++i)
            prepared.push_back(values[i % values.size()]);
        make_list(context, prepared, out);
    } else {
        auto &data = *context.payload<ListObject>(self).contents;
        for (;;) {
            std::size_t extra = 0;
            if (locked(context, data, [&] {
                    auto &items = data.items;
                    const auto size = items.size();
                    if (!size || repeats == 1)
                        return true;
                    if (!repeats) {
                        items.swap(prepared);
                        ++data.mutation_epoch;
                        context.account_external(self, sizeof(ListObject::Storage));
                        return true;
                    }
                    if (repeats > items.max_size() / size)
                        throw std::bad_alloc();
                    const auto needed = size * repeats;
                    if (needed > items.capacity()) {
                        extra = needed - size;
                        return false;
                    }
                    for (std::size_t i = size; i < needed; ++i)
                        items.push_back(items[i % size]);
                    ++data.mutation_epoch;
                    context.account_external(self, sizeof(ListObject::Storage) +
                                                       items.capacity() * sizeof(Value));
                    return true;
                }))
                break;
            reserve_extra(context, self, extra);
        }
        out.ok(self.get(context));
    }
}
} // namespace
void make_list(Attachment &context, std::span<const Value> values, ReturnSlot &out)
{
    RootScope roots(context);
    for (auto value : values)
        roots.root(value);
    context.allocate_instance<ListObject>(roots.root(context.builtin(CoreObject::ListType)), out,
                                          std::vector<Value>(values.begin(), values.end()));
}
namespace detail {
void sort_list(Attachment &context, Handle self, Handle key, bool reverse, ReturnSlot &out)
{
    RootScope roots(context);
    self = roots.root(self.get(context));
    key = roots.root(key.get(context));
    auto &data = *context.payload<ListObject>(self).contents;
    std::deque<ReturnSlot> values, keys;
    std::vector<Value> saved, reordered, discarded;
    std::vector<std::size_t> order, scratch;
    std::size_t count = 0;
    std::uint64_t epoch = 0;
    for (;;) {
        const auto size = list_size(context, self);
        prepare_roots(context, values, size);
        prepare_roots(context, keys, size);
        order.resize(size);
        scratch.resize(size);
        reordered.resize(size, context.builtin(CoreObject::None));
        if (locked(context, data, [&] {
                if (data.items.size() > values.size() || data.items.size() > order.size())
                    return false;
                count = data.items.size();
                for (std::size_t i = 0; i < count; ++i)
                    values[i].ok(data.items[i]);
                data.items.swap(saved);
                epoch = ++data.mutation_epoch;
                return true;
            }))
            break;
    }
    // No managed allocation is needed to restore the detached list, even on a native fault.
    struct Restore {
        Attachment &context;
        Handle self;
        ListObject::Storage &data;
        std::vector<Value> &saved;
        std::vector<Value> &discarded;
        bool active = true;
        ~Restore()
        {
            if (active)
                locked(context, data, [&] {
                    data.items.swap(discarded);
                    data.items.swap(saved);
                    ++data.mutation_epoch;
                    context.account_external(self, sizeof(ListObject::Storage) +
                                                       data.items.capacity() * sizeof(Value));
                });
        }
    } restore{context, self, data, saved, discarded};
    reordered.resize(count, context.builtin(CoreObject::None));
    ReturnSlot result(context), converted(context);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
        if (key.get(context) == context.builtin(CoreObject::None))
            keys[i].ok(values[i].value());
        else {
            const std::array args{values[i].value()};
            call(context, key, {args, {}}, result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            keys[i].ok(result.value());
        }
    }
    for (std::size_t width = 1; width < count;) {
        for (std::size_t start = 0; start < count;) {
            const auto middle = start + std::min(width, count - start);
            const auto end = middle + std::min(width, count - middle);
            auto left = start, right = middle, dest = start;
            while (left < middle && right < end) {
                RootScope pair(context);
                const auto a = pair.root(keys[order[reverse ? left : right]].value());
                const auto b = pair.root(keys[order[reverse ? right : left]].value());
                rich_compare(context, a, b, CompareOp::Lt, result);
                if (failed(result)) {
                    out.raised(result.exception());
                    return;
                }
                truth(context, pair.root(result.value()), converted);
                if (failed(converted)) {
                    out.raised(converted.exception());
                    return;
                }
                scratch[dest++] = converted.value() == context.builtin(CoreObject::True)
                                      ? order[right++]
                                      : order[left++];
            }
            while (left < middle)
                scratch[dest++] = order[left++];
            while (right < end)
                scratch[dest++] = order[right++];
            start = end;
        }
        order.swap(scratch);
        if (width > count / 2)
            break;
        width *= 2;
    }
    for (std::size_t i = 0; i < count; ++i)
        reordered[i] = values[order[i]].value();
    bool modified = false;
    locked(context, data, [&] {
        modified = data.mutation_epoch != epoch;
        data.items.swap(discarded);
        data.items.swap(reordered);
        ++data.mutation_epoch;
        context.account_external(self, sizeof(ListObject::Storage) +
                                           data.items.capacity() * sizeof(Value));
        restore.active = false;
    });
    if (modified)
        raise_error(context, CoreObject::ValueErrorType, U"list modified during sort", out);
    else
        out.ok(context.builtin(CoreObject::None));
}
void list_append(Attachment &context, Handle self, Handle value, ReturnSlot &out)
{
    const std::array values{value.get(context)};
    insert_values(context, self, values);
    out.ok(context.builtin(CoreObject::None));
}
void list_extend(Attachment &context, Handle self, Handle source, ReturnSlot &out)
{
    extend_list(context, self, source, out);
}
std::size_t list_size(Attachment &context, Handle self)
{
    auto &data = *context.payload<ListObject>(self).contents;
    return locked(context, data, [&] { return data.items.size(); });
}
void list_item(Attachment &context, Handle self, std::size_t at, ReturnSlot &out)
{
    const auto owner = self.get(context);
    out.check_caller(context);
    auto &data = *context.payload<ListObject>(owner).contents;
    locked(context, data, [&] {
        if (at < data.items.size())
            out.ok(data.items[at]);
        else
            out.reset(context);
    });
}
void list_snapshot(Attachment &context, Handle self, RootScope &roots, std::vector<Value> &values)
{
    self = roots.root(self.get(context));
    auto &data = *context.payload<ListObject>(self).contents;
    std::deque<ReturnSlot> slots;
    std::size_t count = 0;
    for (;;) {
        prepare_roots(context, slots, list_size(context, self));
        if (locked(context, data, [&] {
                if (data.items.size() > slots.size())
                    return false;
                count = data.items.size();
                for (std::size_t i = 0; i < count; ++i)
                    slots[i].ok(data.items[i]);
                return true;
            }))
            break;
    }
    values.clear();
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(roots.root(slots[i].value()).get(context));
}
void install_lists(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto install = [&](std::u32string name, SignatureLayout signature, NativeEntry entry,
                       NativeSlotKind kind = NativeSlotKind::Ordinary) {
        install_native_attribute(context, core, at(CoreObject::ListType), std::move(name),
                                 std::move(signature), entry, {}, kind);
    };
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto create = unary;
    create.varargs = U"args";
    create.varkwargs = U"kwargs";
    install(U"__new__", create, list_new);
    RootScope roots(context);
    ReturnSlot key(context), wrapper(context), default_index(context);
    const auto dictionary = *TypeView(context, at(CoreObject::ListType)).dictionary();
    const auto function = *name_lookup(context, dictionary, U"__new__");
    context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                  function);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
    name_store(context, dictionary, key.value(), wrapper.value());
    auto init = unary;
    init.parameters.push_back(
        {U"iterable", ParameterKind::PositionalOnly, at(CoreObject::EmptyTuple).get(context)});
    install(U"__init__", init, list_init);
    install(U"__len__", unary, size);
    install(U"__getitem__", pair, item<ItemOperation::Get>);
    install(U"__delitem__", pair, item<ItemOperation::Delete>);
    install(U"append", pair, append);
    install(U"extend", pair, extend);
    install(U"__iadd__", pair, inplace_add, NativeSlotKind::Sequence);
    install(U"__add__", pair, add, NativeSlotKind::Sequence);
    install(U"__mul__", pair, repeat<false>, NativeSlotKind::Sequence);
    install(U"__rmul__", pair, repeat<false>, NativeSlotKind::Sequence);
    install(U"__imul__", pair, repeat<true>, NativeSlotKind::Sequence);
    install(U"clear", unary, clear);
    install(U"copy", unary, copy);
    install(U"reverse", unary, reverse);
    auto sort_signature = unary;
    sort_signature.parameters.push_back(
        {U"key", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)});
    sort_signature.parameters.push_back(
        {U"reverse", ParameterKind::KeywordOnly, at(CoreObject::False).get(context)});
    install(U"sort", sort_signature, sort);
    install(U"__iter__", unary, iterator);
    install(U"count", pair, search<Search::Count>);
    install(U"remove", pair, search<Search::Remove>);
    install(U"__contains__", pair, search<Search::Contains>);
    auto indexed = pair;
    indexed.varargs = U"bounds";
    install(U"index", indexed, search<Search::Index>);
    auto three = pair;
    three.parameters.push_back({U"value2", ParameterKind::PositionalOnly});
    install(U"__setitem__", three, item<ItemOperation::Set>);
    install(U"insert", three, insert);
    context.allocate_instance<IntObject>(at(CoreObject::IntType), default_index, -1);
    auto optional = unary;
    optional.parameters.push_back({U"index", ParameterKind::PositionalOnly, default_index.value()});
    install(U"pop", optional, item<ItemOperation::Pop>);
    const std::array<std::u32string, 6> names{U"__eq__", U"__ne__", U"__lt__",
                                              U"__le__", U"__gt__", U"__ge__"};
    const std::array entries{compare<CompareOp::Eq>, compare<CompareOp::Ne>,
                             compare<CompareOp::Lt>, compare<CompareOp::Le>,
                             compare<CompareOp::Gt>, compare<CompareOp::Ge>};
    for (std::size_t i = 0; i < names.size(); ++i)
        install(names[i], pair, entries[i]);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__hash__");
    name_store(context, dictionary, key.value(), at(CoreObject::None).get(context));
    install_native_attribute(context, core, at(CoreObject::ListIteratorType), U"__iter__", unary,
                             iterator_self);
    install_native_attribute(context, core, at(CoreObject::ListIteratorType), U"__next__", unary,
                             next);
}
} // namespace detail
} // namespace cxxpython::core
