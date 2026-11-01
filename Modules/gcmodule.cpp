#include "cxxpython/import.hpp"
#include "cxxpython/protocol.hpp"

namespace cxxpython::core::detail {
namespace {
void collect(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    index(context, roots.root(args[0]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    std::int64_t generation;
    if (!int_fits_signed(context, out.value(), generation) || generation < 0 || generation > 2) {
        raise_error(context, CoreObject::ValueErrorType, U"invalid generation", out);
        return;
    }
    // Generation arguments currently select the same full tracing collection;
    // the return count is physical reclamation, not invented CPython gen stats.
    const auto count = context.collect();
    make_int(context, count, out);
}
template <bool Enabled>
void set_enabled(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    context.set_gc_enabled(Enabled);
    out.ok(context.builtin(CoreObject::None));
}
void is_enabled(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    out.ok(context.builtin(context.gc_enabled() ? CoreObject::True : CoreObject::False));
}
void is_finalized(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    out.ok(context.builtin(context.is_finalized(roots.root(args[0])) ? CoreObject::True
                                                                     : CoreObject::False));
}
void is_tracked(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    // Every Python object participates in this runtime's tracing heap, including leaves.
    out.ok(context.builtin(CoreObject::True));
}
void get_objects(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    if (args[0] != context.builtin(CoreObject::None)) {
        index(context, roots.root(args[0]), out);
        if (out.tag() == ResultTag::Raised)
            return;
        std::int64_t generation;
        if (!int_fits_signed(context, out.value(), generation) || generation < -1 ||
            generation > 2) {
            raise_error(context, CoreObject::ValueErrorType, U"invalid generation", out);
            return;
        }
        // All objects belong to the physical full-heap collector (generation 2).
        if (generation == 0 || generation == 1) {
            make_list(context, {}, out);
            return;
        }
    }
    std::vector<Value> objects;
    context.inspect_heap(HeapInspection::Objects, {}, roots, objects);
    make_list(context, objects, out);
}
template <HeapInspection Kind>
void inspect_edges(Attachment &context, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    const auto targets = context.payload<TupleObject>(args[0]).items;
    std::vector<Value> objects;
    context.inspect_heap(Kind, targets, roots, objects);
    make_list(context, objects, out);
}
void counter_dictionary(Attachment &context,
                        std::initializer_list<std::pair<const char32_t *, std::size_t>> counters,
                        ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot dictionary(context), name(context), number(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                          dictionary);
    auto result = roots.root(dictionary.value());
    for (auto [key, value] : counters) {
        make_str(context, key, name);
        make_int(context, value, number);
        dict_store(context, result, roots.root(name.value()), roots.root(number.value()), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    out.ok(result.get(context));
}
void get_stats(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    const auto stats = context.stats();
    RootScope roots(context);
    std::vector<Value> generations;
    for (std::size_t generation = 0; generation < 3; ++generation) {
        counter_dictionary(context,
                           {{U"collections", generation == 2 ? stats.collections : 0},
                            {U"collected", generation == 2 ? stats.reclaimed_objects : 0},
                            {U"uncollectable", 0}},
                           out);
        if (out.tag() == ResultTag::Raised)
            return;
        generations.push_back(roots.root(out.value()).get(context));
    }
    make_list(context, generations, out);
}
void get_count(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    const auto stats = context.stats();
    RootScope roots(context);
    make_int(context, stats.allocations_since_gc, out);
    const Value counters[] = {roots.root(out.value()).get(context),
                              context.builtin(CoreObject::IntZero),
                              context.builtin(CoreObject::IntZero)};
    make_tuple(context, counters, out);
}
void get_heap_stats(Attachment &context, Value, std::span<const Value>, ReturnSlot &out)
{
    const auto s = context.stats();
    counter_dictionary(context,
                       {{U"objects", s.objects},
                        {U"live_bytes", s.live_bytes},
                        {U"metadata_bytes", s.metadata_bytes},
                        {U"roots", s.roots},
                        {U"root_cells", s.root_cells},
                        {U"collections", s.collections},
                        {U"reclaimed_objects", s.reclaimed_objects},
                        {U"reclaimed_bytes", s.reclaimed_bytes},
                        {U"allocated_bytes", s.allocated_bytes},
                        {U"gc_budget", s.gc_budget},
                        {U"allocations_since_gc", s.allocations_since_gc},
                        {U"last_pause_ns", s.last_pause_ns},
                        {U"last_rendezvous_ns", s.last_rendezvous_ns},
                        {U"max_rendezvous_ns", s.max_rendezvous_ns},
                        {U"registered_threads", s.registered_threads},
                        {U"spans", s.spans},
                        {U"reserved_bytes", s.reserved_bytes},
                        {U"transfer_roots", s.transfer_roots}},
                       out);
}
} // namespace
void initialize_gc(Attachment &context, Handle module, ReturnSlot &out)
{
    RootScope roots(context);
    const auto dictionary = roots.root(module_dict(context, module.get(context)));
    ReturnSlot name(context), function(context), two(context);
    make_int(context, 2, two);
    auto install = [&](const char32_t *key, SignatureLayout signature, NativeEntry entry) {
        const std::u32string_view text(key);
        make_native_function(context, std::string(text.begin(), text.end()), std::move(signature),
                             entry, {}, function, NativeFunctionBinding::Function);
        make_str(context, key, name);
        dict_store(context, dictionary, roots.root(name.value()), roots.root(function.value()),
                   out);
        return out.tag() != ResultTag::Raised;
    };
    SignatureLayout generation;
    generation.parameters = {{U"generation", ParameterKind::PositionalOrKeyword, two.value()}};
    if (!install(U"collect", generation, collect) || !install(U"enable", {}, set_enabled<true>) ||
        !install(U"disable", {}, set_enabled<false>) || !install(U"isenabled", {}, is_enabled))
        return;
    SignatureLayout unary;
    unary.parameters = {{U"obj", ParameterKind::PositionalOnly}};
    SignatureLayout optional_generation;
    optional_generation.parameters = {
        {U"generation", ParameterKind::PositionalOrKeyword, context.builtin(CoreObject::None)}};
    SignatureLayout variadic;
    variadic.varargs = U"objs";
    if (!install(U"is_finalized", unary, is_finalized) ||
        !install(U"is_tracked", unary, is_tracked) ||
        !install(U"get_objects", optional_generation, get_objects) ||
        !install(U"get_referents", variadic, inspect_edges<HeapInspection::Referents>) ||
        !install(U"get_referrers", variadic, inspect_edges<HeapInspection::Referrers>) ||
        !install(U"get_stats", {}, get_stats) || !install(U"get_count", {}, get_count) ||
        !install(U"get_heap_stats", {}, get_heap_stats))
        return;
    ReturnSlot callbacks(context), candidate(context);
    context.load_gc_callbacks(callbacks);
    if (callbacks.tag() == ResultTag::Empty) {
        make_list(context, {}, candidate);
        context.publish_gc_callbacks(roots.root(candidate.value()), callbacks);
    }
    make_str(context, U"callbacks", name);
    dict_store(context, dictionary, roots.root(name.value()), roots.root(callbacks.value()), out);
    if (out.tag() == ResultTag::Raised)
        return;
    make_list(context, {}, candidate);
    make_str(context, U"garbage", name);
    dict_store(context, dictionary, roots.root(name.value()), roots.root(candidate.value()), out);
    if (out.tag() != ResultTag::Raised)
        out.ok(context.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
