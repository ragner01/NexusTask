#include "task_engine/task_engine.hpp"
#include <any>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace task_engine;

namespace {
std::shared_ptr<Task> make_graph_task(TaskId id, const std::string& name) {
    return std::make_shared<Task>(id, []() -> TaskResult { return {}; }, name);
}
}

int main() {
    std::cout << "=== Regression / Feature Exercise ===\n\n";

    TaskExecutor executor(4);
    executor.start();

    std::cout << "1. Retry Handling\n------------------\n";
    TaskOptions retry_options;
    retry_options.max_retries = 2;
    retry_options.priority = TaskPriority::High;
    std::atomic<int> retry_attempts{0};

    auto retry_task = executor.submit([&]() -> TaskResult {
        auto attempt = retry_attempts.fetch_add(1, std::memory_order_relaxed) + 1;
        std::cout << "Retryable task attempt " << attempt << "\n";
        if (attempt < 3) {
            throw std::runtime_error("Planned failure to test retry path");
        }
        return attempt;
    }, "RetryableTask", retry_options, [](TaskResult result) {
        std::cout << "Retryable task succeeded on attempt "
                  << std::any_cast<int>(result) << "\n";
    });

    std::cout << "2. Cancellation\n----------------\n";
    auto cancellable_task = executor.submit([]() -> TaskResult {
        for (int i = 0; i < 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cout << "Long task heartbeat " << i << "\n";
        }
        return {};
    }, "CancellableTask");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    bool cancelled = executor.cancel(cancellable_task);
    std::cout << "Cancellation requested: " << (cancelled ? "YES" : "NO") << "\n";

    executor.wait_for_completion();
    auto local_metrics = executor.metrics();
    std::cout << "Local executor completed " << local_metrics.tasks_completed
              << " tasks, retries recorded: " << local_metrics.tasks_retried << "\n\n";

    std::cout << "3. Dependency Graph Utilities\n------------------------------\n";
    DependencyGraph graph;
    auto ta = make_graph_task(1001, "A");
    auto tb = make_graph_task(1002, "B");
    auto tc = make_graph_task(1003, "C");
    graph.add_task(ta);
    graph.add_task(tb);
    graph.add_task(tc);
    graph.add_dependency(tb->id(), ta->id());
    graph.add_dependency(tc->id(), tb->id());

    auto order = graph.topological_order();
    std::cout << "Topological order: ";
    for (auto id : order) {
        std::cout << id << ' ';
    }
    std::cout << "\nCycle present? " << (graph.has_cycles() ? "YES" : "NO") << "\n";

    graph.add_dependency(ta->id(), tc->id());
    std::cout << "Cycle after adding edge A->C? "
              << (graph.has_cycles() ? "YES" : "NO") << "\n\n";

    std::cout << "4. Distributed In-Memory Submission\n------------------------------------\n";
    DistributedExecutor distributed("127.0.0.1", 9000, 2);
    distributed.start();
    distributed.add_node("127.0.0.1", 9001, 4);

    auto remote_executor = std::make_shared<TaskExecutor>(2);
    remote_executor->start();
    distributed.attach_in_memory_executor("127.0.0.1", 9001, remote_executor);
    distributed.enable_in_memory_transport();

    auto distributed_task = distributed.submit([]() -> TaskResult {
        std::cout << "Executing on remote executor thread "
                  << std::this_thread::get_id() << "\n";
        return std::string("remote-success");
    }, "RemoteEcho");

    distributed.wait_for_completion();
    remote_executor->wait_for_completion();

    auto remote_metrics = remote_executor->metrics();
    std::cout << "Remote executor completed "
              << remote_metrics.tasks_completed << " distributed task(s)\n\n";

    remote_executor->stop();
    distributed.stop();
    executor.stop();

    std::cout << "Regression scenarios finished.\n";
    return 0;
}
