#pragma once

#include "task.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace task_engine {

/**
 * Resource limits for task execution
 */
struct ResourceLimits {
    std::chrono::milliseconds max_execution_time{0};  // 0 = unlimited
    size_t max_memory_bytes{0};  // 0 = unlimited
    size_t max_cpu_percent{0};  // 0 = unlimited (0-100)
    
    bool has_time_limit() const { return max_execution_time.count() > 0; }
    bool has_memory_limit() const { return max_memory_bytes > 0; }
    bool has_cpu_limit() const { return max_cpu_percent > 0; }
};

/**
 * Resource usage tracker
 */
struct ResourceUsage {
    std::chrono::milliseconds execution_time{0};
    size_t memory_bytes{0};
    double cpu_percent{0.0};
    
    bool exceeds(const ResourceLimits& limits) const {
        if (limits.has_time_limit() && execution_time > limits.max_execution_time) {
            return true;
        }
        if (limits.has_memory_limit() && memory_bytes > limits.max_memory_bytes) {
            return true;
        }
        if (limits.has_cpu_limit() && cpu_percent > limits.max_cpu_percent) {
            return true;
        }
        return false;
    }
};

/**
 * Resource monitor for tracking task resource usage
 */
class ResourceMonitor {
public:
    ResourceMonitor() = default;
    
    /**
     * Start monitoring a task
     */
    void start_monitoring(TaskId task_id, const ResourceLimits& limits) {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskResourceInfo info;
        info.limits = limits;
        info.start_time = std::chrono::steady_clock::now();
        info.monitoring = true;
        resources_[task_id] = std::move(info);
    }
    
    /**
     * Stop monitoring a task
     */
    void stop_monitoring(TaskId task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_.erase(task_id);
    }
    
    /**
     * Update resource usage for a task
     */
    void update_usage(TaskId task_id, size_t memory_bytes, double cpu_percent) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(task_id);
        if (it != resources_.end()) {
            it->second.usage.memory_bytes = memory_bytes;
            it->second.usage.cpu_percent = cpu_percent;
        }
    }
    
    /**
     * Check if task exceeds limits
     */
    bool check_limits(TaskId task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(task_id);
        if (it == resources_.end()) {
            return false;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.start_time);
        it->second.usage.execution_time = elapsed;
        
        return it->second.usage.exceeds(it->second.limits);
    }
    
    /**
     * Get resource usage for a task
     */
    ResourceUsage get_usage(TaskId task_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(task_id);
        if (it != resources_.end()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.start_time);
            ResourceUsage usage = it->second.usage;
            usage.execution_time = elapsed;
            return usage;
        }
        return ResourceUsage{};
    }
    
    /**
     * Get all monitored tasks
     */
    std::vector<TaskId> get_monitored_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TaskId> result;
        result.reserve(resources_.size());
        for (const auto& [id, info] : resources_) {
            if (info.monitoring) {
                result.push_back(id);
            }
        }
        return result;
    }

private:
    struct TaskResourceInfo {
        ResourceLimits limits;
        ResourceUsage usage;
        std::chrono::steady_clock::time_point start_time;
        bool monitoring{false};
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<TaskId, TaskResourceInfo> resources_;
};

} // namespace task_engine

