#pragma once

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>

namespace task_engine {

/**
 * Performance monitoring and metrics collection
 * Tracks submissions, queue wait, retry/cancel stats, and concurrency levels
 */
class PerformanceMonitor {
public:
    struct MetricsSnapshot {
        uint64_t tasks_submitted{0};
        uint64_t tasks_completed{0};
        uint64_t tasks_failed{0};
        uint64_t tasks_retried{0};
        uint64_t tasks_cancelled{0};
        uint64_t total_execution_time_us{0};
        uint64_t max_execution_time_us{0};
        uint64_t min_execution_time_us{UINT64_MAX};
        uint64_t total_queue_wait_time_us{0};
        uint64_t inflight_tasks{0};
        uint64_t peak_inflight{0};
    };

    PerformanceMonitor()
        : start_time_(std::chrono::steady_clock::now())
    {}

    void record_submission() {
        metrics_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);
    }

    void record_start() {
        auto inflight = metrics_.inflight_tasks.fetch_add(1, std::memory_order_relaxed) + 1;
        auto peak = metrics_.peak_inflight.load(std::memory_order_acquire);
        while (inflight > peak) {
            if (metrics_.peak_inflight.compare_exchange_weak(
                    peak, inflight,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                break;
            }
        }
    }

    void record_completion(int64_t execution_time_us) {
        if (execution_time_us > 0) {
            metrics_.total_execution_time_us.fetch_add(
                static_cast<uint64_t>(execution_time_us), std::memory_order_relaxed);
        }
        metrics_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
        metrics_.inflight_tasks.fetch_sub(1, std::memory_order_relaxed);
        update_extrema(static_cast<uint64_t>(execution_time_us));
    }

    void record_failure() {
        metrics_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
        metrics_.inflight_tasks.fetch_sub(1, std::memory_order_relaxed);
    }

    void record_retry() {
        metrics_.tasks_retried.fetch_add(1, std::memory_order_relaxed);
    }

    void record_cancellation() {
        metrics_.tasks_cancelled.fetch_add(1, std::memory_order_relaxed);
    }

    void record_queue_wait(uint64_t wait_time_us) {
        metrics_.queue_wait_time_us.fetch_add(wait_time_us, std::memory_order_relaxed);
    }

    MetricsSnapshot get_metrics() const {
        MetricsSnapshot snapshot;
        snapshot.tasks_submitted = metrics_.tasks_submitted.load(std::memory_order_acquire);
        snapshot.tasks_completed = metrics_.tasks_completed.load(std::memory_order_acquire);
        snapshot.tasks_failed = metrics_.tasks_failed.load(std::memory_order_acquire);
        snapshot.tasks_retried = metrics_.tasks_retried.load(std::memory_order_acquire);
        snapshot.tasks_cancelled = metrics_.tasks_cancelled.load(std::memory_order_acquire);
        snapshot.total_execution_time_us = metrics_.total_execution_time_us.load(std::memory_order_acquire);
        snapshot.max_execution_time_us = metrics_.max_execution_time_us.load(std::memory_order_acquire);
        snapshot.min_execution_time_us = metrics_.min_execution_time_us.load(std::memory_order_acquire);
        snapshot.total_queue_wait_time_us = metrics_.queue_wait_time_us.load(std::memory_order_acquire);
        snapshot.inflight_tasks = metrics_.inflight_tasks.load(std::memory_order_acquire);
        snapshot.peak_inflight = metrics_.peak_inflight.load(std::memory_order_acquire);
        return snapshot;
    }

    double average_execution_time() const {
        auto completed = metrics_.tasks_completed.load(std::memory_order_acquire);
        if (completed == 0) return 0.0;
        auto total = metrics_.total_execution_time_us.load(std::memory_order_acquire);
        return static_cast<double>(total) / completed;
    }

    double average_queue_wait_time() const {
        auto submitted = metrics_.tasks_submitted.load(std::memory_order_acquire);
        if (submitted == 0) return 0.0;
        auto total = metrics_.queue_wait_time_us.load(std::memory_order_acquire);
        return static_cast<double>(total) / submitted;
    }

    double throughput_per_second() const {
        auto completed = metrics_.tasks_completed.load(std::memory_order_acquire);
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time_).count();
        if (elapsed <= 0.0) return 0.0;
        return static_cast<double>(completed) / elapsed;
    }

    uint64_t inflight_tasks() const {
        return metrics_.inflight_tasks.load(std::memory_order_acquire);
    }

    uint64_t peak_inflight() const {
        return metrics_.peak_inflight.load(std::memory_order_acquire);
    }

    void reset() {
        metrics_ = Metrics{};
        start_time_ = std::chrono::steady_clock::now();
    }

private:
    struct Metrics {
        std::atomic<uint64_t> tasks_submitted{0};
        std::atomic<uint64_t> tasks_completed{0};
        std::atomic<uint64_t> tasks_failed{0};
        std::atomic<uint64_t> tasks_retried{0};
        std::atomic<uint64_t> tasks_cancelled{0};
        std::atomic<uint64_t> total_execution_time_us{0};
        std::atomic<uint64_t> max_execution_time_us{0};
        std::atomic<uint64_t> min_execution_time_us{UINT64_MAX};
        std::atomic<uint64_t> queue_wait_time_us{0};
        std::atomic<uint64_t> inflight_tasks{0};
        std::atomic<uint64_t> peak_inflight{0};
    };

    void update_extrema(uint64_t exec_time_us) {
        auto current_max = metrics_.max_execution_time_us.load(std::memory_order_acquire);
        while (exec_time_us > current_max) {
            if (metrics_.max_execution_time_us.compare_exchange_weak(
                    current_max, exec_time_us,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                break;
            }
        }

        auto current_min = metrics_.min_execution_time_us.load(std::memory_order_acquire);
        while (exec_time_us < current_min) {
            if (metrics_.min_execution_time_us.compare_exchange_weak(
                    current_min, exec_time_us,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                break;
            }
        }
    }

    Metrics metrics_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace task_engine
