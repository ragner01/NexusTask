#pragma once

#include "task_executor.hpp"
#include "performance_monitor.hpp"
#include <chrono>
#include <atomic>
#include <mutex>
#include <string>

namespace task_engine {

/**
 * Health monitor for executor and system health
 */
class HealthMonitor {
public:
    struct HealthStatus {
        bool healthy{true};
        std::string status_message;
        std::chrono::steady_clock::time_point last_check;
        size_t active_tasks{0};
        size_t pending_tasks{0};
        double throughput{0.0};
        double avg_latency_us{0.0};
    };

    explicit HealthMonitor(TaskExecutor& executor)
        : executor_(executor)
        , last_check_(std::chrono::steady_clock::now())
    {}

    /**
     * Perform health check
     */
    HealthStatus check() {
        std::lock_guard<std::mutex> lock(mutex_);
        HealthStatus status;
        status.last_check = std::chrono::steady_clock::now();
        
        auto monitor = executor_.monitor();
        if (!monitor) {
            status.healthy = false;
            status.status_message = "Monitor not available";
            return status;
        }

        auto metrics = monitor->get_metrics();
        status.active_tasks = metrics.inflight_tasks;
        status.pending_tasks = executor_.pending_tasks();
        status.throughput = monitor->throughput_per_second();
        status.avg_latency_us = monitor->average_execution_time();

        // Health criteria
        if (status.active_tasks > max_inflight_tasks_) {
            status.healthy = false;
            status.status_message = "Too many inflight tasks: " + 
                                   std::to_string(status.active_tasks);
        } else if (status.pending_tasks > max_pending_tasks_) {
            status.healthy = false;
            status.status_message = "Too many pending tasks: " + 
                                   std::to_string(status.pending_tasks);
        } else if (status.throughput < min_throughput_) {
            status.healthy = false;
            status.status_message = "Throughput too low: " + 
                                   std::to_string(status.throughput);
        } else {
            status.healthy = true;
            status.status_message = "OK";
        }

        last_status_ = status;
        return status;
    }

    /**
     * Get last health status
     */
    HealthStatus get_last_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_status_;
    }

    /**
     * Configure health thresholds
     */
    void set_thresholds(size_t max_inflight = 1000,
                       size_t max_pending = 10000,
                       double min_throughput = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_inflight_tasks_ = max_inflight;
        max_pending_tasks_ = max_pending;
        min_throughput_ = min_throughput;
    }

    /**
     * Check if system is healthy
     */
    bool is_healthy() {
        return check().healthy;
    }

private:
    TaskExecutor& executor_;
    mutable std::mutex mutex_;
    HealthStatus last_status_;
    std::chrono::steady_clock::time_point last_check_;
    
    size_t max_inflight_tasks_{1000};
    size_t max_pending_tasks_{10000};
    double min_throughput_{1.0};
};

} // namespace task_engine

