#pragma once

#include "task_executor.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace task_engine {

/**
 * Task batch for grouping and managing multiple related tasks
 */
class TaskBatch {
public:
    explicit TaskBatch(TaskExecutor& executor)
        : executor_(executor)
    {}

    /**
     * Add a task to the batch
     */
    TaskId add(std::function<TaskResult()> func,
               const std::string& name = "",
               TaskOptions options = {}) {
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
        auto id = executor_.submit_with_dependencies(
            std::move(func), dependencies, name, std::move(options));
        task_ids_.push_back(id);
        return id;
    }

    /**
     * Wait for all tasks in the batch to complete
     */
    void wait() {
        executor_.wait_for_completion();
    }

    /**
     * Get all task IDs in this batch
     */
    const std::vector<TaskId>& task_ids() const {
        return task_ids_;
    }

    /**
     * Get task status for all tasks
     */
    std::vector<TaskStatus> get_statuses() const {
        std::vector<TaskStatus> statuses;
        statuses.reserve(task_ids_.size());
        for (auto id : task_ids_) {
            statuses.push_back(executor_.get_task_status(id));
        }
        return statuses;
    }

    /**
     * Cancel all tasks in the batch
     */
    size_t cancel_all() {
        size_t cancelled = 0;
        for (auto id : task_ids_) {
            if (executor_.cancel(id)) {
                ++cancelled;
            }
        }
        return cancelled;
    }

private:
    TaskExecutor& executor_;
    std::vector<TaskId> task_ids_;
};

} // namespace task_engine

