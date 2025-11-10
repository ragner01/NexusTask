#pragma once

#include "performance_monitor.hpp"
#include "task_executor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace task_engine {

/**
 * Distributed task executor for multi-node execution
 * Supports task distribution across network nodes
 */
class DistributedExecutor {
public:
    using SubmissionOptions = TaskExecutor::SubmissionOptions;

    enum class DistributionPolicy {
        LocalFirst,
        LeastLoaded,
        RoundRobin,
        Broadcast
    };

    struct NodeInfo {
        std::string address;
        uint16_t port;
        size_t capacity;
        std::atomic<size_t> current_load{0};
        std::atomic<bool> reachable{true};
        std::chrono::steady_clock::time_point last_heartbeat{std::chrono::steady_clock::now()};
        PerformanceMonitor::MetricsSnapshot metrics;
        std::shared_ptr<TaskExecutor> remote_executor;
    };

    using RemoteSubmitCallback = std::function<TaskId(
        const NodeInfo&,
        std::function<TaskResult()>,
        const std::string&,
        const SubmissionOptions&)>;

    DistributedExecutor(
        const std::string& local_address,
        uint16_t local_port,
        size_t num_local_threads = std::thread::hardware_concurrency())
        : local_executor_(std::make_unique<TaskExecutor>(num_local_threads))
        , local_address_(local_address)
        , local_port_(local_port)
        , running_(false)
        , policy_(DistributionPolicy::LocalFirst)
    {
        local_executor_->start();
    }

    ~DistributedExecutor() {
        stop();
    }

    /**
     * Add a remote node
     */
    void add_node(const std::string& address, uint16_t port, size_t capacity) {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto node = std::make_unique<NodeInfo>();
        node->address = address;
        node->port = port;
        node->capacity = capacity;
        node->current_load.store(0, std::memory_order_release);
        nodes_.push_back(std::move(node));
    }

    void remove_node(const std::string& address, uint16_t port) {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
            [&](const auto& node) {
                return node->address == address && node->port == port;
            }),
            nodes_.end());
    }

    void attach_in_memory_executor(const std::string& address,
                                   uint16_t port,
                                   std::shared_ptr<TaskExecutor> executor) {
        if (!executor) {
            throw std::invalid_argument("Executor must not be null");
        }
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto* node = find_node_unlocked(address, port);
        if (!node) {
            auto new_node = std::make_unique<NodeInfo>();
            new_node->address = address;
            new_node->port = port;
            auto capacity = executor->pending_tasks() + executor->monitor()->inflight_tasks();
            new_node->capacity = std::max<size_t>(1, static_cast<size_t>(capacity));
            node = new_node.get();
            nodes_.push_back(std::move(new_node));
        }
        node->remote_executor = std::move(executor);
    }

    void update_node_metrics(const std::string& address,
                             uint16_t port,
                             const PerformanceMonitor::MetricsSnapshot& metrics) {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& node : nodes_) {
            if (node->address == address && node->port == port) {
                node->metrics = metrics;
                node->current_load.store(metrics.inflight_tasks, std::memory_order_release);
                node->last_heartbeat = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    void mark_node_unreachable(const std::string& address, uint16_t port) {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& node : nodes_) {
            if (node->address == address && node->port == port) {
                node->reachable.store(false, std::memory_order_release);
                break;
            }
        }
    }

    void set_policy(DistributionPolicy policy) {
        policy_ = policy;
    }

    void set_remote_submitter(RemoteSubmitCallback callback) {
        remote_submitter_ = std::move(callback);
    }

    void enable_in_memory_transport() {
        set_remote_submitter([](const NodeInfo& node,
                                std::function<TaskResult()> func,
                                const std::string& name,
                                const SubmissionOptions& submission) -> TaskId {
            if (!node.remote_executor) {
                throw std::runtime_error("Remote node missing in-memory executor");
            }
            auto executor = node.remote_executor;
            if (submission.dependencies.empty()) {
                return executor->submit(std::move(func), name,
                    submission.task_options, submission.callback);
            }
            return executor->submit_with_dependencies(
                std::move(func), submission.dependencies, name,
                submission.task_options, submission.callback);
        });
    }

    /**
     * Start distributed executor
     */
    void start() {
        running_.store(true, std::memory_order_release);
        // In a full implementation, this would start network listeners
        // For now, we'll use local execution with load balancing simulation
    }

    /**
     * Stop distributed executor
     */
    void stop() {
        running_.store(false, std::memory_order_release);
        local_executor_->stop();
    }

    /**
     * Submit task (distributed across nodes)
     */
    TaskId submit(std::function<TaskResult()> func,
                  const std::string& name = "",
                  SubmissionOptions submission = {}) {
        if (policy_ == DistributionPolicy::Broadcast) {
            auto ids = broadcast(std::move(func), name, submission);
            return ids.empty() ? 0 : ids.front();
        }

        auto* node = select_node();
        if (!node) {
            return local_executor_->submit(std::move(func), name,
                submission.task_options, submission.callback);
        }

        if (remote_submitter_ && node->reachable.load(std::memory_order_acquire)) {
            return remote_submitter_(*node, std::move(func), name, submission);
        }

        return local_executor_->submit(std::move(func), name,
            submission.task_options, submission.callback);
    }

    std::vector<TaskId> broadcast(std::function<TaskResult()> func,
                                  const std::string& name = "",
                                  SubmissionOptions submission = {}) {
        std::vector<TaskId> ids;
        ids.reserve(nodes_.size() + 1);
        ids.push_back(local_executor_->submit(func, name,
            submission.task_options, submission.callback));

        if (remote_submitter_) {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            for (auto& node : nodes_) {
                if (!node->reachable.load(std::memory_order_acquire)) {
                    continue;
                }
                ids.push_back(remote_submitter_(*node, func, name, submission));
            }
        }
        return ids;
    }

    /**
     * Wait for all tasks to complete
     */
    void wait_for_completion() {
        local_executor_->wait_for_completion();
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& node : nodes_) {
            if (node->remote_executor) {
                node->remote_executor->wait_for_completion();
            }
        }
    }

    TaskExecutor& local_executor() { return *local_executor_; }

private:
    NodeInfo* select_node() {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        if (nodes_.empty()) {
            return nullptr;
        }

        NodeInfo* candidate = nullptr;
        switch (policy_) {
        case DistributionPolicy::LeastLoaded: {
            size_t min_load = SIZE_MAX;
            for (auto& node : nodes_) {
                if (!node->reachable.load(std::memory_order_acquire)) continue;
                auto load = node->current_load.load(std::memory_order_acquire);
                if (load < min_load && load < node->capacity) {
                    min_load = load;
                    candidate = node.get();
                }
            }
            break;
        }
        case DistributionPolicy::RoundRobin: {
            for (size_t i = 0; i < nodes_.size(); ++i) {
                auto index = (round_robin_cursor_ + i) % nodes_.size();
                auto& node = nodes_[index];
                if (node->reachable.load(std::memory_order_acquire) &&
                    node->current_load.load(std::memory_order_acquire) < node->capacity) {
                    candidate = node.get();
                    round_robin_cursor_ = (index + 1) % nodes_.size();
                    break;
                }
            }
            break;
        }
        case DistributionPolicy::LocalFirst:
        default:
            for (auto& node : nodes_) {
                if (node->reachable.load(std::memory_order_acquire)) {
                    candidate = node.get();
                    break;
                }
            }
            break;
        case DistributionPolicy::Broadcast:
            candidate = nullptr;
            break;
        }
        return candidate;
    }

    NodeInfo* find_node_unlocked(const std::string& address, uint16_t port) {
        for (auto& node : nodes_) {
            if (node->address == address && node->port == port) {
                return node.get();
            }
        }
        return nullptr;
    }

    std::unique_ptr<TaskExecutor> local_executor_;
    std::string local_address_;
    [[maybe_unused]] uint16_t local_port_;
    std::atomic<bool> running_;
    DistributionPolicy policy_;
    RemoteSubmitCallback remote_submitter_;
    std::atomic<size_t> round_robin_cursor_{0};
    
    std::mutex nodes_mutex_;
    std::vector<std::unique_ptr<NodeInfo>> nodes_;
};

} // namespace task_engine
