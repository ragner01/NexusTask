#pragma once

#include "task_executor.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

namespace task_engine {

/**
 * Task group for managing collections of related tasks
 * Provides group-level operations and monitoring
 */
class TaskGroup {
public:
    explicit TaskGroup(TaskExecutor& executor, const std::string& name = "")
        : executor_(executor)
        , name_(name.empty() ? "Group_" + std::to_string(reinterpret_cast<uintptr_t>(this)) : name)
    {}

    /**
     * Add a task to the group
     */
    TaskId add(std::function<TaskResult()> func,
               const std::string& name = "",
               TaskOptions options = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto id = executor_.submit(std::move(func), name, std::move(options));
        task_ids_.push_back(id);
        return id;
    }

    /**
     * Add a task with dependencies
     */
    TaskId add_with_dependencies(std::function<TaskResult()> func,
                                 const std::vector<TaskId>& dependencies,
                                 const std::string& name = "",
                                 TaskOptions options = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto id = executor_.submit_with_dependencies(
            std::move(func), dependencies, name, std::move(options));
        task_ids_.push_back(id);
        return id;
    }

    /**
     * Wait for all tasks in the group to complete
     */
    void wait() {
        executor_.wait_for_completion();
    }

    /**
     * Cancel all tasks in the group
     */
    size_t cancel_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t cancelled = 0;
        for (auto id : task_ids_) {
            if (executor_.cancel(id)) {
                ++cancelled;
            }
        }
        return cancelled;
    }

    /**
     * Get group statistics
     */
    struct GroupStats {
        size_t total_tasks{0};
        size_t completed{0};
        size_t failed{0};
        size_t running{0};
        size_t pending{0};
        size_t cancelled{0};
    };

    GroupStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        GroupStats stats;
        stats.total_tasks = task_ids_.size();
        
        for (auto id : task_ids_) {
            auto status = executor_.get_task_status(id);
            switch (status) {
                case TaskStatus::Completed: ++stats.completed; break;
                case TaskStatus::Failed: ++stats.failed; break;
                case TaskStatus::Running: ++stats.running; break;
                case TaskStatus::Pending:
                case TaskStatus::Ready: ++stats.pending; break;
                case TaskStatus::Cancelled: ++stats.cancelled; break;
            }
        }
        
        return stats;
    }

    /**
     * Check if all tasks are completed
     */
    bool all_completed() const {
        auto stats = get_stats();
        return stats.completed + stats.failed + stats.cancelled == stats.total_tasks;
    }

    /**
     * Get group name
     */
    const std::string& name() const { return name_; }

    /**
     * Get all task IDs in the group
     */
    std::vector<TaskId> task_ids() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return task_ids_;
    }

    /**
     * Clear all tasks from the group (doesn't cancel them)
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        task_ids_.clear();
    }

private:
    TaskExecutor& executor_;
    std::string name_;
    mutable std::mutex mutex_;
    std::vector<TaskId> task_ids_;
};

} // namespace task_engine

