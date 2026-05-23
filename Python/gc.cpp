#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace cxxpython
{
    std::size_t Runtime::collect_garbage()
    {
        std::scoped_lock collection_lock(gc_mutex_);
        std::unordered_set<Object *> marked;
        std::vector<Object *> worklist;

        auto enqueue = [&marked, &worklist](Object *object) {
            if (!object)
                return;
            if (marked.insert(object).second)
                worklist.push_back(object);
        };

        visit_roots(enqueue);
        visit_thread_states([&enqueue](ThreadState &thread) {
            thread.visit_frames(enqueue);
        });

        while (!worklist.empty())
        {
            Object *object = worklist.back();
            worklist.pop_back();
            object->visit_children(enqueue);
        }

        {
            std::scoped_lock lock(roots_mutex_);
            roots_.erase(std::remove_if(roots_.begin(),
                                        roots_.end(),
                                        [&marked](Object *root) {
                                            return !root || marked.find(root) == marked.end();
                                        }),
                         roots_.end());
        }

        std::scoped_lock lock(heap_mutex_);
        const std::size_t before = heap_.size();
        heap_.erase(std::remove_if(heap_.begin(),
                                   heap_.end(),
                                   [&marked](const std::unique_ptr<Object> &object) {
                                       return !object || marked.find(object.get()) == marked.end();
                                   }),
                    heap_.end());
        const std::size_t collected = before - heap_.size();
        allocations_since_gc_.store(0, std::memory_order_relaxed);
        gc_requested_.store(false, std::memory_order_relaxed);
        return collected;
    }

    std::size_t Runtime::collect_garbage_if_requested()
    {
        if (!gc_enabled_.load(std::memory_order_relaxed) ||
            !gc_requested_.load(std::memory_order_relaxed))
        {
            return 0;
        }
        return collect_garbage();
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
