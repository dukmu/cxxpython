// Set operations follow CPython 3.12.14 Objects/setobject.c (PSF license).
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "internal/setobject.hpp"

#include <stdexcept>

namespace cxxpython::core {
namespace {
// Set and exact-dict snapshots retain their cached hashes and precise roots.
void set_update_impl(Attachment &ctx, Handle self, Handle iterable, ReturnSlot &out)
{
    if (!is_set(ctx, self.get(ctx)) ||
        !is_subtype(ctx, ctx.type_of(self.get(ctx)), ctx.builtin(CoreObject::SetType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"set descriptor requires a set", out);
        return;
    }

    RootScope roots(ctx);
    self = roots.root(self.get(ctx));
    iterable = roots.root(iterable.get(ctx));
    auto table = roots.root(ctx.payload<SetObject>(self).table);
    auto none = roots.root(ctx.builtin(CoreObject::None));
    ReturnSlot result(ctx);

    if (is_set(ctx, iterable.get(ctx)) ||
        ctx.type_of(iterable.get(ctx)) == ctx.builtin(CoreObject::DictType)) {
        Value source = is_set(ctx, iterable.get(ctx)) ? ctx.payload<SetObject>(iterable).table
                                                      : iterable.get(ctx);
        DictView view(ctx, source);
        for (const auto &entry : view.entries()) {
            RootScope step(ctx);
            detail::dict_store_hash(ctx, table, step.root(entry.key), none, entry.hash, result);
            if (result.tag() == ResultTag::Raised) {
                out.raised(result.exception());
                return;
            }
        }
    } else {
        ReturnSlot iterator(ctx), item(ctx);
        get_iter(ctx, iterable, iterator);
        if (iterator.tag() == ResultTag::Raised) {
            out.raised(iterator.exception());
            return;
        }
        auto it = roots.root(iterator.value());
        for (;;) {
            auto status = iter_next(ctx, it, item);
            if (status == NextStatus::Done)
                break;
            if (status == NextStatus::Raised) {
                out.raised(item.exception());
                return;
            }
            RootScope step(ctx);
            dict_store(ctx, table, step.root(item.value()), none, result);
            if (result.tag() == ResultTag::Raised) {
                out.raised(result.exception());
                return;
            }
        }
    }
    out.ok(ctx.builtin(CoreObject::None));
}

enum class set_op_kind { Union, Intersection, Difference, Symmetric };

bool set_op_receiver(Attachment &ctx, Handle function, Value self, bool mutable_only,
                     ReturnSlot &out)
{
    const auto expected = ctx.payload<NativeFunctionObject>(function).captures[0];
    if (ctx.has_layout<SetObject>(self) && is_subtype(ctx, ctx.type_of(self), expected) &&
        (!mutable_only || is_subtype(ctx, ctx.type_of(self), ctx.builtin(CoreObject::SetType))))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"set descriptor requires a set", out);
    return false;
}

void set_op_new_table(Attachment &ctx, ReturnSlot &out)
{
    RootScope roots(ctx);
    ctx.allocate_instance<DictObject>(roots.root(ctx.builtin(CoreObject::DictType)), out);
}

// Walk a source without holding dictionary locks across Python callbacks.
// Returning false stops the walk, including when the callback sets an exception.
template <typename F>
bool set_op_walk(Attachment &ctx, Handle source, bool dict_fast, F &&visit, ReturnSlot &out,
                 bool set_fast = true)
{
    RootScope roots(ctx);
    source = roots.root(source.get(ctx));
    bool set = set_fast && is_set(ctx, source.get(ctx));
    if (set || (dict_fast && ctx.type_of(source.get(ctx)) == ctx.builtin(CoreObject::DictType))) {
        Value table = set ? ctx.payload<SetObject>(source).table : source.get(ctx);
        DictView view(ctx, table);
        for (const auto &entry : view.entries()) {
            RootScope step(ctx);
            if (!visit(step.root(entry.key), entry.hash))
                return false;
        }
        return true;
    }
    ReturnSlot iterator(ctx), item(ctx), hashed(ctx);
    get_iter(ctx, source, iterator);
    if (iterator.tag() == ResultTag::Raised) {
        out.raised(iterator.exception());
        return false;
    }
    auto it = roots.root(iterator.value());
    for (;;) {
        auto status = iter_next(ctx, it, item);
        if (status == NextStatus::Done)
            return true;
        if (status == NextStatus::Raised) {
            out.raised(item.exception());
            return false;
        }
        RootScope step(ctx);
        auto key = step.root(item.value());
        hash(ctx, key, hashed);
        if (hashed.tag() == ResultTag::Raised) {
            out.raised(hashed.exception());
            return false;
        }
        std::int64_t h = 0;
        if (!int_fits_signed(ctx, hashed.value(), h))
            throw std::logic_error("invalid hash result");
        if (!visit(key, h))
            return false;
    }
}

bool set_op_store(Attachment &ctx, Handle table, Handle key, std::int64_t h, ReturnSlot &out)
{
    RootScope roots(ctx);
    detail::dict_store_hash(ctx, table, key, roots.root(ctx.builtin(CoreObject::None)), h, out);
    return out.tag() != ResultTag::Raised;
}

bool set_op_copy(Attachment &ctx, Handle table, Handle source, ReturnSlot &out)
{
    return set_op_walk(
        ctx, source, true,
        [&](Handle key, std::int64_t h) { return set_op_store(ctx, table, key, h, out); }, out);
}

// Return a fresh hidden dictionary. Set operands walk the smaller snapshot;
// arbitrary iterables, including exact dicts, hash each yielded key.
void set_op_intersection(Attachment &ctx, Handle table, Handle source, ReturnSlot &out)
{
    RootScope roots(ctx);
    table = roots.root(table.get(ctx));
    source = roots.root(source.get(ctx));
    ReturnSlot result(ctx), work(ctx);
    set_op_new_table(ctx, result);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    auto destination = roots.root(result.value());
    if (is_set(ctx, source.get(ctx))) {
        auto other = roots.root(ctx.payload<SetObject>(source).table);
        auto small = other;
        auto large = table;
        if (detail::dictionary_size(ctx, other.get(ctx)) >
            detail::dictionary_size(ctx, table.get(ctx))) {
            small = table;
            large = other;
        }
        DictView view(ctx, small.get(ctx));
        for (const auto &entry : view.entries()) {
            RootScope step(ctx);
            auto key = step.root(entry.key);
            detail::dict_lookup_hash(ctx, large, key, entry.hash, work);
            if (work.tag() == ResultTag::Raised) {
                out.raised(work.exception());
                return;
            }
            if (work.tag() == ResultTag::Ok &&
                !set_op_store(ctx, destination, key, entry.hash, work)) {
                out.raised(work.exception());
                return;
            }
        }
    } else {
        bool stopped = false;
        bool completed = set_op_walk(
            ctx, source, false,
            [&](Handle key, std::int64_t h) {
                detail::dict_lookup_hash(ctx, table, key, h, work);
                if (work.tag() == ResultTag::Raised)
                    return false;
                if (work.tag() == ResultTag::Ok) {
                    if (!set_op_store(ctx, destination, key, h, work))
                        return false;
                    if (detail::dictionary_size(ctx, destination.get(ctx)) >=
                        detail::dictionary_size(ctx, table.get(ctx))) {
                        stopped = true;
                        return false;
                    }
                }
                return true;
            },
            work);
        if (!completed && !stopped) {
            out.raised(work.exception());
            return;
        }
    }
    out.ok(destination.get(ctx));
}

// Operate on an unpublished or mutable dictionary; source entries remain rooted
// throughout each comparison and commit.
bool set_op_apply(Attachment &ctx, Handle table, Handle source, set_op_kind op, ReturnSlot &out)
{
    RootScope roots(ctx);
    table = roots.root(table.get(ctx));
    source = roots.root(source.get(ctx));
    if (op == set_op_kind::Union)
        return set_op_copy(ctx, table, source, out);
    if (op == set_op_kind::Difference) {
        ReturnSlot smaller(ctx);
        Handle input = source;
        bool dict_fast = false;
        if (is_set(ctx, source.get(ctx)) &&
            (detail::dictionary_size(ctx, ctx.payload<SetObject>(source).table) >> 3) >
                detail::dictionary_size(ctx, table.get(ctx))) {
            set_op_intersection(ctx, table, source, smaller);
            if (smaller.tag() == ResultTag::Raised) {
                out.raised(smaller.exception());
                return false;
            }
            input = roots.root(smaller.value());
            dict_fast = true;
        }
        return set_op_walk(
            ctx, input, dict_fast,
            [&](Handle key, std::int64_t h) {
                detail::dict_erase_hash(ctx, table, key, h, out);
                return out.tag() != ResultTag::Raised;
            },
            out);
    }
    if (op == set_op_kind::Symmetric) {
        // Deduplicate arbitrary iterables before toggling any keys.
        ReturnSlot temp(ctx);
        Handle input = source;
        if (!is_set(ctx, source.get(ctx)) &&
            ctx.type_of(source.get(ctx)) != ctx.builtin(CoreObject::DictType)) {
            set_op_new_table(ctx, temp);
            if (temp.tag() == ResultTag::Raised) {
                out.raised(temp.exception());
                return false;
            }
            input = roots.root(temp.value());
            if (!set_op_copy(ctx, input, source, out))
                return false;
        }
        return set_op_walk(
            ctx, input, true,
            [&](Handle key, std::int64_t h) {
                detail::dict_erase_hash(ctx, table, key, h, out);
                if (out.tag() == ResultTag::Raised)
                    return false;
                return out.tag() != ResultTag::Empty || set_op_store(ctx, table, key, h, out);
            },
            out);
    }
    ReturnSlot temp(ctx);
    set_op_intersection(ctx, table, source, temp);
    if (temp.tag() == ResultTag::Raised) {
        out.raised(temp.exception());
        return false;
    }
    auto result = roots.root(temp.value());
    dict_clear(ctx, table, out);
    if (out.tag() == ResultTag::Raised)
        return false;
    DictView entries(ctx, result.get(ctx));
    for (const auto &entry : entries.entries()) {
        RootScope step(ctx);
        if (!set_op_store(ctx, table, step.root(entry.key), entry.hash, out))
            return false;
    }
    return true;
}

void set_op_result(Attachment &ctx, Handle self, std::span<const Value> sources, set_op_kind op,
                   ReturnSlot &out)
{
    RootScope roots(ctx);
    self = roots.root(self.get(ctx));
    std::vector<Handle> inputs;
    for (Value value : sources)
        inputs.push_back(roots.root(value));
    ReturnSlot table_out(ctx), work(ctx);
    Handle table;
    std::size_t start = 0;
    if (op == set_op_kind::Intersection && !inputs.empty()) {
        auto original = roots.root(ctx.payload<SetObject>(self).table);
        set_op_intersection(ctx, original, inputs[0], table_out);
        if (table_out.tag() == ResultTag::Raised) {
            out.raised(table_out.exception());
            return;
        }
        table = roots.root(table_out.value());
        start = 1;
    } else if (op == set_op_kind::Difference && !inputs.empty() &&
               (is_set(ctx, inputs[0].get(ctx)) ||
                ctx.type_of(inputs[0].get(ctx)) == ctx.builtin(CoreObject::DictType)) &&
               (detail::dictionary_size(ctx, ctx.payload<SetObject>(self).table) >> 2) <=
                   detail::dictionary_size(ctx, is_set(ctx, inputs[0].get(ctx))
                                                    ? ctx.payload<SetObject>(inputs[0]).table
                                                    : inputs[0].get(ctx))) {
        set_op_new_table(ctx, table_out);
        if (table_out.tag() == ResultTag::Raised) {
            out.raised(table_out.exception());
            return;
        }
        table = roots.root(table_out.value());
        auto other =
            roots.root(is_set(ctx, inputs[0].get(ctx)) ? ctx.payload<SetObject>(inputs[0]).table
                                                       : inputs[0].get(ctx));
        DictView view(ctx, ctx.payload<SetObject>(self).table);
        for (const auto &entry : view.entries()) {
            RootScope step(ctx);
            auto key = step.root(entry.key);
            detail::dict_lookup_hash(ctx, other, key, entry.hash, work);
            if (work.tag() == ResultTag::Raised) {
                out.raised(work.exception());
                return;
            }
            if (work.tag() == ResultTag::Empty &&
                !set_op_store(ctx, table, key, entry.hash, work)) {
                out.raised(work.exception());
                return;
            }
        }
        start = 1;
    } else {
        set_op_new_table(ctx, table_out);
        if (table_out.tag() == ResultTag::Raised) {
            out.raised(table_out.exception());
            return;
        }
        table = roots.root(table_out.value());
        if (!set_op_copy(ctx, table, self, work)) {
            out.raised(work.exception());
            return;
        }
    }
    for (std::size_t i = start; i < inputs.size(); ++i) {
        if (op == set_op_kind::Intersection) {
            set_op_intersection(ctx, table, inputs[i], work);
            if (work.tag() == ResultTag::Raised) {
                out.raised(work.exception());
                return;
            }
            table = roots.root(work.value());
        } else if (!set_op_apply(ctx, table, inputs[i], op, work)) {
            out.raised(work.exception());
            return;
        }
    }
    auto type = is_subtype(ctx, ctx.type_of(self.get(ctx)), ctx.builtin(CoreObject::FrozenSetType))
                    ? CoreObject::FrozenSetType
                    : CoreObject::SetType;
    ctx.allocate_instance<SetObject>(roots.root(ctx.builtin(type)), out, table.get(ctx));
}

template <set_op_kind Op, bool Update, bool Variadic>
void set_op_method(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!set_op_receiver(ctx, function, args[0], Update, out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    std::vector<Handle> sources;
    if constexpr (Variadic) {
        for (Value value : ctx.payload<TupleObject>(args[1]).items)
            sources.push_back(roots.root(value));
    } else {
        sources.push_back(roots.root(args[1]));
    }
    if constexpr (!Update) {
        std::vector<Value> values;
        for (auto source : sources)
            values.push_back(source.get(ctx));
        set_op_result(ctx, self, values, Op, out);
    } else {
        auto table = roots.root(ctx.payload<SetObject>(self).table);
        if constexpr (Op == set_op_kind::Intersection) {
            if (sources.empty()) {
                out.ok(ctx.builtin(CoreObject::None));
                return;
            }
            ReturnSlot temporary(ctx);
            set_op_intersection(ctx, table, sources[0], temporary);
            if (temporary.tag() == ResultTag::Raised) {
                out.raised(temporary.exception());
                return;
            }
            auto result = roots.root(temporary.value());
            for (std::size_t i = 1; i < sources.size(); ++i) {
                set_op_intersection(ctx, result, sources[i], temporary);
                if (temporary.tag() == ResultTag::Raised) {
                    out.raised(temporary.exception());
                    return;
                }
                result = roots.root(temporary.value());
            }
            DictView old(ctx, table.get(ctx));
            for (const auto &entry : old.entries()) {
                RootScope step(ctx);
                auto key = step.root(entry.key);
                detail::dict_erase_hash(ctx, table, key, entry.hash, out);
                if (out.tag() == ResultTag::Raised)
                    return;
            }
            DictView replacement(ctx, result.get(ctx));
            for (const auto &entry : replacement.entries()) {
                RootScope step(ctx);
                if (!set_op_store(ctx, table, step.root(entry.key), entry.hash, out))
                    return;
            }
            out.ok(ctx.builtin(CoreObject::None));
            return;
        }
        for (auto source : sources) {
            if (!set_op_apply(ctx, table, source, Op, out))
                return;
        }
        out.ok(ctx.builtin(CoreObject::None));
    }
}

template <set_op_kind Op, bool Inplace, bool Reverse>
void set_op_operator(Attachment &ctx, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!set_op_receiver(ctx, function, args[0], Inplace, out))
        return;
    if (!is_set(ctx, args[1])) {
        out.ok(ctx.builtin(CoreObject::NotImplemented));
        return;
    }
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto other = roots.root(args[1]);
    if constexpr (Inplace) {
        auto table = roots.root(ctx.payload<SetObject>(self).table);
        if (!set_op_apply(ctx, table, other, Op, out))
            return;
        out.ok(self.get(ctx));
    } else {
        Value source = Reverse ? self.get(ctx) : other.get(ctx);
        set_op_result(ctx, Reverse ? other : self, std::span<const Value>(&source, 1), Op, out);
    }
}

enum class set_op_relation { Subset, Superset, Disjoint };
template <set_op_relation Relation>
void set_op_relation_method(Attachment &ctx, Handle function, std::span<const Value> args,
                            ReturnSlot &out)
{
    if (!set_op_receiver(ctx, function, args[0], false, out))
        return;
    RootScope roots(ctx);
    auto self = roots.root(args[0]);
    auto other = roots.root(args[1]);
    auto table = roots.root(ctx.payload<SetObject>(self).table);
    bool answer = true;
    if constexpr (Relation != set_op_relation::Disjoint) {
        if (is_set(ctx, other.get(ctx))) {
            auto other_table = roots.root(ctx.payload<SetObject>(other).table);
            auto subset = Relation == set_op_relation::Subset ? table : other_table;
            auto superset = Relation == set_op_relation::Subset ? other_table : table;
            if (detail::dictionary_size(ctx, subset.get(ctx)) >
                detail::dictionary_size(ctx, superset.get(ctx))) {
                out.ok(ctx.builtin(CoreObject::False));
                return;
            }
            DictView entries(ctx, subset.get(ctx));
            for (const auto &entry : entries.entries()) {
                RootScope step(ctx);
                detail::dict_lookup_hash(ctx, superset, step.root(entry.key), entry.hash, out);
                if (out.tag() == ResultTag::Raised)
                    return;
                if (out.tag() == ResultTag::Empty) {
                    out.ok(ctx.builtin(CoreObject::False));
                    return;
                }
            }
            out.ok(ctx.builtin(CoreObject::True));
            return;
        }
    }
    if constexpr (Relation == set_op_relation::Subset) {
        ReturnSlot intersection(ctx);
        Value source = other.get(ctx);
        set_op_result(ctx, self, std::span<const Value>(&source, 1), set_op_kind::Intersection,
                      intersection);
        if (intersection.tag() == ResultTag::Raised) {
            out.raised(intersection.exception());
            return;
        }
        auto result = roots.root(intersection.value());
        answer = detail::dictionary_size(ctx, ctx.payload<SetObject>(result).table) ==
                 detail::dictionary_size(ctx, table.get(ctx));
    } else {
        bool set_fast = true;
        Handle walk = other;
        Handle lookup = table;
        if constexpr (Relation == set_op_relation::Disjoint) {
            if (self.get(ctx) == other.get(ctx)) {
                out.ok(ctx.builtin(detail::dictionary_size(ctx, table.get(ctx)) == 0
                                       ? CoreObject::True
                                       : CoreObject::False));
                return;
            }
            auto other_type = ctx.type_of(other.get(ctx));
            set_fast = other_type == ctx.builtin(CoreObject::SetType) ||
                       other_type == ctx.builtin(CoreObject::FrozenSetType);
            if (set_fast && detail::dictionary_size(ctx, ctx.payload<SetObject>(other).table) >
                                detail::dictionary_size(ctx, table.get(ctx))) {
                walk = self;
                lookup = roots.root(ctx.payload<SetObject>(other).table);
            }
        }
        bool completed = set_op_walk(
            ctx, walk, false,
            [&](Handle key, std::int64_t h) {
                detail::dict_lookup_hash(ctx, lookup, key, h, out);
                if (out.tag() == ResultTag::Raised)
                    return false;
                bool present = out.tag() == ResultTag::Ok;
                if ((Relation == set_op_relation::Disjoint && present) ||
                    (Relation == set_op_relation::Superset && !present)) {
                    answer = false;
                    return false;
                }
                return true;
            },
            out, set_fast);
        if (!completed && out.tag() == ResultTag::Raised)
            return;
    }
    out.ok(ctx.builtin(answer ? CoreObject::True : CoreObject::False));
}

void set_op_install(Attachment &ctx, std::span<const Handle> core)
{
    SignatureLayout pair;
    pair.parameters = {{U"self", ParameterKind::PositionalOnly},
                       {U"other", ParameterKind::PositionalOnly}};
    SignatureLayout variadic;
    variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
    variadic.varargs = U"others";
    auto install = [&](CoreObject type, const char32_t *name, const SignatureLayout &sig,
                       NativeEntry entry) {
        auto owner = core[static_cast<std::size_t>(type)];
        detail::install_native_attribute(ctx, core, owner, name, sig, entry, {owner.get(ctx)});
    };
    for (auto type : {CoreObject::SetType, CoreObject::FrozenSetType}) {
        install(type, U"union", variadic, set_op_method<set_op_kind::Union, false, true>);
        install(type, U"intersection", variadic,
                set_op_method<set_op_kind::Intersection, false, true>);
        install(type, U"difference", variadic, set_op_method<set_op_kind::Difference, false, true>);
        install(type, U"symmetric_difference", pair,
                set_op_method<set_op_kind::Symmetric, false, false>);
        install(type, U"issubset", pair, set_op_relation_method<set_op_relation::Subset>);
        install(type, U"issuperset", pair, set_op_relation_method<set_op_relation::Superset>);
        install(type, U"isdisjoint", pair, set_op_relation_method<set_op_relation::Disjoint>);
        install(type, U"__or__", pair, set_op_operator<set_op_kind::Union, false, false>);
        install(type, U"__ror__", pair, set_op_operator<set_op_kind::Union, false, true>);
        install(type, U"__and__", pair, set_op_operator<set_op_kind::Intersection, false, false>);
        install(type, U"__rand__", pair, set_op_operator<set_op_kind::Intersection, false, true>);
        install(type, U"__sub__", pair, set_op_operator<set_op_kind::Difference, false, false>);
        install(type, U"__rsub__", pair, set_op_operator<set_op_kind::Difference, false, true>);
        install(type, U"__xor__", pair, set_op_operator<set_op_kind::Symmetric, false, false>);
        install(type, U"__rxor__", pair, set_op_operator<set_op_kind::Symmetric, false, true>);
    }
    auto type = CoreObject::SetType;
    install(type, U"update", variadic, set_op_method<set_op_kind::Union, true, true>);
    install(type, U"intersection_update", variadic,
            set_op_method<set_op_kind::Intersection, true, true>);
    install(type, U"difference_update", variadic,
            set_op_method<set_op_kind::Difference, true, true>);
    install(type, U"symmetric_difference_update", pair,
            set_op_method<set_op_kind::Symmetric, true, false>);
    install(type, U"__ior__", pair, set_op_operator<set_op_kind::Union, true, false>);
    install(type, U"__iand__", pair, set_op_operator<set_op_kind::Intersection, true, false>);
    install(type, U"__isub__", pair, set_op_operator<set_op_kind::Difference, true, false>);
    install(type, U"__ixor__", pair, set_op_operator<set_op_kind::Symmetric, true, false>);
}

} // namespace

void detail::install_set_operations(Attachment &ctx, std::span<const Handle> core)
{
    set_op_install(ctx, core);
}

void detail::set_update(Attachment &ctx, Handle self, Handle iterable, ReturnSlot &out)
{
    set_update_impl(ctx, self, iterable, out);
}
} // namespace cxxpython::core
