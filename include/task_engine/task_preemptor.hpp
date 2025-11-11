#pragma once

#include "task.hpp"
#include "task_executor.hpp"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace task_engine {

/**
 * Task preemption manager for high-priority task scheduling
 */
class TaskPreemptor {
public:
    explicit TaskPreemptor(TaskExecutor& executor)
        : executor_(executor)
        , enabled_(true)
    {}
    
    /**
     * Enable/disable preemption
     */
    void set_enabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_release);
    }
    
    /**
     * Check if preemption is enabled
     */
    bool is_enabled() const {
        return enabled_.load(std::memory_order_acquire);
    }
    
    /**
     * Request preemption of lower-priority tasks for a high-priority task
     * Returns number of tasks preempted
     */
    size_t preempt_for(TaskId high_priority_task_id, TaskPriority min_priority) {
        if (!is_enabled()) {
            return 0;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        size_t preempted = 0;
        
        // Get all running tasks with lower priority
        // Note: This is a simplified implementation
        // In a real system, you'd need access to the thread pool's running tasks
        
        // For now, we'll track preemption requests
        preemption_requests_[high_priority_task_id] = min_priority;
        
        return preempted;
    }
    
    /**
     * Cancel preemption request
     */
    void cancel_preemption(TaskId task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        preemption_requests_.erase(task_id);
    }
    
    /**
     * Check if a task should be preempted
     */
    bool should_preempt(TaskId task_id, TaskPriority priority) const {
        if (!is_enabled()) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [high_priority_id, min_priority] : preemption_requests_) {
            if (priority < min_priority) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * Get number of active preemption requests
     */
    size_t active_preemptions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return preemption_requests_.size();
    }

private:
    TaskExecutor& executor_;
    std::atomic<bool> enabled_;
    mutable std::mutex mutex_;
    std::unordered_map<TaskId, TaskPriority> preemption_requests_;
};

} // namespace task_engine

