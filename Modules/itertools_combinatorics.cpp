// Algorithms adapted from CPython 3.12.14 Modules/itertoolsmodule.c, PSF licensed.
#include "internal/itertools_combinatorics.hpp"

#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <numeric>

namespace cxxpython::core::detail {
namespace {
struct Product {
    ObjectHeader header;
    const Value pools;
    std::vector<std::size_t> indices;
    bool started = false, stopped = false;
    std::atomic_flag active = ATOMIC_FLAG_INIT;

    Product(InitialType type, Value inputs, std::vector<std::size_t> positions)
        : header(type), pools(inputs), indices(std::move(positions))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pools);
    }
    std::size_t external_bytes() const noexcept
    {
        return indices.capacity() * sizeof(std::size_t);
    }
};

template <bool Replacement> struct Combinations {
    ObjectHeader header;
    const Value pool;
    std::vector<std::size_t> indices;
    bool started = false, stopped;
    std::atomic_flag active = ATOMIC_FLAG_INIT;

    Combinations(InitialType type, Value input, std::vector<std::size_t> positions, bool done)
        : header(type), pool(input), indices(std::move(positions)), stopped(done)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pool);
    }
    std::size_t external_bytes() const noexcept
    {
        return indices.capacity() * sizeof(std::size_t);
    }
};

struct Permutations {
    ObjectHeader header;
    const Value pool;
    std::vector<std::size_t> indices, cycles;
    bool started = false, stopped;
    std::atomic_flag active = ATOMIC_FLAG_INIT;

    Permutations(InitialType type, Value input, std::vector<std::size_t> positions,
                 std::vector<std::size_t> counters, bool done)
        : header(type), pool(input), indices(std::move(positions)), cycles(std::move(counters)),
          stopped(done)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(pool);
    }
    std::size_t external_bytes() const noexcept
    {
        return (indices.capacity() + cycles.capacity()) * sizeof(std::size_t);
    }
};

bool failed(const ReturnSlot &slot)
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
void iterator_self(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (check_self<T>(ctx, args[0], out))
        out.ok(args[0]);
}
struct Claim {
    std::atomic_flag &flag;
    bool acquired;
    explicit Claim(std::atomic_flag &value)
        : flag(value), acquired(!flag.test_and_set(std::memory_order_acquire))
    {}
    ~Claim()
    {
        if (acquired)
            flag.clear(std::memory_order_release);
    }
};
bool acquire(Attachment &ctx, const Claim &claim, ReturnSlot &out)
{
    if (claim.acquired)
        return true;
    raise_error(ctx, CoreObject::RuntimeErrorType, U"iterator already executing", out);
    return false;
}

void tuple_from(Attachment &ctx, Handle input, ReturnSlot &out)
{
    RootScope roots(ctx);
    const Value args[] = {input.get(ctx)};
    call(ctx, roots.root(ctx.builtin(CoreObject::TupleType)), {std::span<const Value>(args, 1), {}},
         out);
}
bool as_ssize(Attachment &ctx, Handle input, std::int64_t &result, ReturnSlot &out)
{
    ReturnSlot converted(ctx);
    index(ctx, input, converted);
    if (failed(converted)) {
        out.raised(converted.exception());
        return false;
    }
    if (!int_fits_signed(ctx, converted.value(), result)) {
        raise_error(ctx, CoreObject::OverflowErrorType,
                    U"Python int too large to convert to C ssize_t", out);
        return false;
    }
    return true;
}
bool vector_size(Attachment &ctx, std::size_t count, ReturnSlot &out)
{
    if (count <= std::vector<std::size_t>().max_size())
        return true;
    out.raised(ctx.builtin(CoreObject::MemoryError));
    return false;
}

void product_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Product>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]);
    auto repeat_arg = roots.root(args[1]);
    auto arguments = roots.root(args[2]);
    std::int64_t repeat;
    if (!as_ssize(ctx, repeat_arg, repeat, out))
        return;
    if (repeat < 0) {
        raise_error(ctx, CoreObject::ValueErrorType, U"repeat argument cannot be negative", out);
        return;
    }
    const auto &inputs = ctx.payload<TupleObject>(arguments).items;
    const auto count = repeat == 0 ? 0 : inputs.size();
    constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
    if (repeat && count > limit / sizeof(std::size_t) / static_cast<std::size_t>(repeat)) {
        raise_error(ctx, CoreObject::OverflowErrorType, U"repeat argument too large", out);
        return;
    }
    const auto size = count * static_cast<std::size_t>(repeat);
    if (!vector_size(ctx, size, out))
        return;
    std::vector<std::size_t> positions(size, 0);
    std::vector<Value> pools;
    if (size > pools.max_size()) {
        out.raised(ctx.builtin(CoreObject::MemoryError));
        return;
    }
    pools.reserve(size);
    ReturnSlot pool(ctx), tuple(ctx);
    for (std::size_t i = 0; i < count; ++i) {
        tuple_from(ctx, roots.root(inputs[i]), pool);
        if (failed(pool)) {
            out.raised(pool.exception());
            return;
        }
        pools.push_back(roots.root(pool.value()).get(ctx));
    }
    for (std::size_t i = count; i < size; ++i)
        pools.push_back(pools[i - count]);
    cxxpython::core::make_tuple(ctx, pools, tuple);
    if (failed(tuple)) {
        out.raised(tuple.exception());
        return;
    }
    ctx.allocate_instance<Product>(type, out, tuple.value(), std::move(positions));
}

void product_next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Product>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Product>(self);
    Claim claim(state.active);
    if (!acquire(ctx, claim, out))
        return;
    if (state.stopped) {
        stop_iteration(ctx, out);
        return;
    }
    const auto &pools = ctx.payload<TupleObject>(state.pools).items;
    if (!state.started) {
        for (auto pool : pools) {
            if (ctx.payload<TupleObject>(pool).items.empty()) {
                state.stopped = true;
                stop_iteration(ctx, out);
                return;
            }
        }
    } else {
        std::size_t i = state.indices.size();
        while (i) {
            --i;
            auto &index = state.indices[i];
            if (++index < ctx.payload<TupleObject>(pools[i]).items.size())
                break;
            index = 0;
        }
        if (!i && (state.indices.empty() || state.indices[0] == 0)) {
            state.stopped = true;
            stop_iteration(ctx, out);
            return;
        }
    }
    // An allocation failure permanently exhausts the CPython iterator.
    state.stopped = true;
    std::vector<Value> result;
    result.reserve(pools.size());
    for (std::size_t i = 0; i < pools.size(); ++i)
        result.push_back(ctx.payload<TupleObject>(pools[i]).items[state.indices[i]]);
    cxxpython::core::make_tuple(ctx, result, out);
    if (!failed(out)) {
        state.started = true;
        state.stopped = false;
    }
}

template <bool Replacement>
void combinations_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    using State = Combinations<Replacement>;
    if (!check_class<State>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]), input = roots.root(args[1]);
    std::int64_t r;
    if (!as_ssize(ctx, roots.root(args[2]), r, out))
        return;
    ReturnSlot pool(ctx);
    tuple_from(ctx, input, pool);
    if (failed(pool)) {
        out.raised(pool.exception());
        return;
    }
    if (r < 0) {
        raise_error(ctx, CoreObject::ValueErrorType, U"r must be non-negative", out);
        return;
    }
    const auto count = static_cast<std::size_t>(r);
    if (!vector_size(ctx, count, out))
        return;
    std::vector<std::size_t> positions(count, 0);
    if constexpr (!Replacement)
        std::iota(positions.begin(), positions.end(), std::size_t{0});
    const auto n = ctx.payload<TupleObject>(pool.value()).items.size();
    const bool done = Replacement ? (n == 0 && count != 0) : count > n;
    ctx.allocate_instance<State>(type, out, pool.value(), std::move(positions), done);
}

template <bool Replacement>
void combinations_next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    using State = Combinations<Replacement>;
    if (!check_self<State>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<State>(self);
    Claim claim(state.active);
    if (!acquire(ctx, claim, out))
        return;
    if (state.stopped) {
        stop_iteration(ctx, out);
        return;
    }
    const auto &pool = ctx.payload<TupleObject>(state.pool).items;
    auto &indices = state.indices;
    const auto n = pool.size(), r = indices.size();
    if (state.started) {
        std::size_t i = r;
        while (i) {
            const auto at = i - 1;
            const auto maximum = Replacement ? n - 1 : at + n - r;
            if (indices[at] != maximum)
                break;
            --i;
        }
        if (!i) {
            state.stopped = true;
            stop_iteration(ctx, out);
            return;
        }
        --i;
        ++indices[i];
        for (std::size_t j = i + 1; j < r; ++j)
            indices[j] = indices[j - 1] + (Replacement ? 0 : 1);
    }
    state.stopped = true;
    std::vector<Value> result;
    result.reserve(r);
    for (auto i : indices)
        result.push_back(pool[i]);
    cxxpython::core::make_tuple(ctx, result, out);
    if (!failed(out)) {
        state.started = true;
        state.stopped = false;
    }
}

void permutations_new(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_class<Permutations>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto type = roots.root(args[0]), input = roots.root(args[1]), r_arg = roots.root(args[2]);
    ReturnSlot pool(ctx);
    tuple_from(ctx, input, pool);
    if (failed(pool)) {
        out.raised(pool.exception());
        return;
    }
    const auto n = ctx.payload<TupleObject>(pool.value()).items.size();
    std::int64_t r = static_cast<std::int64_t>(n);
    if (r_arg.get(ctx) != ctx.builtin(CoreObject::None)) {
        if (!is_subtype(ctx, ctx.type_of(r_arg), ctx.builtin(CoreObject::IntType))) {
            raise_error(ctx, CoreObject::TypeErrorType, U"Expected int as r", out);
            return;
        }
        if (!int_fits_signed(ctx, r_arg.get(ctx), r)) {
            raise_error(ctx, CoreObject::OverflowErrorType,
                        U"Python int too large to convert to C ssize_t", out);
            return;
        }
    }
    if (r < 0) {
        raise_error(ctx, CoreObject::ValueErrorType, U"r must be non-negative", out);
        return;
    }
    const auto count = static_cast<std::size_t>(r);
    if (!vector_size(ctx, n, out) || !vector_size(ctx, count, out))
        return;
    std::vector<std::size_t> positions(n), cycles(count, 0);
    std::iota(positions.begin(), positions.end(), std::size_t{0});
    for (std::size_t i = 0; i < std::min(n, count); ++i)
        cycles[i] = n - i;
    ctx.allocate_instance<Permutations>(type, out, pool.value(), std::move(positions),
                                        std::move(cycles), count > n);
}

void permutations_next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_self<Permutations>(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto &state = ctx.payload<Permutations>(self);
    Claim claim(state.active);
    if (!acquire(ctx, claim, out))
        return;
    if (state.stopped) {
        stop_iteration(ctx, out);
        return;
    }
    const auto &pool = ctx.payload<TupleObject>(state.pool).items;
    const auto n = pool.size(), r = state.cycles.size();
    if (state.started) {
        bool advanced = false;
        for (std::size_t i = r; i > 0;) {
            --i;
            if (--state.cycles[i] == 0) {
                std::rotate(state.indices.begin() + i, state.indices.begin() + i + 1,
                            state.indices.end());
                state.cycles[i] = n - i;
            } else {
                std::swap(state.indices[i], state.indices[n - state.cycles[i]]);
                advanced = true;
                break;
            }
        }
        if (!advanced) {
            state.stopped = true;
            stop_iteration(ctx, out);
            return;
        }
    }
    state.stopped = true;
    std::vector<Value> result;
    result.reserve(r);
    for (std::size_t i = 0; i < r; ++i)
        result.push_back(pool[state.indices[i]]);
    cxxpython::core::make_tuple(ctx, result, out);
    if (!failed(out)) {
        state.started = true;
        state.stopped = false;
    }
}

void state_anchor(Attachment &ctx, Handle, std::span<const Value>, ReturnSlot &out)
{
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

void initialize_itertools_combinatorics(Attachment &ctx, Handle module, ReturnSlot &out)
{
    RootScope roots(ctx);
    auto dictionary = roots.root(module_dict(ctx, module.get(ctx)));
    std::vector<Value> previous;
    if (auto found = name_lookup(ctx, dictionary.get(ctx), U"_itertools_combinatorics_state")) {
        auto anchor = roots.root(*found);
        if (ctx.has_layout<NativeFunctionObject>(anchor.get(ctx)) &&
            ctx.payload<NativeFunctionObject>(anchor).entry == state_anchor &&
            ctx.payload<NativeFunctionObject>(anchor).captures.size() == 4)
            previous = ctx.payload<NativeFunctionObject>(anchor).captures;
    }
    std::vector<Handle> core, types;
    for (std::size_t i = 0; i < core_count; ++i)
        core.push_back(roots.root(ctx.builtin(static_cast<CoreObject>(i))));
    ReturnSlot type_slot(ctx), value(ctx), key(ctx);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto make_type = [&](const char *name, LayoutDescriptor layout, SignatureLayout constructor,
                         NativeEntry create, NativeEntry iter, NativeEntry next) {
        const auto position = types.size();
        if (!previous.empty()) {
            types.push_back(roots.root(previous[position]));
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
        install_native_attribute(ctx, core, type, U"__new__", constructor, create);
        auto dict = *TypeView(ctx, type).dictionary();
        auto function = roots.root(*name_lookup(ctx, dict, U"__new__"));
        ctx.allocate_instance<StaticMethodObject>(
            core[static_cast<std::size_t>(CoreObject::StaticMethodType)], value, function.get(ctx));
        make_str(ctx, U"__new__", key);
        name_store(ctx, dict, key.value(), value.value());
        install_native_attribute(ctx, core, type, U"__iter__", unary, iter);
        install_native_attribute(ctx, core, type, U"__next__", unary, next);
        return true;
    };
    auto product_sig = unary;
    product_sig.parameters.push_back(
        {U"repeat", ParameterKind::KeywordOnly, ctx.builtin(CoreObject::IntOne)});
    product_sig.varargs = U"iterables";
    auto required = unary;
    required.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
    required.parameters.push_back({U"r", ParameterKind::PositionalOrKeyword});
    auto optional = unary;
    optional.parameters.push_back({U"iterable", ParameterKind::PositionalOrKeyword});
    optional.parameters.push_back(
        {U"r", ParameterKind::PositionalOrKeyword, ctx.builtin(CoreObject::None)});
    if (!make_type("product", LayoutDescriptor::of<Product>(), product_sig, product_new,
                   iterator_self<Product>, product_next) ||
        !make_type("permutations", LayoutDescriptor::of<Permutations>(), optional, permutations_new,
                   iterator_self<Permutations>, permutations_next) ||
        !make_type("combinations", LayoutDescriptor::of<Combinations<false>>(), required,
                   combinations_new<false>, iterator_self<Combinations<false>>,
                   combinations_next<false>) ||
        !make_type("combinations_with_replacement", LayoutDescriptor::of<Combinations<true>>(),
                   required, combinations_new<true>, iterator_self<Combinations<true>>,
                   combinations_next<true>))
        return;
    if (previous.empty()) {
        std::vector<Value> captures;
        for (auto type : types)
            captures.push_back(type.get(ctx));
        make_native_function(ctx, "_itertools_combinatorics_state", {}, state_anchor, captures,
                             value, NativeFunctionBinding::Function);
        auto anchor = roots.root(value.value());
        make_str(ctx, U"_itertools_combinatorics_state", key);
        dict_store(ctx, dictionary, roots.root(key.value()), anchor, out);
        if (failed(out))
            return;
    }
    const char32_t *names[] = {U"product", U"permutations", U"combinations",
                               U"combinations_with_replacement"};
    for (std::size_t i = 0; i < types.size(); ++i) {
        make_str(ctx, names[i], key);
        dict_store(ctx, dictionary, roots.root(key.value()), types[i], out);
        if (failed(out))
            return;
    }
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
