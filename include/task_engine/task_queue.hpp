#pragma once

#include <atomic>
#include <cstddef>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace task_engine {

/**
 * Lock-free circular buffer task queue using atomic operations
 * Implements MPMC (Multiple Producer Multiple Consumer) pattern
 */
template<typename Task>
class LockFreeTaskQueue {
public:
    explicit LockFreeTaskQueue(size_t capacity)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , buffer_(std::make_unique<std::atomic<Task*>[]>(capacity))
    {
        // Ensure capacity is power of 2 for efficient modulo
        if ((capacity & mask_) != 0) {
            throw std::invalid_argument("Capacity must be power of 2");
        }
        
        // Initialize all slots to nullptr
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~LockFreeTaskQueue() = default;

    LockFreeTaskQueue(const LockFreeTaskQueue&) = delete;
    LockFreeTaskQueue& operator=(const LockFreeTaskQueue&) = delete;

    size_t capacity() const noexcept { return capacity_; }

    /**
     * Try to enqueue a task (lock-free)
     * @return true if successful, false if queue is full
     */
    bool try_enqueue(Task* task) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t next_tail = (tail + 1) & mask_;

        // Check if queue is full
        if (next_tail == (head & mask_)) {
            return false;
        }

        // Try to claim slot
        Task* expected = nullptr;
        if (buffer_[tail].compare_exchange_weak(
                expected, task,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }

        return false;
    }

    /**
     * Wait until space is available or deadline reached
     */
    template<typename Clock, typename Duration>
    bool wait_enqueue_until(Task* task, const std::chrono::time_point<Clock, Duration>& deadline) {
        while (std::chrono::steady_clock::now() < deadline) {
            if (try_enqueue(task)) {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }

    template<typename Rep, typename Period>
    bool wait_enqueue_for(Task* task, const std::chrono::duration<Rep, Period>& timeout) {
        return wait_enqueue_until(task, std::chrono::steady_clock::now() + timeout);
    }

    /**
     * Try to dequeue a task (lock-free)
     * @return pointer to task if successful, nullptr if queue is empty
     */
    Task* try_dequeue() {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (head == tail) {
            return nullptr;
        }

        Task* task = buffer_[head & mask_].load(std::memory_order_acquire);
        if (task == nullptr) {
            return nullptr;
        }

        // Try to claim slot
        if (buffer_[head & mask_].compare_exchange_weak(
                task, nullptr,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            head_.store((head + 1) & mask_, std::memory_order_release);
            return task;
        }

        return nullptr;
    }

    bool try_dequeue(Task*& out) {
        if (auto* value = try_dequeue()) {
            out = value;
            return true;
        }
        return false;
    }

    template<typename Clock, typename Duration>
    Task* wait_dequeue_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto* value = try_dequeue()) {
                return value;
            }
            std::this_thread::yield();
        }
        return nullptr;
    }

    template<typename Rep, typename Period>
    Task* wait_dequeue_for(const std::chrono::duration<Rep, Period>& timeout) {
        return wait_dequeue_until(std::chrono::steady_clock::now() + timeout);
    }

    bool try_enqueue_batch(std::span<Task* const> tasks) {
        for (auto* task : tasks) {
            if (!try_enqueue(task)) {
                return false;
            }
        }
        return true;
    }

    size_t try_dequeue_batch(std::span<Task*> out) {
        size_t count = 0;
        for (auto& slot : out) {
            auto* task = try_dequeue();
            if (!task) {
                break;
            }
            slot = task;
            ++count;
        }
        return count;
    }

    /**
     * Check if queue is empty
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    bool full() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t next_tail = (tail + 1) & mask_;
        return next_tail == (head & mask_);
    }

    /**
     * Get approximate size (not exact due to lock-free nature)
     */
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        if (tail >= head) {
            return tail - head;
        }
        return (capacity_ - head) + tail;
    }

    size_t slack() const {
        return capacity_ - size();
    }

    void clear() {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        while (head != tail) {
            buffer_[head].store(nullptr, std::memory_order_release);
            head = (head + 1) & mask_;
        }
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<std::atomic<Task*>[]> buffer_;
    alignas(64) std::atomic<size_t> head_{0};  // Cache line aligned
    alignas(64) std::atomic<size_t> tail_{0};  // Cache line aligned
};

} // namespace task_engine
