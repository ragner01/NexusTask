#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace task_engine {

// Forward declarations
class DependencyGraph;

using TaskId = uint64_t;
using TaskResult = std::any;

/**
 * Task execution status
 */
enum class TaskStatus {
    Pending,
    Ready,
    Running,
    Completed,
    Failed,
    Cancelled
};

enum class TaskPriority : uint8_t {
    Low = 0,
    Normal,
    High,
    Critical
};

struct TaskOptions {
    TaskPriority priority{TaskPriority::Normal};
    size_t max_retries{0};
    std::chrono::microseconds timeout{0};
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::string category;
    bool track_metrics{true};
};

/**
 * High-level task abstraction with dependency tracking
 */
class Task {
    friend class DependencyGraph;
public:
    using TaskFunction = std::function<TaskResult()>;
    using TaskCallback = std::function<void(TaskResult)>;

    Task(TaskId id, TaskFunction func, std::string name = "", TaskOptions options = {})
        : id_(id)
        , name_(name.empty() ? "Task_" + std::to_string(id) : std::move(name))
        , func_(std::move(func))
        , status_(TaskStatus::Pending)
        , dependencies_count_(0)
        , options_(std::move(options))
        , created_time_(std::chrono::steady_clock::now())
    {}

    ~Task() = default;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

    /**
     * Execute the task
     */
    TaskResult execute() {
        if (cancelled_.load(std::memory_order_acquire)) {
            status_.store(TaskStatus::Cancelled, std::memory_order_release);
            throw std::runtime_error("Task cancelled prior to execution");
        }

        status_.store(TaskStatus::Running, std::memory_order_release);
        start_time_ = std::chrono::steady_clock::now();
        attempts_.fetch_add(1, std::memory_order_acq_rel);
        last_scheduled_time_ = start_time_;

        try {
            if (options_.deadline && std::chrono::steady_clock::now() > *options_.deadline) {
                status_.store(TaskStatus::Failed, std::memory_order_release);
                throw std::runtime_error("Task deadline exceeded before execution");
            }

            auto result = func_();
            auto finished = std::chrono::steady_clock::now();
            end_time_ = finished;

            if (options_.timeout.count() > 0 && finished - *start_time_ > options_.timeout) {
                status_.store(TaskStatus::Failed, std::memory_order_release);
                throw std::runtime_error("Task exceeded configured timeout");
            }

            status_.store(TaskStatus::Completed, std::memory_order_release);
            return result;
        } catch (...) {
            status_.store(TaskStatus::Failed, std::memory_order_release);
            end_time_ = std::chrono::steady_clock::now();
            throw;
        }
    }

    /**
     * Add dependency - this task depends on another task
     */
    void add_dependency(Task* dependency) {
        dependencies_.push_back(dependency);
        dependency->dependents_.push_back(this);
        dependencies_count_.fetch_add(1, std::memory_order_acq_rel);
    }

    /**
     * Notify that a dependency has completed
     */
    void notify_dependency_completed() {
        auto remaining = dependencies_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            status_.store(TaskStatus::Ready, std::memory_order_release);
        }
    }

    /**
     * Check if task is ready to execute (all dependencies satisfied)
     */
    bool is_ready() const {
        return dependencies_count_.load(std::memory_order_acquire) == 0 &&
               status_.load(std::memory_order_acquire) == TaskStatus::Pending;
    }

    TaskId id() const { return id_; }
    const std::string& name() const { return name_; }
    TaskStatus status() const { return status_.load(std::memory_order_acquire); }
    TaskPriority priority() const noexcept { return options_.priority; }
    void set_priority(TaskPriority priority) noexcept { options_.priority = priority; }
    const std::string& category() const noexcept { return options_.category; }
    void set_category(std::string category) { options_.category = std::move(category); }
    size_t max_retries() const noexcept { return options_.max_retries; }
    size_t attempts() const noexcept { return attempts_.load(std::memory_order_acquire); }
    bool can_retry() const noexcept {
        return !cancelled_.load(std::memory_order_acquire) && attempts() <= options_.max_retries;
    }
    bool cancel() {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            status_.store(TaskStatus::Cancelled, std::memory_order_release);
            return true;
        }
        return false;
    }
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

    auto execution_time() const {
        if (end_time_ && start_time_) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                *end_time_ - *start_time_).count();
        }
        return int64_t(0);
    }

    void set_callback(TaskCallback callback) {
        callback_ = std::move(callback);
    }

    void invoke_callback(const TaskResult& result) {
        if (callback_) {
            callback_(result);
        }
    }

    void set_deadline(std::chrono::steady_clock::time_point deadline) {
        options_.deadline = deadline;
    }

    std::optional<std::chrono::steady_clock::time_point> deadline() const {
        return options_.deadline;
    }

    bool deadline_passed() const {
        return options_.deadline && std::chrono::steady_clock::now() > *options_.deadline;
    }

    std::chrono::microseconds timeout() const noexcept { return options_.timeout; }

    void set_timeout(std::chrono::microseconds timeout) {
        options_.timeout = timeout;
    }

    void annotate(std::string key, std::string value) {
        metadata_[std::move(key)] = std::move(value);
    }

    std::optional<std::string> annotation(const std::string& key) const {
        if (auto it = metadata_.find(key); it != metadata_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::chrono::steady_clock::time_point created_time() const { return created_time_; }
    std::optional<std::chrono::steady_clock::time_point> start_time() const { return start_time_; }
    std::optional<std::chrono::steady_clock::time_point> end_time() const { return end_time_; }
    std::optional<std::chrono::steady_clock::time_point> last_scheduled_time() const { return last_scheduled_time_; }

    void reset_for_retry() {
        status_.store(TaskStatus::Pending, std::memory_order_release);
        start_time_.reset();
        end_time_.reset();
    }

private:
    TaskId id_;
    std::string name_;
    TaskFunction func_;
    std::atomic<TaskStatus> status_;
    
    std::vector<Task*> dependencies_;
    std::vector<Task*> dependents_;
    std::atomic<int> dependencies_count_;
    TaskOptions options_;
    std::unordered_map<std::string, std::string> metadata_;
    std::atomic<bool> cancelled_{false};
    std::atomic<size_t> attempts_{0};

    TaskCallback callback_;

    std::chrono::steady_clock::time_point created_time_;
    std::optional<std::chrono::steady_clock::time_point> start_time_;
    std::optional<std::chrono::steady_clock::time_point> end_time_;
    std::optional<std::chrono::steady_clock::time_point> last_scheduled_time_;
};

} // namespace task_engine
