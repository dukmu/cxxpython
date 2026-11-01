// Adapted from CPython 3.12.14 Modules/itertoolsmodule.c, PSF licensed.
#include "internal/itertoolsmodule.hpp"
#include "internal/itertools_combinatorics.hpp"
#include "internal/itertools_groups.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <atomic>
#include <limits>
#include <thread>

namespace cxxpython::core::detail {
namespace {
struct Pairwise {
    ObjectHeader header;
    AtomicGcEdge iterator, previous;
    std::atomic_flag active = ATOMIC_FLAG_INIT;

    Pairwise(InitialType type, Value input) : header(type), iterator(input) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(previous);
    }
};

struct Chain {
    ObjectHeader header;
    AtomicGcEdge source, current;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Chain(InitialType type, Value input) : header(type), source(input) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(source);
        tracer.visit(current);
    }
};
struct Repeat {
    ObjectHeader header;
    const Value element;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    std::int64_t remaining;
    Repeat(InitialType type, Value item, std::int64_t count)
        : header(type), element(item), remaining(count)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(element);
    }
};
struct Islice {
    ObjectHeader header;
    AtomicGcEdge iterator;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    std::int64_t next, stop, step, count = 0;
    Islice(InitialType type, Value input, std::int64_t start, std::int64_t end, std::int64_t stride)
        : header(type), iterator(input), next(start), stop(end), step(stride)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
    }
};
struct Starmap {
    ObjectHeader header;
    const Value iterator, function;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Starmap(InitialType type, Value input, Value callable)
        : header(type), iterator(input), function(callable)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(function);
    }
};
enum class PredicateMode { FilterFalse, TakeWhile, DropWhile };
template <PredicateMode Mode> struct PredicateIterator {
    ObjectHeader header;
    const Value iterator, predicate;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    bool switched = false;
    PredicateIterator(InitialType type, Value input, Value function)
        : header(type), iterator(input), predicate(function)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(predicate);
    }
};
struct Compress {
    ObjectHeader header;
    const Value data, selectors;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Compress(InitialType type, Value input, Value choices)
        : header(type), data(input), selectors(choices)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(data);
        tracer.visit(selectors);
    }
};
struct Accumulate {
    ObjectHeader header;
    const Value iterator, function;
    AtomicGcEdge total, initial;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Accumulate(InitialType type, Value input, Value callable, std::optional<Value> first)
        : header(type), iterator(input), function(callable), initial(first)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(function);
        tracer.visit(total);
        tracer.visit(initial);
    }
};
struct Batched {
    ObjectHeader header;
    AtomicGcEdge iterator;
    const std::int64_t size;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Batched(InitialType type, Value input, std::int64_t count)
        : header(type), iterator(input), size(count)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
    }
};
struct Cycle {
    ObjectHeader header;
    AtomicGcEdge iterator;
    const Value saved;
    std::size_t count = 0, position = 0;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    Cycle(InitialType type, Value input, Value cache) : header(type), iterator(input), saved(cache)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(iterator);
        tracer.visit(saved);
    }
};
struct Count {
    ObjectHeader header;
    AtomicGcEdge current;
    const Value step;
    std::int64_t fast_value;
    bool fast;
    std::atomic_flag active = ATOMIC_FLAG_INIT;
    std::atomic<std::thread::id> owner{};
    Count(InitialType type, Value start, Value increment, std::int64_t number, bool fast_mode)
        : header(type), current(start), step(increment), fast_value(number), fast(fast_mode)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(current);
        tracer.visit(step);
    }
};

struct Lease {
    std::atomic_flag &flag;
    bool acquired;
    std::atomic<std::thread::id> *owner = nullptr;
    explicit Lease(std::atomic_flag &value)
        : flag(value), acquired(!flag.test_and_set(std::memory_order_acquire))
    {}
    Lease(Attachment &ctx, std::atomic_flag &value, std::atomic<std::thread::id> &identity)
        : flag(value), acquired(false), owner(&identity)
    {
        while (flag.test_and_set(std::memory_order_acquire)) {
            if (owner->load(std::memory_order_acquire) == std::this_thread::get_id())
                return;
            NativeWait parked(ctx);
            flag.wait(true, std::memory_order_acquire);
        }
        owner->store(std::this_thread::get_id(), std::memory_order_release);
        acquired = true;
    }
    ~Lease()
    {
        release();
    }
    void release() noexcept
    {
        if (acquired) {
            if (owner)
                owner->store({}, std::memory_order_release);
            flag.clear(std::memory_order_release);
            if (owner)
                flag.notify_one();
            acquired = false;
        }
    }
};

bool failed(ReturnSlot &slot)
{
    return slot.tag() == ResultTag::Raised;
}

template <class T> bool check_class(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<TypeObject>(value) &&
        ctx.payload<TypeObject>(value).instance_layout.native_family ==
            LayoutDescriptor::of<T>().native_family)
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"invalid iterator type", out);
    return false;
}

template <class T> bool check_self(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<T>(value))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"invalid iterator receiver", out);
    return false;
}

template <class T>
void iterator_self(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (check_self<T>(ctx, args[0], out))
        out.ok(args[0]);
}

bool acquire(Attachment &ctx, const Lease &lease, ReturnSlot &out)
{
    if (lease.acquired)
        return true;
    raise_error(ctx, CoreObject::RuntimeErrorType, U"iterator already executing", out);
    return false;
}

void chain_make(Attachment &ctx, Value cls, Value iterable, ReturnSlot &out)
{
    if (!check_class<Chain>(ctx, cls, out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(cls);
    ReturnSlot input(ctx);
    get_iter(ctx, roots.root(iterable), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    ctx.allocate_instance<Chain>(type, out, input.value());
}
void chain_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    chain_make(ctx, args[0], args[1], out);
}
void chain_from(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    chain_make(ctx, args[0], args[1], out);
}
void chain_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Chain>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Chain>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot source(ctx), current(ctx), item(ctx);
    ctx.load(self.get(ctx), state.source, source);
    if (source.tag() == ResultTag::Empty) {
        stop_iteration(ctx, out);
        return;
    }
    auto input = roots.root(source.value());
    for (;;) {
        RootScope iteration(ctx);
        ctx.load(self.get(ctx), state.current, current);
        if (current.tag() == ResultTag::Empty) {
            auto status = iter_next(ctx, input, item);
            if (status != NextStatus::Item) {
                ctx.clear(self, state.source);
                if (status == NextStatus::Raised)
                    out.raised(item.exception());
                else
                    stop_iteration(ctx, out);
                return;
            }
            get_iter(ctx, iteration.root(item.value()), current);
            if (failed(current)) {
                ctx.clear(self, state.source);
                out.raised(current.exception());
                return;
            }
            ctx.store(self.get(ctx), state.current, current.value());
        }
        auto status = iter_next(ctx, iteration.root(current.value()), item);
        if (status == NextStatus::Item) {
            out.ok(item.value());
            return;
        }
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        ctx.clear(self, state.current);
    }
}

void repeat_new(Attachment &ctx, Value function, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Repeat>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto element = roots.root(args[1]);
    std::int64_t count = -1;
    const auto sentinel = ctx.payload<NativeFunctionObject>(function).captures[0];
    if (args[2] != sentinel) {
        ReturnSlot number(ctx);
        index(ctx, roots.root(args[2]), number);
        if (failed(number)) {
            out.raised(number.exception());
            return;
        }
        if (!int_fits_signed(ctx, number.value(), count)) {
            raise_error(ctx, CoreObject::OverflowErrorType,
                        U"Python int too large to convert to C ssize_t", out);
            return;
        }
        if (count < 0)
            count = 0;
    }
    ctx.allocate_instance<Repeat>(type, out, element.get(ctx), count);
}
void repeat_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Repeat>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Repeat>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    if (state.remaining == 0) {
        stop_iteration(ctx, out);
        return;
    }
    if (state.remaining > 0)
        --state.remaining;
    out.ok(state.element);
}
void repeat_hint(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Repeat>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto &state = ctx.payload<Repeat>(roots.root(args[0]));
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    if (state.remaining < 0)
        raise_error(ctx, CoreObject::TypeErrorType, U"len() of unsized object", out);
    else
        make_int(ctx, state.remaining, out);
}

bool slice_index(Attachment &ctx, Value value, std::int64_t &number)
{
    if (value == ctx.builtin(CoreObject::None))
        return true;
    RootScope roots(ctx);
    ReturnSlot converted(ctx);
    index(ctx, roots.root(value), converted);
    return !failed(converted) && int_fits_signed(ctx, converted.value(), number);
}
void islice_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Islice>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto arguments = roots.root(args[1]);
    const auto &items = ctx.payload<TupleObject>(arguments).items;
    if (items.size() < 2 || items.size() > 4) {
        raise_error(ctx, CoreObject::TypeErrorType, U"islice expected 2 to 4 arguments", out);
        return;
    }
    std::int64_t start = 0, stop = -1, step = 1;
    if (items.size() > 2 && !slice_index(ctx, items[1], start))
        start = -1;
    auto stop_arg = items[items.size() == 2 ? 1 : 2];
    if (!slice_index(ctx, stop_arg, stop) ||
        (stop_arg != ctx.builtin(CoreObject::None) && stop < 0)) {
        raise_error(
            ctx, CoreObject::ValueErrorType,
            U"Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.", out);
        return;
    }
    if (start < 0) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.",
                    out);
        return;
    }
    if (items.size() == 4 && (!slice_index(ctx, items[3], step) || step < 1)) {
        raise_error(ctx, CoreObject::ValueErrorType,
                    U"Step for islice() must be a positive integer or None.", out);
        return;
    }
    ReturnSlot input(ctx);
    get_iter(ctx, roots.root(items[0]), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    ctx.allocate_instance<Islice>(type, out, input.value(), start, stop, step);
}
void islice_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Islice>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Islice>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot input(ctx), item(ctx);
    ctx.load(self.get(ctx), state.iterator, input);
    if (input.tag() == ResultTag::Empty) {
        stop_iteration(ctx, out);
        return;
    }
    auto iterator = roots.root(input.value());
    auto advance = [&] {
        auto status = iter_next(ctx, iterator, item);
        if (status != NextStatus::Item) {
            ctx.clear(self, state.iterator);
            if (status == NextStatus::Raised)
                out.raised(item.exception());
            else
                stop_iteration(ctx, out);
            return false;
        }
        return true;
    };
    while (state.count < state.next) {
        if (!advance())
            return;
        ++state.count;
    }
    if (state.stop != -1 && state.count >= state.stop) {
        ctx.clear(self, state.iterator);
        stop_iteration(ctx, out);
        return;
    }
    if (!advance())
        return;
    ++state.count;
    if (state.next > std::numeric_limits<std::int64_t>::max() - state.step)
        state.next = state.stop;
    else {
        state.next += state.step;
        if (state.stop != -1 && state.next > state.stop)
            state.next = state.stop;
    }
    out.ok(item.value());
}

void starmap_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Starmap>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto callable = roots.root(args[1]);
    ReturnSlot input(ctx);
    get_iter(ctx, roots.root(args[2]), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    ctx.allocate_instance<Starmap>(type, out, input.value(), callable.get(ctx));
}
void starmap_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Starmap>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Starmap>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot item(ctx), tuple(ctx);
    auto status = iter_next(ctx, roots.root(state.iterator), item);
    if (status != NextStatus::Item) {
        if (status == NextStatus::Raised)
            out.raised(item.exception());
        else
            stop_iteration(ctx, out);
        return;
    }
    const Value argument[] = {item.value()};
    call(ctx, roots.root(ctx.builtin(CoreObject::TupleType)), {argument, {}}, tuple);
    if (failed(tuple)) {
        out.raised(tuple.exception());
        return;
    }
    auto values = roots.root(tuple.value());
    call(ctx, roots.root(state.function), {ctx.payload<TupleObject>(values).items, {}}, out);
}

template <PredicateMode Mode>
void predicate_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    using State = PredicateIterator<Mode>;
    if (!check_class<State>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto predicate = roots.root(args[1]);
    ReturnSlot input(ctx);
    get_iter(ctx, roots.root(args[2]), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    ctx.allocate_instance<State>(type, out, input.value(), predicate.get(ctx));
}
template <PredicateMode Mode>
void predicate_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    using State = PredicateIterator<Mode>;
    if (!check_self<State>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<State>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    if constexpr (Mode == PredicateMode::TakeWhile) {
        if (state.switched) {
            stop_iteration(ctx, out);
            return;
        }
    }
    auto input = roots.root(state.iterator);
    auto predicate = roots.root(state.predicate);
    ReturnSlot item(ctx), result(ctx), decision(ctx);
    for (;;) {
        auto status = iter_next(ctx, input, item);
        if (status != NextStatus::Item) {
            if (status == NextStatus::Raised)
                out.raised(item.exception());
            else
                stop_iteration(ctx, out);
            return;
        }
        if constexpr (Mode == PredicateMode::DropWhile) {
            if (state.switched) {
                out.ok(item.value());
                return;
            }
        }
        if constexpr (Mode == PredicateMode::FilterFalse) {
            if (predicate.get(ctx) == ctx.builtin(CoreObject::None))
                result.ok(item.value());
            else {
                const Value argument[] = {item.value()};
                call(ctx, predicate, {argument, {}}, result);
            }
        } else {
            const Value argument[] = {item.value()};
            call(ctx, predicate, {argument, {}}, result);
        }
        if (failed(result)) {
            out.raised(result.exception());
            return;
        }
        {
            RootScope current(ctx);
            truth(ctx, current.root(result.value()), decision);
        }
        if (failed(decision)) {
            out.raised(decision.exception());
            return;
        }
        bool yes = decision.value() == ctx.builtin(CoreObject::True);
        if constexpr (Mode == PredicateMode::FilterFalse) {
            if (!yes) {
                out.ok(item.value());
                return;
            }
        } else if constexpr (Mode == PredicateMode::TakeWhile) {
            if (!yes) {
                state.switched = true;
                stop_iteration(ctx, out);
                return;
            }
            out.ok(item.value());
            return;
        } else if (!yes) {
            state.switched = true;
            out.ok(item.value());
            return;
        }
    }
}
void compress_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Compress>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    ReturnSlot data(ctx), selectors(ctx);
    get_iter(ctx, roots.root(args[1]), data);
    if (failed(data)) {
        out.raised(data.exception());
        return;
    }
    get_iter(ctx, roots.root(args[2]), selectors);
    if (failed(selectors)) {
        out.raised(selectors.exception());
        return;
    }
    ctx.allocate_instance<Compress>(type, out, data.value(), selectors.value());
}
void compress_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Compress>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Compress>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    auto data = roots.root(state.data), selectors = roots.root(state.selectors);
    ReturnSlot item(ctx), choice(ctx), decision(ctx);
    for (;;) {
        auto status = iter_next(ctx, data, item);
        if (status == NextStatus::Item)
            status = iter_next(ctx, selectors, choice);
        if (status != NextStatus::Item) {
            if (status == NextStatus::Raised)
                out.raised((failed(item) ? item : choice).exception());
            else
                stop_iteration(ctx, out);
            return;
        }
        {
            RootScope current(ctx);
            truth(ctx, current.root(choice.value()), decision);
        }
        if (failed(decision)) {
            out.raised(decision.exception());
            return;
        }
        if (decision.value() == ctx.builtin(CoreObject::True)) {
            out.ok(item.value());
            return;
        }
    }
}
void accumulate_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Accumulate>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]), function = roots.root(args[2]), first = roots.root(args[3]);
    ReturnSlot input(ctx);
    get_iter(ctx, roots.root(args[1]), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    std::optional<Value> initial;
    if (first.get(ctx) != ctx.builtin(CoreObject::None))
        initial = first.get(ctx);
    ctx.allocate_instance<Accumulate>(type, out, input.value(), function.get(ctx), initial);
}
void accumulate_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Accumulate>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Accumulate>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot initial(ctx), total(ctx), item(ctx), result(ctx);
    ctx.load(self.get(ctx), state.initial, initial);
    if (initial.tag() == ResultTag::Ok) {
        ctx.store(self.get(ctx), state.total, initial.value());
        ctx.clear(self, state.initial);
        out.ok(initial.value());
        return;
    }
    auto status = iter_next(ctx, roots.root(state.iterator), item);
    if (status != NextStatus::Item) {
        if (status == NextStatus::Raised)
            out.raised(item.exception());
        else
            stop_iteration(ctx, out);
        return;
    }
    ctx.load(self.get(ctx), state.total, total);
    if (total.tag() == ResultTag::Empty) {
        ctx.store(self.get(ctx), state.total, item.value());
        out.ok(item.value());
        return;
    }
    if (state.function == ctx.builtin(CoreObject::None)) {
        binary(ctx, roots.root(total.value()), roots.root(item.value()), BinaryOp::Add, result);
    } else {
        const Value arguments[] = {total.value(), item.value()};
        call(ctx, roots.root(state.function), {arguments, {}}, result);
    }
    if (failed(result)) {
        out.raised(result.exception());
        return;
    }
    ctx.store(self.get(ctx), state.total, result.value());
    out.ok(result.value());
}
void batched_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Batched>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]), iterable = roots.root(args[1]);
    ReturnSlot number(ctx), input(ctx);
    index(ctx, roots.root(args[2]), number);
    if (failed(number)) {
        out.raised(number.exception());
        return;
    }
    std::int64_t count;
    if (!int_fits_signed(ctx, number.value(), count)) {
        raise_error(ctx, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return;
    }
    if (count < 1) {
        raise_error(ctx, CoreObject::ValueErrorType, U"n must be at least one", out);
        return;
    }
    get_iter(ctx, iterable, input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    ctx.allocate_instance<Batched>(type, out, input.value(), count);
}
void batched_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Batched>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Batched>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot input(ctx), item(ctx);
    ctx.load(self.get(ctx), state.iterator, input);
    if (input.tag() == ResultTag::Empty) {
        stop_iteration(ctx, out);
        return;
    }
    auto iterator = roots.root(input.value());
    std::vector<Value> values;
    if (static_cast<std::uint64_t>(state.size) > values.max_size()) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
        return;
    }
    values.reserve(static_cast<std::size_t>(state.size));
    for (std::int64_t i = 0; i < state.size; ++i) {
        auto status = iter_next(ctx, iterator, item);
        if (status == NextStatus::Raised) {
            ctx.clear(self, state.iterator);
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Done) {
            if (values.empty()) {
                ctx.clear(self, state.iterator);
                stop_iteration(ctx, out);
                return;
            }
            break;
        }
        values.push_back(roots.root(item.value()).get(ctx));
    }
    cxxpython::core::make_tuple(ctx, values, out);
}
void cycle_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Cycle>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    ReturnSlot input(ctx), cache(ctx);
    get_iter(ctx, roots.root(args[1]), input);
    if (failed(input)) {
        out.raised(input.exception());
        return;
    }
    make_list(ctx, {}, cache);
    if (failed(cache)) {
        out.raised(cache.exception());
        return;
    }
    ctx.allocate_instance<Cycle>(type, out, input.value(), cache.value());
}
void cycle_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Cycle>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Cycle>(self);
    Lease lease(state.active);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot input(ctx), item(ctx), result(ctx);
    auto cache = roots.root(state.saved);
    ctx.load(self.get(ctx), state.iterator, input);
    if (input.tag() == ResultTag::Ok) {
        auto status = iter_next(ctx, roots.root(input.value()), item);
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return;
        }
        if (status == NextStatus::Item) {
            list_append(ctx, cache, roots.root(item.value()), result);
            if (failed(result)) {
                out.raised(result.exception());
                return;
            }
            ++state.count;
            out.ok(item.value());
            return;
        }
        ctx.clear(self, state.iterator);
    }
    if (!state.count) {
        stop_iteration(ctx, out);
        return;
    }
    make_int(ctx, static_cast<std::int64_t>(state.position), result);
    get_item(ctx, cache, roots.root(result.value()), out);
    if (!failed(out)) {
        ++state.position;
        if (state.position == state.count)
            state.position = 0;
    }
}
bool numeric(Attachment &ctx, Value value)
{
    RootScope roots(ctx);
    auto type = roots.root(ctx.type_of(value));
    return is_subtype(ctx, type.get(ctx), ctx.builtin(CoreObject::ComplexType)) ||
           type_attribute(ctx, type.get(ctx), U"__index__").has_value() ||
           type_attribute(ctx, type.get(ctx), U"__int__").has_value() ||
           type_attribute(ctx, type.get(ctx), U"__float__").has_value();
}
void count_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Count>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]), start = roots.root(args[1]), step = roots.root(args[2]);
    if (!numeric(ctx, start.get(ctx)) || !numeric(ctx, step.get(ctx))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"a number is required", out);
        return;
    }
    std::int64_t number = 0, stride = 0;
    const auto int_type = ctx.builtin(CoreObject::IntType);
    bool fast = is_subtype(ctx, ctx.type_of(start), int_type) &&
                is_subtype(ctx, ctx.type_of(step), int_type) &&
                int_fits_signed(ctx, start.get(ctx), number) &&
                int_fits_signed(ctx, step.get(ctx), stride) && stride == 1;
    ctx.allocate_instance<Count>(type, out, start.get(ctx), step.get(ctx), number, fast);
}
void count_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Count>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Count>(self);
    Lease lease(ctx, state.active, state.owner);
    if (!acquire(ctx, lease, out))
        return;
    ReturnSlot current(ctx), next(ctx);
    if (state.fast && state.fast_value < std::numeric_limits<std::int64_t>::max()) {
        // Reserve before boxing: allocation/GC may invoke another counter user.
        const auto value = state.fast_value++;
        lease.release();
        make_int(ctx, value, out);
        return;
    }
    if (state.fast) {
        make_int(ctx, state.fast_value, current);
        if (failed(current)) {
            out.raised(current.exception());
            return;
        }
        ctx.store(self.get(ctx), state.current, current.value());
        state.fast = false;
    }
    ctx.load(self.get(ctx), state.current, current);
    binary(ctx, roots.root(current.value()), roots.root(state.step), BinaryOp::Add, next);
    if (failed(next)) {
        out.raised(next.exception());
        return;
    }
    ctx.store(self.get(ctx), state.current, next.value());
    out.ok(current.value());
}

void pairwise_new(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!ctx.has_layout<TypeObject>(args[0]) ||
        ctx.payload<TypeObject>(args[0]).instance_layout.native_family !=
            LayoutDescriptor::of<Pairwise>().native_family) {
        raise_error(ctx, CoreObject::TypeErrorType, U"pairwise.__new__ requires a pairwise type",
                    out);
        return;
    }
    RootScope roots(ctx);
    auto cls = roots.root(args[0]);
    ReturnSlot iterator(ctx);
    get_iter(ctx, roots.root(args[1]), iterator);
    if (failed(iterator)) {
        out.raised(iterator.exception());
        return;
    }
    ctx.allocate_instance<Pairwise>(cls, out, iterator.value());
}

bool receiver(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<Pairwise>(value))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"pairwise iterator required", out);
    return false;
}

void pairwise_iter(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (receiver(ctx, args[0], out))
        out.ok(args[0]);
}

void pairwise_next(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Pairwise>(self);
    if (state.active.test_and_set(std::memory_order_acquire)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"pairwise iterator already executing", out);
        return;
    }
    struct Release {
        std::atomic_flag &flag;
        ~Release()
        {
            flag.clear(std::memory_order_release);
        }
    } release{state.active};

    ReturnSlot iterator(ctx), old(ctx), item(ctx);
    ctx.load(self.get(ctx), state.iterator, iterator);
    if (iterator.tag() == ResultTag::Empty) {
        stop_iteration(ctx, out);
        return;
    }
    auto input = roots.root(iterator.value());
    ctx.load(self.get(ctx), state.previous, old);
    auto advance = [&](ReturnSlot &result) {
        auto status = iter_next(ctx, input, result);
        if (status != NextStatus::Item) {
            ctx.clear(self, state.iterator);
            ctx.clear(self, state.previous);
            if (status == NextStatus::Raised)
                out.raised(result.exception());
            else
                stop_iteration(ctx, out);
        }
        return status == NextStatus::Item;
    };
    if (old.tag() == ResultTag::Empty) {
        if (!advance(old))
            return;
        ctx.store(self.get(ctx), state.previous, old.value());
    }
    if (!advance(item))
        return;
    const Value pair[] = {old.value(), item.value()};
    cxxpython::core::make_tuple(ctx, pair, out);
    ctx.store(self.get(ctx), state.previous, item.value());
}

// The module namespace retains this function and its type capture on reload.
void state_anchor(Attachment &ctx, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

void initialize_itertools(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    ReturnSlot type_slot(ctx), value(ctx), key(ctx);
    auto anchor = name_lookup(ctx, dictionary.get(ctx), U"_itertools_state");
    std::vector<Value> previous;
    if (anchor) {
        auto saved = roots.root(*anchor);
        if (ctx.has_layout<NativeFunctionObject>(saved.get(ctx)) &&
            ctx.payload<NativeFunctionObject>(saved).entry == state_anchor)
            previous = ctx.payload<NativeFunctionObject>(saved).captures;
    }
    std::vector<Handle> core, types;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto wrap = [&](Handle type, const char32_t *name, CoreObject wrapper_type) {
        auto dict = *TypeView(ctx, type).dictionary();
        auto function = roots.root(*name_lookup(ctx, dict, name));
        if (wrapper_type == CoreObject::StaticMethodType)
            ctx.allocate_instance<StaticMethodObject>(core[static_cast<std::size_t>(wrapper_type)],
                                                      value, function.get(ctx));
        else
            ctx.allocate_instance<ClassMethodObject>(core[static_cast<std::size_t>(wrapper_type)],
                                                     value, function.get(ctx));
        make_str(ctx, name, key);
        name_store(ctx, dict, key.value(), value.value());
    };
    auto make_type = [&](const char *name, LayoutDescriptor layout, SignatureLayout constructor,
                         NativeEntry create, NativeEntry iter, NativeEntry next,
                         std::vector<Value> captures = {}) {
        const auto position = types.size();
        if (position < previous.size()) {
            type_slot.ok(previous[position]);
            types.push_back(roots.root(type_slot.value()));
            return true;
        }
        NativeTypeSpec spec{{name}, layout};
        spec.type.module = U"itertools";
        create_native_type(ctx, spec, type_slot);
        if (failed(type_slot)) {
            out.raised(type_slot.exception());
            return false;
        }
        auto type = roots.root(type_slot.value());
        types.push_back(type);
        install_native_attribute(ctx, core, type, U"__new__", constructor, create,
                                 std::move(captures));
        wrap(type, U"__new__", CoreObject::StaticMethodType);
        install_native_attribute(ctx, core, type, U"__iter__", unary, iter);
        install_native_attribute(ctx, core, type, U"__next__", unary, next);
        return true;
    };
    auto pair_sig = unary;
    pair_sig.parameters.push_back({U"iterable", ParameterKind::PositionalOnly});
    if (!make_type("pairwise", LayoutDescriptor::of<Pairwise>(), pair_sig, pairwise_new,
                   pairwise_iter, pairwise_next))
        return;
    auto variadic = unary;
    variadic.varargs = U"args";
    if (!make_type("chain", LayoutDescriptor::of<Chain>(), variadic, chain_new,
                   iterator_self<Chain>, chain_next))
        return;
    if (previous.size() < 2) {
        install_native_attribute(ctx, core, types[1], U"from_iterable", pair_sig, chain_from);
        wrap(types[1], U"from_iterable", CoreObject::ClassMethodType);
    }
    cxxpython::core::make_tuple(ctx, {}, value);
    auto sentinel = roots.root(value.value());
    auto repeat_sig = unary;
    repeat_sig.parameters.push_back({U"object", ParameterKind::PositionalOrKeyword});
    repeat_sig.parameters.push_back(
        {U"times", ParameterKind::PositionalOrKeyword, sentinel.get(ctx)});
    if (!make_type("repeat", LayoutDescriptor::of<Repeat>(), repeat_sig, repeat_new,
                   iterator_self<Repeat>, repeat_next, {sentinel.get(ctx)}))
        return;
    if (previous.size() < 3)
        install_native_attribute(ctx, core, types[2], U"__length_hint__", unary, repeat_hint);
    if (!make_type("islice", LayoutDescriptor::of<Islice>(), variadic, islice_new,
                   iterator_self<Islice>, islice_next))
        return;
    auto star_sig = unary;
    star_sig.parameters.push_back({U"function", ParameterKind::PositionalOnly});
    star_sig.parameters.push_back({U"iterable", ParameterKind::PositionalOnly});
    if (!make_type("starmap", LayoutDescriptor::of<Starmap>(), star_sig, starmap_new,
                   iterator_self<Starmap>, starmap_next))
        return;
    using Filter = PredicateIterator<PredicateMode::FilterFalse>;
    using Take = PredicateIterator<PredicateMode::TakeWhile>;
    using Drop = PredicateIterator<PredicateMode::DropWhile>;
    if (!make_type("filterfalse", LayoutDescriptor::of<Filter>(), star_sig,
                   predicate_new<PredicateMode::FilterFalse>, iterator_self<Filter>,
                   predicate_next<PredicateMode::FilterFalse>) ||
        !make_type("takewhile", LayoutDescriptor::of<Take>(), star_sig,
                   predicate_new<PredicateMode::TakeWhile>, iterator_self<Take>,
                   predicate_next<PredicateMode::TakeWhile>) ||
        !make_type("dropwhile", LayoutDescriptor::of<Drop>(), star_sig,
                   predicate_new<PredicateMode::DropWhile>, iterator_self<Drop>,
                   predicate_next<PredicateMode::DropWhile>) ||
        !make_type("compress", LayoutDescriptor::of<Compress>(), star_sig, compress_new,
                   iterator_self<Compress>, compress_next))
        return;
    auto accumulate_sig = unary;
    accumulate_sig.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
    accumulate_sig.parameters.push_back(
        {U"func", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::None)});
    accumulate_sig.parameters.push_back(
        {U"initial", ParameterKind::KeywordOnly, ctx.builtin(CoreObject::None)});
    if (!make_type("accumulate", LayoutDescriptor::of<Accumulate>(), accumulate_sig, accumulate_new,
                   iterator_self<Accumulate>, accumulate_next))
        return;
    auto batched_sig = unary;
    batched_sig.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
    batched_sig.parameters.push_back({U"n", ParameterKind::PositionalOrKeyword});
    if (!make_type("batched", LayoutDescriptor::of<Batched>(), batched_sig, batched_new,
                   iterator_self<Batched>, batched_next))
        return;
    if (!make_type("cycle", LayoutDescriptor::of<Cycle>(), pair_sig, cycle_new,
                   iterator_self<Cycle>, cycle_next))
        return;
    auto count_sig = unary;
    count_sig.parameters.push_back(
        {U"start", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::IntZero)});
    count_sig.parameters.push_back(
        {U"step", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::IntOne)});
    if (!make_type("count", LayoutDescriptor::of<Count>(), count_sig, count_new,
                   iterator_self<Count>, count_next))
        return;
    std::vector<Value> captures;
    for (auto type : types)
        captures.push_back(type.get(ctx));
    make_native_function(ctx, "_itertools_state", {}, state_anchor, captures, value,
                         NativeFunctionBinding::Function);
    auto state = roots.root(value.value());
    make_str(ctx, U"_itertools_state", key);
    dict_store(ctx, dictionary, roots.root(key.value()), state, out);
    if (failed(out))
        return;
    const char32_t *names[] = {U"pairwise", U"chain",       U"repeat",    U"islice",
                               U"starmap",  U"filterfalse", U"takewhile", U"dropwhile",
                               U"compress", U"accumulate",  U"batched",   U"cycle",
                               U"count"};
    for (std::size_t i = 0; i < types.size(); ++i) {
        make_str(ctx, names[i], key);
        dict_store(ctx, dictionary, roots.root(key.value()), types[i], out);
        if (failed(out))
            return;
    }
    initialize_itertools_combinatorics(ctx, module, out);
    if (!failed(out))
        initialize_itertools_groups(ctx, module, out);
}
} // namespace cxxpython::core::detail
