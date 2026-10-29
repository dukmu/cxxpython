#include "cxxpython/core/runtime.hpp"
#include "cxxpython/core/type.hpp"
#include "cxxpython/core/objects.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <deque>
#include <limits>
#include <random>
#include <unordered_map>

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
        deallocate_storage(object);
    }
    namespace detail
    {
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

        // Owner-only writes. Collection scans registered arenas only while
        // their owners are stopped; epoch admission replaces the serial gate next.
        struct RootArena
        {
            explicit RootArena(std::uint64_t identity) : identity(identity) {}
            const std::uint64_t identity;
            std::deque<RootCell> cells;
            RootCell *free = nullptr;
            std::size_t count = 0;
        };
        struct ThreadRecord
        {
            explicit ThreadRecord(std::uint64_t identity) : owner(std::this_thread::get_id()), roots(identity) {}
            const std::thread::id owner;
            RootArena roots;
        };

        struct Allocation
        {
            const LayoutDescriptor layout;
            ObjectHeader *object;
            Allocation *next_mark = nullptr;
            std::size_t external_bytes = 0;
            bool marked = false;
        };

        struct State
        {
            explicit State(HeapConfig config_in) : config(config_in), budget(config.minimum_gc_budget) {}
            ~State()
            {
                // Physical destruction never follows GC edges or invokes Python.
                for (auto &[object, allocation] : heap)
                    allocation.layout.destroy_storage(object);
            }

            void mark(ObjectHeader *object) noexcept
            {
                if (!object)
                    return;
                auto found = heap.find(object);
                assert(found != heap.end());
                auto &allocation = found->second;
                if (allocation.marked)
                    return;
                allocation.marked = true;
                allocation.next_mark = worklist;
                worklist = &allocation;
            }

            std::size_t collect()
            {
                const auto start = std::chrono::steady_clock::now();
                Tracer tracer(this, [](void *state, ObjectHeader *object) noexcept {
                    static_cast<State *>(state)->mark(object);
                });
                for (const auto &thread : threads)
                    for (const auto &cell : thread->roots.cells)
                        if (cell.scope && cell.tag != ResultTag::Empty)
                            mark(cell.object);
                mark(object_type);
                mark(type_type);
                for (auto *object : core)
                    mark(object);
                while (worklist)
                {
                    Allocation *allocation = worklist;
                    worklist = allocation->next_mark;
                    tracer.visit(allocation->object->type);
                    allocation->layout.trace_storage(*allocation->object, tracer);
                }
                std::size_t collected = 0;
                reclaimed_bytes = 0;
                for (auto it = heap.begin(); it != heap.end();)
                {
                    auto &allocation = it->second;
                    if (allocation.marked)
                    {
                        allocation.marked = false;
                        ++it;
                        continue;
                    }
                    const auto bytes = allocation.layout.storage_size() + allocation.external_bytes;
                    live_bytes -= bytes;
                    reclaimed_bytes += bytes;
                    allocation.layout.destroy_storage(it->first);
                    it = heap.erase(it);
                    ++collected;
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
            std::mutex execution;
            std::atomic<bool> accepting{true};
            std::unordered_map<ObjectHeader *, Allocation> heap;
            std::vector<std::shared_ptr<ThreadRecord>> threads;
            std::uint64_t next_arena_id = 1;
            Allocation *worklist = nullptr;
            std::size_t live_bytes = 0;
            std::size_t allocated_since_gc = 0;
            std::size_t budget;
            std::size_t collections = 0;
            std::size_t reclaimed_bytes = 0;
            std::uint64_t last_pause_ns = 0;
            bool gc_enabled = true;
            std::size_t allocations_remaining = std::numeric_limits<std::size_t>::max();
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
    void Interpreter::close() noexcept { state_->accepting.store(false, std::memory_order_release); }

    Attachment::Attachment(Interpreter &interpreter)
        : state_(interpreter.state_), permission_(state_->execution, std::defer_lock)
    {
        if (!state_->accepting.load(std::memory_order_acquire))
            throw std::logic_error("interpreter is closing");
        permission_.lock();
        if (!state_->accepting.load(std::memory_order_acquire))
            throw std::logic_error("interpreter is closing");
        if (!state_->next_arena_id) throw std::overflow_error("root arena identities exhausted");
        thread_record_ = std::make_shared<detail::ThreadRecord>(state_->next_arena_id++);
        state_->threads.push_back(thread_record_);
    }

    Attachment::~Attachment()
    {
        require_attached();
        if (thread_record_->roots.count != 0 || !repr_stack_.empty() || current_frame_)
            std::terminate();
        std::erase(state_->threads, thread_record_);
    }

    void Attachment::require_attached() const
    {
        if (thread_record_->owner != std::this_thread::get_id() || !permission_.owns_lock())
            throw std::logic_error("Python heap access requires an attached owning thread");
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
        if (!state_->heap.contains(value.object()))
            throw std::logic_error("value does not belong to this interpreter");
    }

    void Attachment::require_layout(Value value, const void *family) const
    {
        require_owns(value);
        if (state_->heap.at(value.object()).layout.native_family != family)
            throw std::logic_error("object has incompatible native layout");
    }
    bool Attachment::has_native_layout(Value value, const void *family) const
    {
        require_owns(value);
        return state_->heap.at(value.object()).layout.native_family == family;
    }
    std::optional<Value> *Attachment::instance_dictionary(Value object)
    {
        require_owns(object);
        const auto &layout = state_->heap.at(object.object()).layout;
        return layout.fields.dictionary ? layout.field_cells(object.object()) + layout.fields.slots : nullptr;
    }
    std::optional<Value> &Attachment::instance_slot(Value object, std::size_t index)
    {
        require_owns(object);
        const auto &layout = state_->heap.at(object.object()).layout;
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
        require_attached();
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

    RootScope::RootScope(Attachment &context) : context_(context) { context_.require_attached(); }
    RootScope::~RootScope()
    {
        context_.require_attached();
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
        require_attached();
        out.reset(*this);
        if (state_->allocations_remaining == 0)
            throw std::bad_alloc();
        if (state_->gc_enabled && (state_->config.collect_every_allocation ||
            bytes >= state_->budget || state_->allocated_since_gc >= state_->budget - bytes))
            collect();
    }

    void Attachment::adopt(ObjectHeader *object, LayoutDescriptor layout, ReturnSlot &out, std::size_t external)
    {
        const auto storage_bytes = layout.storage_size();
        if (external > std::numeric_limits<std::size_t>::max() - storage_bytes)
            throw std::bad_alloc();
        const auto bytes = storage_bytes + external;
        if (bytes > std::numeric_limits<std::size_t>::max() - state_->live_bytes)
            throw std::bad_alloc();
        state_->heap.emplace(object, detail::Allocation{layout, object, nullptr, external});
        state_->live_bytes += bytes;
        state_->allocated_since_gc += std::min(bytes,
            std::numeric_limits<std::size_t>::max() - state_->allocated_since_gc);
        if (state_->allocations_remaining != std::numeric_limits<std::size_t>::max())
            --state_->allocations_remaining;
        // prepare_allocation validated this caller-owned cell; heap insertion
        // established the domain. Publication does not query heap or allocate.
        out.store_root_noexcept(ResultTag::Ok, object);
    }

    void Attachment::require_edge(Handle owner, const GcEdge &edge) const
    {
        Value value = owner.get(*this);
        const auto &layout = state_->heap.at(value.object()).layout;
        auto offset = reinterpret_cast<std::uintptr_t>(&edge) -
                      reinterpret_cast<std::uintptr_t>(value.object());
        if (offset > layout.size || layout.size - offset < sizeof(GcEdge))
            throw std::logic_error("edge is outside owner storage");
    }

    void Attachment::store(Handle owner, GcEdge &edge, Handle value)
    {
        require_edge(owner, edge);
        edge.object_ = value.get(*this).object();
    }
    void Attachment::clear(Handle owner, GcEdge &edge)
    {
        require_edge(owner, edge);
        edge.object_ = nullptr;
    }

    void Attachment::account_external(Handle owner, std::size_t bytes)
    {
        account_external(owner.get(*this), bytes);
    }
    void Attachment::account_external(Value owner, std::size_t bytes)
    {
        require_owns(owner);
        auto &allocation = state_->heap.at(owner.object());
        auto without_previous = state_->live_bytes - allocation.external_bytes;
        if (bytes > std::numeric_limits<std::size_t>::max() - without_previous)
            throw std::bad_alloc();
        if (bytes > allocation.external_bytes)
        {
            auto increase = bytes - allocation.external_bytes;
            state_->allocated_since_gc += std::min(increase,
                std::numeric_limits<std::size_t>::max() - state_->allocated_since_gc);
        }
        allocation.external_bytes = bytes;
        state_->live_bytes = without_previous + bytes;
    }

    std::size_t Attachment::collect() { require_attached(); return state_->collect(); }
    void Attachment::set_allocation_limit(std::size_t remaining)
    { require_attached(); state_->allocations_remaining = remaining; }

    void Attachment::bootstrap()
    {
        require_attached();
        if (state_->type_type)
            return;
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
        require_attached();
        if (call_depth_ >= state_->config.max_call_depth)
            return false;
        ++call_depth_;
        return true;
    }
    void Attachment::leave_call() noexcept { assert(call_depth_ != 0); --call_depth_; }
    bool Attachment::enter_native_call()
    {
        require_attached();
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
        for (const auto &[object, allocation] : state_->heap)
        {
            if (allocation.layout.native_family != &detail::native_layout_id<TypeObject> || !object->type.object_) continue;
            auto value = Value(*object);
            auto &type = payload<TypeObject>(value);
            if (type.state != TypeState::Structural || !is_subtype(*this, type_of(value), type_type())) continue;
            previous_types.push_back({roots.root(value), type.name, type.qualname, type.bootstrap_module, type.dictionary, type.state});
        }
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
            // The image is available to native factories under the execution
            // lock, but not published until all preexisting B2 types are ready.
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
        require_attached();
        std::size_t roots = 0, cells = 0;
        for (const auto &thread : state_->threads) { roots += thread->roots.count; cells += thread->roots.cells.size(); }
        return {state_->heap.size(), state_->live_bytes,
                state_->heap.size() * sizeof(detail::Allocation) +
                    cells * sizeof(detail::RootCell) + state_->threads.size() * sizeof(detail::ThreadRecord),
                roots, cells, state_->collections,
                state_->reclaimed_bytes, state_->last_pause_ns, state_->threads.size()};
    }

    NativeWait::NativeWait(Attachment &context) : context_(context)
    {
        context_.require_attached();
        context_.permission_.unlock();
    }
    NativeWait::~NativeWait() { context_.permission_.lock(); }
}
