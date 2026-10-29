#include "cxxpython/core/runtime.hpp"
#include "cxxpython/core/type.hpp"
#include "cxxpython/core/objects.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <list>
#include <random>

namespace cxxpython::core
{
    void LayoutDescriptor::trace_storage(const ObjectHeader &object, Tracer &tracer) const noexcept
    {
        trace(object, tracer);
        const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
        if (!count) return;
        auto cells = field_cells(const_cast<ObjectHeader *>(&object));
        for (std::size_t i = 0; i < count; ++i)
            if (cells[i]) tracer.visit(*cells[i]);
    }
    void LayoutDescriptor::destroy_storage(ObjectHeader *object) const noexcept
    {
        const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
        if (count)
        {
            auto cells = field_cells(object);
            for (std::size_t i = 0; i < count; ++i) std::destroy_at(cells + i);
        }
        destroy(object);
    }
    namespace detail
    {
        struct TransferRecord
        {
            ObjectHeader header;
            const Value payload;
            TransferRecord *previous = nullptr, *next = nullptr; // Coordinator-owned root links, not heap edges.
            TransferRecord(ObjectHeader head, Value value) : header(head), payload(value) {}
            void trace(Tracer &tracer) const noexcept { tracer.visit(payload); }
        };
        std::uint64_t siphash24(std::span<const std::byte> data, std::array<std::uint64_t, 2> key) noexcept
        {
            // SipHash-2-4: two compression rounds per word, four final rounds.
            std::uint64_t a = 0x736f6d6570736575ULL ^ key[0], b = 0x646f72616e646f6dULL ^ key[1];
            std::uint64_t c = 0x6c7967656e657261ULL ^ key[0], d = 0x7465646279746573ULL ^ key[1];
            auto round = [&] {
                a += b; b = std::rotl(b, 13); b ^= a; a = std::rotl(a, 32);
                c += d; d = std::rotl(d, 16); d ^= c;
                a += d; d = std::rotl(d, 21); d ^= a;
                c += b; b = std::rotl(b, 17); b ^= c; c = std::rotl(c, 32);
            };
            std::size_t position = 0;
            while (data.size() - position >= 8)
            {
                std::uint64_t word = 0;
                for (unsigned i = 0; i < 8; ++i) word |= std::to_integer<std::uint64_t>(data[position + i]) << (i * 8);
                d ^= word; round(); round(); a ^= word; position += 8;
            }
            std::uint64_t tail = static_cast<std::uint64_t>(data.size()) << 56;
            for (std::size_t i = 0; i < data.size() - position; ++i)
                tail |= std::to_integer<std::uint64_t>(data[position + i]) << (i * 8);
            d ^= tail; round(); round(); a ^= tail; c ^= 0xff;
            round(); round(); round(); round();
            return a ^ b ^ c ^ d;
        }
        struct RootCell
        {
            ObjectHeader *object = nullptr;
            const void *scope = nullptr;
            RootCell *next_free = nullptr;
            std::uint64_t generation = 0;
            std::uint64_t revision = 0;
            ResultTag tag = ResultTag::Empty;
        };

        // Owner-only writes; the epoch gate publishes a frozen arena to GC.
        struct RootArena
        {
            explicit RootArena(std::uint64_t identity) : identity(identity) {}
            const std::uint64_t identity;
            std::deque<RootCell> cells;
            RootCell *free = nullptr;
            std::size_t count = 0;
        };
        struct AllocationSpan;
        // Adjacent metadata has a fixed offset from every object, including
        // over-aligned payloads. Span domain/layout never change after creation.
        struct Allocation
        {
            explicit Allocation(AllocationSpan *span) : span(span) {}
            Allocation *next = nullptr; // Free list or STW mark worklist.
            std::atomic<std::size_t> external_bytes{0};
            AllocationSpan *const span;
            bool live = false, marked = false;

            ObjectHeader *object() noexcept
            { return reinterpret_cast<ObjectHeader *>(reinterpret_cast<std::byte *>(this) + sizeof(Allocation)); }
            static Allocation &from(ObjectHeader *object) noexcept
            { return *reinterpret_cast<Allocation *>(reinterpret_cast<std::byte *>(object) - sizeof(Allocation)); }
        };
        struct AllocationSpan
        {
            AllocationSpan(State *owner, LayoutDescriptor layout, std::size_t target_bytes) : owner(owner), layout(layout)
            {
                const auto alignment = layout.storage_alignment();
                const auto limit = std::numeric_limits<std::size_t>::max();
                if (alignment - 1 > limit - sizeof(Allocation)) throw std::bad_alloc();
                object_offset = (sizeof(Allocation) + alignment - 1) & ~(alignment - 1);
                const auto bytes = layout.storage_size();
                if (bytes > limit - object_offset || alignment - 1 > limit - object_offset - bytes) throw std::bad_alloc();
                stride = (object_offset + bytes + alignment - 1) & ~(alignment - 1);
                capacity = std::max(std::size_t{1}, target_bytes / stride);
                reserved_bytes = capacity * stride;
                storage = static_cast<std::byte *>(layout.allocate_storage(reserved_bytes));
            }
            ~AllocationSpan()
            {
                for (std::size_t i = 0; i < extent; ++i)
                {
                    auto &allocation = at(i);
                    if (allocation.live) layout.destroy_storage(allocation.object());
                    std::destroy_at(&allocation);
                }
                layout.deallocate_storage(storage);
            }
            AllocationSpan(const AllocationSpan &) = delete;
            AllocationSpan &operator=(const AllocationSpan &) = delete;
            Allocation &at(std::size_t index) const noexcept
            { return *reinterpret_cast<Allocation *>(storage + index * stride + object_offset - sizeof(Allocation)); }
            bool available() const noexcept { return free || extent != capacity; }
            Allocation &take() noexcept
            {
                if (!free) return *std::construct_at(&at(extent++), this);
                auto *allocation = free; free = free->next; allocation->next = nullptr; return *allocation;
            }
            void discard(Allocation &allocation) noexcept
            {
                if (allocation.live) --live_count;
                allocation.live = false; allocation.marked = false; allocation.external_bytes.store(0, std::memory_order_relaxed);
                allocation.next = free; free = &allocation;
            }

            State *const owner;
            const LayoutDescriptor layout;
            std::byte *storage;
            std::size_t object_offset, stride, capacity, reserved_bytes;
            std::size_t extent = 0, live_count = 0;
            Allocation *free = nullptr;
        };
        struct ThreadRecord
        {
            explicit ThreadRecord(std::uint64_t identity) : owner(std::this_thread::get_id()), roots(identity) {}
            const std::thread::id owner;
            RootArena roots;
            std::atomic<MutatorState> state{MutatorState::PendingAttach};
            std::uint64_t observed_epoch = 0, ack_epoch = 0, wait_epoch = 0;
            std::size_t collection_result = 0;
            std::list<AllocationSpan> spans;
            AllocationSpan *current_span = nullptr;
            std::size_t pending_bytes = 0;
        };

        std::size_t saturated_add(std::size_t a, std::size_t b) noexcept
        { return a + std::min(b, std::numeric_limits<std::size_t>::max() - a); }

        struct State
        {
            explicit State(HeapConfig config_in) : config(config_in), budget(config.minimum_gc_budget) {}

            bool other_active(const ThreadRecord *self = nullptr) const
            {
                return std::any_of(threads.begin(), threads.end(), [&](const auto &thread) {
                    return thread.get() != self && thread->state.load(std::memory_order_acquire) == MutatorState::Active;
                });
            }
            void resume(ThreadRecord &thread, std::unique_lock<std::mutex> &gate)
            {
                changed.wait(gate, [&] { return phase == GcPhase::Running && (!core_quarantine || !other_active(&thread)); });
                thread.observed_epoch = completed_epoch;
                thread.state.store(MutatorState::Active, std::memory_order_release);
            }
            HeapStats capture_stats() const
            {
                std::size_t roots = 0, cells = 0;
                for (const auto &thread : threads) { roots += thread->roots.count; cells += thread->roots.cells.size(); }
                HeapStats result{0, 0, cells * sizeof(RootCell) + threads.size() * sizeof(ThreadRecord),
                    roots, cells, collections, reclaimed_bytes, last_pause_ns, threads.size(), last_rendezvous_ns, max_rendezvous_ns};
                result.transfer_roots = transfer_count;
                auto count = [&](const auto &spans) {
                    for (const auto &span : spans)
                    {
                        ++result.spans; result.objects += span.live_count;
                        result.reserved_bytes = saturated_add(result.reserved_bytes, span.reserved_bytes + sizeof(AllocationSpan));
                        result.metadata_bytes += sizeof(AllocationSpan) + span.capacity * sizeof(Allocation);
                        for (std::size_t i = 0; i < span.extent; ++i)
                            if (const auto &allocation = span.at(i); allocation.live)
                                result.live_bytes = saturated_add(result.live_bytes, saturated_add(span.layout.storage_size(), allocation.external_bytes.load(std::memory_order_relaxed)));
                    }
                };
                count(retired_spans); for (const auto &thread : threads) count(thread->spans); return result;
            }
            void charge(ThreadRecord &thread, std::size_t bytes) noexcept
            {
                thread.pending_bytes = saturated_add(thread.pending_bytes, bytes);
                if (thread.pending_bytes < std::min(std::size_t{4096}, budget)) return;
                flush_charge(thread);
            }
            void flush_charge(ThreadRecord &thread) noexcept
            {
                if (!thread.pending_bytes) return;
                auto charged = allocated_since_gc.load(std::memory_order_relaxed);
                while (!allocated_since_gc.compare_exchange_weak(charged, saturated_add(charged, thread.pending_bytes), std::memory_order_relaxed)) {}
                thread.pending_bytes = 0;
            }
            void unlink_transfer(TransferRecord &record) noexcept
            {
                if (record.previous) record.previous->next = record.next;
                else { assert(transfer_head == &record); transfer_head = record.next; }
                if (record.next) record.next->previous = record.previous;
                record.previous = record.next = nullptr; assert(transfer_count); --transfer_count;
            }
            std::size_t rendezvous(ThreadRecord &caller, HeapStats *snapshot = nullptr)
            {
                std::unique_lock gate(coordinator);
                for (;;)
                {
                    if (phase == GcPhase::Running) break;
                    const auto epoch = request_epoch.load(std::memory_order_relaxed);
                    const bool joined_collection = collection_request;
                    caller.wait_epoch = epoch; caller.ack_epoch = epoch;
                    caller.state.store(MutatorState::Parked, std::memory_order_release); changed.notify_all();
                    resume(caller, gate);
                    if (!snapshot && joined_collection) return caller.collection_result;
                }
                if (snapshot && !other_active(&caller)) { *snapshot = capture_stats(); return 0; }
                const auto epoch = request_epoch.load(std::memory_order_relaxed) + 1;
                if (!epoch) throw std::overflow_error("GC epochs exhausted");
                phase = GcPhase::Requesting; collection_request = snapshot == nullptr;
                caller.wait_epoch = epoch; caller.ack_epoch = epoch;
                caller.state.store(MutatorState::Collector, std::memory_order_release);
                const auto requested = std::chrono::steady_clock::now();
                request_epoch.store(epoch, std::memory_order_release); changed.notify_all();
                changed.wait(gate, [&] { return !other_active(&caller); });
                if (!snapshot)
                {
                    last_rendezvous_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - requested).count());
                    max_rendezvous_ns = std::max(max_rendezvous_ns, last_rendezvous_ns);
                }
                phase = GcPhase::Collecting;
                // Membership cannot change until Running; exiting owners keep
                // their record/State leases while waiting for this round.
                const auto collected = snapshot ? (*snapshot = capture_stats(), std::size_t{0}) : sweep();
                for (const auto &thread : threads)
                    if (thread->wait_epoch == epoch) thread->collection_result = collected;
                completed_epoch = epoch; caller.observed_epoch = epoch;
                caller.state.store(MutatorState::Active, std::memory_order_release);
                phase = GcPhase::Running; changed.notify_all(); return collected;
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

            std::size_t sweep()
            {
                const auto start = std::chrono::steady_clock::now();
                Tracer tracer(this, [](void *state, ObjectHeader *object) noexcept {
                    static_cast<State *>(state)->mark(object);
                });
                for (const auto &thread : threads)
                    for (const auto &cell : thread->roots.cells)
                        if (cell.scope && cell.tag != ResultTag::Empty)
                            mark(cell.object);
                for (auto *record = transfer_head; record; record = record->next) mark(&record->header);
                mark(object_type);
                mark(type_type);
                for (auto *object : core)
                    mark(object);
                while (worklist)
                {
                    Allocation *allocation = worklist;
                    worklist = allocation->next;
                    tracer.visit(allocation->object()->type);
                    allocation->span->layout.trace_storage(*allocation->object(), tracer);
                }
                std::size_t collected = 0;
                reclaimed_bytes = 0;
                std::size_t live_bytes = 0;
                auto sweep_spans = [&](auto &spans) {
                    for (auto it = spans.begin(); it != spans.end();)
                    {
                        auto &span = *it;
                        for (std::size_t i = 0; i < span.extent; ++i)
                        {
                            auto &allocation = span.at(i); if (!allocation.live) continue;
                            const auto bytes = saturated_add(span.layout.storage_size(), allocation.external_bytes.load(std::memory_order_relaxed));
                            if (allocation.marked)
                            {
                                allocation.marked = false; allocation.next = nullptr; live_bytes = saturated_add(live_bytes, bytes); continue;
                            }
                            reclaimed_bytes = saturated_add(reclaimed_bytes, bytes);
                            span.layout.destroy_storage(allocation.object()); span.discard(allocation); ++collected;
                        }
                        if (!span.live_count) it = spans.erase(it); else ++it;
                    }
                };
                sweep_spans(retired_spans);
                for (const auto &thread : threads)
                {
                    thread->current_span = nullptr; sweep_spans(thread->spans); thread->pending_bytes = 0;
                }
                // Budget all allocations, including leaf objects and external buffers.
                budget = std::max(config.minimum_gc_budget, live_bytes);
                allocated_since_gc = 0;
                ++collections;
                last_pause_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - start).count());
                return collected;
            }

            HeapConfig config;
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
            bool core_quarantine = false;
            std::atomic<bool> accepting{true};
            std::list<AllocationSpan> retired_spans;
            std::vector<std::shared_ptr<ThreadRecord>> threads;
            std::uint64_t next_arena_id = 1;
            TransferRecord *transfer_head = nullptr;
            std::size_t transfer_count = 0;
            Allocation *worklist = nullptr;
            std::atomic<std::size_t> allocated_since_gc{0};
            std::size_t budget;
            std::size_t collections = 0;
            std::size_t reclaimed_bytes = 0;
            std::uint64_t last_pause_ns = 0;
            std::uint64_t last_rendezvous_ns = 0, max_rendezvous_ns = 0;
            std::atomic<bool> gc_enabled{true};
            std::atomic<std::size_t> allocations_remaining{std::numeric_limits<std::size_t>::max()};
            ObjectHeader *object_type = nullptr;
            ObjectHeader *type_type = nullptr;
            std::array<ObjectHeader *, detail::core_count> core{};
            bool core_published = false;
            std::uint64_t type_epoch = 1;
            CallExecutor call_executor = nullptr;
        };
    }

    Interpreter::Interpreter(HeapConfig config) : state_(std::make_shared<detail::State>(config)) {}
    Interpreter::~Interpreter() { close(); }
    void Interpreter::close() noexcept
    {
        std::lock_guard gate(state_->coordinator);
        state_->accepting.store(false, std::memory_order_release); state_->changed.notify_all();
    }
    GcSnapshot Interpreter::gc_state() const
    {
        std::lock_guard gate(state_->coordinator);
        GcSnapshot result{state_->phase, state_->request_epoch.load(std::memory_order_acquire), state_->completed_epoch, state_->threads.size(), 0, 0, 0, state_->collections};
        result.transfer_roots = state_->transfer_count;
        for (const auto &thread : state_->threads)
            switch (thread->state.load(std::memory_order_acquire))
            {
                case MutatorState::Active: ++result.active; break;
                case MutatorState::Parked: ++result.parked; break;
                case MutatorState::NativeWait: ++result.native_wait; break;
                default: break;
            }
        return result;
    }

    Attachment::Attachment(Interpreter &interpreter)
        : state_(interpreter.state_)
    {
        std::unique_lock gate(state_->coordinator);
        state_->changed.wait(gate, [&] { return !state_->accepting.load(std::memory_order_acquire) || state_->phase == GcPhase::Running; });
        if (!state_->accepting.load(std::memory_order_acquire))
            throw std::logic_error("interpreter is closing");
        if (state_->core_quarantine && state_->other_active())
            throw std::logic_error("parallel ObjectOps admission is unavailable until N2 migration");
        if (!state_->next_arena_id) throw std::overflow_error("root arena identities exhausted");
        thread_record_ = std::make_shared<detail::ThreadRecord>(state_->next_arena_id++);
        state_->threads.push_back(thread_record_);
        thread_record_->observed_epoch = state_->completed_epoch;
        thread_record_->state.store(MutatorState::Active, std::memory_order_release);
    }

    Attachment::~Attachment()
    {
        require_attached();
        if (thread_record_->roots.count != 0 || !repr_stack_.empty() || current_frame_ || no_safepoint_depth_ || allocation_in_progress_)
            std::terminate();
        std::unique_lock gate(state_->coordinator);
        state_->flush_charge(*thread_record_);
        thread_record_->ack_epoch = state_->request_epoch.load(std::memory_order_relaxed);
        thread_record_->state.store(MutatorState::Exiting, std::memory_order_release); state_->changed.notify_all();
        state_->changed.wait(gate, [&] { return state_->phase == GcPhase::Running; });
        state_->retired_spans.splice(state_->retired_spans.end(), thread_record_->spans);
        std::erase(state_->threads, thread_record_);
        state_->changed.notify_all();
    }

    void Attachment::require_attached() const
    {
        if (thread_record_->owner != std::this_thread::get_id() || thread_record_->state.load(std::memory_order_relaxed) != MutatorState::Active)
            throw std::logic_error("Python heap access requires an attached owning thread");
    }
    void Attachment::require_safepoint() const
    {
        require_attached();
        if (no_safepoint_depth_ || allocation_in_progress_)
            throw std::logic_error("operation may stop or allocate during a protected native operation");
    }
    CallExecutor Attachment::call_executor() const
    {
        require_attached();
        if (!state_->core_published || !state_->call_executor) throw std::logic_error("no registered function executor");
        return state_->call_executor;
    }
    std::optional<Value> Attachment::current_frame() const
    { require_attached(); return current_frame_; }
    std::optional<Value> Attachment::handled_exception() const
    { require_attached(); return handled_exception_; }

    std::uint64_t Attachment::keyed_hash(std::span<const std::byte> data) const
    {
        require_attached();
        return detail::siphash24(data, state_->hash_key);
    }
    bool Attachment::enter_repr(Handle object)
    {
        require_safepoint();
        const auto value = object.get(*this);
        if (std::find(repr_stack_.begin(), repr_stack_.end(), value) != repr_stack_.end()) return false;
        repr_stack_.push_back(value); return true;
    }
    void Attachment::leave_repr(Value object) noexcept
    {
        if (repr_stack_.empty() || repr_stack_.back() != object) std::terminate();
        repr_stack_.pop_back();
    }

    void Attachment::require_owns(Value value) const
    {
        require_attached();
        const auto &allocation = detail::Allocation::from(value.object());
        if (allocation.span->owner != state_.get())
            throw std::logic_error("value does not belong to this interpreter");
        if (!allocation.live) throw std::logic_error("value is not a published allocation");
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
        require_owns(value); return detail::Allocation::from(value.object()).span->layout;
    }
    std::optional<Value> *Attachment::instance_dictionary(Value object)
    {
        const auto &layout = allocation_layout(object);
        return layout.fields.dictionary ? layout.field_cells(object.object()) + layout.fields.slots : nullptr;
    }
    std::optional<Value> &Attachment::instance_slot(Value object, std::size_t index)
    {
        const auto &layout = allocation_layout(object);
        if (index >= layout.fields.slots) throw std::logic_error("slot is outside immutable instance layout");
        return layout.field_cells(object.object())[index];
    }

    detail::RootCell &Attachment::cell(Handle handle) const
    {
        require_attached();
        if (handle.owner_ != state_.get() || handle.arena_id_ != thread_record_->roots.identity || !handle.cell_ ||
            !handle.cell_->scope || handle.cell_->generation != handle.generation_)
            throw std::logic_error("foreign or expired root handle");
        return *handle.cell_;
    }

    Handle Attachment::acquire(const void *scope)
    {
        require_safepoint();
        auto &arena = thread_record_->roots;
        auto *cell = arena.free;
        if (cell)
            arena.free = cell->next_free;
        else
        {
            arena.cells.emplace_back();
            cell = &arena.cells.back();
        }
        ++cell->generation;
        cell->revision = 0;
        cell->object = nullptr;
        cell->scope = scope;
        cell->tag = ResultTag::Empty;
        ++arena.count;
        Handle handle;
        handle.owner_ = state_.get();
        handle.arena_id_ = arena.identity;
        handle.cell_ = cell;
        handle.generation_ = cell->generation;
        return handle;
    }

    void Attachment::release(Handle handle) noexcept
    {
        // A scope may retain old tokens after explicit, non-LIFO release.
        auto &arena = thread_record_->roots;
        assert(handle.owner_ == state_.get() && handle.arena_id_ == arena.identity);
        auto &cell = *handle.cell_;
        if (!cell.scope || cell.generation != handle.generation_)
            return;
        cell.object = nullptr;
        cell.scope = nullptr;
        cell.tag = ResultTag::Empty;
        cell.next_free = arena.free;
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

    RootScope::RootScope(Attachment &context) : context_(context) { context_.require_safepoint(); }
    RootScope::~RootScope()
    {
        context_.require_safepoint();
        for (auto handle : handles_)
            context_.release(handle);
    }

    Handle RootScope::root(Value value)
    {
        context_.require_owns(value);
        auto handle = context_.acquire(this);
        try { handles_.push_back(handle); }
        catch (...) { context_.release(handle); throw; }
        auto &cell = context_.cell(handle);
        cell.object = value.object();
        cell.tag = ResultTag::Ok;
        return handle;
    }

    void RootScope::release(Handle handle)
    {
        context_.require_safepoint();
        if (context_.cell(handle).scope != this)
            throw std::logic_error("root belongs to another scope");
        auto found = std::find_if(handles_.begin(), handles_.end(), [handle](Handle entry) {
            return entry.cell_ == handle.cell_ && entry.generation_ == handle.generation_;
        });
        assert(found != handles_.end());
        *found = handles_.back();
        handles_.pop_back();
        context_.release(handle);
    }

    ReturnSlot::ReturnSlot(Attachment &context) : context_(context), root_(context.acquire(this)) {}
    ReturnSlot::~ReturnSlot()
    {
        context_.require_attached();
        context_.release(root_);
    }
    void ReturnSlot::reset() { write(ResultTag::Empty, nullptr); }
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
    void ReturnSlot::ok(Value value) { write(ResultTag::Ok, value.object()); }
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
        (void)context_.cell(root_);
        store_root_noexcept(tag, object);
    }
    void ReturnSlot::store_root_noexcept(ResultTag tag, ObjectHeader *object) noexcept
    {
        auto &cell = *root_.cell_;
        cell.object = object;
        cell.tag = tag;
        ++cell.revision;
    }
    ResultView ReturnSlot::result() const
    { return ResultView(context_, root_, context_.cell(root_).revision); }

    detail::RootCell &ResultView::cell() const
    {
        auto &cell = context_->cell(root_);
        if (cell.revision != revision_)
            throw std::logic_error("result view was overwritten");
        return cell;
    }
    ResultTag ResultView::tag() const { return cell().tag; }
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
        if (state_->allocations_remaining.load(std::memory_order_relaxed) == 0) throw std::bad_alloc();
        const auto charged = detail::saturated_add(state_->allocated_since_gc.load(std::memory_order_relaxed), thread_record_->pending_bytes);
        if (state_->gc_enabled && (state_->config.collect_every_allocation || bytes >= state_->budget || charged >= state_->budget - bytes)) collect();
    }

    void *Attachment::reserve_storage(LayoutDescriptor layout)
    {
        auto &thread = *thread_record_;
        if (!thread.current_span || thread.current_span->layout != layout || !thread.current_span->available())
        {
            thread.current_span = nullptr;
            std::size_t target_bytes = 4096;
            for (auto &span : thread.spans)
                if (span.layout == layout)
                {
                    if (span.available()) { thread.current_span = &span; break; }
                    target_bytes = 64 * 1024;
                }
            if (!thread.current_span)
            {
                thread.spans.emplace_back(state_.get(), layout, target_bytes); thread.current_span = &thread.spans.back();
            }
        }
        return thread.current_span->take().object();
    }
    void Attachment::abandon_storage(ObjectHeader *object) noexcept
    {
        auto &allocation = detail::Allocation::from(object); allocation.span->discard(allocation);
    }
    void Attachment::adopt(ObjectHeader *object, ReturnSlot &out, std::size_t external)
    {
        auto &allocation = detail::Allocation::from(object);
        const auto storage_bytes = allocation.span->layout.storage_size();
        if (external > std::numeric_limits<std::size_t>::max() - storage_bytes)
            throw std::bad_alloc();
        const auto bytes = storage_bytes + external;
        auto remaining = state_->allocations_remaining.load(std::memory_order_relaxed);
        while (remaining != std::numeric_limits<std::size_t>::max())
        {
            if (!remaining) throw std::bad_alloc();
            if (state_->allocations_remaining.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) break;
        }
        {
            NoSafepoint commit(*this);
            allocation.external_bytes.store(external, std::memory_order_relaxed); allocation.live = true;
            ++allocation.span->live_count; out.store_root_noexcept(ResultTag::Ok, object);
        }
        state_->charge(*thread_record_, bytes);
    }

    void Attachment::require_edge(Value value, const void *edge, std::size_t bytes) const
    {
        const auto &layout = allocation_layout(value);
        auto offset = reinterpret_cast<std::uintptr_t>(edge) -
                      reinterpret_cast<std::uintptr_t>(value.object());
        if (offset > layout.size || layout.size - offset < bytes)
            throw std::logic_error("edge is outside owner storage");
    }

    void Attachment::store(Handle owner, GcEdge &edge, Handle value)
    {
        require_edge(owner.get(*this), &edge, sizeof(edge));
        auto *object = value.get(*this).object(); NoSafepoint write(*this); edge.object_ = object;
    }
    void Attachment::clear(Handle owner, GcEdge &edge)
    {
        require_edge(owner.get(*this), &edge, sizeof(edge));
        NoSafepoint write(*this); edge.object_ = nullptr;
    }

    void Attachment::load(Value owner, const AtomicGcEdge &edge, ReturnSlot &out)
    {
        out.check_caller(*this); require_edge(owner, &edge, sizeof(edge));
        NoSafepoint read(*this);
        auto *object = edge.object_.load(std::memory_order_acquire);
        out.store_root_noexcept(object ? ResultTag::Ok : ResultTag::Empty, object);
    }
    void Attachment::store(Value owner, AtomicGcEdge &edge, std::optional<Value> value)
    {
        require_edge(owner, &edge, sizeof(edge)); if (value) require_owns(*value);
        NoSafepoint write(*this); edge.object_.store(value ? value->object() : nullptr, std::memory_order_release);
    }

    Transfer Attachment::make_transfer(Handle value)
    {
        require_safepoint(); const auto payload = value.get(*this); ReturnSlot out(*this);
        allocate<detail::TransferRecord>(out, payload);
        auto &record = this->payload<detail::TransferRecord>(out.result().value());
        {
            std::lock_guard gate(state_->coordinator); NoSafepoint publish(*this);
            record.next = state_->transfer_head;
            if (record.next) record.next->previous = &record;
            state_->transfer_head = &record; ++state_->transfer_count;
        }
        return Transfer(state_, &record);
    }
    void Attachment::receive(Transfer &transfer, ReturnSlot &out)
    {
        require_safepoint(); out.check_caller(*this);
        if (transfer.state_.get() != state_.get() || !transfer.record_)
            throw std::logic_error("transfer is empty or belongs to another interpreter");
        {
            std::lock_guard gate(state_->coordinator); NoSafepoint publish(*this);
            auto &record = *transfer.record_;
            out.store_root_noexcept(ResultTag::Ok, record.payload.object());
            state_->unlink_transfer(record); transfer.record_ = nullptr;
        }
        transfer.state_.reset();
    }
    Transfer::~Transfer() { reset(); }
    Transfer::Transfer(Transfer &&other) noexcept
        : state_(std::move(other.state_)), record_(std::exchange(other.record_, nullptr)) {}
    Transfer &Transfer::operator=(Transfer &&other) noexcept
    {
        if (this != &other) { reset(); state_ = std::move(other.state_); record_ = std::exchange(other.record_, nullptr); }
        return *this;
    }
    void Transfer::reset() noexcept
    {
        auto state = std::move(state_);
        if (auto *record = std::exchange(record_, nullptr))
        { std::lock_guard gate(state->coordinator); state->unlink_transfer(*record); }
    }

    void Attachment::account_external(Handle owner, std::size_t bytes)
    {
        account_external(owner.get(*this), bytes);
    }
    void Attachment::account_external(Value owner, std::size_t bytes)
    {
        require_safepoint();
        require_owns(owner); auto &allocation = detail::Allocation::from(owner.object());
        if (bytes > std::numeric_limits<std::size_t>::max() - allocation.span->layout.storage_size()) throw std::bad_alloc();
        const auto previous = allocation.external_bytes.exchange(bytes, std::memory_order_relaxed);
        if (bytes > previous) state_->charge(*thread_record_, bytes - previous);
    }

    std::size_t Attachment::collect() { require_safepoint(); return state_->rendezvous(*thread_record_); }
    void Attachment::poll()
    {
        require_safepoint();
        if (state_->request_epoch.load(std::memory_order_acquire) == thread_record_->observed_epoch) return;
        std::unique_lock gate(state_->coordinator);
        if (state_->phase != GcPhase::Running)
        {
            thread_record_->ack_epoch = state_->request_epoch.load(std::memory_order_relaxed);
            thread_record_->state.store(MutatorState::Parked, std::memory_order_release); state_->changed.notify_all();
        }
        state_->resume(*thread_record_, gate);
    }
    void Attachment::set_allocation_limit(std::size_t remaining)
    { require_attached(); state_->allocations_remaining.store(remaining, std::memory_order_relaxed); }

    void Attachment::bootstrap()
    {
        require_attached();
        if (state_->type_type)
            return;
        {
            std::lock_guard gate(state_->coordinator);
            if (state_->threads.size() != 1) throw std::logic_error("core bootstrap requires exclusive migration admission");
            state_->core_quarantine = true;
        }
        const bool previous_gc = state_->gc_enabled;
        state_->gc_enabled = false;
        try
        {
            ReturnSlot object(*this);
            ReturnSlot type(*this);
            detail::build_type_seeds(*this, object, type);
            state_->object_type = object.result().value().object();
            state_->type_type = type.result().value().object();
        }
        catch (...)
        {
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
        auto *type = object.object()->type.object_;
        if (!type)
            throw std::logic_error("synthetic storage object has no Python type");
        return Value(*type);
    }
    bool Attachment::core_ready() const
    { require_attached(); return state_->core_published; }
    Value Attachment::builtin(CoreObject object) const
    {
        require_attached();
        auto index = static_cast<std::size_t>(object);
        if (index >= state_->core.size() || !state_->core[index])
            throw std::logic_error("B3 core objects have not been initialized");
        return Value(*state_->core[index]);
    }
    std::uint64_t Attachment::type_epoch() const { require_attached(); return state_->type_epoch; }
    void Attachment::invalidate_types() { require_attached(); ++state_->type_epoch; }
    bool Attachment::enter_call()
    {
        require_safepoint();
        if (call_depth_ >= state_->config.max_call_depth)
            return false;
        ++call_depth_;
        return true;
    }
    void Attachment::leave_call() noexcept { assert(call_depth_ != 0); --call_depth_; }
    bool Attachment::enter_native_call()
    {
        require_safepoint();
        if (native_depth_ >= state_->config.max_native_depth) return false;
        ++native_depth_; return true;
    }
    void Attachment::leave_native_call() noexcept { assert(native_depth_ != 0); --native_depth_; }

    void Attachment::bootstrap_core()
    {
        bootstrap();
        if (core_ready())
            return;
        struct TypeSnapshot
        {
            Handle type;
            decltype(TypeObject::name) name;
            decltype(TypeObject::qualname) qualname;
            std::optional<std::u32string> module;
            std::optional<Value> dictionary;
            TypeState state;
        };
        RootScope roots(*this);
        std::vector<TypeSnapshot> previous_types;
        auto save_types = [&](auto &spans) {
            for (auto &span : spans)
            {
                if (span.layout.native_family != &detail::native_layout_id<TypeObject>) continue;
                for (std::size_t i = 0; i < span.extent; ++i)
                {
                    auto &allocation = span.at(i); if (!allocation.live || !allocation.object()->type.object_) continue;
                    auto value = Value(*allocation.object()); auto &type = payload<TypeObject>(value);
                    if (type.state != TypeState::Structural || !is_subtype(*this, type_of(value), type_type())) continue;
                    previous_types.push_back({roots.root(value), type.name, type.qualname, type.bootstrap_module, type.dictionary, type.state});
                }
            }
        };
        save_types(state_->retired_spans); for (const auto &thread : state_->threads) save_types(thread->spans);
        const bool previous_gc = state_->gc_enabled;
        state_->gc_enabled = false;
        try
        {
            std::vector<std::unique_ptr<ReturnSlot>> storage;
            std::vector<ReturnSlot *> outputs;
            for (std::size_t i = 0; i < detail::core_count; ++i)
            {
                storage.push_back(std::make_unique<ReturnSlot>(*this));
                outputs.push_back(storage.back().get());
            }
            detail::build_core_objects(*this, outputs);
            for (std::size_t i = 0; i < outputs.size(); ++i)
                state_->core[i] = outputs[i]->result().value().object();
            // Core quarantine excludes other Active owners; publish only after
            // all preexisting B2 types are ready.
            for (const auto &saved : previous_types) detail::type_ready(*this, saved.type);
            payload<TypeObject>(object_type()).immutable = true;
            payload<TypeObject>(type_type()).immutable = true;
            state_->call_executor = detail::execute_function;
            state_->core_published = true;
        }
        catch (...)
        {
            for (auto &saved : previous_types)
            {
                auto &type = payload<TypeObject>(saved.type);
                type.name = std::move(saved.name);
                type.qualname = std::move(saved.qualname);
                type.bootstrap_module = std::move(saved.module);
                type.dictionary = saved.dictionary;
                type.state = saved.state;
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
    void Attachment::set_gc_enabled(bool enabled) { require_attached(); state_->gc_enabled = enabled; }
    HeapStats Attachment::stats() const
    {
        require_safepoint(); HeapStats result{}; state_->rendezvous(*thread_record_, &result); return result;
    }

    NoSafepoint::NoSafepoint(Attachment &context) : context_(context)
    { context_.require_attached(); ++context_.no_safepoint_depth_; }
    NoSafepoint::~NoSafepoint()
    {
        // Stop/detach paths reject a live scope; ownership cannot change legally.
        assert((context_.require_attached(), true));
        assert(context_.no_safepoint_depth_); --context_.no_safepoint_depth_;
    }

    NativeWait::NativeWait(Attachment &context) : context_(context)
    {
        context_.require_safepoint();
        std::lock_guard gate(context_.state_->coordinator);
        context_.thread_record_->ack_epoch = context_.state_->request_epoch.load(std::memory_order_relaxed);
        context_.thread_record_->state.store(MutatorState::NativeWait, std::memory_order_release); context_.state_->changed.notify_all();
    }
    NativeWait::~NativeWait()
    { std::unique_lock gate(context_.state_->coordinator); context_.state_->resume(*context_.thread_record_, gate); }
}
