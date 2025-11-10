#pragma once

#include "task.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace task_engine {

/**
 * Task dependency graph for managing complex task workflows
 * Supports topological sorting and dependency resolution
 */
class DependencyGraph {
public:
    DependencyGraph() = default;
    ~DependencyGraph() = default;

    DependencyGraph(const DependencyGraph&) = delete;
    DependencyGraph& operator=(const DependencyGraph&) = delete;

    /**
     * Add a task to the graph
     */
    void add_task(std::shared_ptr<Task> task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[task->id()] = task;
    }

    /**
     * Add a dependency: task depends on dependency
     */
    void add_dependency(TaskId task_id, TaskId dependency_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto task_it = tasks_.find(task_id);
        auto dep_it = tasks_.find(dependency_id);
        
        if (task_it != tasks_.end() && dep_it != tasks_.end()) {
            task_it->second->add_dependency(dep_it->second.get());
        }
    }

    /**
     * Get all ready tasks (tasks with no unsatisfied dependencies)
     */
    std::vector<std::shared_ptr<Task>> get_ready_tasks() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<Task>> ready;
        
        for (const auto& [id, task] : tasks_) {
            if (task->is_ready()) {
                ready.push_back(task);
            }
        }
        
        return ready;
    }

    /**
     * Notify that a task has completed
     */
    void notify_task_completed(TaskId task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = tasks_.find(task_id);
        if (it != tasks_.end()) {
            auto& task = it->second;
            
            // Notify all dependents
            for (auto* dependent : task->dependents_) {
                dependent->notify_dependency_completed();
            }
        }
    }

    /**
     * Check if all tasks are completed
     */
    bool all_completed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& [id, task] : tasks_) {
            auto status = task->status();
            if (status != TaskStatus::Completed && status != TaskStatus::Failed) {
                return false;
            }
        }
        return true;
    }

    /**
     * Get task by ID
     */
    std::shared_ptr<Task> get_task(TaskId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        return (it != tasks_.end()) ? it->second : nullptr;
    }

    /**
     * Remove task from the graph, detaching edges
     */
    bool remove_task(TaskId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            return false;
        }

        auto task = it->second.get();
        for (auto* dependency : task->dependencies_) {
            auto& dependents = dependency->dependents_;
            dependents.erase(
                std::remove(dependents.begin(), dependents.end(), task),
                dependents.end());
        }

        for (auto* dependent : task->dependents_) {
            auto& deps = dependent->dependencies_;
            deps.erase(std::remove(deps.begin(), deps.end(), task), deps.end());
            dependent->dependencies_count_.fetch_sub(1, std::memory_order_relaxed);
        }

        tasks_.erase(it);
        return true;
    }

    size_t task_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    bool contains(TaskId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.find(id) != tasks_.end();
    }

    std::vector<TaskId> dependents(TaskId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TaskId> result;
        if (auto it = tasks_.find(id); it != tasks_.end()) {
            for (auto* dependent : it->second->dependents_) {
                result.push_back(dependent->id());
            }
        }
        return result;
    }

    std::vector<TaskId> topological_order() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_map<TaskId, size_t> indegree;
        std::unordered_map<TaskId, std::vector<TaskId>> adjacency;

        for (const auto& [id, task] : tasks_) {
            indegree[id] = task->dependencies_.size();
            for (auto* dependent : task->dependents_) {
                adjacency[id].push_back(dependent->id());
            }
        }

        std::queue<TaskId> ready;
        for (const auto& [id, degree] : indegree) {
            if (degree == 0) {
                ready.push(id);
            }
        }

        std::vector<TaskId> order;
        while (!ready.empty()) {
            auto id = ready.front();
            ready.pop();
            order.push_back(id);

            for (auto dependent_id : adjacency[id]) {
                auto& degree = indegree[dependent_id];
                if (--degree == 0) {
                    ready.push(dependent_id);
                }
            }
        }
        return order;
    }

    bool has_cycles() const {
        auto order = topological_order();
        std::lock_guard<std::mutex> lock(mutex_);
        return order.size() != tasks_.size();
    }

    /**
     * Clear all tasks
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<TaskId, std::shared_ptr<Task>> tasks_;
};

} // namespace task_engine
