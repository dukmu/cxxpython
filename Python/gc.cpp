#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include <algorithm>
#include <vector>

namespace cxxpython
{
    std::size_t Runtime::collect_garbage()
    {
        if (VirtualMachine *vm = active_vm())
            return collect_garbage_at_safepoint(vm->thread_state(), true);
        return collect_garbage_unlocked(true);
    }

    std::size_t Runtime::collect_garbage_unlocked(bool collect_old)
    {
        std::scoped_lock collection_lock(gc_mutex_);
        std::vector<Object *> worklist;
        std::vector<Object *> marked_objects;

        auto enqueue = [&worklist, &marked_objects](Object *object) {
            if (!object || object->gc_marked())
                return;
            object->gc_mark();
            worklist.push_back(object);
            marked_objects.push_back(object);
        };

        visit_roots(enqueue);
        {
            std::scoped_lock lock(heap_mutex_);
            for (const auto &object : heap_)
            {
                if (object && !collect_old && object->gc_generation() == GCGeneration::Old)
                    enqueue(object.get());
            }
        }
        visit_thread_states([&enqueue](ThreadState &thread) {
            thread.visit_frames(enqueue);
            thread.visit_temporary_roots(enqueue);
        });

        while (!worklist.empty())
        {
            Object *object = worklist.back();
            worklist.pop_back();
            object->visit_children(enqueue);
        }

        for (Object *object : marked_objects)
            object->promote_gc_generation();

        {
            std::scoped_lock lock(roots_mutex_);
            roots_.erase(std::remove_if(roots_.begin(),
                                        roots_.end(),
                                        [](Object *root) {
                                            return !root || !root->gc_marked();
                                        }),
                         roots_.end());
        }

        std::scoped_lock lock(heap_mutex_);
        const std::size_t before = heap_.size();

        for (const auto &object : heap_)
        {
            if (!object || object->gc_marked())
                continue;
            if (object->ref_count() != 0)
                object->clear_references();
        }

        heap_.erase(std::remove_if(heap_.begin(),
                                    heap_.end(),
                                    [collect_old](const std::unique_ptr<Object> &object) {
                                        if (!object)
                                            return true;
                                        if (!collect_old && object->gc_generation() == GCGeneration::Old)
                                            return false;
                                        return !object->gc_marked();
                                    }),
                    heap_.end());
        const std::size_t collected = before - heap_.size();

        for (Object *object : marked_objects)
            object->gc_unmark();

        allocations_since_gc_.store(0, std::memory_order_relaxed);
        gc_requested_.store(false, std::memory_order_relaxed);
        return collected;
    }

    std::size_t Runtime::collect_garbage_if_requested(ThreadState &current_thread)
    {
        if (!garbage_collection_requested())
            return 0;
        return collect_garbage_at_safepoint(current_thread, false);
    }

    bool Runtime::all_other_active_threads_at_gc_safepoint(ThreadState &current_thread) const
    {
        std::scoped_lock lock(thread_states_mutex_);
        for (ThreadState *thread : thread_states_)
        {
            if (!thread || thread == &current_thread)
                continue;
            if (thread->frame_depth() == 0)
                continue;
            if (!thread->at_gc_safepoint())
                return false;
        }
        return true;
    }

    std::size_t Runtime::collect_garbage_at_safepoint(ThreadState &current_thread, bool force)
    {
        std::unique_lock lock(gc_coordination_mutex_);
        if (gc_stop_requested_)
        {
            ThreadGCStateGuard safepoint(current_thread, ThreadGCState::OpcodeSafepoint);
            gc_condition_.notify_all();
            gc_condition_.wait(lock, [this] { return !gc_stop_requested_; });
            return 0;
        }

        if (!force &&
            (!gc_enabled_.load(std::memory_order_relaxed) ||
             !gc_requested_.load(std::memory_order_relaxed)))
        {
            return 0;
        }

        gc_stop_requested_ = true;
        ThreadGCStateGuard safepoint(current_thread, ThreadGCState::OpcodeSafepoint);
        gc_condition_.notify_all();
        gc_condition_.wait(lock, [this, &current_thread] {
            return all_other_active_threads_at_gc_safepoint(current_thread);
        });
        lock.unlock();

        const std::size_t collected = collect_garbage_unlocked(force);

        lock.lock();
        gc_stop_requested_ = false;
        lock.unlock();
        gc_condition_.notify_all();
        return collected;
    }

    bool Runtime::garbage_collection_enabled() const noexcept
    {
        return gc_enabled_.load(std::memory_order_relaxed);
    }

    void Runtime::enable_garbage_collection() noexcept
    {
        gc_enabled_.store(true, std::memory_order_relaxed);
    }

    void Runtime::disable_garbage_collection() noexcept
    {
        gc_enabled_.store(false, std::memory_order_relaxed);
    }

    std::size_t Runtime::garbage_collection_count() const noexcept
    {
        return allocations_since_gc_.load(std::memory_order_relaxed);
    }

    std::size_t Runtime::garbage_collection_threshold() const noexcept
    {
        return gc_threshold_.load(std::memory_order_relaxed);
    }

    void Runtime::set_garbage_collection_threshold(std::size_t threshold) noexcept
    {
        gc_threshold_.store(threshold, std::memory_order_relaxed);
        if (threshold != 0 &&
            allocations_since_gc_.load(std::memory_order_relaxed) >= threshold)
        {
            gc_requested_.store(true, std::memory_order_relaxed);
        }
    }
}
