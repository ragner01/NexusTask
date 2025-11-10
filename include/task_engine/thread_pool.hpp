#pragma once

#include "task_queue.hpp"
#include "task.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace task_engine {

namespace detail {
constexpr size_t kPriorityLevels = 4;

inline constexpr size_t priority_index(TaskPriority priority) {
    return static_cast<size_t>(priority);
}

class PriorityQueueSet {
public:
    explicit PriorityQueueSet(size_t capacity_per_queue) {
        for (auto& queue : queues_) {
            queue = std::make_unique<LockFreeTaskQueue<Task>>(capacity_per_queue);
        }
    }

    bool try_enqueue(Task* task) {
        return try_enqueue(task, task->priority());
    }

    bool try_enqueue(Task* task, TaskPriority priority) {
        return queues_[priority_index(priority)]->try_enqueue(task);
    }

    template<typename Rep, typename Period>
    bool wait_enqueue_for(Task* task,
                          TaskPriority priority,
                          const std::chrono::duration<Rep, Period>& timeout) {
        return queues_[priority_index(priority)]->wait_enqueue_for(task, timeout);
    }

    Task* try_dequeue_highest() {
        for (size_t idx = queues_.size(); idx-- > 0;) {
            if (auto* task = queues_[idx]->try_dequeue()) {
                return task;
            }
        }
        return nullptr;
    }

    Task* steal_highest() {
        return try_dequeue_highest();
    }

    bool empty() const {
        for (const auto& queue : queues_) {
            if (!queue->empty()) {
                return false;
            }
        }
        return true;
    }

    size_t size() const {
        size_t total = 0;
        for (const auto& queue : queues_) {
            total += queue->size();
        }
        return total;
    }

private:
    std::array<std::unique_ptr<LockFreeTaskQueue<Task>>, kPriorityLevels> queues_;
};
} // namespace detail

/**
 * High-performance thread pool with work-stealing scheduler
 * Implements efficient task distribution across worker threads
 */
class ThreadPool {
public:
    struct LifecycleHooks {
        std::function<void(Task*)> on_start;
        std::function<void(Task*, const TaskResult&)> on_success;
        std::function<void(Task*, const std::exception_ptr&)> on_failure;
    };

    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads)
        , running_(true)
        , global_queue_(std::make_unique<detail::PriorityQueueSet>(2048))
    {
        local_queues_.reserve(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i) {
            local_queues_.push_back(std::make_unique<detail::PriorityQueueSet>(512));
        }
        workers_.reserve(num_threads_);
        // Create worker threads
        for (size_t i = 0; i < num_threads_; ++i) {
            workers_.emplace_back(&ThreadPool::worker_loop, this, i);
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * Submit a task for execution
     */
    void submit(Task* task, std::optional<TaskPriority> priority_override = std::nullopt) {
        if (!running_.load(std::memory_order_acquire)) {
            throw std::runtime_error("ThreadPool is shutdown");
        }

        auto priority = priority_override.value_or(task->priority());
        if (!try_submit_to_local(task, priority)) {
            while (!global_queue_->try_enqueue(task, priority)) {
                if (!global_queue_->wait_enqueue_for(task, priority, std::chrono::microseconds(50))) {
                    if (!running_.load(std::memory_order_acquire)) {
                        break;
                    }
                    std::this_thread::yield();
                } else {
                    break;
                }
            }
        }
        cv_.notify_one();
    }

    bool try_submit(Task* task, std::optional<TaskPriority> priority_override = std::nullopt) {
        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }
        auto priority = priority_override.value_or(task->priority());
        if (try_submit_to_local(task, priority)) {
            cv_.notify_one();
            return true;
        }
        if (global_queue_->try_enqueue(task, priority)) {
            cv_.notify_one();
            return true;
        }
        return false;
    }

    /**
     * Shutdown the thread pool gracefully
     */
    void shutdown() {
        running_.store(false, std::memory_order_release);
        cv_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /**
     * Get number of threads
     */
    size_t num_threads() const { return num_threads_; }

    void pause() {
        paused_.store(true, std::memory_order_release);
    }

    void resume() {
        paused_.store(false, std::memory_order_release);
        cv_.notify_all();
    }

    bool is_paused() const {
        return paused_.load(std::memory_order_acquire);
    }

    void set_lifecycle_hooks(LifecycleHooks hooks) {
        lifecycle_hooks_ = std::move(hooks);
    }

    size_t pending_tasks() const {
        size_t total = global_queue_->size();
        for (const auto& queue : local_queues_) {
            total += queue->size();
        }
        return total;
    }

private:
    bool try_submit_to_local(Task* task, TaskPriority priority) {
        auto target = next_worker_.fetch_add(1, std::memory_order_relaxed) % num_threads_;
        return local_queues_[target]->try_enqueue(task, priority);
    }

    void worker_loop(size_t thread_id) {
        while (running_.load(std::memory_order_acquire)) {
            if (paused_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return !paused_.load(std::memory_order_acquire) ||
                           !running_.load(std::memory_order_acquire);
                });
            }

            Task* task = nullptr;
            
            // Try local queue first (LIFO for cache locality)
            task = local_queues_[thread_id]->try_dequeue_highest();

            // Try global queue
            if (!task) {
                task = global_queue_->try_dequeue_highest();
            }

            // Work stealing: try other threads' queues
            if (!task) {
                for (size_t i = 0; i < num_threads_; ++i) {
                    if (i == thread_id) {
                        continue;
                    }
                    task = local_queues_[i]->steal_highest();
                    if (task) break;
                }
            }
            
            // Execute task if found
            if (task) {
                try {
                    if (lifecycle_hooks_.on_start) {
                        lifecycle_hooks_.on_start(task);
                    }
                    auto result = task->execute();
                    if (lifecycle_hooks_.on_success) {
                        lifecycle_hooks_.on_success(task, result);
                    } else {
                        task->invoke_callback(result);
                    }
                } catch (...) {
                    if (lifecycle_hooks_.on_failure) {
                        lifecycle_hooks_.on_failure(task, std::current_exception());
                    }
                }
            } else {
                // No work available, wait
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { 
                    return !running_.load(std::memory_order_acquire) || 
                           !global_queue_->empty(); 
                });
            }
        }
    }

    size_t num_threads_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_{false};
    std::atomic<size_t> next_worker_{0};
    
    std::unique_ptr<detail::PriorityQueueSet> global_queue_;
    std::vector<std::unique_ptr<detail::PriorityQueueSet>> local_queues_;
    LifecycleHooks lifecycle_hooks_;
    
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace task_engine
