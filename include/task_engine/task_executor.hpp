#pragma once

#include "dependency_graph.hpp"
#include "performance_monitor.hpp"
#include "task.hpp"
#include "thread_pool.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace task_engine {

/**
 * High-level task executor that manages task lifecycle and dependencies
 * Extends scheduling with monitoring, retry handling, and async handles
 */
class TaskExecutor {
public:
    struct SubmissionOptions {
        std::vector<TaskId> dependencies;
        TaskOptions task_options;
        Task::TaskCallback callback;
    };

    struct TaskHandle {
        TaskId id;
        std::shared_future<TaskResult> future;
    };

    explicit TaskExecutor(size_t num_threads = std::thread::hardware_concurrency())
        : thread_pool_(std::make_unique<ThreadPool>(num_threads))
        , graph_(std::make_unique<DependencyGraph>())
        , scheduler_running_(false)
        , next_task_id_(1)
        , monitor_(std::make_shared<PerformanceMonitor>())
    {
        install_pool_hooks();
    }

    TaskExecutor(std::unique_ptr<ThreadPool> pool,
                 std::shared_ptr<PerformanceMonitor> monitor = std::make_shared<PerformanceMonitor>())
        : thread_pool_(std::move(pool))
        , graph_(std::make_unique<DependencyGraph>())
        , scheduler_running_(false)
        , next_task_id_(1)
        , monitor_(std::move(monitor))
    {
        install_pool_hooks();
    }

    ~TaskExecutor() {
        stop();
    }

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    void start() {
        if (!scheduler_running_.exchange(true)) {
            scheduler_thread_ = std::thread(&TaskExecutor::scheduler_loop, this);
        }
    }

    void stop() {
        if (scheduler_running_.exchange(false)) {
            if (scheduler_thread_.joinable()) {
                scheduler_thread_.join();
            }
        }
        thread_pool_->shutdown();
    }

    TaskId submit(std::function<TaskResult()> func,
                  const std::string& name = "",
                  TaskOptions options = {},
                  Task::TaskCallback callback = {}) {
        SubmissionOptions submission;
        submission.task_options = std::move(options);
        submission.callback = std::move(callback);
        return submit_internal(std::move(func), name, std::move(submission));
    }

    TaskId submit_with_dependencies(std::function<TaskResult()> func,
                                    const std::vector<TaskId>& dependencies,
                                    const std::string& name = "",
                                    TaskOptions options = {},
                                    Task::TaskCallback callback = {}) {
        SubmissionOptions submission;
        submission.dependencies = dependencies;
        submission.task_options = std::move(options);
        submission.callback = std::move(callback);
        return submit_internal(std::move(func), name, std::move(submission));
    }

    TaskHandle submit_async(std::function<TaskResult()> func,
                            const std::string& name = "",
                            SubmissionOptions submission = {}) {
        auto promise = std::make_shared<std::promise<TaskResult>>();
        auto future = promise->get_future().share();
        auto user_callback = submission.callback;
        submission.callback = [promise, user_callback](TaskResult result) {
            if (user_callback) {
                user_callback(result);
            }
            promise->set_value(result);
        };
        auto task_id = submit_internal(std::move(func), name, std::move(submission), promise);
        return TaskHandle{task_id, std::move(future)};
    }

    void wait_for_completion() {
        while (!graph_->all_completed()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    TaskStatus get_task_status(TaskId id) const {
        auto task = graph_->get_task(id);
        return task ? task->status() : TaskStatus::Failed;
    }

    bool cancel(TaskId id) {
        auto task = graph_->get_task(id);
        if (task && task->cancel()) {
            fail_promise(id, std::make_exception_ptr(std::runtime_error("Task cancelled")));
            if (monitor_) {
                monitor_->record_cancellation();
            }
            graph_->notify_task_completed(id);
            return true;
        }
        return false;
    }

    PerformanceMonitor::MetricsSnapshot metrics() const {
        return monitor_->get_metrics();
    }

    size_t pending_tasks() const {
        return thread_pool_->pending_tasks();
    }

    std::shared_ptr<PerformanceMonitor> monitor() const { return monitor_; }

private:
    TaskId submit_internal(std::function<TaskResult()> func,
                           std::string name,
                           SubmissionOptions submission,
                           std::shared_ptr<std::promise<TaskResult>> promise = nullptr) {
        auto task_id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
        auto task = std::make_shared<Task>(task_id, std::move(func), std::move(name), submission.task_options);

        if (monitor_) {
            monitor_->record_submission();
        }

        if (promise) {
            register_promise(task_id, promise);
        }

        task->set_callback([this, task_id, callback = std::move(submission.callback)](TaskResult result) {
            if (callback) {
                callback(result);
            }
            fulfill_promise(task_id, result);
            graph_->notify_task_completed(task_id);
        });

        graph_->add_task(task);
        for (auto dep_id : submission.dependencies) {
            graph_->add_dependency(task_id, dep_id);
        }

        if (task->is_ready()) {
            thread_pool_->submit(task.get(), task->priority());
        }

        return task_id;
    }

    void install_pool_hooks() {
        ThreadPool::LifecycleHooks hooks;
        hooks.on_start = [this](Task* task) {
            if (monitor_) {
                monitor_->record_start();
                auto wait = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - task->created_time()).count();
                if (wait > 0) {
                    monitor_->record_queue_wait(static_cast<uint64_t>(wait));
                }
            }
        };
        hooks.on_success = [this](Task* task, const TaskResult& result) {
            if (monitor_) {
                monitor_->record_completion(task->execution_time());
            }
            task->invoke_callback(result);
        };
        hooks.on_failure = [this](Task* task, const std::exception_ptr& error) {
            if (monitor_) {
                monitor_->record_failure();
            }
            handle_failure(task, error);
        };
        thread_pool_->set_lifecycle_hooks(std::move(hooks));
    }

    void handle_failure(Task* task, const std::exception_ptr& error) {
        if (task->can_retry()) {
            if (monitor_) {
                monitor_->record_retry();
            }
            task->reset_for_retry();
            thread_pool_->submit(task, task->priority());
            return;
        }
        fail_promise(task->id(), error);
        graph_->notify_task_completed(task->id());
    }

    void register_promise(TaskId id, std::shared_ptr<std::promise<TaskResult>> promise) {
        std::lock_guard<std::mutex> lock(promise_mutex_);
        task_promises_[id] = std::move(promise);
    }

    void fulfill_promise(TaskId id, const TaskResult& result) {
        std::shared_ptr<std::promise<TaskResult>> promise;
        {
            std::lock_guard<std::mutex> lock(promise_mutex_);
            auto it = task_promises_.find(id);
            if (it != task_promises_.end()) {
                promise = std::move(it->second);
                task_promises_.erase(it);
            }
        }
        if (promise) {
            promise->set_value(result);
        }
    }

    void fail_promise(TaskId id, const std::exception_ptr& error) {
        std::shared_ptr<std::promise<TaskResult>> promise;
        {
            std::lock_guard<std::mutex> lock(promise_mutex_);
            auto it = task_promises_.find(id);
            if (it != task_promises_.end()) {
                promise = std::move(it->second);
                task_promises_.erase(it);
            }
        }
        if (promise) {
            promise->set_exception(error);
        }
    }

    void scheduler_loop() {
        while (scheduler_running_.load(std::memory_order_acquire)) {
            auto ready_tasks = graph_->get_ready_tasks();
            for (auto& task : ready_tasks) {
                auto status = task->status();
                if (status == TaskStatus::Pending || status == TaskStatus::Ready) {
                    thread_pool_->submit(task.get(), task->priority());
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<DependencyGraph> graph_;
    std::shared_ptr<PerformanceMonitor> monitor_;

    std::atomic<bool> scheduler_running_;
    std::thread scheduler_thread_;
    std::atomic<TaskId> next_task_id_;

    std::mutex promise_mutex_;
    std::unordered_map<TaskId, std::shared_ptr<std::promise<TaskResult>>> task_promises_;
};

} // namespace task_engine
