#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace cxxpython
{
    std::size_t Runtime::collect_garbage()
    {
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
        return before - heap_.size();
    }
}
