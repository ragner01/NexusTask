#pragma once

#include "task_executor.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace task_engine {

/**
 * Task scheduler for delayed and periodic task execution
 */
class TaskScheduler {
public:
    struct ScheduledTask {
        std::function<TaskResult()> func;
        std::string name;
        TaskOptions options;
        std::chrono::steady_clock::time_point scheduled_time;
        std::chrono::milliseconds interval{0};  // 0 = one-time, >0 = periodic
        TaskId id{0};
    };

    explicit TaskScheduler(TaskExecutor& executor)
        : executor_(executor)
        , running_(false)
    {}

    ~TaskScheduler() {
        stop();
    }

    /**
     * Schedule a task to run at a specific time
     */
    TaskId schedule_at(std::function<TaskResult()> func,
                       const std::chrono::steady_clock::time_point& when,
                       const std::string& name = "",
                       TaskOptions options = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        ScheduledTask task;
        task.func = std::move(func);
        task.name = name.empty() ? "ScheduledTask_" + std::to_string(next_id_++) : name;
        task.options = std::move(options);
        task.scheduled_time = when;
        task.interval = std::chrono::milliseconds(0);  // One-time
        task.id = next_id_++;
        
        scheduled_tasks_.push(task);
        cv_.notify_one();
        
        return task.id;
    }

    /**
     * Schedule a task to run after a delay
     */
    TaskId schedule_after(std::function<TaskResult()> func,
                          const std::chrono::milliseconds& delay,
                          const std::string& name = "",
                          TaskOptions options = {}) {
        return schedule_at(std::move(func),
                          std::chrono::steady_clock::now() + delay,
                          name, std::move(options));
    }

    /**
     * Schedule a periodic task
     */
    TaskId schedule_periodic(std::function<TaskResult()> func,
                             const std::chrono::milliseconds& interval,
                             const std::string& name = "",
                             TaskOptions options = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        ScheduledTask task;
        task.func = std::move(func);
        task.name = name.empty() ? "PeriodicTask_" + std::to_string(next_id_++) : name;
        task.options = std::move(options);
        task.scheduled_time = std::chrono::steady_clock::now() + interval;
        task.interval = interval;
        task.id = next_id_++;
        
        scheduled_tasks_.push(task);
        cv_.notify_one();
        
        return task.id;
    }

    /**
     * Cancel a scheduled task
     */
    bool cancel(TaskId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Rebuild priority queue without the cancelled task
        std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, ScheduledTaskComparator> temp;
        bool found = false;
        
        while (!scheduled_tasks_.empty()) {
            auto task = scheduled_tasks_.top();
            scheduled_tasks_.pop();
            if (task.id == id) {
                found = true;
            } else {
                temp.push(task);
            }
        }
        scheduled_tasks_ = std::move(temp);
        
        return found;
    }

    /**
     * Start the scheduler
     */
    void start() {
        if (!running_.exchange(true)) {
            scheduler_thread_ = std::thread(&TaskScheduler::scheduler_loop, this);
        }
    }

    /**
     * Stop the scheduler
     */
    void stop() {
        if (running_.exchange(false)) {
            cv_.notify_all();
            if (scheduler_thread_.joinable()) {
                scheduler_thread_.join();
            }
        }
    }

private:
    void scheduler_loop() {
        while (running_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (scheduled_tasks_.empty()) {
                cv_.wait(lock, [this] {
                    return !running_.load(std::memory_order_acquire) || 
                           !scheduled_tasks_.empty();
                });
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            const auto& next_task = scheduled_tasks_.top();
            
            if (next_task.scheduled_time <= now) {
                ScheduledTask task = next_task;
                scheduled_tasks_.pop();
                lock.unlock();

                // Submit task
                executor_.submit(task.func, task.name, task.options);

                // If periodic, reschedule
                if (task.interval.count() > 0) {
                    lock.lock();
                    task.scheduled_time = now + task.interval;
                    scheduled_tasks_.push(task);
                    lock.unlock();
                }
            } else {
                // Wait until next task is due
                cv_.wait_until(lock, next_task.scheduled_time, [this] {
                    return !running_.load(std::memory_order_acquire);
                });
            }
        }
    }

    struct ScheduledTaskComparator {
        bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
            return a.scheduled_time > b.scheduled_time;  // Min-heap
        }
    };

    TaskExecutor& executor_;
    std::atomic<bool> running_;
    std::thread scheduler_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, ScheduledTaskComparator> scheduled_tasks_;
    std::atomic<TaskId> next_id_{1};
};

} // namespace task_engine

