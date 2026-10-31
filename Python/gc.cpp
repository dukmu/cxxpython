#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/type.hpp"
#include "cxxpython/weakrefobject.hpp"
#include "internal/errors.hpp"
#include "internal/import.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <random>
#include <unordered_set>

namespace cxxpython::core {
void LayoutDescriptor::trace_storage(const ObjectHeader &object, Tracer &tracer) const noexcept
{
    trace(object, tracer);
    const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
    if (!count)
        return;
    auto cells = field_cells(const_cast<ObjectHeader *>(&object));
    for (std::size_t i = 0; i < count; ++i)
        tracer.visit(cells[i]);
}
void LayoutDescriptor::destroy_storage(ObjectHeader *object) const noexcept
{
    const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
    if (count) {
        auto cells = field_cells(object);
        for (std::size_t i = 0; i < count; ++i)
            std::destroy_at(cells + i);
    }
    destroy(object);
}
namespace detail {
struct TransferRecord {
    ObjectHeader header;
    const Value payload;
    TransferRecord *previous = nullptr,
                   *next = nullptr; // Coordinator-owned root links, not heap edges.
    TransferRecord(InitialType head, Value value) : header(head), payload(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(payload);
    }
};
std::uint64_t siphash24(std::span<const std::byte> data, std::array<std::uint64_t, 2> key) noexcept
{
    // SipHash-2-4: two compression rounds per word, four final rounds.
    std::uint64_t a = 0x736f6d6570736575ULL ^ key[0], b = 0x646f72616e646f6dULL ^ key[1];
    std::uint64_t c = 0x6c7967656e657261ULL ^ key[0], d = 0x7465646279746573ULL ^ key[1];
    auto round = [&] {
        a += b;
        b = std::rotl(b, 13);
        b ^= a;
        a = std::rotl(a, 32);
        c += d;
        d = std::rotl(d, 16);
        d ^= c;
        a += d;
        d = std::rotl(d, 21);
        d ^= a;
        c += b;
        b = std::rotl(b, 17);
        b ^= c;
        c = std::rotl(c, 32);
    };
    std::size_t position = 0;
    while (data.size() - position >= 8) {
        std::uint64_t word = 0;
        for (unsigned i = 0; i < 8; ++i)
            word |= std::to_integer<std::uint64_t>(data[position + i]) << (i * 8);
        d ^= word;
        round();
        round();
        a ^= word;
        position += 8;
    }
    std::uint64_t tail = static_cast<std::uint64_t>(data.size()) << 56;
    for (std::size_t i = 0; i < data.size() - position; ++i)
        tail |= std::to_integer<std::uint64_t>(data[position + i]) << (i * 8);
    d ^= tail;
    round();
    round();
    a ^= tail;
    c ^= 0xff;
    round();
    round();
    round();
    round();
    return a ^ b ^ c ^ d;
}
struct RootCell {
    ObjectHeader *object = nullptr;
    const void *scope = nullptr;
    RootCell *next = nullptr; // Owning RootScope chain, or arena free list after release.
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    ResultTag tag = ResultTag::Empty;
};

// Owner-only writes; the epoch gate publishes a frozen arena to GC.
struct RootArena {
    explicit RootArena(std::uint64_t identity) : identity(identity) {}
    const std::uint64_t identity;
    std::deque<RootCell> cells;
    RootCell *free = nullptr;
    std::size_t count = 0;
};
struct AllocationSpan;
// Adjacent metadata has a fixed offset from every object, including
// over-aligned payloads. Span domain/layout never change after creation.
struct Allocation {
    explicit Allocation(AllocationSpan *span) : span(span) {}
    Allocation *next = nullptr; // Free list or STW mark worklist.
    std::atomic<std::size_t> external_bytes{0};
    AllocationSpan *const span;
    bool live = false, marked = false;
    enum class Finalization : unsigned char { Unseen, Queued, Running, Finalized };
    Finalization finalization = Finalization::Unseen;
    Allocation *finalizer_next = nullptr;
    WeakRefObject *weakrefs = nullptr;

    ObjectHeader *object() noexcept
    {
        return reinterpret_cast<ObjectHeader *>(reinterpret_cast<std::byte *>(this) +
                                                sizeof(Allocation));
    }
    static Allocation &from(ObjectHeader *object) noexcept
    {
        return *reinterpret_cast<Allocation *>(reinterpret_cast<std::byte *>(object) -
                                               sizeof(Allocation));
    }
};
struct AllocationSpan {
    AllocationSpan(State *owner, LayoutDescriptor layout, std::size_t target_bytes)
        : owner(owner), layout(layout)
    {
        const auto alignment = layout.storage_alignment();
        const auto limit = std::numeric_limits<std::size_t>::max();
        if (alignment - 1 > limit - sizeof(Allocation))
            throw std::bad_alloc();
        object_offset = (sizeof(Allocation) + alignment - 1) & ~(alignment - 1);
        const auto bytes = layout.storage_size();
        if (bytes > limit - object_offset || alignment - 1 > limit - object_offset - bytes)
            throw std::bad_alloc();
        stride = (object_offset + bytes + alignment - 1) & ~(alignment - 1);
        capacity = std::max(std::size_t{1}, target_bytes / stride);
        reserved_bytes = capacity * stride;
        storage = static_cast<std::byte *>(layout.allocate_storage(reserved_bytes));
    }
    ~AllocationSpan()
    {
        for (std::size_t i = 0; i < extent; ++i) {
            auto &allocation = at(i);
            if (allocation.live)
                layout.destroy_storage(allocation.object());
            std::destroy_at(&allocation);
        }
        layout.deallocate_storage(storage);
    }
    AllocationSpan(const AllocationSpan &) = delete;
    AllocationSpan &operator=(const AllocationSpan &) = delete;
    Allocation &at(std::size_t index) const noexcept
    {
        return *reinterpret_cast<Allocation *>(storage + index * stride + object_offset -
                                               sizeof(Allocation));
    }
    bool available() const noexcept
    {
        return free || extent != capacity;
    }
    Allocation &take() noexcept
    {
        if (!free)
            return *std::construct_at(&at(extent++), this);
        auto *allocation = free;
        free = free->next;
        allocation->next = nullptr;
        return *allocation;
    }
    void discard(Allocation &allocation) noexcept
    {
        if (allocation.live)
            --live_count;
        allocation.live = false;
        allocation.marked = false;
        allocation.external_bytes.store(0, std::memory_order_relaxed);
        allocation.finalization = Allocation::Finalization::Unseen;
        allocation.finalizer_next = nullptr;
        assert(!allocation.weakrefs);
        allocation.next = free;
        free = &allocation;
    }

    State *const owner;
    const LayoutDescriptor layout;
    std::byte *storage;
    std::size_t object_offset, stride, capacity, reserved_bytes;
    std::size_t extent = 0, live_count = 0;
    Allocation *free = nullptr;
};
struct ThreadRecord {
    struct ProtocolEntry {
        ObjectHeader *type = nullptr, *owner = nullptr, *descriptor = nullptr;
        Protocol protocol = Protocol::Count;
        SlotState state = SlotState::Missing;
        std::uint64_t epoch = 0;
    };
    explicit ThreadRecord(std::uint64_t identity)
        : owner(std::this_thread::get_id()), roots(identity)
    {}
    const std::thread::id owner;
    RootArena roots;
    std::atomic<MutatorState> state{MutatorState::PendingAttach};
    std::uint64_t observed_epoch = 0, ack_epoch = 0, wait_epoch = 0;
    std::size_t collection_result = 0;
    std::list<AllocationSpan> spans;
    AllocationSpan *current_span = nullptr;
    std::size_t pending_bytes = 0;
    std::size_t allocations_since_gc = 0;
    bool collecting_callbacks = false;
    bool dispatching = false;
    Allocation *finalizers = nullptr;
    WeakRefObject *weak_callbacks = nullptr;
    ObjectHeader *exit_sentinel = nullptr;
    void (*release_exit_sentinel)(ObjectHeader *) noexcept = nullptr;
    // Borrowed identities, not roots. Sweep clears them before reuse.
    std::array<ProtocolEntry, 64> protocols;
    ProtocolEntry &protocol_entry(Value type, Protocol protocol) noexcept
    {
        const auto hash = (reinterpret_cast<std::uintptr_t>(type.object()) >> 4) ^
                          (static_cast<std::size_t>(protocol) * 17);
        return protocols[hash % protocols.size()];
    }
};

std::size_t saturated_add(std::size_t a, std::size_t b) noexcept
{
    return a + std::min(b, std::numeric_limits<std::size_t>::max() - a);
}

struct State {
    explicit State(HeapConfig config_in) : config(config_in), budget(config.minimum_gc_budget) {}

    // Weak identity cache; sweep removes entries before reclaiming allocations.
    std::map<std::u32string, ObjectHeader *> interned_strings;

    bool other_active(const ThreadRecord *self = nullptr) const
    {
        return std::any_of(threads.begin(), threads.end(), [&](const auto &thread) {
            return thread.get() != self &&
                   thread->state.load(std::memory_order_acquire) == MutatorState::Active;
        });
    }

    void resume(ThreadRecord &thread, std::unique_lock<std::mutex> &gate)
    {
        changed.wait(gate, [&] { return phase == GcPhase::Running; });
        thread.observed_epoch = completed_epoch;
        thread.state.store(MutatorState::Active, std::memory_order_release);
    }
    HeapStats capture_stats() const
    {
        std::size_t roots = 0, cells = 0;
        for (const auto &thread : threads) {
            roots += thread->roots.count;
            roots += thread->exit_sentinel != nullptr;
            cells += thread->roots.cells.size();
        }
        HeapStats result{0,
                         0,
                         cells * sizeof(RootCell) + threads.size() * sizeof(ThreadRecord),
                         roots,
                         cells,
                         collections,
                         reclaimed_bytes,
                         last_pause_ns,
                         threads.size(),
                         last_rendezvous_ns,
                         max_rendezvous_ns};
        result.transfer_roots = transfer_count;
        result.reclaimed_objects = reclaimed_objects;
        result.allocated_bytes = allocated_since_gc.load(std::memory_order_relaxed);
        result.gc_budget = budget;
        auto count = [&](const auto &spans) {
            for (const auto &span : spans) {
                ++result.spans;
                result.objects += span.live_count;
                result.reserved_bytes = saturated_add(result.reserved_bytes,
                                                      span.reserved_bytes + sizeof(AllocationSpan));
                result.metadata_bytes +=
                    sizeof(AllocationSpan) + span.capacity * sizeof(Allocation);
                for (std::size_t i = 0; i < span.extent; ++i)
                    if (const auto &allocation = span.at(i); allocation.live)
                        result.live_bytes = saturated_add(
                            result.live_bytes, saturated_add(span.layout.storage_size(),
                                                             allocation.external_bytes.load(
                                                                 std::memory_order_relaxed)));
            }
        };
        count(retired_spans);
        for (const auto &thread : threads) {
            count(thread->spans);
            result.allocations_since_gc =
                saturated_add(result.allocations_since_gc, thread->allocations_since_gc);
            result.allocated_bytes = saturated_add(result.allocated_bytes, thread->pending_bytes);
        }
        result.allocations_since_gc =
            saturated_add(result.allocations_since_gc, retired_allocations_since_gc);
        return result;
    }
    void charge(ThreadRecord &thread, std::size_t bytes) noexcept
    {
        thread.pending_bytes = saturated_add(thread.pending_bytes, bytes);
        if (thread.pending_bytes < std::min(std::size_t{4096}, budget))
            return;
        flush_charge(thread);
    }
    void flush_charge(ThreadRecord &thread) noexcept
    {
        if (!thread.pending_bytes)
            return;
        auto charged = allocated_since_gc.load(std::memory_order_relaxed);
        while (!allocated_since_gc.compare_exchange_weak(
            charged, saturated_add(charged, thread.pending_bytes), std::memory_order_relaxed)) {
        }
        thread.pending_bytes = 0;
    }
    void unlink_transfer(TransferRecord &record) noexcept
    {
        if (record.previous)
            record.previous->next = record.next;
        else {
            assert(transfer_head == &record);
            transfer_head = record.next;
        }
        if (record.next)
            record.next->previous = record.previous;
        record.previous = record.next = nullptr;
        assert(transfer_count);
        --transfer_count;
    }
    std::size_t rendezvous(ThreadRecord &caller, void (*observe)(State &, void *) = nullptr,
                           void *data = nullptr)
    {
        std::unique_lock gate(coordinator);
        for (;;) {
            if (phase == GcPhase::Running)
                break;
            const auto epoch = request_epoch.load(std::memory_order_relaxed);
            const bool joined_collection = collection_request;
            caller.wait_epoch = epoch;
            caller.ack_epoch = epoch;
            caller.state.store(MutatorState::Parked, std::memory_order_release);
            changed.notify_all();
            resume(caller, gate);
            if (!observe && joined_collection)
                return caller.collection_result;
        }
        if (observe && !other_active(&caller)) {
            observe(*this, data);
            return 0;
        }
        const auto epoch = request_epoch.load(std::memory_order_relaxed) + 1;
        if (!epoch)
            throw std::overflow_error("GC epochs exhausted");
        phase = GcPhase::Requesting;
        collection_request = observe == nullptr;
        caller.wait_epoch = epoch;
        caller.ack_epoch = epoch;
        caller.state.store(MutatorState::Collector, std::memory_order_release);
        const auto requested = std::chrono::steady_clock::now();
        request_epoch.store(epoch, std::memory_order_release);
        changed.notify_all();
        changed.wait(gate, [&] { return !other_active(&caller); });
        if (!observe) {
            last_rendezvous_ns =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::steady_clock::now() - requested)
                                               .count());
            max_rendezvous_ns = std::max(max_rendezvous_ns, last_rendezvous_ns);
        }
        phase = GcPhase::Collecting;
        // Membership cannot change until Running; exiting owners keep
        // their record/State leases while waiting for this round.
        auto finish = [&](std::size_t collected) {
            for (const auto &thread : threads)
                if (thread->wait_epoch == epoch)
                    thread->collection_result = collected;
            completed_epoch = epoch;
            caller.observed_epoch = epoch;
            caller.state.store(MutatorState::Active, std::memory_order_release);
            phase = GcPhase::Running;
            changed.notify_all();
        };
        std::size_t collected;
        try {
            if (observe) {
                observe(*this, data);
                collected = 0;
            } else
                collected = sweep();
        } catch (...) {
            finish(0);
            throw;
        }
        finish(collected);
        return collected;
    }

    void mark(ObjectHeader *object) noexcept
    {
        if (!object)
            return;
        auto &allocation = Allocation::from(object);
        assert(allocation.span->owner == this && allocation.live);
        if (allocation.marked)
            return;
        allocation.marked = true;
        allocation.next = worklist;
        worklist = &allocation;
    }
    template <class F> void each_allocation(F visit)
    {
        auto scan = [&](auto &spans) {
            for (auto &span : spans)
                for (std::size_t i = 0; i < span.extent; ++i)
                    if (auto &allocation = span.at(i); allocation.live)
                        visit(allocation);
        };
        scan(retired_spans);
        for (const auto &thread : threads)
            scan(thread->spans);
    }
    static bool python_object(ObjectHeader *object) noexcept
    {
        return object && object->type.object_.load(std::memory_order_relaxed);
    }
    bool inspectable(ObjectHeader *object) const noexcept
    {
        if (!python_object(object))
            return false;
        // Structural host objects remain inspectable before core bootstrap.
        // Afterwards, construction roots must not expose unfinished Python types.
        if (!core_published)
            return true;
        auto ready = [](ObjectHeader *type) {
            return reinterpret_cast<const TypeObject *>(type)->state.load(
                       std::memory_order_acquire) == TypeState::Ready;
        };
        if (!ready(object->type.object_.load(std::memory_order_relaxed)))
            return false;
        const auto family = Allocation::from(object).span->layout.native_family;
        if (family == &native_layout_id<TypeObject>)
            return ready(object);
        if (family == &native_layout_id<DictObject>) {
            if (auto *owner =
                    reinterpret_cast<const DictObject *>(object)->namespace_owner.object_.load(
                        std::memory_order_relaxed))
                return ready(owner);
        } else if (family == &native_layout_id<GetSetDescriptorObject>)
            return ready(reinterpret_cast<const GetSetDescriptorObject *>(object)->owner.object());
        else if (family == &native_layout_id<MemberDescriptorObject>)
            return ready(reinterpret_cast<const MemberDescriptorObject *>(object)->owner.object());
        else if (family == &native_layout_id<MethodDescriptorObject>)
            return ready(reinterpret_cast<const MethodDescriptorObject *>(object)->owner.object());
        return true;
    }
    // Internal snapshots/storage are transparent to Python heap inspection.
    // Stop at each Python object rather than following its transitive graph.
    std::vector<ObjectHeader *> referents(ObjectHeader *source) const
    {
        struct Edges {
            const State &state;
            std::vector<ObjectHeader *> pending, visible;
            std::exception_ptr error;
        } edges{*this, {}, {}, {}};
        Tracer tracer(&edges, [](void *opaque, ObjectHeader *object) noexcept {
            auto &edges = *static_cast<Edges *>(opaque);
            if (!object || edges.error)
                return;
            try {
                if (!python_object(object))
                    edges.pending.push_back(object);
                else if (edges.state.inspectable(object))
                    edges.visible.push_back(object);
            } catch (...) {
                edges.error = std::current_exception();
            }
        });
        auto trace = [&](ObjectHeader *object) {
            tracer.visit(object->type);
            Allocation::from(object).span->layout.trace_storage(*object, tracer);
            if (edges.error)
                std::rethrow_exception(edges.error);
        };
        trace(source);
        std::unordered_set<ObjectHeader *> visited;
        while (!edges.pending.empty()) {
            auto *object = edges.pending.back();
            edges.pending.pop_back();
            if (visited.insert(object).second)
                trace(object);
        }
        return std::move(edges.visible);
    }
    static bool python_finalizer(Allocation &allocation) noexcept
    {
        auto *type = allocation.object()->type.object_.load(std::memory_order_relaxed);
        if (!type ||
            Allocation::from(type).span->layout.native_family != &native_layout_id<TypeObject>)
            return false;
        const auto &data = *reinterpret_cast<const TypeObject *>(type);
        if (data.state != TypeState::Ready)
            return false;
        for (auto base : data.mro) {
            const auto *metadata = reinterpret_cast<const TypeObject *>(base.object())
                                       ->metadata.object_.load(std::memory_order_relaxed);
            if (metadata && reinterpret_cast<const TypeSnapshot *>(metadata)->finalizer)
                return true;
        }
        return false;
    }
    static void unlink_weakref(WeakRefObject &ref) noexcept
    {
        auto *target = ref.target.object_.load(std::memory_order_relaxed);
        if (!target)
            return;
        if (ref.previous)
            ref.previous->next = ref.next;
        else
            Allocation::from(target).weakrefs = ref.next;
        if (ref.next)
            ref.next->previous = ref.previous;
        ref.previous = ref.next = nullptr;
    }

    std::size_t sweep()
    {
        const auto start = std::chrono::steady_clock::now();
        Tracer tracer(this, [](void *state, ObjectHeader *object) noexcept {
            static_cast<State *>(state)->mark(object);
        });
        for (const auto &thread : threads) {
            thread->protocols.fill({});
            mark(thread->exit_sentinel);
            for (const auto &cell : thread->roots.cells)
                if (cell.scope && cell.tag != ResultTag::Empty)
                    mark(cell.object);
        }
        for (auto *record = transfer_head; record; record = record->next)
            mark(&record->header);
        mark(object_type);
        mark(type_type);
        for (auto *object : core)
            mark(object);
        mark(exit_callbacks);
        mark(gc_callbacks);
        mark(signal_state);
        mark(audit_hooks);
        mark(sys_dictionary);
        auto trace_work = [&] {
            while (worklist) {
                Allocation *allocation = worklist;
                worklist = allocation->next;
                tracer.visit(allocation->object()->type);
                allocation->span->layout.trace_storage(*allocation->object(), tracer);
            }
        };
        trace_work();
        // Weak death is decided from program roots, before quarantine retains
        // the complete candidate graph. No Python or managed allocation here.
        each_allocation([&](Allocation &allocation) {
            if (allocation.marked)
                return;
            auto *ref = std::exchange(allocation.weakrefs, nullptr);
            while (ref) {
                auto *next = ref->next;
                ref->target.object_.store(nullptr, std::memory_order_relaxed);
                ref->previous = ref->next = nullptr;
                auto *callback = ref->callback.object_.load(std::memory_order_relaxed);
                if (callback && callback != core[static_cast<std::size_t>(CoreObject::None)] &&
                    Allocation::from(&ref->header).marked) {
                    ref->pending_next = nullptr;
                    if (weak_callback_tail)
                        weak_callback_tail->pending_next = ref;
                    else
                        weak_callback_head = ref;
                    weak_callback_tail = ref;
                }
                ref = next;
            }
        });
        auto queue_finalizer = [&](Allocation &allocation) {
            allocation.finalization = Allocation::Finalization::Queued;
            allocation.finalizer_next = nullptr;
            if (finalizer_tail)
                finalizer_tail->finalizer_next = &allocation;
            else
                finalizer_head = &allocation;
            finalizer_tail = &allocation;
        };
        // Register the complete Python finalizer isolate before tracing it.
        // Native resources reachable from a potentially resurrected object
        // must not be disposed in that same finalization batch.
        each_allocation([&](Allocation &allocation) {
            if (!allocation.marked && allocation.finalization == Allocation::Finalization::Unseen &&
                python_finalizer(allocation))
                queue_finalizer(allocation);
        });
        auto retain = [&](Allocation *finalizers, WeakRefObject *callbacks) {
            for (auto *item = finalizers; item; item = item->finalizer_next)
                mark(item->object());
            for (auto *item = callbacks; item; item = item->pending_next)
                mark(&item->header);
        };
        retain(finalizer_head, weak_callback_head);
        for (const auto &thread : threads)
            retain(thread->finalizers, thread->weak_callbacks);
        trace_work();
        each_allocation([&](Allocation &allocation) {
            if (!allocation.marked && allocation.finalization == Allocation::Finalization::Unseen &&
                allocation.span->layout.finalize)
                queue_finalizer(allocation);
        });
        retain(finalizer_head, nullptr);
        trace_work();
        std::size_t collected = 0;
        std::erase_if(interned_strings,
                      [](const auto &entry) { return !Allocation::from(entry.second).marked; });
        reclaimed_bytes = 0;
        std::size_t live_bytes = 0;
        auto sweep_spans = [&](auto &spans) {
            for (auto it = spans.begin(); it != spans.end();) {
                auto &span = *it;
                for (std::size_t i = 0; i < span.extent; ++i) {
                    auto &allocation = span.at(i);
                    if (!allocation.live)
                        continue;
                    const auto bytes =
                        saturated_add(span.layout.storage_size(),
                                      allocation.external_bytes.load(std::memory_order_relaxed));
                    if (allocation.marked) {
                        allocation.marked = false;
                        allocation.next = nullptr;
                        live_bytes = saturated_add(live_bytes, bytes);
                        continue;
                    }
                    reclaimed_bytes = saturated_add(reclaimed_bytes, bytes);
                    if (span.layout.native_family == &native_layout_id<WeakRefObject>)
                        unlink_weakref(*reinterpret_cast<WeakRefObject *>(allocation.object()));
                    span.layout.destroy_storage(allocation.object());
                    span.discard(allocation);
                    ++collected;
                }
                if (!span.live_count)
                    it = spans.erase(it);
                else
                    ++it;
            }
        };
        sweep_spans(retired_spans);
        for (const auto &thread : threads) {
            thread->current_span = nullptr;
            sweep_spans(thread->spans);
            thread->pending_bytes = 0;
            thread->allocations_since_gc = 0;
        }
        // Budget all allocations, including leaf objects and external buffers.
        budget = std::max(config.minimum_gc_budget, live_bytes);
        allocated_since_gc = 0;
        retired_allocations_since_gc = 0;
        reclaimed_objects = saturated_add(reclaimed_objects, collected);
        ++collections;
        last_pause_ns =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - start)
                                           .count());
        return collected;
    }

    HeapConfig config;
    std::atomic<std::size_t> native_stack_size{0};
    const std::array<std::uint64_t, 2> hash_key = [] {
        std::random_device random;
        std::uniform_int_distribution<std::uint64_t> distribution;
        return std::array{distribution(random), distribution(random)};
    }();
    std::mutex coordinator;
    std::condition_variable changed;
    GcPhase phase = GcPhase::Running;
    bool collection_request = false;
    std::atomic<std::uint64_t> request_epoch{0};
    std::uint64_t completed_epoch = 0;
    bool bootstrapping = false;
    std::atomic<bool> accepting{true};
    std::list<AllocationSpan> retired_spans;
    std::vector<std::shared_ptr<ThreadRecord>> threads;
    std::uint64_t next_arena_id = 1;
    TransferRecord *transfer_head = nullptr;
    std::size_t transfer_count = 0;
    Allocation *worklist = nullptr;
    Allocation *finalizer_head = nullptr, *finalizer_tail = nullptr;
    WeakRefObject *weak_callback_head = nullptr, *weak_callback_tail = nullptr;
    std::atomic<std::size_t> allocated_since_gc{0};
    std::size_t budget;
    std::size_t collections = 0;
    std::size_t reclaimed_objects = 0, retired_allocations_since_gc = 0;
    std::size_t reclaimed_bytes = 0;
    std::uint64_t last_pause_ns = 0;
    std::uint64_t last_rendezvous_ns = 0, max_rendezvous_ns = 0;
    std::atomic<bool> gc_enabled{true};
    std::atomic<std::size_t> allocations_remaining{std::numeric_limits<std::size_t>::max()};
    ObjectHeader *object_type = nullptr;
    ObjectHeader *type_type = nullptr;
    std::array<ObjectHeader *, detail::core_count> core{};
    ObjectHeader *exit_callbacks = nullptr;
    ObjectHeader *gc_callbacks = nullptr;
    ObjectHeader *signal_state = nullptr;
    ObjectHeader *audit_hooks = nullptr;
    ObjectHeader *sys_dictionary = nullptr;
    bool core_published = false;
    std::atomic<std::uint64_t> type_epoch{0}, exception_epoch{0};
    CallExecutor call_executor = nullptr;
    ImportState imports;
};
} // namespace detail

Interpreter::Interpreter(HeapConfig config) : state_(std::make_shared<detail::State>(config)) {}
detail::ImportState &Attachment::imports()
{
    require_attached();
    return state_->imports;
}
Interpreter::~Interpreter()
{
    close();
}
void Interpreter::close() noexcept
{
    std::lock_guard gate(state_->coordinator);
    state_->accepting.store(false, std::memory_order_release);
    state_->changed.notify_all();
}
GcSnapshot Interpreter::gc_state() const
{
    std::lock_guard gate(state_->coordinator);
    GcSnapshot result{state_->phase,
                      state_->request_epoch.load(std::memory_order_acquire),
                      state_->completed_epoch,
                      state_->threads.size(),
                      0,
                      0,
                      0,
                      state_->collections};
    result.transfer_roots = state_->transfer_count;
    for (const auto &thread : state_->threads)
        switch (thread->state.load(std::memory_order_acquire)) {
        case MutatorState::Active:
            ++result.active;
            break;
        case MutatorState::Parked:
            ++result.parked;
            break;
        case MutatorState::NativeWait:
            ++result.native_wait;
            break;
        default:
            break;
        }
    return result;
}

Attachment::Attachment(Interpreter &interpreter) : Attachment(interpreter.state_) {}
Attachment::Attachment(const Transfer &launch) : Attachment(launch.state_) {}
Attachment::Attachment(std::shared_ptr<detail::State> state) : state_(std::move(state))
{
    if (!state_)
        throw std::invalid_argument("attachment requires a live interpreter lease");
    std::unique_lock gate(state_->coordinator);
    state_->changed.wait(gate, [&] {
        return !state_->accepting.load(std::memory_order_acquire) ||
               (state_->phase == GcPhase::Running && !state_->bootstrapping);
    });
    if (!state_->accepting.load(std::memory_order_acquire))
        throw std::logic_error("interpreter is closing");
    if (!state_->next_arena_id)
        throw std::overflow_error("root arena identities exhausted");
    thread_record_ = std::make_shared<detail::ThreadRecord>(state_->next_arena_id++);
    state_->threads.push_back(thread_record_);
    thread_record_->observed_epoch = state_->completed_epoch;
    thread_record_->state.store(MutatorState::Active, std::memory_order_release);
}

Attachment::~Attachment()
{
    require_attached();
    if (thread_record_->roots.count != 0 || !repr_stack_.empty() || current_frame_ ||
        no_safepoint_depth_ || allocation_in_progress_)
        std::terminate();
    std::unique_lock gate(state_->coordinator);
    state_->flush_charge(*thread_record_);
    thread_record_->ack_epoch = state_->request_epoch.load(std::memory_order_relaxed);
    thread_record_->state.store(MutatorState::Exiting, std::memory_order_release);
    state_->changed.notify_all();
    state_->changed.wait(gate, [&] { return state_->phase == GcPhase::Running; });
    state_->retired_spans.splice(state_->retired_spans.end(), thread_record_->spans);
    state_->retired_allocations_since_gc = detail::saturated_add(
        state_->retired_allocations_since_gc, thread_record_->allocations_since_gc);
    // Lock waiters cannot regain heap access until this retirement gate is released.
    if (thread_record_->exit_sentinel)
        thread_record_->release_exit_sentinel(thread_record_->exit_sentinel);
    std::erase(state_->threads, thread_record_);
    state_->changed.notify_all();
}

void Attachment::require_attached() const
{
    if (thread_record_->owner != std::this_thread::get_id() ||
        thread_record_->state.load(std::memory_order_relaxed) != MutatorState::Active)
        throw std::logic_error("Python heap access requires an attached owning thread");
}
void Attachment::require_safepoint() const
{
    require_attached();
    if (no_safepoint_depth_ || allocation_in_progress_)
        throw std::logic_error(
            "operation may stop or allocate during a protected native operation");
}
CallExecutor Attachment::call_executor() const
{
    require_attached();
    if (!state_->core_published || !state_->call_executor)
        throw std::logic_error("no registered function executor");
    return state_->call_executor;
}
std::optional<Value> Attachment::current_frame() const
{
    require_attached();
    return current_frame_;
}
std::optional<Value> Attachment::handled_exception() const
{
    require_attached();
    return handled_exception_;
}

std::uint64_t Attachment::keyed_hash(std::span<const std::byte> data) const
{
    require_attached();
    return detail::siphash24(data, state_->hash_key);
}
bool Attachment::enter_repr(Handle object)
{
    require_safepoint();
    const auto value = object.get(*this);
    if (std::find(repr_stack_.begin(), repr_stack_.end(), value) != repr_stack_.end())
        return false;
    repr_stack_.push_back(value);
    return true;
}
void Attachment::leave_repr(Value object) noexcept
{
    if (repr_stack_.empty() || repr_stack_.back() != object)
        std::terminate();
    repr_stack_.pop_back();
}

void Attachment::require_owns(Value value) const
{
    require_attached();
    const auto &allocation = detail::Allocation::from(value.object());
    if (allocation.span->owner != state_.get())
        throw std::logic_error("value does not belong to this interpreter");
    if (!allocation.live)
        throw std::logic_error("value is not a published allocation");
}

void Attachment::require_layout(Value value, const void *family) const
{
    if (allocation_layout(value).native_family != family)
        throw std::logic_error("object has incompatible native layout");
}
bool Attachment::has_native_layout(Value value, const void *family) const
{
    return allocation_layout(value).native_family == family;
}
const LayoutDescriptor &Attachment::allocation_layout(Value value) const
{
    require_owns(value);
    return detail::Allocation::from(value.object()).span->layout;
}
AtomicGcEdge *Attachment::instance_dictionary(Value object)
{
    const auto &layout = allocation_layout(object);
    return layout.fields.dictionary ? layout.field_cells(object.object()) + layout.fields.slots
                                    : nullptr;
}
AtomicGcEdge &Attachment::instance_slot(Value object, std::size_t index)
{
    const auto &layout = allocation_layout(object);
    if (index >= layout.fields.slots)
        throw std::logic_error("slot is outside immutable instance layout");
    return layout.field_cells(object.object())[index];
}

detail::RootCell &Attachment::cell(Handle handle) const
{
    require_attached();
    if (handle.owner_ != state_.get() || handle.arena_id_ != thread_record_->roots.identity ||
        !handle.cell_ || !handle.cell_->scope || handle.cell_->generation != handle.generation_)
        throw std::logic_error("foreign or expired root handle");
    return *handle.cell_;
}

Handle Attachment::acquire(const void *scope)
{
    require_safepoint();
    return acquire_cell(scope);
}
Handle Attachment::acquire_cell(const void *scope)
{
    auto &arena = thread_record_->roots;
    auto *cell = arena.free;
    if (cell)
        arena.free = cell->next;
    else {
        arena.cells.emplace_back();
        cell = &arena.cells.back();
    }
    ++cell->generation;
    cell->revision = 0;
    cell->object = nullptr;
    cell->scope = scope;
    cell->next = nullptr;
    cell->tag = ResultTag::Empty;
    ++arena.count;
    Handle handle;
    handle.owner_ = state_.get();
    handle.arena_id_ = arena.identity;
    handle.cell_ = cell;
    handle.generation_ = cell->generation;
    return handle;
}

void Attachment::release(detail::RootCell &cell) noexcept
{
    auto &arena = thread_record_->roots;
    assert(cell.scope);
    cell.object = nullptr;
    cell.scope = nullptr;
    cell.tag = ResultTag::Empty;
    cell.next = arena.free;
    arena.free = &cell;
    --arena.count;
}

Value Handle::get(const Attachment &context) const
{
    auto &cell = context.cell(*this);
    if (cell.tag == ResultTag::Empty)
        throw std::logic_error("root has no value");
    return Value(*cell.object);
}

RootScope::RootScope(Attachment &context) : context_(context)
{
    context_.require_safepoint();
}
RootScope::~RootScope()
{
    context_.require_safepoint();
    while (head_) {
        auto *cell = head_;
        head_ = cell->next;
        context_.release(*cell);
    }
}

Handle RootScope::root(Value value)
{
    context_.require_owns(value);
    auto handle = context_.acquire(this);
    auto &cell = *handle.cell_;
    cell.object = value.object();
    cell.tag = ResultTag::Ok;
    cell.next = head_;
    head_ = &cell;
    return handle;
}

void RootScope::release(Handle handle)
{
    context_.require_safepoint();
    auto &cell = context_.cell(handle);
    if (cell.scope != this)
        throw std::logic_error("root belongs to another scope");
    auto **link = &head_;
    while (*link != &cell)
        link = &(*link)->next;
    *link = cell.next;
    context_.release(cell);
}

ReturnSlot::ReturnSlot(Attachment &context) : context_(context), root_(context.acquire(this)) {}
ReturnSlot::~ReturnSlot()
{
    context_.require_attached();
    context_.release(*root_.cell_);
}
void ReturnSlot::reset()
{
    write(ResultTag::Empty, nullptr);
}
void ReturnSlot::reset(const Attachment &caller)
{
    check_caller(caller);
    reset();
}
void ReturnSlot::check_caller(const Attachment &caller) const
{
    if (&caller != &context_)
        throw std::logic_error("operation requires a caller-owned return slot");
    caller.require_attached();
}
void ReturnSlot::ok(Value value)
{
    write(ResultTag::Ok, value.object());
}
void ReturnSlot::raised(Value exception)
{
    if (context_.core_ready())
        validate_exception(context_, exception);
    write(ResultTag::Raised, exception.object());
}
void ReturnSlot::write(ResultTag tag, ObjectHeader *object)
{
    if (object)
        context_.require_owns(Value(*object));
    else
        context_.require_attached();
    store_root_noexcept(tag, object);
}
void ReturnSlot::store_root_noexcept(ResultTag tag, ObjectHeader *object) noexcept
{
    auto &cell = *root_.cell_;
    cell.object = object;
    cell.tag = tag;
    ++cell.revision;
}
ResultTag ReturnSlot::tag() const
{
    context_.require_attached();
    return root_.cell_->tag;
}
Value ReturnSlot::value() const
{
    if (tag() != ResultTag::Ok)
        throw std::logic_error("result is not Ok");
    return Value(*root_.cell_->object);
}
Value ReturnSlot::exception() const
{
    if (tag() != ResultTag::Raised)
        throw std::logic_error("result is not Raised");
    return Value(*root_.cell_->object);
}
ResultView ReturnSlot::result() const
{
    context_.require_attached();
    return ResultView(context_, root_, root_.cell_->revision);
}

detail::RootCell &ResultView::cell() const
{
    auto &cell = context_->cell(root_);
    if (cell.revision != revision_)
        throw std::logic_error("result view was overwritten");
    return cell;
}
ResultTag ResultView::tag() const
{
    return cell().tag;
}
Value ResultView::value() const
{
    auto &root = cell();
    if (root.tag != ResultTag::Ok)
        throw std::logic_error("result is not Ok");
    return Value(*root.object);
}
Value ResultView::exception() const
{
    auto &root = cell();
    if (root.tag != ResultTag::Raised)
        throw std::logic_error("result is not Raised");
    return Value(*root.object);
}

void Attachment::prepare_allocation(ReturnSlot &out, std::size_t bytes)
{
    require_safepoint();
    out.reset(*this);
    poll();
    if (state_->allocations_remaining.load(std::memory_order_relaxed) == 0)
        throw std::bad_alloc();
    const auto charged = detail::saturated_add(
        state_->allocated_since_gc.load(std::memory_order_relaxed), thread_record_->pending_bytes);
    if (state_->gc_enabled && (state_->config.collect_every_allocation || bytes >= state_->budget ||
                               charged >= state_->budget - bytes))
        collect();
}

void *Attachment::reserve_storage(LayoutDescriptor layout)
{
    auto &thread = *thread_record_;
    if (!thread.current_span || thread.current_span->layout != layout ||
        !thread.current_span->available()) {
        thread.current_span = nullptr;
        std::size_t target_bytes = 4096;
        for (auto &span : thread.spans)
            if (span.layout == layout) {
                if (span.available()) {
                    thread.current_span = &span;
                    break;
                }
                target_bytes = 64 * 1024;
            }
        if (!thread.current_span) {
            thread.spans.emplace_back(state_.get(), layout, target_bytes);
            thread.current_span = &thread.spans.back();
        }
    }
    return thread.current_span->take().object();
}
void Attachment::abandon_storage(ObjectHeader *object) noexcept
{
    auto &allocation = detail::Allocation::from(object);
    allocation.span->discard(allocation);
}
void Attachment::adopt(ObjectHeader *object, ReturnSlot &out, std::size_t external)
{
    auto &allocation = detail::Allocation::from(object);
    const auto storage_bytes = allocation.span->layout.storage_size();
    if (external > std::numeric_limits<std::size_t>::max() - storage_bytes)
        throw std::bad_alloc();
    const auto bytes = storage_bytes + external;
    auto remaining = state_->allocations_remaining.load(std::memory_order_relaxed);
    while (remaining != std::numeric_limits<std::size_t>::max()) {
        if (!remaining)
            throw std::bad_alloc();
        if (state_->allocations_remaining.compare_exchange_weak(remaining, remaining - 1,
                                                                std::memory_order_relaxed))
            break;
    }
    {
        NoSafepoint commit(*this);
        allocation.external_bytes.store(external, std::memory_order_relaxed);
        allocation.live = true;
        ++allocation.span->live_count;
        out.store_root_noexcept(ResultTag::Ok, object);
    }
    state_->charge(*thread_record_, bytes);
    thread_record_->allocations_since_gc =
        detail::saturated_add(thread_record_->allocations_since_gc, std::size_t{1});
}

void Attachment::require_edge(Value value, const void *edge, std::size_t bytes) const
{
    const auto &layout = allocation_layout(value);
    auto offset =
        reinterpret_cast<std::uintptr_t>(edge) - reinterpret_cast<std::uintptr_t>(value.object());
    const auto size = layout.storage_size();
    if (offset > size || size - offset < bytes)
        throw std::logic_error("edge is outside owner storage");
}

void Attachment::store(Handle owner, GcEdge &edge, Handle value)
{
    require_edge(owner.get(*this), &edge, sizeof(edge));
    auto *object = value.get(*this).object();
    NoSafepoint write(*this);
    edge.object_ = object;
}
void Attachment::clear(Handle owner, GcEdge &edge)
{
    require_edge(owner.get(*this), &edge, sizeof(edge));
    NoSafepoint write(*this);
    edge.object_ = nullptr;
}

void Attachment::load(Value owner, const AtomicGcEdge &edge, ReturnSlot &out)
{
    out.check_caller(*this);
    require_edge(owner, &edge, sizeof(edge));
    NoSafepoint read(*this);
    auto *object = edge.object_.load(std::memory_order_acquire);
    out.store_root_noexcept(object ? ResultTag::Ok : ResultTag::Empty, object);
}
void Attachment::store(Value owner, AtomicGcEdge &edge, std::optional<Value> value)
{
    require_edge(owner, &edge, sizeof(edge));
    if (value)
        require_owns(*value);
    NoSafepoint write(*this);
    edge.object_.store(value ? value->object() : nullptr, std::memory_order_release);
}
bool Attachment::compare_exchange(Value owner, AtomicGcEdge &edge, std::optional<Value> expected,
                                  std::optional<Value> desired)
{
    require_edge(owner, &edge, sizeof(edge));
    if (expected)
        require_owns(*expected);
    if (desired)
        require_owns(*desired);
    auto *observed = expected ? expected->object() : nullptr;
    NoSafepoint commit(*this);
    return edge.object_.compare_exchange_strong(observed, desired ? desired->object() : nullptr,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire);
}
void Attachment::exchange(Value owner, AtomicGcEdge &edge, std::optional<Value> value,
                          ReturnSlot &previous)
{
    previous.check_caller(*this);
    require_edge(owner, &edge, sizeof(edge));
    if (value)
        require_owns(*value);
    NoSafepoint commit(*this);
    auto *object =
        edge.object_.exchange(value ? value->object() : nullptr, std::memory_order_acq_rel);
    previous.store_root_noexcept(object ? ResultTag::Ok : ResultTag::Empty, object);
}

Transfer Attachment::make_transfer(Handle value)
{
    require_safepoint();
    const auto payload = value.get(*this);
    ReturnSlot out(*this);
    allocate<detail::TransferRecord>(out, payload);
    auto &record = this->payload<detail::TransferRecord>(out.value());
    {
        std::lock_guard gate(state_->coordinator);
        NoSafepoint publish(*this);
        record.next = state_->transfer_head;
        if (record.next)
            record.next->previous = &record;
        state_->transfer_head = &record;
        ++state_->transfer_count;
    }
    return Transfer(state_, &record);
}
void Attachment::receive(Transfer &transfer, ReturnSlot &out)
{
    require_safepoint();
    out.check_caller(*this);
    if (transfer.state_.get() != state_.get() || !transfer.record_)
        throw std::logic_error("transfer is empty or belongs to another interpreter");
    {
        std::lock_guard gate(state_->coordinator);
        NoSafepoint publish(*this);
        auto &record = *transfer.record_;
        out.store_root_noexcept(ResultTag::Ok, record.payload.object());
        state_->unlink_transfer(record);
        transfer.record_ = nullptr;
    }
    transfer.state_.reset();
}
Transfer::~Transfer()
{
    reset();
}
Transfer::Transfer(Transfer &&other) noexcept
    : state_(std::move(other.state_)), record_(std::exchange(other.record_, nullptr))
{}
Transfer &Transfer::operator=(Transfer &&other) noexcept
{
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        record_ = std::exchange(other.record_, nullptr);
    }
    return *this;
}
void Transfer::reset() noexcept
{
    auto state = std::move(state_);
    if (auto *record = std::exchange(record_, nullptr)) {
        std::lock_guard gate(state->coordinator);
        state->unlink_transfer(*record);
    }
}

void Attachment::intern(Handle string, ReturnSlot &out)
{
    const auto value = string.get(*this);
    out.reset(*this);
    if (type_of(value) != builtin(CoreObject::StrType)) {
        raise_error(*this, CoreObject::TypeErrorType, U"intern() requires an exact str", out);
        return;
    }
    const auto &text = payload<StrObject>(value).value;
    {
        std::lock_guard gate(state_->coordinator);
        if (auto found = state_->interned_strings.find(text);
            found != state_->interned_strings.end()) {
            out.ok(Value(*found->second));
            return;
        }
    }
    decltype(state_->interned_strings) prepared;
    prepared.emplace(text, value.object());
    auto node = prepared.extract(prepared.begin());
    std::lock_guard gate(state_->coordinator);
    auto inserted = state_->interned_strings.insert(std::move(node));
    out.ok(Value(*inserted.position->second));
}
std::size_t Attachment::object_size(Handle owner) const
{
    const auto &allocation = detail::Allocation::from(owner.get(*this).object());
    return detail::saturated_add(allocation.span->layout.storage_size(),
                                 allocation.external_bytes.load(std::memory_order_relaxed));
}
std::size_t Attachment::gc_metadata_size(Handle owner) const
{
    return detail::Allocation::from(owner.get(*this).object()).span->object_offset;
}
void Attachment::account_external(Handle owner, std::size_t bytes)
{
    account_external(owner.get(*this), bytes);
}
void Attachment::account_external(Value owner, std::size_t bytes)
{
    require_attached();
    require_owns(owner);
    auto &allocation = detail::Allocation::from(owner.object());
    if (bytes > std::numeric_limits<std::size_t>::max() - allocation.span->layout.storage_size())
        throw std::bad_alloc();
    const auto previous = allocation.external_bytes.exchange(bytes, std::memory_order_relaxed);
    if (bytes > previous)
        state_->charge(*thread_record_, bytes - previous);
}

std::size_t Attachment::collect()
{
    require_safepoint();
    auto perform = [&] {
        auto collected = state_->rendezvous(*thread_record_);
        if (dispatch_finalizers())
            collected += state_->rendezvous(*thread_record_);
        return collected;
    };
    if (thread_record_->collecting_callbacks)
        return perform();
    ReturnSlot callbacks(*this);
    load_gc_callbacks(callbacks);
    if (callbacks.tag() == ResultTag::Empty)
        return perform();
    RootScope roots(*this);
    auto list = roots.root(callbacks.value());
    thread_record_->collecting_callbacks = true;
    try {
        dispatch_gc_callbacks(list, true, 0);
        auto collected = perform();
        dispatch_gc_callbacks(list, false, collected);
        thread_record_->collecting_callbacks = false;
        return collected;
    } catch (...) {
        thread_record_->collecting_callbacks = false;
        throw;
    }
}
void Attachment::dispatch_gc_callbacks(Handle callbacks, bool starting, std::size_t collected)
{
    if (!detail::list_size(*this, callbacks))
        return;
    try {
        RootScope roots(*this);
        ReturnSlot phase(*this), info(*this), name(*this), number(*this), result(*this);
        make_str(*this, starting ? U"start" : U"stop", phase);
        allocate_instance<DictObject>(roots.root(builtin(CoreObject::DictType)), info);
        const auto dictionary = roots.root(info.value());
        for (auto [key, value] : {std::pair{U"generation", std::size_t{2}},
                                  {U"collected", collected},
                                  {U"uncollectable", 0}}) {
            make_str(*this, key, name);
            make_int(*this, value, number);
            dict_store(*this, dictionary, roots.root(name.value()), roots.root(number.value()),
                       result);
            if (result.tag() == ResultTag::Raised) {
                detail::write_unraisable(*this, roots.root(result.exception()), callbacks);
                return;
            }
        }
        for (std::size_t i = 0;; ++i) {
            detail::list_item(*this, callbacks, i, result);
            if (result.tag() == ResultTag::Empty)
                break;
            RootScope invocation(*this);
            auto callback = invocation.root(result.value());
            const Value args[] = {phase.value(), dictionary.get(*this)};
            call(*this, callback, {args}, result);
            if (result.tag() == ResultTag::Raised)
                detail::write_unraisable(*this, invocation.root(result.exception()), callback);
        }
    } catch (const std::bad_alloc &) {
        RootScope roots(*this);
        detail::write_unraisable(*this, roots.root(builtin(CoreObject::MemoryError)), callbacks);
    }
}
bool Attachment::supports_weakrefs(Handle target) const
{
    const auto value = target.get(*this);
    auto *type = value.object()->type.object_.load(std::memory_order_acquire);
    // Semantic capability belongs to the frozen ready layout. In particular,
    // object/type seeds predate the completed bootstrap layout publication.
    return type && reinterpret_cast<const TypeObject *>(type)->instance_layout.fields.weakrefs;
}
void Attachment::weakref_lookup(Handle target, Handle type, ReturnSlot &out)
{
    out.reset(*this);
    const auto value = target.get(*this);
    const auto requested = type.get(*this);
    std::lock_guard gate(state_->coordinator);
    for (auto *ref = detail::Allocation::from(value.object()).weakrefs; ref; ref = ref->next)
        if (ref->basic && type_of(Value(ref->header)) == requested) {
            out.ok(Value(ref->header));
            return;
        }
}
void Attachment::register_weakref(Handle self, Handle target, bool cache, ReturnSlot &out)
{
    out.reset(*this);
    if (!supports_weakrefs(target))
        throw std::logic_error("weak reference registration requires a weak-capable target");
    auto &ref = payload<WeakRefObject>(self);
    auto &target_data = detail::Allocation::from(target.get(*this).object());
    const auto type = type_of(self);
    std::lock_guard gate(state_->coordinator);
    if (cache)
        for (auto *current = target_data.weakrefs; current; current = current->next)
            if (current->basic && type_of(Value(current->header)) == type) {
                // The unpublished candidate must not retain a weak edge after
                // losing the cache commit: it was never linked to the target.
                ref.target.object_.store(nullptr, std::memory_order_relaxed);
                out.ok(Value(current->header));
                return;
            }
    ref.basic = cache;
    auto *after = !cache && target_data.weakrefs && target_data.weakrefs->basic
                      ? target_data.weakrefs
                      : nullptr;
    ref.previous = after;
    ref.next = after ? after->next : target_data.weakrefs;
    if (ref.next)
        ref.next->previous = &ref;
    if (after)
        after->next = &ref;
    else
        target_data.weakrefs = &ref;
    out.ok(self.get(*this));
}
void Attachment::weakrefs(Handle target, RootScope &roots, std::vector<Value> &out)
{
    const auto value = target.get(*this);
    std::lock_guard gate(state_->coordinator);
    for (auto *ref = detail::Allocation::from(value.object()).weakrefs; ref; ref = ref->next)
        out.push_back(roots.root(Value(ref->header)).get(*this));
}
bool Attachment::dispatch_finalizers()
{
    auto &thread = *thread_record_;
    detail::Allocation *tail;
    WeakRefObject *weak_tail;
    {
        std::lock_guard gate(state_->coordinator);
        if (thread.dispatching || (!state_->finalizer_head && !state_->weak_callback_head))
            return false;
        thread.dispatching = true;
        thread.finalizers = std::exchange(state_->finalizer_head, nullptr);
        tail = std::exchange(state_->finalizer_tail, nullptr);
        thread.weak_callbacks = std::exchange(state_->weak_callback_head, nullptr);
        weak_tail = std::exchange(state_->weak_callback_tail, nullptr);
    }
    auto finish_weak = [&](WeakRefObject &item) {
        std::lock_guard gate(state_->coordinator);
        thread.weak_callbacks = item.pending_next;
        item.pending_next = nullptr;
    };
    auto finish_finalizer = [&](detail::Allocation &item) {
        std::lock_guard gate(state_->coordinator);
        item.finalization = detail::Allocation::Finalization::Finalized;
        thread.finalizers = item.finalizer_next;
        item.finalizer_next = nullptr;
    };
    try {
        while (auto *item = thread.weak_callbacks) {
            try {
                RootScope roots(*this);
                const auto self = roots.root(Value(item->header));
                ReturnSlot loaded(*this);
                load(self.get(*this), item->callback, loaded);
                const auto callback = roots.root(loaded.value());
                ReturnSlot result(*this);
                const Value args[] = {self.get(*this)};
                call(*this, callback, {args}, result);
                if (result.tag() == ResultTag::Raised)
                    detail::write_unraisable(*this, roots.root(result.exception()), callback);
            } catch (...) {
                finish_weak(*item);
                throw;
            }
            finish_weak(*item);
        }
        while (auto *item = thread.finalizers) {
            {
                std::lock_guard gate(state_->coordinator);
                item->finalization = detail::Allocation::Finalization::Running;
            }
            try {
                RootScope roots(*this);
                const auto self = roots.root(Value(*item->object()));
                ReturnSlot result(*this);
                try {
                    auto method =
                        core_ready() && item->object()->type.object_.load(std::memory_order_acquire)
                            ? detail::type_attribute(*this, type_of(self), U"__del__")
                            : std::optional<Value>{};
                    if (method)
                        detail::invoke_special(*this, self, roots.root(*method), {}, result);
                    else if (item->span->layout.finalize)
                        item->span->layout.finalize(*this, self, result);
                    if (result.tag() == ResultTag::Raised)
                        detail::write_unraisable(*this, roots.root(result.exception()),
                                                 method ? std::optional<Handle>{roots.root(*method)}
                                                        : std::optional<Handle>{self});
                } catch (const std::bad_alloc &) {
                    if (core_ready())
                        detail::write_unraisable(
                            *this, roots.root(builtin(CoreObject::MemoryError)), self);
                    else
                        throw;
                }
            } catch (...) {
                finish_finalizer(*item);
                throw;
            }
            finish_finalizer(*item);
        }
    } catch (...) {
        // A host fault propagates, but must not strand another candidate in
        // a departing ThreadRecord or cause the failed callback to run twice.
        std::lock_guard gate(state_->coordinator);
        if (thread.finalizers) {
            tail->finalizer_next = state_->finalizer_head;
            state_->finalizer_head = thread.finalizers;
            if (!state_->finalizer_tail)
                state_->finalizer_tail = tail;
        }
        if (thread.weak_callbacks) {
            weak_tail->pending_next = state_->weak_callback_head;
            state_->weak_callback_head = thread.weak_callbacks;
            if (!state_->weak_callback_tail)
                state_->weak_callback_tail = weak_tail;
        }
        thread.finalizers = nullptr;
        thread.weak_callbacks = nullptr;
        thread.dispatching = false;
        throw;
    }
    {
        std::lock_guard gate(state_->coordinator);
        thread.dispatching = false;
    }
    return true;
}
void Attachment::poll()
{
    require_safepoint();
    if (state_->request_epoch.load(std::memory_order_acquire) == thread_record_->observed_epoch)
        return;
    std::unique_lock gate(state_->coordinator);
    if (state_->phase != GcPhase::Running) {
        thread_record_->ack_epoch = state_->request_epoch.load(std::memory_order_relaxed);
        thread_record_->state.store(MutatorState::Parked, std::memory_order_release);
        state_->changed.notify_all();
    }
    state_->resume(*thread_record_, gate);
}
void Attachment::set_allocation_limit(std::size_t remaining)
{
    require_attached();
    state_->allocations_remaining.store(remaining, std::memory_order_relaxed);
}

namespace {
class BootstrapAdmission final {
  public:
    explicit BootstrapAdmission(detail::State &state) : state_(state)
    {
        std::lock_guard gate(state_.coordinator);
        if (state_.threads.size() != 1 || state_.bootstrapping)
            throw std::logic_error("core bootstrap requires a single attached initializer");
        state_.bootstrapping = true;
    }
    ~BootstrapAdmission()
    {
        std::lock_guard gate(state_.coordinator);
        state_.bootstrapping = false;
        state_.changed.notify_all();
    }

  private:
    detail::State &state_;
};
} // namespace
void Attachment::bootstrap()
{
    require_attached();
    if (state_->type_type)
        return;
    BootstrapAdmission admission(*state_);
    const bool previous_gc = state_->gc_enabled;
    state_->gc_enabled = false;
    try {
        ReturnSlot object(*this);
        ReturnSlot type(*this);
        detail::build_type_seeds(*this, object, type);
        state_->object_type = object.value().object();
        state_->type_type = type.value().object();
    } catch (...) {
        state_->gc_enabled = previous_gc;
        collect();
        throw;
    }
    state_->gc_enabled = previous_gc;
}

Value Attachment::object_type() const
{
    require_attached();
    if (!state_->object_type)
        throw std::logic_error("object/type seeds have not been initialized");
    return Value(*state_->object_type);
}
Value Attachment::type_type() const
{
    require_attached();
    if (!state_->type_type)
        throw std::logic_error("object/type seeds have not been initialized");
    return Value(*state_->type_type);
}
Value Attachment::type_of(Handle object) const
{
    return type_of(object.get(*this));
}
Value Attachment::type_of(Value object) const
{
    require_owns(object);
    auto *type = object.object()->type.object_.load(std::memory_order_acquire);
    if (!type)
        throw std::logic_error("synthetic storage object has no Python type");
    return Value(*type);
}
bool Attachment::core_ready() const
{
    require_attached();
    return state_->core_published;
}
Value Attachment::builtin(CoreObject object) const
{
    require_attached();
    auto index = static_cast<std::size_t>(object);
    if (index >= state_->core.size() || !state_->core[index])
        throw std::logic_error("core runtime image has not been initialized");
    return Value(*state_->core[index]);
}
void Attachment::load_exit_callbacks(ReturnSlot &out)
{
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (state_->exit_callbacks)
        out.ok(Value(*state_->exit_callbacks));
}
void Attachment::publish_exit_callbacks(Handle candidate, ReturnSlot &out)
{
    const auto value = candidate.get(*this);
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (!state_->exit_callbacks)
        state_->exit_callbacks = value.object();
    out.ok(Value(*state_->exit_callbacks));
}
void Attachment::load_gc_callbacks(ReturnSlot &out)
{
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (state_->gc_callbacks)
        out.ok(Value(*state_->gc_callbacks));
}
void Attachment::publish_gc_callbacks(Handle candidate, ReturnSlot &out)
{
    const auto value = candidate.get(*this);
    if (!has_layout<ListObject>(value))
        throw std::logic_error("GC callbacks require a list");
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (!state_->gc_callbacks)
        state_->gc_callbacks = value.object();
    out.ok(Value(*state_->gc_callbacks));
}
void Attachment::load_signal_state(ReturnSlot &out)
{
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (state_->signal_state)
        out.ok(Value(*state_->signal_state));
}
void Attachment::publish_signal_state(Handle candidate, ReturnSlot &out)
{
    const auto value = candidate.get(*this);
    std::lock_guard gate(state_->coordinator);
    if (!state_->signal_state)
        state_->signal_state = value.object();
    out.ok(Value(*state_->signal_state));
}
void Attachment::load_audit_hooks(ReturnSlot &out)
{
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (state_->audit_hooks)
        out.ok(Value(*state_->audit_hooks));
}
void Attachment::publish_audit_hooks(Handle candidate, ReturnSlot &out)
{
    const auto value = candidate.get(*this);
    std::lock_guard gate(state_->coordinator);
    if (!state_->audit_hooks)
        state_->audit_hooks = value.object();
    out.ok(Value(*state_->audit_hooks));
}
void Attachment::load_sys_dictionary(ReturnSlot &out)
{
    out.reset(*this);
    std::lock_guard gate(state_->coordinator);
    if (state_->sys_dictionary)
        out.ok(Value(*state_->sys_dictionary));
}
void Attachment::set_sys_dictionary(Handle dictionary)
{
    const auto value = dictionary.get(*this);
    std::lock_guard gate(state_->coordinator);
    state_->sys_dictionary = value.object();
}
void Attachment::set_exit_sentinel(Handle sentinel, void (*release)(ObjectHeader *) noexcept)
{
    require_attached();
    if (!release)
        throw std::invalid_argument("exit sentinel requires a native release action");
    thread_record_->exit_sentinel = sentinel.get(*this).object();
    thread_record_->release_exit_sentinel = release;
}
std::size_t Attachment::native_thread_stack_size(std::optional<std::size_t> replacement)
{
    require_attached();
    if (replacement)
        return state_->native_stack_size.exchange(*replacement, std::memory_order_relaxed);
    return state_->native_stack_size.load(std::memory_order_relaxed);
}
std::uint64_t Attachment::publication_epoch(PublicationDomain domain) const
{
    require_attached();
    return (domain == PublicationDomain::Type ? state_->type_epoch : state_->exception_epoch)
        .load(std::memory_order_acquire);
}
bool Attachment::begin_publication(PublicationDomain domain, std::uint64_t epoch)
{
    require_attached();
    assert(no_safepoint_depth_);
    return !(epoch & 1) &&
           (domain == PublicationDomain::Type ? state_->type_epoch : state_->exception_epoch)
               .compare_exchange_strong(epoch, epoch + 1, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
}
void Attachment::end_publication(PublicationDomain domain, std::uint64_t epoch)
{
    require_attached();
    assert(no_safepoint_depth_);
    (domain == PublicationDomain::Type ? state_->type_epoch : state_->exception_epoch)
        .store(epoch + 2, std::memory_order_release);
}
std::uint64_t Attachment::type_epoch() const
{
    return publication_epoch(PublicationDomain::Type);
}
std::optional<ResolvedSlot> Attachment::cached_protocol(Handle type, Protocol protocol,
                                                        RootScope &roots)
{
    const auto value = type.get(*this);
    const auto epoch = type_epoch();
    const auto entry = thread_record_->protocol_entry(value, protocol);
    if ((epoch & 1) || entry.type != value.object() || entry.protocol != protocol ||
        entry.epoch != epoch)
        return {};
    // Root insertion never polls. This Active owner cannot be swept while
    // promoting a borrowed entry; every real sweep clears the whole cache.
    return ResolvedSlot{protocol,
                        entry.state,
                        roots.root(value),
                        entry.owner ? roots.root(Value(*entry.owner)) : Handle{},
                        entry.descriptor ? roots.root(Value(*entry.descriptor)) : Handle{},
                        epoch};
}
void Attachment::cache_protocol(const ResolvedSlot &slot)
{
    const auto type = slot.type.get(*this);
    const auto owner = slot.state != SlotState::Missing ? slot.owner.get(*this).object() : nullptr;
    const auto descriptor =
        slot.state != SlotState::Missing ? slot.descriptor.get(*this).object() : nullptr;
    thread_record_->protocol_entry(type, slot.protocol) = {type.object(), owner,      descriptor,
                                                           slot.protocol, slot.state, slot.epoch};
}
void Attachment::invalidate_types()
{
    for (;;) {
        const auto epoch = type_epoch();
        {
            NoSafepoint commit(*this);
            if (begin_publication(PublicationDomain::Type, epoch)) {
                end_publication(PublicationDomain::Type, epoch);
                return;
            }
        }
        poll();
        std::this_thread::yield();
    }
}
bool Attachment::enter_call()
{
    require_safepoint();
    if (call_depth_ >= state_->config.max_call_depth)
        return false;
    ++call_depth_;
    return true;
}
std::size_t Attachment::call_depth_limit() const
{
    require_safepoint();
    return state_->config.max_call_depth;
}
void Attachment::leave_call() noexcept
{
    assert(call_depth_ != 0);
    --call_depth_;
}
bool Attachment::enter_native_call()
{
    require_safepoint();
    if (native_depth_ >= state_->config.max_native_depth)
        return false;
    ++native_depth_;
    return true;
}
void Attachment::leave_native_call() noexcept
{
    assert(native_depth_ != 0);
    --native_depth_;
}

void Attachment::bootstrap_core()
{
    bootstrap();
    if (core_ready())
        return;
    BootstrapAdmission admission(*state_);
    struct TypeSnapshot {
        Handle type;
        decltype(TypeObject::name) name;
        decltype(TypeObject::qualname) qualname;
        std::optional<std::u32string> module;
        std::optional<Value> dictionary;
        std::optional<Value> subclasses;
        TypeState state;
        bool immutable;
    };
    RootScope roots(*this);
    std::vector<TypeSnapshot> previous_types;
    auto save_types = [&](auto &spans) {
        for (auto &span : spans) {
            if (span.layout.native_family != &detail::native_layout_id<TypeObject>)
                continue;
            for (std::size_t i = 0; i < span.extent; ++i) {
                auto &allocation = span.at(i);
                if (!allocation.live ||
                    !allocation.object()->type.object_.load(std::memory_order_relaxed))
                    continue;
                auto value = Value(*allocation.object());
                auto &type = payload<TypeObject>(value);
                if (type.state != TypeState::Structural ||
                    !is_subtype(*this, type_of(value), type_type()))
                    continue;
                ReturnSlot registry(*this);
                load(value, type.subclasses, registry);
                auto subclasses =
                    registry.tag() == ResultTag::Ok
                        ? std::optional<Value>{roots.root(registry.value()).get(*this)}
                        : std::optional<Value>{};
                previous_types.push_back({roots.root(value), type.name, type.qualname,
                                          type.declared_module, type.dictionary, subclasses,
                                          type.state, type.immutable});
            }
        }
    };
    save_types(state_->retired_spans);
    for (const auto &thread : state_->threads)
        save_types(thread->spans);
    const bool previous_gc = state_->gc_enabled;
    state_->gc_enabled = false;
    try {
        std::vector<std::unique_ptr<ReturnSlot>> storage;
        std::vector<ReturnSlot *> outputs;
        for (std::size_t i = 0; i < detail::core_count; ++i) {
            storage.push_back(std::make_unique<ReturnSlot>(*this));
            outputs.push_back(storage.back().get());
        }
        detail::build_core_objects(*this, outputs);
        for (std::size_t i = 0; i < outputs.size(); ++i)
            state_->core[i] = outputs[i]->result().value().object();
        // Only image construction excludes new attachments. Ordinary
        // ObjectOps and bytecode execute concurrently after publication.
        for (const auto &saved : previous_types)
            detail::type_ready(*this, saved.type);
        payload<TypeObject>(object_type()).immutable = true;
        payload<TypeObject>(type_type()).immutable = true;
        RootScope publication_roots(*this);
        std::vector<Handle> ready_types;
        auto remember_types = [&](auto &spans) {
            for (auto &span : spans)
                if (span.layout.native_family == &detail::native_layout_id<TypeObject>)
                    for (std::size_t i = 0; i < span.extent; ++i)
                        if (auto &allocation = span.at(i);
                            allocation.live &&
                            payload<TypeObject>(Value(*allocation.object())).state ==
                                TypeState::Ready)
                            ready_types.push_back(
                                publication_roots.root(Value(*allocation.object())));
        };
        remember_types(state_->retired_spans);
        for (const auto &thread : state_->threads)
            remember_types(thread->spans);
        for (auto type : ready_types)
            detail::publish_type(*this, type.get(*this));
        state_->call_executor = detail::execute_function;
        state_->core_published = true;
        for (auto type : ready_types) {
            ReturnSlot registry(*this);
            allocate_instance<DictObject>(publication_roots.root(builtin(CoreObject::DictType)),
                                          registry);
            store(type.get(*this), payload<TypeObject>(type).subclasses, registry.value());
        }
        for (auto type : ready_types)
            for (auto base : payload<TypeObject>(type).bases)
                detail::register_subclass(*this, publication_roots.root(base), type);
    } catch (...) {
        for (auto &saved : previous_types) {
            auto &type = payload<TypeObject>(saved.type);
            store(saved.type.get(*this), type.metadata, {});
            store(saved.type.get(*this), type.subclasses, saved.subclasses);
            type.name = std::move(saved.name);
            type.qualname = std::move(saved.qualname);
            type.declared_module = std::move(saved.module);
            type.dictionary = saved.dictionary;
            type.state = saved.state;
            type.immutable = saved.immutable;
            account_external(saved.type, type.external_bytes());
        }
        state_->core.fill(nullptr);
        state_->core_published = false;
        state_->call_executor = nullptr;
        state_->gc_enabled = previous_gc;
        collect();
        throw;
    }
    state_->gc_enabled = previous_gc;
}
void Attachment::set_gc_enabled(bool enabled)
{
    require_attached();
    state_->gc_enabled = enabled;
}
bool Attachment::gc_enabled() const
{
    require_attached();
    return state_->gc_enabled.load(std::memory_order_acquire);
}
bool Attachment::is_finalized(Handle object) const
{
    const auto value = object.get(*this);
    std::lock_guard gate(state_->coordinator);
    const auto state = detail::Allocation::from(value.object()).finalization;
    return state == detail::Allocation::Finalization::Running ||
           state == detail::Allocation::Finalization::Finalized;
}
HeapStats Attachment::stats() const
{
    require_safepoint();
    HeapStats result{};
    state_->rendezvous(
        *thread_record_,
        [](detail::State &state, void *out) {
            *static_cast<HeapStats *>(out) = state.capture_stats();
        },
        &result);
    return result;
}
void Attachment::inspect_heap(HeapInspection kind, std::span<const Value> targets, RootScope &roots,
                              std::vector<Value> &out)
{
    require_safepoint();
    if (&roots.context_ != this)
        throw std::logic_error("heap inspection requires caller-owned roots");
    for (auto value : targets) {
        require_owns(value);
        roots.root(value);
    }
    auto inspect = [&](detail::State &state) {
        auto publish = [&](ObjectHeader *object) {
            auto handle = acquire_cell(&roots);
            auto &cell = *handle.cell_;
            cell.object = object;
            cell.tag = ResultTag::Ok;
            cell.next = roots.head_;
            roots.head_ = &cell;
            out.emplace_back(*object);
        };
        if (kind == HeapInspection::Referents) {
            for (auto target : targets)
                for (auto *object : state.referents(target.object()))
                    publish(object);
            return;
        }
        std::unordered_set<ObjectHeader *> sought;
        for (auto target : targets)
            sought.insert(target.object());
        if (kind == HeapInspection::Referrers && sought.empty())
            return;
        state.each_allocation([&](detail::Allocation &allocation) {
            auto *object = allocation.object();
            if (!state.inspectable(object))
                return;
            if (kind == HeapInspection::Objects) {
                publish(object);
                return;
            }
            for (auto *referent : state.referents(object))
                if (sought.contains(referent)) {
                    publish(object);
                    break;
                }
        });
    };
    state_->rendezvous(
        *thread_record_,
        [](detail::State &state, void *data) { (*static_cast<decltype(inspect) *>(data))(state); },
        &inspect);
}

NoSafepoint::NoSafepoint(Attachment &context) : context_(context)
{
    context_.require_attached();
    ++context_.no_safepoint_depth_;
}
NoSafepoint::~NoSafepoint()
{
    // Stop/detach paths reject a live scope; ownership cannot change legally.
    assert((context_.require_attached(), true));
    assert(context_.no_safepoint_depth_);
    --context_.no_safepoint_depth_;
}

void Attachment::enter_native_wait()
{
    require_safepoint();
    std::lock_guard gate(state_->coordinator);
    thread_record_->ack_epoch = state_->request_epoch.load(std::memory_order_relaxed);
    thread_record_->state.store(MutatorState::NativeWait, std::memory_order_release);
    state_->changed.notify_all();
}
void Attachment::leave_native_wait()
{
    if (thread_record_->owner != std::this_thread::get_id() ||
        thread_record_->state.load(std::memory_order_relaxed) != MutatorState::NativeWait)
        throw std::logic_error("native callback requires its waiting owning thread");
    std::unique_lock gate(state_->coordinator);
    state_->resume(*thread_record_, gate);
}
NativeWait::NativeWait(Attachment &context) : context_(context)
{
    context_.enter_native_wait();
}
NativeWait::~NativeWait()
{
    context_.leave_native_wait();
}
NativeCallback::NativeCallback(NativeWait &native) : context_(native.context_)
{
    context_.leave_native_wait();
}
NativeCallback::~NativeCallback()
{
    context_.enter_native_wait();
}
} // namespace cxxpython::core
